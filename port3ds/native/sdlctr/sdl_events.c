/* SDL2 subset for the 3DS: event queue and input.
 *
 * Controls (see port3ds/README.md):
 *   touch screen     - left click/drag at the touched point of the zoomed view
 *   L + touch        - move the cursor without clicking (hover)
 *   R + touch        - right click
 *   circle pad       - move the cursor
 *   A / B            - left / right mouse button at the cursor
 *   L + circle pad   - mouse wheel (up/down)
 *   C-stick          - pan the zoomed view (New 3DS)
 *   ZL / ZR          - zoom out / in (New 3DS); also L+R+left/right
 *   D-pad            - arrow keys
 *   X                - software keyboard (text input)
 *   Y                - Enter
 *   START            - Escape
 *   SELECT           - swap screens
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdlctr.h"

#define QUEUE_SIZE 1024
#define MAX_WATCHES 8

sdlctr_view sdlctr_view_state = { 1024, 768, 0.625f, 0, 0, 0, 512, 384, 0, 1, 0 };

static SDL_Event queue[QUEUE_SIZE];
static int queue_head, queue_count;
static LightLock queue_lock = 1;
static LightEvent queue_signal;
static int queue_ready;

static struct { SDL_EventFilter filter; void *userdata; } watches[MAX_WATCHES];
static int watch_count;
static uint32_t next_user_event = SDL_USEREVENT;

static uint8_t key_state[SDL_NUM_SCANCODES];
static int quit_sent;
static uint32_t last_pump;
static float cursor_fx = 512, cursor_fy = 384;
static int touch_active, touch_button;
static uint32_t last_click_time;
static int last_click_x, last_click_y, last_click_button;
static int text_input_requested;
static aptHookCookie apt_cookie;

void sdlctr_text_input_dialog(void);

static void ensure_queue(void)
{
	if (!queue_ready) {
		LightEvent_Init(&queue_signal, RESET_ONESHOT);
		queue_ready = 1;
	}
}

int sdlctr_push_event(SDL_Event *event)
{
	int i, n;
	ensure_queue();
	event->key.timestamp = sdlctr_ticks();
	/* watches see every event when it is added, as in SDL */
	n = watch_count;
	for (i = 0; i < n; i++)
		if (watches[i].filter) watches[i].filter(watches[i].userdata, event);
	LightLock_Lock(&queue_lock);
	if (queue_count == QUEUE_SIZE) {
		LightLock_Unlock(&queue_lock);
		return 0;
	}
	queue[(queue_head + queue_count) % QUEUE_SIZE] = *event;
	queue_count++;
	LightLock_Unlock(&queue_lock);
	LightEvent_Signal(&queue_signal);
	return 1;
}

static void push_window_event(int what)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.type = SDL_WINDOWEVENT;
	e.window.windowID = 1;
	e.window.event = (uint8_t)what;
	sdlctr_push_event(&e);
}

static void push_key(int scancode, int down)
{
	SDL_Event e;
	if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES) return;
	memset(&e, 0, sizeof(e));
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.windowID = 1;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = scancode;
	key_state[scancode] = down ? 1 : 0;
	sdlctr_push_event(&e);
}

static void push_motion(int x, int y)
{
	SDL_Event e;
	sdlctr_view *v = &sdlctr_view_state;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= v->logical_w) x = v->logical_w - 1;
	if (y >= v->logical_h) y = v->logical_h - 1;
	if (x == v->mouse_x && y == v->mouse_y) return;
	memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEMOTION;
	e.motion.windowID = 1;
	e.motion.state = v->mouse_buttons;
	e.motion.x = x;
	e.motion.y = y;
	e.motion.xrel = x - v->mouse_x;
	e.motion.yrel = y - v->mouse_y;
	v->mouse_x = x;
	v->mouse_y = y;
	cursor_fx = (float)x;
	cursor_fy = (float)y;
	sdlctr_push_event(&e);
}

static void push_button(int button, int down)
{
	SDL_Event e;
	sdlctr_view *v = &sdlctr_view_state;
	uint32_t mask = button == SDL_BUTTON_LEFT ? SDL_BUTTON_LMASK :
	                button == SDL_BUTTON_RIGHT ? SDL_BUTTON_RMASK : SDL_BUTTON_MMASK;
	uint32_t now = sdlctr_ticks();
	memset(&e, 0, sizeof(e));
	e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button.windowID = 1;
	e.button.button = (uint8_t)button;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.x = v->mouse_x;
	e.button.y = v->mouse_y;
	e.button.clicks = 1;
	if (down) {
		if (button == last_click_button && now - last_click_time <= 500 &&
		    abs(v->mouse_x - last_click_x) <= 6 && abs(v->mouse_y - last_click_y) <= 6) {
			e.button.clicks = 2;
			last_click_time = 0;
		} else {
			last_click_time = now;
		}
		last_click_button = button;
		last_click_x = v->mouse_x;
		last_click_y = v->mouse_y;
		v->mouse_buttons |= mask;
	} else {
		v->mouse_buttons &= ~mask;
	}
	sdlctr_push_event(&e);
}

static void push_wheel(int amount)
{
	SDL_Event e;
	sdlctr_view *v = &sdlctr_view_state;
	memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEWHEEL;
	e.wheel.windowID = 1;
	e.wheel.y = amount;
	e.wheel.preciseY = (float)amount;
	e.wheel.mouseX = v->mouse_x;
	e.wheel.mouseY = v->mouse_y;
	sdlctr_push_event(&e);
}

static const float zoom_levels[] = { 0.3125f, 0.4f, 0.5f, 0.625f, 0.8f, 1.0f };
#define ZOOM_COUNT (int)(sizeof(zoom_levels) / sizeof(zoom_levels[0]))

static void change_zoom(int dir)
{
	sdlctr_view *v = &sdlctr_view_state;
	int i, best = 0;
	float cx, cy;
	for (i = 0; i < ZOOM_COUNT; i++)
		if (zoom_levels[i] <= v->zoom + 0.001f) best = i;
	best += dir;
	if (best < 0) best = 0;
	if (best >= ZOOM_COUNT) best = ZOOM_COUNT - 1;
	/* keep the cursor at the same place on the screen */
	cx = (v->mouse_x - v->view_x) * v->zoom;
	cy = (v->mouse_y - v->view_y) * v->zoom;
	v->zoom = zoom_levels[best];
	v->view_x = v->mouse_x - cx / v->zoom;
	v->view_y = v->mouse_y - cy / v->zoom;
	sdlctr_view_follow_mouse();
}

/* Clamp the zoomed region to the game screen and keep the cursor inside it. */
void sdlctr_view_follow_mouse(void)
{
	sdlctr_view *v = &sdlctr_view_state;
	float vw = 320.0f / v->zoom, vh = 240.0f / v->zoom;
	float margin_x = vw * 0.15f, margin_y = vh * 0.15f;
	if (v->mouse_x < v->view_x + margin_x) v->view_x = v->mouse_x - margin_x;
	if (v->mouse_x > v->view_x + vw - margin_x) v->view_x = v->mouse_x - vw + margin_x;
	if (v->mouse_y < v->view_y + margin_y) v->view_y = v->mouse_y - margin_y;
	if (v->mouse_y > v->view_y + vh - margin_y) v->view_y = v->mouse_y - vh + margin_y;
	if (v->view_x > v->logical_w - vw) v->view_x = v->logical_w - vw;
	if (v->view_y > v->logical_h - vh) v->view_y = v->logical_h - vh;
	if (v->view_x < 0) v->view_x = 0;
	if (v->view_y < 0) v->view_y = 0;
}

static void touch_to_game(int px, int py, int *gx, int *gy)
{
	sdlctr_view *v = &sdlctr_view_state;
	*gx = (int)(v->view_x + px / v->zoom);
	*gy = (int)(v->view_y + py / v->zoom);
}

static void apt_hook(APT_HookType hook, void *param)
{
	(void)param;
	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		sdlctr_view_state.suspended = 1;
		push_window_event(SDL_WINDOWEVENT_FOCUS_LOST);
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		sdlctr_view_state.suspended = 0;
		push_window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
		push_window_event(SDL_WINDOWEVENT_EXPOSED);
		break;
	default:
		break;
	}
}

void sdlctr_events_init(void)
{
	static int done;
	if (done) return;
	done = 1;
	ensure_queue();
	aptHook(&apt_cookie, apt_hook, NULL);
	push_window_event(SDL_WINDOWEVENT_SHOWN);
	push_window_event(SDL_WINDOWEVENT_EXPOSED);
	push_window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
	push_window_event(SDL_WINDOWEVENT_ENTER);
}

static void map_key(u32 down, u32 up, u32 button, int scancode)
{
	if (down & button) push_key(scancode, 1);
	if (up & button) push_key(scancode, 0);
}

static void scan_input(void)
{
	static u32 last_tick;
	sdlctr_view *v = &sdlctr_view_state;
	u32 now = sdlctr_ticks(), down, up, held;
	float dt;
	circlePosition cp, cs;
	touchPosition tp;
	int gx, gy;

	hidScanInput();
	down = hidKeysDown();
	up = hidKeysUp();
	held = hidKeysHeld();
	dt = last_tick ? (now - last_tick) / 1000.0f : 0.0f;
	if (dt > 0.1f) dt = 0.1f;
	last_tick = now;

	/* keys */
	map_key(down, up, KEY_DUP, 82);
	map_key(down, up, KEY_DDOWN, 81);
	map_key(down, up, KEY_DLEFT, 80);
	map_key(down, up, KEY_DRIGHT, 79);
	map_key(down, up, KEY_START, 41);
	map_key(down, up, KEY_Y, 40);

	if (down & KEY_SELECT) v->swap_screens = !v->swap_screens;
	if (down & KEY_ZL) change_zoom(-1);
	if (down & KEY_ZR) change_zoom(1);
	if (down & KEY_X) text_input_requested = 1;

	/* mouse buttons on A/B */
	if (down & KEY_A) push_button(SDL_BUTTON_LEFT, 1);
	if (up & KEY_A) push_button(SDL_BUTTON_LEFT, 0);
	if (down & KEY_B) push_button(SDL_BUTTON_RIGHT, 1);
	if (up & KEY_B) push_button(SDL_BUTTON_RIGHT, 0);

	/* circle pad: cursor, or wheel with L */
	hidCircleRead(&cp);
	if (abs(cp.dx) > 15 || abs(cp.dy) > 15) {
		if (held & KEY_L) {
			static float wheel;
			wheel += cp.dy / 154.0f * dt * 8.0f;
			while (wheel >= 1) { push_wheel(1); wheel -= 1; }
			while (wheel <= -1) { push_wheel(-1); wheel += 1; }
		} else {
			float speed = 700.0f / v->zoom * 0.625f;
			float mag = (float)(abs(cp.dx) > abs(cp.dy) ? abs(cp.dx) : abs(cp.dy)) / 154.0f;
			cursor_fx += cp.dx / 154.0f * speed * mag * dt;
			cursor_fy -= cp.dy / 154.0f * speed * mag * dt;
			push_motion((int)cursor_fx, (int)cursor_fy);
			sdlctr_view_follow_mouse();
		}
	}

	/* C-stick: pan the view */
	hidCstickRead(&cs);
	if (abs(cs.dx) > 15 || abs(cs.dy) > 15) {
		v->view_x += cs.dx / 154.0f * 900.0f * dt;
		v->view_y -= cs.dy / 154.0f * 900.0f * dt;
		if (v->view_x < 0) v->view_x = 0;
		if (v->view_y < 0) v->view_y = 0;
		if (v->view_x > v->logical_w - 320.0f / v->zoom) v->view_x = v->logical_w - 320.0f / v->zoom;
		if (v->view_y > v->logical_h - 240.0f / v->zoom) v->view_y = v->logical_h - 240.0f / v->zoom;
		if (v->view_x < 0) v->view_x = 0;
		if (v->view_y < 0) v->view_y = 0;
	}

	/* touch screen: the bottom screen shows the zoomed view unless swapped,
	   in which case it shows the whole game screen */
	if (held & KEY_TOUCH) {
		hidTouchRead(&tp);
		if (v->swap_screens) {
			float fit = 320.0f / v->logical_w;
			if (240.0f / v->logical_h < fit) fit = 240.0f / v->logical_h;
			gx = (int)((tp.px - (320 - v->logical_w * fit) / 2) / fit);
			gy = (int)((tp.py - (240 - v->logical_h * fit) / 2) / fit);
		} else {
			touch_to_game(tp.px, tp.py, &gx, &gy);
		}
		push_motion(gx, gy);
		if (!touch_active) {
			touch_active = 1;
			touch_button = (held & KEY_L) ? 0 : (held & KEY_R) ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
			if (touch_button) push_button(touch_button, 1);
		}
	} else if (touch_active) {
		touch_active = 0;
		if (touch_button) push_button(touch_button, 0);
		touch_button = 0;
	}
}

/* Test aid: input_script.txt in the game folder plays scripted input, one
   action per line: "<seconds since start> click|rclick|move <x> <y>" in game
   coordinates, or "<seconds> key <SDL scancode>". */
#define SCRIPT_MAX 256
static struct { uint32_t ms; char what; int a, b; } script[SCRIPT_MAX];
static int script_count, script_next, script_loaded, script_release;
static uint32_t script_start;

static void run_script(void)
{
	uint32_t now = sdlctr_ticks();
	if (!script_loaded) {
		FILE *f = fopen("sdmc:/3ds/SpaceRangersHD/input_script.txt", "r");
		char line[128], what[16];
		float sec;
		script_loaded = 1;
		script_start = now;
		if (!f) return;
		while (script_count < SCRIPT_MAX && fgets(line, sizeof line, f)) {
			int a = 0, b = 0;
			if (sscanf(line, "%f %15s %d %d", &sec, what, &a, &b) < 3) continue;
			script[script_count].ms = (uint32_t)(sec * 1000);
			script[script_count].what = what[0] == 'r' ? 'r' : what[0];
			script[script_count].a = a;
			script[script_count].b = b;
			script_count++;
		}
		fclose(f);
	}
	if (script_release) {
		push_button(script_release, 0);
		script_release = 0;
		return;
	}
	while (script_next < script_count && now - script_start >= script[script_next].ms) {
		int i = script_next++;
		if (script[i].what == 'k') {
			push_key(script[i].a, 1);
			push_key(script[i].a, 0);
			continue;
		}
		push_motion(script[i].a, script[i].b);
		if (script[i].what == 'c' || script[i].what == 'r') {
			script_release = script[i].what == 'c' ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
			push_button(script_release, 1);
			return;
		}
	}
}

void sdlctr_pump_events(void)
{
	uint32_t now = sdlctr_ticks();
	sdlctr_events_init();
	if (last_pump && now - last_pump < 4) return;
	last_pump = now;
	if (!aptMainLoop()) {
		if (!quit_sent) {
			SDL_Event e;
			memset(&e, 0, sizeof(e));
			e.type = SDL_QUIT;
			sdlctr_push_event(&e);
			quit_sent = 1;
		}
		return;
	}
	scan_input();
	run_script();
}

/* Called by the renderer after a frame has been presented, when no GPU
   frame is open: blocking system applets are safe here. */
void sdlctr_input_frame(void)
{
	if (text_input_requested) {
		text_input_requested = 0;
		sdlctr_text_input_dialog();
	}
}

static int take_event(SDL_Event *event, uint32_t min, uint32_t max, int remove)
{
	int i, found = 0;
	LightLock_Lock(&queue_lock);
	for (i = 0; i < queue_count; i++) {
		int idx = (queue_head + i) % QUEUE_SIZE;
		if (queue[idx].type >= min && queue[idx].type <= max) {
			if (event) *event = queue[idx];
			if (remove) {
				/* close the gap */
				int j;
				for (j = i; j > 0; j--)
					queue[(queue_head + j) % QUEUE_SIZE] = queue[(queue_head + j - 1) % QUEUE_SIZE];
				queue_head = (queue_head + 1) % QUEUE_SIZE;
				queue_count--;
			}
			found = 1;
			break;
		}
	}
	LightLock_Unlock(&queue_lock);
	return found;
}

int SDL_PollEvent(SDL_Event *event)
{
	sdlctr_pump_events();
	return take_event(event, 0, 0xFFFFFFFFu, event != NULL);
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
	uint32_t start = sdlctr_ticks();
	for (;;) {
		int wait;
		if (SDL_PollEvent(event)) return 1;
		if (timeout >= 0) {
			uint32_t elapsed = sdlctr_ticks() - start;
			if ((int)elapsed >= timeout) return 0;
			wait = timeout - (int)elapsed;
		} else {
			wait = 1000;
		}
		if (wait > 8) wait = 8; /* input is polled */
		LightEvent_WaitTimeout(&queue_signal, (s64)wait * 1000000LL);
	}
}

int SDL_WaitEvent(SDL_Event *event) { return SDL_WaitEventTimeout(event, -1); }

int SDL_PushEvent(SDL_Event *event)
{
	return sdlctr_push_event(event);
}

int SDL_PeepEvents(SDL_Event *events, int numevents, int action, uint32_t min, uint32_t max)
{
	int i, n = 0;
	if (action == SDL_ADDEVENT) {
		for (i = 0; i < numevents; i++)
			if (sdlctr_push_event(&events[i])) n++;
		return n;
	}
	if (action == SDL_GETEVENT || action == SDL_PEEKEVENT) {
		if (action == SDL_GETEVENT) sdlctr_pump_events();
		if (action == SDL_PEEKEVENT) {
			/* count matching events, copying up to numevents */
			LightLock_Lock(&queue_lock);
			for (i = 0; i < queue_count; i++) {
				SDL_Event *e = &queue[(queue_head + i) % QUEUE_SIZE];
				if (e->type >= min && e->type <= max) {
					if (events && n < numevents) events[n] = *e;
					n++;
				}
			}
			LightLock_Unlock(&queue_lock);
			return events ? (n < numevents ? n : numevents) : n;
		}
		while (n < numevents && take_event(&events[n], min, max, 1)) n++;
		return n;
	}
	return sdlctr_set_error("invalid PeepEvents action");
}

int SDL_HasEvents(uint32_t min, uint32_t max)
{
	sdlctr_pump_events();
	return take_event(NULL, min, max, 0);
}

uint32_t SDL_RegisterEvents(int count)
{
	uint32_t base = next_user_event;
	if (count <= 0 || next_user_event + count > SDL_LASTEVENT) return (uint32_t)-1;
	next_user_event += count;
	return base;
}

void SDL_AddEventWatch(SDL_EventFilter filter, void *userdata)
{
	if (watch_count < MAX_WATCHES) {
		watches[watch_count].filter = filter;
		watches[watch_count].userdata = userdata;
		watch_count++;
	}
}

void SDL_DelEventWatch(SDL_EventFilter filter, void *userdata)
{
	int i;
	for (i = 0; i < watch_count; i++)
		if (watches[i].filter == filter && watches[i].userdata == userdata) {
			memmove(&watches[i], &watches[i + 1], (watch_count - i - 1) * sizeof(watches[0]));
			watch_count--;
			return;
		}
}

void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}

const uint8_t *SDL_GetKeyboardState(int *count)
{
	if (count) *count = SDL_NUM_SCANCODES;
	return key_state;
}

int SDL_GetModState(void) { return 0; }

uint32_t SDL_GetMouseState(int *x, int *y)
{
	if (x) *x = sdlctr_view_state.mouse_x;
	if (y) *y = sdlctr_view_state.mouse_y;
	return sdlctr_view_state.mouse_buttons;
}

uint32_t SDL_GetGlobalMouseState(int *x, int *y) { return SDL_GetMouseState(x, y); }

void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y)
{
	(void)window;
	push_motion(x, y);
	sdlctr_view_follow_mouse();
}

/* --------------------------------------------------------------- text input */

static void push_text(const char *utf8)
{
	/* SDL_TEXTINPUT carries at most 31 bytes; split at code point boundaries */
	size_t len = strlen(utf8), pos = 0;
	while (pos < len) {
		SDL_Event e;
		size_t n = len - pos;
		if (n > 31) {
			n = 31;
			while (n > 0 && ((unsigned char)utf8[pos + n] & 0xC0) == 0x80) n--;
		}
		memset(&e, 0, sizeof(e));
		e.type = SDL_TEXTINPUT;
		e.text.windowID = 1;
		memcpy(e.text.text, utf8 + pos, n);
		sdlctr_push_event(&e);
		pos += n;
	}
}

void sdlctr_text_input_dialog(void)
{
	SwkbdState swkbd;
	char buf[256];
	SwkbdButton button;
	memset(buf, 0, sizeof(buf));
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(buf) / 4 - 1);
	swkbdSetHintText(&swkbd, "Text");
	swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
	button = swkbdInputText(&swkbd, buf, sizeof(buf));
	if (button == SWKBD_BUTTON_CONFIRM && buf[0])
		push_text(buf);
}

/* --------------------------------------------------------------- message boxes */

void sdlctr_video_quiesce(void);

static int show_message(const char *title, const char *message,
                        const SDL_MessageBoxButtonData *buttons, int count)
{
	sdlctr_log("MessageBox: %s: %s", title ? title : "", message ? message : "");
	sdlctr_video_quiesce();
	if (count <= 1) {
		errorConf err;
		char text[1900];
		snprintf(text, sizeof(text), "%s\n\n%s", title ? title : "", message ? message : "");
		errorInit(&err, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
		errorText(&err, text);
		errorDisp(&err);
		return count == 1 ? buttons[0].buttonid : 0;
	} else {
		/* the software keyboard applet doubles as a dialog with up to 3 buttons */
		SwkbdState swkbd;
		char out[4];
		int i, n = count > 3 ? 3 : count;
		/* with two buttons the applet uses its left and right buttons */
		static const SwkbdButton two[] = { SWKBD_BUTTON_LEFT, SWKBD_BUTTON_RIGHT };
		static const SwkbdButton three[] = { SWKBD_BUTTON_LEFT, SWKBD_BUTTON_MIDDLE, SWKBD_BUTTON_RIGHT };
		const SwkbdButton *slots = n == 2 ? two : three;
		SwkbdButton pressed;
		swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, n, 1);
		swkbdSetHintText(&swkbd, message ? message : "");
		for (i = 0; i < n; i++)
			swkbdSetButton(&swkbd, slots[i], buttons[i].text ? buttons[i].text : "?", true);
		pressed = swkbdInputText(&swkbd, out, sizeof(out));
		for (i = 0; i < n; i++)
			if (slots[i] == pressed) return buttons[i].buttonid;
		return buttons[0].buttonid;
	}
}

int SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
	int id = show_message(data->title, data->message, data->buttons, data->numbuttons);
	if (buttonid) *buttonid = id;
	return 0;
}

int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window)
{
	(void)flags; (void)window;
	show_message(title, message, NULL, 0);
	return 0;
}

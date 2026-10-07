/*
 * A subset of the SDL2 API for the Nintendo 3DS, implemented on libctru and
 * citro3d. Only what the Space Rangers HD platform layer uses is provided.
 * Structure layouts and constants match SDL 2.30 (see platform/SDL2.pas).
 */
#ifndef SDLCTR_H
#define SDLCTR_H

#include <stdint.h>
#include <stddef.h>
#include <3ds.h>

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u

#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_INPUT_FOCUS 0x00000200u
#define SDL_WINDOW_MOUSE_FOCUS 0x00000400u

#define SDL_RENDERER_SOFTWARE 1u
#define SDL_RENDERER_ACCELERATED 2u
#define SDL_RENDERER_PRESENTVSYNC 4u
#define SDL_RENDERER_TARGETTEXTURE 8u

#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET 2

#define SDL_PIXELFORMAT_RGB565 0x15151002u
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_PIXELFORMAT_XRGB8888 0x16161804u

#define SDL_BLENDMODE_NONE 0
#define SDL_BLENDMODE_BLEND 1

#define SDL_QUIT 0x100
#define SDL_WINDOWEVENT 0x200
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_TEXTINPUT 0x303
#define SDL_MOUSEMOTION 0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP 0x402
#define SDL_MOUSEWHEEL 0x403
#define SDL_RENDER_TARGETS_RESET 0x2000
#define SDL_RENDER_DEVICE_RESET 0x2001
#define SDL_USEREVENT 0x8000
#define SDL_LASTEVENT 0xFFFF

#define SDL_WINDOWEVENT_SHOWN 1
#define SDL_WINDOWEVENT_EXPOSED 3
#define SDL_WINDOWEVENT_ENTER 10
#define SDL_WINDOWEVENT_LEAVE 11
#define SDL_WINDOWEVENT_FOCUS_GAINED 12
#define SDL_WINDOWEVENT_FOCUS_LOST 13

#define SDL_ADDEVENT 0
#define SDL_PEEKEVENT 1
#define SDL_GETEVENT 2

#define SDL_PRESSED 1
#define SDL_RELEASED 0
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_LMASK 1u
#define SDL_BUTTON_MMASK 2u
#define SDL_BUTTON_RMASK 4u

#define SDL_MUTEX_TIMEDOUT 1
#define SDL_MUTEX_MAXWAIT 0xFFFFFFFFu

#define AUDIO_S16SYS 0x8010
#define AUDIO_F32SYS 0x8120

#define SDL_NUM_SCANCODES 512

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;
typedef struct SDL_Cursor SDL_Cursor;

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_FPoint { float x, y; } SDL_FPoint;
typedef struct SDL_Color { uint8_t r, g, b, a; } SDL_Color;
typedef struct SDL_Vertex {
	SDL_FPoint position;
	SDL_Color color;
	SDL_FPoint tex_coord;
} SDL_Vertex;

typedef struct SDL_RendererInfo {
	const char *name;
	uint32_t flags;
	uint32_t num_texture_formats;
	uint32_t texture_formats[16];
	int max_texture_width;
	int max_texture_height;
} SDL_RendererInfo;

typedef struct SDL_DisplayMode {
	uint32_t format;
	int w, h, refresh_rate;
	void *driverdata;
} SDL_DisplayMode;

typedef struct SDL_Keysym {
	int scancode;
	int sym;
	uint16_t mod;
	uint32_t unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
	uint32_t type, timestamp, windowID;
	uint8_t state, repeat, padding2, padding3;
	SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
	uint32_t type, timestamp, windowID;
	char text[32];
} SDL_TextInputEvent;

typedef struct SDL_WindowEvent {
	uint32_t type, timestamp, windowID;
	uint8_t event, padding1, padding2, padding3;
	int32_t data1, data2;
} SDL_WindowEvent;

typedef struct SDL_MouseMotionEvent {
	uint32_t type, timestamp, windowID, which, state;
	int32_t x, y, xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
	uint32_t type, timestamp, windowID, which;
	uint8_t button, state, clicks, padding1;
	int32_t x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
	uint32_t type, timestamp, windowID, which;
	int32_t x, y;
	uint32_t direction;
	float preciseX, preciseY;
	int32_t mouseX, mouseY;
} SDL_MouseWheelEvent;

typedef struct SDL_UserEvent {
	uint32_t type, timestamp, windowID;
	int32_t code;
	void *data1, *data2;
} SDL_UserEvent;

typedef union SDL_Event {
	uint32_t type;
	SDL_KeyboardEvent key;
	SDL_TextInputEvent text;
	SDL_WindowEvent window;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_MouseWheelEvent wheel;
	SDL_UserEvent user;
	uint8_t padding[56];
} SDL_Event;

typedef int (*SDL_EventFilter)(void *userdata, SDL_Event *event);
typedef void (*SDL_AudioCallback)(void *userdata, uint8_t *stream, int len);

typedef struct SDL_AudioSpec {
	int freq;
	uint16_t format;
	uint8_t channels;
	uint8_t silence;
	uint16_t samples;
	uint16_t padding;
	uint32_t size;
	SDL_AudioCallback callback;
	void *userdata;
} SDL_AudioSpec;

typedef struct SDL_MessageBoxButtonData {
	uint32_t flags;
	int buttonid;
	const char *text;
} SDL_MessageBoxButtonData;

typedef struct SDL_MessageBoxData {
	uint32_t flags;
	SDL_Window *window;
	const char *title;
	const char *message;
	int numbuttons;
	const SDL_MessageBoxButtonData *buttons;
	const void *colorScheme;
} SDL_MessageBoxData;

/* Opaque surface used only for cursor images. */
typedef struct SDL_Surface {
	int w, h, pitch;
	uint32_t *pixels; /* ARGB8888 copy */
} SDL_Surface;

_Static_assert(sizeof(SDL_Event) == 56, "SDL_Event size");
_Static_assert(sizeof(SDL_AudioSpec) == 24, "SDL_AudioSpec size");
_Static_assert(sizeof(SDL_Vertex) == 20, "SDL_Vertex size");

/* Internal helpers shared by the implementation files. */
int sdlctr_set_error(const char *fmt, ...);
void sdlctr_log(const char *fmt, ...);
uint32_t sdlctr_ticks(void);
void sdlctr_pump_events(void);
int sdlctr_push_event(SDL_Event *event);
void sdlctr_input_frame(void);

/* Presentation state shared between video and input. */
typedef struct sdlctr_view {
	int logical_w, logical_h;   /* game resolution */
	float zoom;                 /* bottom screen scale (screen px per game px) */
	float view_x, view_y;       /* top-left of the zoomed region, game px */
	int swap_screens;           /* 0: overview on top, zoom on bottom */
	int mouse_x, mouse_y;       /* game px */
	uint32_t mouse_buttons;
	int cursor_visible;
	int suspended;              /* HOME menu / sleep */
} sdlctr_view;

extern sdlctr_view sdlctr_view_state;
void sdlctr_view_follow_mouse(void);

#endif

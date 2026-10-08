/* SDL2 subset for the 3DS: window, renderer and textures on citro3d.
 *
 * The game renders into its own target textures (the main one is the
 * game screen, e.g. 1024x768). Drawing to the "window" only happens when
 * the game presents: it copies the screen texture to the window. That copy
 * is recorded, and SDL_RenderPresent shows it on both 3DS screens: one
 * screen shows the whole game screen scaled down, the other a zoomed
 * region that follows the cursor (see sdl_events.c for the controls).
 *
 * Orientation: the PICA samples t = 0 at the last row of texture memory
 * and render targets put row 0 of the projection there too. Pixel row y
 * of an uploaded texture is therefore stored at memory row (th - 1 - y),
 * which gives every texture the same convention: game row y is sampled at
 * t = y / th and rendered with the projection below.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <citro3d.h>
#include "sdlctr.h"
#include "render_shbin.h"

#define MAX_TEX_SIZE 1024
#define VBUF_VERTICES (6 * 32768)
#define CMDBUF_SIZE (1024 * 1024)

#define SCREEN_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct {
	float x, y, u, v;
	uint8_t r, g, b, a;
} vertex;

struct SDL_Window {
	int w, h;
	uint32_t flags;
};

struct SDL_Texture {
	uint32_t format;
	int access;
	int w, h;          /* SDL size */
	int sw, sh;        /* stored size (after downscaling) */
	int shift;         /* downscale shift for textures above 1024 */
	int tw, th;        /* PICA texture size */
	C3D_Tex tex;
	C3D_RenderTarget *target;
	int blend;
	int linear;
	uint32_t used_frame;
	int dead;
	SDL_Texture *next_dead;
};

struct SDL_Renderer {
	SDL_Window *window;
	uint32_t flags;
	int vsync;
};

struct SDL_Cursor {
	int w, h, hot_x, hot_y;
	C3D_Tex tex;
	int tw, th;
};

static SDL_Window main_window;
static SDL_Renderer main_renderer;
static int video_ready;

static DVLB_s *shader_dvlb;
static shaderProgram_s shader;
static int uloc_projection;
static vertex *vbuf;
static int vcount;           /* vertices used in this frame */
static int batch_start;      /* first vertex of the pending batch */

static C3D_RenderTarget *screen_top, *screen_bottom;
static int in_frame;
static uint32_t frame_id = 1;

/* current SDL state */
static SDL_Texture *cur_target;          /* NULL = window */
static uint8_t draw_r, draw_g, draw_b, draw_a = 255;
static int draw_blend;
static int clip_on;
static SDL_Rect clip;
static SDL_Texture *window_source;        /* texture copied to the window */
static SDL_Rect window_source_rect;
static int window_cleared;

/* state of the pending batch */
static SDL_Texture *batch_tex;
static int batch_blend = -1, batch_linear = -1, batch_untextured = -1;
static int bound_valid;

static SDL_Texture *dead_list;
static SDL_Cursor *cur_cursor;
static int cursor_shown = 1;

void sdlctr_events_init(void);
int SDL_LockMutex(SDL_mutex *m);
int SDL_UnlockMutex(SDL_mutex *m);
SDL_mutex *SDL_CreateMutex(void);

/* Texture creation and destruction may happen on loader threads; the GPU
   is driven from the render thread only. */
static SDL_mutex *video_lock;

/* ------------------------------------------------------------ helpers */

static int pow2(int v)
{
	int p = 8;
	while (p < v) p <<= 1;
	return p;
}

static inline u32 morton8(u32 x, u32 y)
{
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

static inline u32 tile_offset_raw(u32 x, u32 y, u32 tw)
{
	return (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) + morton8(x & 7, y & 7);
}

/* offset of game pixel (x, y) in a tiled texture of tw x th */
static inline u32 tile_offset(u32 x, u32 y, u32 tw, u32 th)
{
	return tile_offset_raw(x, th - 1 - y, tw);
}

static inline u32 argb_to_rgba(u32 p) { return (p << 8) | (p >> 24); }
static inline u32 rgba_to_argb(u32 p) { return (p >> 8) | (p << 24); }

static void video_init(void)
{
	C3D_AttrInfo *attr;
	C3D_BufInfo *buf;
	if (video_ready) return;
	video_ready = 1;
	video_lock = SDL_CreateMutex();
	gfxInitDefault();
	C3D_Init(CMDBUF_SIZE);
	screen_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
	C3D_RenderTargetSetOutput(screen_top, GFX_TOP, GFX_LEFT, SCREEN_TRANSFER_FLAGS);
	screen_bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
	C3D_RenderTargetSetOutput(screen_bottom, GFX_BOTTOM, GFX_LEFT, SCREEN_TRANSFER_FLAGS);

	shader_dvlb = DVLB_ParseFile((u32 *)render_shbin, render_shbin_size);
	shaderProgramInit(&shader);
	shaderProgramSetVsh(&shader, &shader_dvlb->DVLE[0]);
	C3D_BindProgram(&shader);
	uloc_projection = shaderInstanceGetUniformLocation(shader.vertexShader, "projection");

	attr = C3D_GetAttrInfo();
	AttrInfo_Init(attr);
	AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 2);         /* v0 position */
	AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);         /* v1 texcoord */
	AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4); /* v2 color */

	vbuf = linearAlloc(VBUF_VERTICES * sizeof(vertex));
	buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, vbuf, sizeof(vertex), 3, 0x210);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	sdlctr_log("video: citro3d ready, vertex buffer %d KiB", (int)(VBUF_VERTICES * sizeof(vertex) / 1024));
}

static void free_texture_now(SDL_Texture *t)
{
	if (t->target) C3D_RenderTargetDelete(t->target);
	C3D_TexDelete(&t->tex);
	free(t);
}

static void collect_dead(void)
{
	SDL_Texture *t = dead_list, *next;
	dead_list = NULL;
	for (; t; t = next) {
		next = t->next_dead;
		free_texture_now(t);
	}
}

static void flush_batch(void);

static void bind_target(void)
{
	C3D_Mtx proj;
	if (!cur_target) return; /* window: nothing to draw into */
	C3D_FrameDrawOn(cur_target->target);
	/* game y = 0 to framebuffer row 0 (see the header comment) */
	Mtx_Ortho(&proj, 0.0f, (float)cur_target->tw, 0.0f, (float)cur_target->th, -1.0f, 1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uloc_projection, &proj);
	bound_valid = 0;
}

static void ensure_frame(void)
{
	if (in_frame) return;
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	in_frame = 1;
	vcount = 0;
	batch_start = 0;
	bind_target();
}

/* Finishes all queued GPU work of the current frame. C3D_FrameSplit only
   submits commands, so end the frame and start an internal one: starting
   waits until the GPU has executed everything, and the command buffer
   starts over. */
static void gpu_sync(void)
{
	if (!in_frame) return;
	flush_batch();
	C3D_FrameEnd(0);
	C3D_FrameBegin(0);
	vcount = 0;
	batch_start = 0;
	frame_id++;
	collect_dead();
	batch_tex = NULL;
	batch_blend = -1;
	bind_target();
}

/* Ends the GPU frame, so that blocking system applets can run. */
void sdlctr_video_quiesce(void)
{
	if (!video_ready || !in_frame) return;
	flush_batch();
	C3D_FrameEnd(0);
	in_frame = 0;
	frame_id++;
	collect_dead();
}

static void apply_scissor(void)
{
	if (clip_on && cur_target) {
		int x0 = clip.x, y0 = clip.y, x1 = clip.x + clip.w, y1 = clip.y + clip.h;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > cur_target->tw) x1 = cur_target->tw;
		if (y1 > cur_target->th) y1 = cur_target->th;
		if (x1 < x0) x1 = x0;
		if (y1 < y0) y1 = y0;
		C3D_SetScissor(GPU_SCISSOR_NORMAL, x0, y0, x1, y1);
	} else {
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	}
}

static void apply_state(SDL_Texture *tex, int blend, int linear)
{
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	if (tex) {
		C3D_TexSetFilter(&tex->tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
		C3D_TexSetWrap(&tex->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		C3D_TexBind(0, &tex->tex);
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		tex->used_frame = frame_id;
	} else {
		C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	if (blend == SDL_BLENDMODE_BLEND)
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		               GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
	else
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	apply_scissor();
}

static void flush_batch(void)
{
	int n = vcount - batch_start;
	if (n <= 0 || !cur_target) {
		batch_start = vcount;
		return;
	}
	apply_state(batch_tex, batch_blend, batch_linear);
	C3D_DrawArrays(GPU_TRIANGLES, batch_start, n);
	batch_start = vcount;
}

/* Returns room for `count` vertices with the given state, flushing as needed. */
static vertex *reserve(int count, SDL_Texture *tex, int blend, int linear)
{
	vertex *v;
	ensure_frame();
	if (vcount + count > VBUF_VERTICES) {
		flush_batch();
		gpu_sync();
	}
	if (tex != batch_tex || blend != batch_blend || (tex && linear != batch_linear) ||
	    batch_untextured != (tex == NULL)) {
		flush_batch();
		batch_tex = tex;
		batch_blend = blend;
		batch_linear = linear;
		batch_untextured = tex == NULL;
	}
	v = &vbuf[vcount];
	vcount += count;
	if (tex) tex->used_frame = frame_id;
	return v;
}

static inline void set_vertex(vertex *v, float x, float y, float u, float t,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	v->x = x; v->y = y; v->u = u; v->v = t;
	v->r = r; v->g = g; v->b = b; v->a = a;
}

static void push_quad(SDL_Texture *tex, int blend, int linear,
                      float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	vertex *v = reserve(6, tex, blend, linear);
	set_vertex(&v[0], x0, y0, u0, v0, r, g, b, a);
	set_vertex(&v[1], x1, y0, u1, v0, r, g, b, a);
	set_vertex(&v[2], x1, y1, u1, v1, r, g, b, a);
	set_vertex(&v[3], x0, y0, u0, v0, r, g, b, a);
	set_vertex(&v[4], x1, y1, u1, v1, r, g, b, a);
	set_vertex(&v[5], x0, y1, u0, v1, r, g, b, a);
}

/* Normalised SDL texture coordinates to PICA texture coordinates. */
static inline float tex_u(SDL_Texture *t, float u) { return u * t->sw / t->tw; }
static inline float tex_v(SDL_Texture *t, float v) { return v * t->sh / t->th; }

/* ------------------------------------------------------------ init / window */

int SDL_InitSubSystem(uint32_t flags)
{
	if (flags & SDL_INIT_VIDEO) {
		video_init();
		sdlctr_events_init();
	}
	return 0;
}

void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }
const char *SDL_GetCurrentVideoDriver(void) { return "n3ds"; }

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags)
{
	(void)title; (void)x; (void)y;
	video_init();
	main_window.w = w;
	main_window.h = h;
	main_window.flags = flags | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
	sdlctr_view_state.logical_w = w;
	sdlctr_view_state.logical_h = h;
	sdlctr_view_follow_mouse();
	sdlctr_log("window %dx%d", w, h);
	return &main_window;
}

void SDL_DestroyWindow(SDL_Window *w) { (void)w; }

uint32_t SDL_GetWindowFlags(SDL_Window *w)
{
	uint32_t f = main_window.flags;
	(void)w;
	if (sdlctr_view_state.suspended) f &= ~(SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS);
	return f;
}

void SDL_SetWindowTitle(SDL_Window *w, const char *t) { (void)w; (void)t; }
void SDL_SetWindowPosition(SDL_Window *w, int x, int y) { (void)w; (void)x; (void)y; }
void SDL_GetWindowPosition(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = 0; if (y) *y = 0; }
void SDL_SetWindowBordered(SDL_Window *w, int b) { (void)w; (void)b; }

void SDL_SetWindowSize(SDL_Window *w, int width, int height)
{
	(void)w;
	main_window.w = width;
	main_window.h = height;
	sdlctr_view_state.logical_w = width;
	sdlctr_view_state.logical_h = height;
	sdlctr_view_follow_mouse();
}

void SDL_GetWindowSize(SDL_Window *w, int *width, int *height)
{
	(void)w;
	if (width) *width = main_window.w;
	if (height) *height = main_window.h;
}

int SDL_SetWindowFullscreen(SDL_Window *w, uint32_t f) { (void)w; (void)f; return 0; }
void SDL_RaiseWindow(SDL_Window *w) { (void)w; }
void SDL_SetWindowGrab(SDL_Window *w, int g) { (void)w; (void)g; }
void SDL_MinimizeWindow(SDL_Window *w) { (void)w; }
SDL_Window *SDL_GetMouseFocus(void) { return &main_window; }

/* The game screen modes offered to the game. 1024x768 is the original
   interface baseline; 800x600 trades layout space for legibility. */
static const struct { int w, h; } modes[] = { { 1024, 768 }, { 800, 600 } };

int SDL_GetNumDisplayModes(int display) { (void)display; return 2; }

int SDL_GetDisplayMode(int display, int index, SDL_DisplayMode *mode)
{
	(void)display;
	if (index < 0 || index >= 2) return sdlctr_set_error("invalid display mode");
	memset(mode, 0, sizeof(*mode));
	mode->format = SDL_PIXELFORMAT_ARGB8888;
	mode->w = modes[index].w;
	mode->h = modes[index].h;
	mode->refresh_rate = 60;
	return 0;
}

int SDL_GetCurrentDisplayMode(int display, SDL_DisplayMode *mode) { return SDL_GetDisplayMode(display, 0, mode); }

/* ------------------------------------------------------------ renderer */

static void fill_info(SDL_RendererInfo *info)
{
	memset(info, 0, sizeof(*info));
	info->name = "citro3d";
	info->flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC;
	info->num_texture_formats = 3;
	info->texture_formats[0] = SDL_PIXELFORMAT_ARGB8888;
	info->texture_formats[1] = SDL_PIXELFORMAT_XRGB8888;
	info->texture_formats[2] = SDL_PIXELFORMAT_RGB565;
	info->max_texture_width = 1024; /* the PICA200 limit */
	info->max_texture_height = 1024;
}

int SDL_GetNumRenderDrivers(void) { return 1; }

int SDL_GetRenderDriverInfo(int index, SDL_RendererInfo *info)
{
	if (index != 0) return sdlctr_set_error("invalid render driver");
	fill_info(info);
	return 0;
}

int SDL_GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info)
{
	(void)r;
	fill_info(info);
	return 0;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags)
{
	(void)index;
	video_init();
	main_renderer.window = window;
	main_renderer.flags = flags;
	main_renderer.vsync = (flags & SDL_RENDERER_PRESENTVSYNC) != 0;
	return &main_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }

int SDL_RenderSetVSync(SDL_Renderer *r, int vsync)
{
	(void)r;
	main_renderer.vsync = vsync;
	return 0;
}

int SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h)
{
	(void)r;
	if (!cur_target && w > 0 && h > 0) {
		sdlctr_view_state.logical_w = w;
		sdlctr_view_state.logical_h = h;
		sdlctr_view_follow_mouse();
	}
	return 0;
}

/* ------------------------------------------------------------ textures */

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, uint32_t format, int access, int w, int h)
{
	SDL_Texture *t;
	GPU_TEXCOLOR gpufmt;
	int ok;
	(void)r;
	if (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_XRGB8888 &&
	    format != SDL_PIXELFORMAT_RGB565) {
		sdlctr_set_error("unsupported texture format %x", (unsigned)format);
		return NULL;
	}
	if (w <= 0 || h <= 0) { sdlctr_set_error("invalid texture size"); return NULL; }
	t = calloc(1, sizeof(*t));
	if (!t) { sdlctr_set_error("out of memory"); return NULL; }
	t->format = format;
	t->access = access;
	t->w = w;
	t->h = h;
	while ((w >> t->shift) > MAX_TEX_SIZE || (h >> t->shift) > MAX_TEX_SIZE) t->shift++;
	if (t->shift && access == SDL_TEXTUREACCESS_TARGET) {
		free(t);
		sdlctr_set_error("render target %dx%d is larger than %d", w, h, MAX_TEX_SIZE);
		return NULL;
	}
	t->sw = (w + (1 << t->shift) - 1) >> t->shift;
	t->sh = (h + (1 << t->shift) - 1) >> t->shift;
	t->tw = pow2(t->sw);
	t->th = pow2(t->sh);
	t->linear = 1;
	gpufmt = format == SDL_PIXELFORMAT_RGB565 ? GPU_RGB565 : GPU_RGBA8;
	/* VRAM is two 3 MiB banks: large render targets (the 1024x768 game
	   screen) use 16-bit color, like the game's own software renderer */
	if (access == SDL_TEXTUREACCESS_TARGET && (size_t)t->tw * t->th * 4 > 2 * 1024 * 1024)
		gpufmt = GPU_RGB565;
	/* Textures destroyed this frame still hold their memory until the GPU is
	   done with them; finish the frame and free them before giving up. */
	if (dead_list && linearSpaceFree() < (u32)t->tw * t->th * 4 + 64 * 1024) {
		if (in_frame) gpu_sync();
		else collect_dead();
	}
	SDL_LockMutex(video_lock);
	if (access == SDL_TEXTUREACCESS_TARGET) {
		ok = C3D_TexInitVRAM(&t->tex, t->tw, t->th, gpufmt);
		if (!ok) ok = C3D_TexInit(&t->tex, t->tw, t->th, gpufmt);
		if (ok) {
			t->target = C3D_RenderTargetCreateFromTex(&t->tex, GPU_TEXFACE_2D, 0, -1);
			if (!t->target) { C3D_TexDelete(&t->tex); ok = 0; }
		}
	} else {
		ok = C3D_TexInit(&t->tex, t->tw, t->th, gpufmt);
		if (ok) {
			memset(t->tex.data, 0, t->tex.size);
			C3D_TexFlush(&t->tex);
		}
	}
	SDL_UnlockMutex(video_lock);
	if (!ok) {
		free(t);
		sdlctr_set_error("out of GPU memory for a %dx%d texture", w, h);
		return NULL;
	}
	if (t->target) {
		/* start transparent black, like other SDL backends */
		C3D_RenderTargetClear(t->target, C3D_CLEAR_COLOR, 0, 0);
	}
	return t;
}

void SDL_DestroyTexture(SDL_Texture *t)
{
	if (!t) return;
	if (t == cur_target) { flush_batch(); cur_target = NULL; }
	if (t == batch_tex) { flush_batch(); batch_tex = NULL; batch_blend = -1; }
	if (t == window_source) window_source = NULL;
	if (in_frame && t->used_frame == frame_id) {
		/* still referenced by queued GPU work */
		t->next_dead = dead_list;
		dead_list = t;
		return;
	}
	free_texture_now(t);
}

int SDL_SetTextureBlendMode(SDL_Texture *t, int mode)
{
	if (t) t->blend = mode;
	return 0;
}

int SDL_SetTextureScaleMode(SDL_Texture *t, int mode)
{
	if (t) t->linear = mode != 0;
	return 0;
}

/* Converts the source to the texture's PICA format and tiles it. step is
   the number of source pixels per stored texel: 1 << shift for a full-size
   source, 1 for a source already at the stored size. */
static void upload_rows(SDL_Texture *t, const uint8_t *pixels, int pitch, uint32_t src_format, int step)
{
	int x, y;
	if (t->tex.fmt == GPU_RGB565) {
		u16 *dst = (u16 *)t->tex.data;
		for (y = 0; y < t->sh; y++) {
			const u16 *row = (const u16 *)(pixels + (size_t)(y * step) * pitch);
			for (x = 0; x < t->sw; x++)
				dst[tile_offset(x, y, t->tw, t->th)] = row[x * step];
		}
	} else if (src_format == SDL_PIXELFORMAT_RGB565) {
		u32 *dst = (u32 *)t->tex.data;
		for (y = 0; y < t->sh; y++) {
			const u16 *row = (const u16 *)(pixels + (size_t)(y * step) * pitch);
			for (x = 0; x < t->sw; x++) {
				u16 p = row[x * step];
				u32 r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
				r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
				dst[tile_offset(x, y, t->tw, t->th)] = (r << 24) | (g << 16) | (b << 8) | 0xFF;
			}
		}
	} else {
		u32 *dst = (u32 *)t->tex.data;
		int opaque = src_format == SDL_PIXELFORMAT_XRGB8888;
		for (y = 0; y < t->sh; y++) {
			const u32 *row = (const u32 *)(pixels + (size_t)(y * step) * pitch);
			for (x = 0; x < t->sw; x++) {
				u32 p = row[x * step];
				dst[tile_offset(x, y, t->tw, t->th)] = opaque ? (p << 8) | 0xFF : argb_to_rgba(p);
			}
		}
	}
}

int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch)
{
	if (!t) return sdlctr_set_error("invalid texture");
	if (rect && (rect->x != 0 || rect->y != 0 || rect->w != t->w || rect->h != t->h))
		return sdlctr_set_error("partial texture updates are not supported");
	if (t->target) {
		/* not used by the game: render targets get their content from drawing */
		return sdlctr_set_error("updating a render target is not supported");
	}
	if (in_frame && t->used_frame == frame_id) gpu_sync();
	upload_rows(t, pixels, pitch, t->format, 1 << t->shift);
	C3D_TexFlush(&t->tex);
	return 0;
}

/* Port extension: update a texture from pixels already at its stored
   (downscaled) size, (w + (1 << shift) - 1) >> shift wide, so that the
   caller does not need a full-size copy of a texture above 1024 pixels. */
int SDL_CTR_UpdateTextureStored(SDL_Texture *t, const void *pixels, int pitch)
{
	if (!t) return sdlctr_set_error("invalid texture");
	if (t->target) return sdlctr_set_error("updating a render target is not supported");
	if (in_frame && t->used_frame == frame_id) gpu_sync();
	upload_rows(t, pixels, pitch, t->format, 1);
	C3D_TexFlush(&t->tex);
	return 0;
}

/* ------------------------------------------------------------ drawing */

int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t)
{
	(void)r;
	if (t && !t->target) return sdlctr_set_error("texture is not a render target");
	if (t == cur_target) return 0;
	flush_batch();
	cur_target = t;
	if (in_frame) bind_target();
	if (!t) {
		window_cleared = 0;
	}
	return 0;
}

SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *r) { (void)r; return cur_target; }

int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect)
{
	int on = rect != NULL;
	(void)r;
	if (on == clip_on && (!on || memcmp(rect, &clip, sizeof(clip)) == 0)) return 0;
	flush_batch();
	clip_on = on;
	if (on) clip = *rect;
	return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
	(void)r;
	draw_r = red; draw_g = green; draw_b = blue; draw_a = alpha;
	return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode)
{
	(void)r;
	draw_blend = mode;
	return 0;
}

int SDL_RenderClear(SDL_Renderer *r)
{
	int saved;
	SDL_Rect saved_clip;
	(void)r;
	if (!cur_target) {
		window_source = NULL;
		window_cleared = 1;
		return 0;
	}
	/* SDL clears the whole target, ignoring the clip rectangle */
	saved = clip_on;
	saved_clip = clip;
	if (clip_on) { flush_batch(); clip_on = 0; }
	push_quad(NULL, SDL_BLENDMODE_NONE, 0, 0, 0, (float)cur_target->tw, (float)cur_target->th,
	          0, 0, 0, 0, draw_r, draw_g, draw_b, draw_a);
	if (saved) { flush_batch(); clip_on = saved; clip = saved_clip; }
	return 0;
}

int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect)
{
	float x0, y0, x1, y1;
	(void)r;
	if (!cur_target) return 0;
	if (rect) {
		x0 = (float)rect->x; y0 = (float)rect->y;
		x1 = (float)(rect->x + rect->w); y1 = (float)(rect->y + rect->h);
	} else {
		x0 = 0; y0 = 0; x1 = (float)cur_target->w; y1 = (float)cur_target->h;
	}
	push_quad(NULL, draw_blend, 0, x0, y0, x1, y1, 0, 0, 0, 0, draw_r, draw_g, draw_b, draw_a);
	return 0;
}

int SDL_RenderDrawPointF(SDL_Renderer *r, float x, float y)
{
	float px, py;
	(void)r;
	if (!cur_target) return 0;
	px = (float)(int)x;
	py = (float)(int)y;
	if (x < 0) px -= 1;
	if (y < 0) py -= 1;
	push_quad(NULL, draw_blend, 0, px, py, px + 1, py + 1, 0, 0, 0, 0, draw_r, draw_g, draw_b, draw_a);
	return 0;
}

int SDL_RenderDrawLineF(SDL_Renderer *r, float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1, dy = y2 - y1, len, nx, ny;
	vertex *v;
	(void)r;
	if (!cur_target) return 0;
	len = sqrtf(dx * dx + dy * dy);
	if (len < 0.0001f) return SDL_RenderDrawPointF(r, x1, y1);
	/* a one pixel wide quad, extended by half a pixel at both ends */
	nx = -dy * 0.5f / len;
	ny = dx * 0.5f / len;
	x1 -= dx * 0.5f / len; y1 -= dy * 0.5f / len;
	x2 += dx * 0.5f / len; y2 += dy * 0.5f / len;
	v = reserve(6, NULL, draw_blend, 0);
	set_vertex(&v[0], x1 + nx, y1 + ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	set_vertex(&v[1], x2 + nx, y2 + ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	set_vertex(&v[2], x2 - nx, y2 - ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	set_vertex(&v[3], x1 + nx, y1 + ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	set_vertex(&v[4], x2 - nx, y2 - ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	set_vertex(&v[5], x1 - nx, y1 - ny, 0, 0, draw_r, draw_g, draw_b, draw_a);
	return 0;
}

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst)
{
	SDL_Rect s, d;
	(void)r;
	if (!t) return sdlctr_set_error("invalid texture");
	s = src ? *src : (SDL_Rect){ 0, 0, t->w, t->h };
	if (!cur_target) {
		/* the game presents its screen: remember what to show */
		window_source = t;
		window_source_rect = s;
		return 0;
	}
	d = dst ? *dst : (SDL_Rect){ 0, 0, cur_target->w, cur_target->h };
	push_quad(t, t->blend, t->linear,
	          (float)d.x, (float)d.y, (float)(d.x + d.w), (float)(d.y + d.h),
	          tex_u(t, (float)s.x / t->w), tex_v(t, (float)s.y / t->h),
	          tex_u(t, (float)(s.x + s.w) / t->w), tex_v(t, (float)(s.y + s.h) / t->h),
	          255, 255, 255, 255);
	return 0;
}

int SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *vertices, int num_vertices,
                       const int *indices, int num_indices)
{
	int i, n, blend;
	vertex *v;
	(void)r;
	if (!cur_target) return 0;
	n = indices ? num_indices : num_vertices;
	n -= n % 3;
	if (n <= 0) return 0;
	blend = t ? t->blend : draw_blend;
	/* SDL_RenderGeometry blends with the texture's mode, or the draw mode */
	for (i = 0; i < n; ) {
		int chunk = n - i, j;
		if (chunk > 3 * 4096) chunk = 3 * 4096;
		v = reserve(chunk, t, blend, t ? t->linear : 0);
		for (j = 0; j < chunk; j++) {
			const SDL_Vertex *s = &vertices[indices ? indices[i + j] : i + j];
			v[j].x = s->position.x;
			v[j].y = s->position.y;
			v[j].u = t ? tex_u(t, s->tex_coord.x) : 0;
			v[j].v = t ? tex_v(t, s->tex_coord.y) : 0;
			v[j].r = s->color.r;
			v[j].g = s->color.g;
			v[j].b = s->color.b;
			v[j].a = s->color.a;
		}
		i += chunk;
	}
	return 0;
}

/* ------------------------------------------------------------ readback */

static inline u32 texel_argb(const SDL_Texture *t, u32 x, u32 y)
{
	if (t->tex.fmt == GPU_RGB565) {
		u16 p = ((const u16 *)t->tex.data)[tile_offset(x, y, t->tw, t->th)];
		u32 r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
		return 0xFF000000u | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) |
		       ((b5 << 3) | (b5 >> 2));
	}
	return rgba_to_argb(((const u32 *)t->tex.data)[tile_offset(x, y, t->tw, t->th)]);
}

/* Port extension: read a static texture back at its SDL size. The texture
   lives in linear memory, so this lets the game drop its CPU-side copy after
   uploading; a downscaled texture comes back upscaled (nearest). */
static int read_texture(SDL_Texture *t, uint32_t format, void *pixels, int pitch, int shift)
{
	int x, y, w = shift ? t->w : t->sw, h = shift ? t->h : t->sh;
	if (t->target) return sdlctr_set_error("use SDL_RenderReadPixels for render targets");
	for (y = 0; y < h; y++) {
		uint8_t *row = (uint8_t *)pixels + (size_t)y * pitch;
		u32 sy = (u32)y >> shift;
		if (sy >= (u32)t->sh) sy = t->sh - 1;
		for (x = 0; x < w; x++) {
			u32 sx = (u32)x >> shift;
			u32 argb;
			if (sx >= (u32)t->sw) sx = t->sw - 1;
			argb = texel_argb(t, sx, sy);
			if (format == SDL_PIXELFORMAT_RGB565)
				((u16 *)row)[x] = (u16)(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
			else if (format == SDL_PIXELFORMAT_XRGB8888)
				((u32 *)row)[x] = argb | 0xFF000000u;
			else
				((u32 *)row)[x] = argb;
		}
	}
	return 0;
}

int SDL_CTR_ReadTexture(SDL_Texture *t, uint32_t format, void *pixels, int pitch)
{
	return t ? read_texture(t, format, pixels, pitch, t->shift) : sdlctr_set_error("invalid texture");
}

/* Reads a static texture at its stored size (see SDL_CTR_UpdateTextureStored). */
int SDL_CTR_ReadTextureStored(SDL_Texture *t, uint32_t format, void *pixels, int pitch)
{
	return t ? read_texture(t, format, pixels, pitch, 0) : sdlctr_set_error("invalid texture");
}

int SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, uint32_t format, void *pixels, int pitch)
{
	SDL_Rect a;
	SDL_Texture *t = cur_target;
	const u32 *data;
	int x, y;
	(void)r;
	if (!t) return sdlctr_set_error("reading the window is not supported");
	a = rect ? *rect : (SDL_Rect){ 0, 0, t->w, t->h };
	ensure_frame();
	gpu_sync();
	GSPGPU_InvalidateDataCache(t->tex.data, t->tex.size);
	data = (const u32 *)t->tex.data;
	for (y = 0; y < a.h; y++) {
		uint8_t *row = (uint8_t *)pixels + (size_t)y * pitch;
		for (x = 0; x < a.w; x++) {
			u32 argb;
			if (t->tex.fmt == GPU_RGB565) {
				u16 p = ((const u16 *)data)[tile_offset(a.x + x, a.y + y, t->tw, t->th)];
				u32 r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
				argb = 0xFF000000u | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) |
				       ((b5 << 3) | (b5 >> 2));
			} else {
				argb = rgba_to_argb(data[tile_offset(a.x + x, a.y + y, t->tw, t->th)]);
			}
			if (format == SDL_PIXELFORMAT_RGB565) {
				((u16 *)row)[x] = (u16)(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
			} else if (format == SDL_PIXELFORMAT_XRGB8888) {
				((u32 *)row)[x] = argb | 0xFF000000u;
			} else {
				((u32 *)row)[x] = argb;
			}
		}
	}
	return 0;
}

int SDL_ConvertPixels(int w, int h, uint32_t src_format, const void *src, int src_pitch,
                      uint32_t dst_format, void *dst, int dst_pitch)
{
	int x, y;
	for (y = 0; y < h; y++) {
		const uint8_t *s = (const uint8_t *)src + (size_t)y * src_pitch;
		uint8_t *d = (uint8_t *)dst + (size_t)y * dst_pitch;
		for (x = 0; x < w; x++) {
			u32 argb;
			if (src_format == SDL_PIXELFORMAT_RGB565) {
				u16 p = ((const u16 *)s)[x];
				u32 r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
				argb = 0xFF000000u | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) |
				       ((b5 << 3) | (b5 >> 2));
			} else {
				argb = ((const u32 *)s)[x];
				if (src_format == SDL_PIXELFORMAT_XRGB8888) argb |= 0xFF000000u;
			}
			if (dst_format == SDL_PIXELFORMAT_RGB565)
				((u16 *)d)[x] = (u16)(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
			else if (dst_format == SDL_PIXELFORMAT_XRGB8888)
				((u32 *)d)[x] = argb | 0xFF000000u;
			else
				((u32 *)d)[x] = argb;
		}
	}
	return 0;
}

/* ------------------------------------------------------------ cursor */

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch,
                                      uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask)
{
	SDL_Surface *s;
	int y;
	(void)rmask; (void)gmask; (void)bmask; (void)amask;
	if (depth != 32) { sdlctr_set_error("only 32-bit surfaces are supported"); return NULL; }
	s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->w = w;
	s->h = h;
	s->pitch = w * 4;
	s->pixels = malloc((size_t)w * h * 4);
	if (!s->pixels) { free(s); return NULL; }
	for (y = 0; y < h; y++)
		memcpy(s->pixels + (size_t)y * w, (uint8_t *)pixels + (size_t)y * pitch, (size_t)w * 4);
	return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s) return;
	free(s->pixels);
	free(s);
}

SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *s, int hot_x, int hot_y)
{
	SDL_Cursor *c;
	int x, y;
	if (!s) return NULL;
	c = calloc(1, sizeof(*c));
	if (!c) return NULL;
	c->w = s->w;
	c->h = s->h;
	c->hot_x = hot_x;
	c->hot_y = hot_y;
	c->tw = pow2(s->w);
	c->th = pow2(s->h);
	if (!C3D_TexInit(&c->tex, c->tw, c->th, GPU_RGBA8)) { free(c); return NULL; }
	memset(c->tex.data, 0, c->tex.size);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++)
			((u32 *)c->tex.data)[tile_offset(x, y, c->tw, c->th)] = argb_to_rgba(s->pixels[(size_t)y * s->w + x]);
	C3D_TexFlush(&c->tex);
	C3D_TexSetFilter(&c->tex, GPU_LINEAR, GPU_LINEAR);
	return c;
}

void SDL_SetCursor(SDL_Cursor *c)
{
	if (c) cur_cursor = c;
}

void SDL_FreeCursor(SDL_Cursor *c)
{
	if (!c) return;
	if (c == cur_cursor) cur_cursor = NULL;
	if (in_frame) gpu_sync();
	C3D_TexDelete(&c->tex);
	free(c);
}

int SDL_ShowCursor(int toggle)
{
	if (toggle >= 0) cursor_shown = toggle != 0;
	sdlctr_view_state.cursor_visible = cursor_shown;
	return cursor_shown;
}

/* ------------------------------------------------------------ present */

static void draw_screen_quad(C3D_RenderTarget *target, int width,
                             float sx, float sy, float sw, float sh,
                             float dx, float dy, float dw, float dh, int linear)
{
	SDL_Texture *t = window_source;
	C3D_Mtx proj;
	vertex *v;
	C3D_FrameDrawOn(target);
	Mtx_OrthoTilt(&proj, 0.0f, (float)width, 240.0f, 0.0f, -1.0f, 1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uloc_projection, &proj);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	if (!t) return;
	v = &vbuf[vcount];
	set_vertex(&v[0], dx, dy, sx / t->tw, sy / t->th, 255, 255, 255, 255);
	set_vertex(&v[1], dx + dw, dy, (sx + sw) / t->tw, sy / t->th, 255, 255, 255, 255);
	set_vertex(&v[2], dx + dw, dy + dh, (sx + sw) / t->tw, (sy + sh) / t->th, 255, 255, 255, 255);
	set_vertex(&v[3], dx, dy, sx / t->tw, sy / t->th, 255, 255, 255, 255);
	set_vertex(&v[4], dx + dw, dy + dh, (sx + sw) / t->tw, (sy + sh) / t->th, 255, 255, 255, 255);
	set_vertex(&v[5], dx, dy + dh, sx / t->tw, (sy + sh) / t->th, 255, 255, 255, 255);
	apply_state(t, SDL_BLENDMODE_NONE, linear);
	C3D_DrawArrays(GPU_TRIANGLES, vcount, 6);
	vcount += 6;
	batch_start = vcount;
}

static void draw_rect_outline(float x, float y, float w, float h, uint8_t a)
{
	vertex *v = &vbuf[vcount];
	float t = 1.0f;
	float r[4][4] = { { x, y, x + w, y + t }, { x, y + h - t, x + w, y + h },
	                  { x, y, x + t, y + h }, { x + w - t, y, x + w, y + h } };
	int i;
	for (i = 0; i < 4; i++) {
		set_vertex(&v[i * 6 + 0], r[i][0], r[i][1], 0, 0, 255, 255, 255, a);
		set_vertex(&v[i * 6 + 1], r[i][2], r[i][1], 0, 0, 255, 255, 255, a);
		set_vertex(&v[i * 6 + 2], r[i][2], r[i][3], 0, 0, 255, 255, 255, a);
		set_vertex(&v[i * 6 + 3], r[i][0], r[i][1], 0, 0, 255, 255, 255, a);
		set_vertex(&v[i * 6 + 4], r[i][2], r[i][3], 0, 0, 255, 255, 255, a);
		set_vertex(&v[i * 6 + 5], r[i][0], r[i][3], 0, 0, 255, 255, 255, a);
	}
	apply_state(NULL, SDL_BLENDMODE_BLEND, 0);
	C3D_DrawArrays(GPU_TRIANGLES, vcount, 24);
	vcount += 24;
	batch_start = vcount;
}

static void draw_cursor(float ox, float oy, float scale)
{
	SDL_Cursor *c = cur_cursor;
	sdlctr_view *vs = &sdlctr_view_state;
	vertex *v;
	float x0, y0, x1, y1, u1, v1;
	C3D_TexEnv *env;
	if (!c || !cursor_shown) return;
	x0 = ox + (vs->mouse_x - c->hot_x) * scale;
	y0 = oy + (vs->mouse_y - c->hot_y) * scale;
	/* keep the cursor legible when the view is scaled down */
	if (scale < 0.75f) scale = 0.75f;
	x1 = x0 + c->w * scale;
	y1 = y0 + c->h * scale;
	u1 = (float)c->w / c->tw;
	v1 = (float)c->h / c->th;
	v = &vbuf[vcount];
	set_vertex(&v[0], x0, y0, 0, 0, 255, 255, 255, 255);
	set_vertex(&v[1], x1, y0, u1, 0, 255, 255, 255, 255);
	set_vertex(&v[2], x1, y1, u1, v1, 255, 255, 255, 255);
	set_vertex(&v[3], x0, y0, 0, 0, 255, 255, 255, 255);
	set_vertex(&v[4], x1, y1, u1, v1, 255, 255, 255, 255);
	set_vertex(&v[5], x0, y1, 0, v1, 255, 255, 255, 255);
	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexBind(0, &c->tex);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
	               GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_DrawArrays(GPU_TRIANGLES, vcount, 6);
	vcount += 6;
	batch_start = vcount;
}

/* Debug aid: while frame_dump.txt exists next to the game data, every 20th
   presented game frame is written to frameNNN.bmp (the full logical frame,
   before it is scaled to the 3DS screens). */
static void dump_frame(SDL_Texture *t)
{
	static unsigned count;
	struct stat st;
	FILE *f;
	int x, y, w, h;
	char name[64];
	if (!t || ++count % 20 != 0) return;
	if (stat("sdmc:/3ds/SpaceRangersHD/frame_dump.txt", &st) != 0) return;
	snprintf(name, sizeof(name), "sdmc:/3ds/SpaceRangersHD/frame%03u.bmp", count / 20 % 1000);
	f = fopen(name, "wb");
	if (!f) return;
	w = window_source_rect.w; h = window_source_rect.h;
	gpu_sync();
	GSPGPU_InvalidateDataCache(t->tex.data, t->tex.size);
	{
		uint32_t row = (uint32_t)w * 3, pad = (4 - row % 4) % 4, size = (row + pad) * h;
		uint8_t hdr[54] = { 'B', 'M' };
		uint32_t v[] = { 54 + size, 0, 54, 40, (uint32_t)w, (uint32_t)h, 1 | (24 << 16), 0, size, 2835, 2835, 0, 0 };
		memcpy(hdr + 2, v, sizeof(v));
		fwrite(hdr, 1, 54, f);
		for (y = h - 1; y >= 0; y--) {
			for (x = 0; x < w; x++) {
				u32 c = texel_argb(t, window_source_rect.x + x, window_source_rect.y + y);
				uint8_t px[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
				fwrite(px, 1, 3, f);
			}
			fwrite("\0\0\0", 1, pad, f);
		}
	}
	fclose(f);
}

void SDL_RenderPresent(SDL_Renderer *r)
{
	sdlctr_view *vs = &sdlctr_view_state;
	SDL_Texture *t;
	float gw, gh, fit, ow, oh, ox, oy, zw, zh;
	C3D_RenderTarget *overview, *zoomed;
	int overview_w, zoomed_w;
	(void)r;
	sdlctr_pump_events();
	ensure_frame();
	flush_batch();
	if (vcount + 64 > VBUF_VERTICES) gpu_sync();
	t = window_source;
	dump_frame(t);
	gw = (float)(t ? window_source_rect.w : vs->logical_w);
	gh = (float)(t ? window_source_rect.h : vs->logical_h);

	if (vs->swap_screens) {
		overview = screen_bottom; overview_w = 320;
		zoomed = screen_top; zoomed_w = 400;
	} else {
		overview = screen_top; overview_w = 400;
		zoomed = screen_bottom; zoomed_w = 320;
	}
	C3D_RenderTargetClear(screen_top, C3D_CLEAR_ALL, 0x000000FF, 0);
	C3D_RenderTargetClear(screen_bottom, C3D_CLEAR_ALL, 0x000000FF, 0);

	/* whole game screen */
	fit = overview_w / gw;
	if (240.0f / gh < fit) fit = 240.0f / gh;
	ow = gw * fit;
	oh = gh * fit;
	ox = (overview_w - ow) / 2;
	oy = (240 - oh) / 2;
	draw_screen_quad(overview, overview_w, 0, 0, gw, gh, ox, oy, ow, oh, 1);
	if (t) {
		zw = zoomed_w / vs->zoom;
		zh = 240.0f / vs->zoom;
		draw_rect_outline(ox + vs->view_x * fit, oy + vs->view_y * fit, zw * fit, zh * fit, 160);
		draw_cursor(ox, oy, fit);
	}

	/* zoomed region */
	zw = zoomed_w / vs->zoom;
	zh = 240.0f / vs->zoom;
	if (zw > gw) zw = gw;
	if (zh > gh) zh = gh;
	draw_screen_quad(zoomed, zoomed_w, vs->view_x, vs->view_y, zw, zh,
	                 (zoomed_w - zw * vs->zoom) / 2, (240 - zh * vs->zoom) / 2,
	                 zw * vs->zoom, zh * vs->zoom, vs->zoom < 0.99f);
	if (t) draw_cursor((zoomed_w - zw * vs->zoom) / 2 - vs->view_x * vs->zoom,
	                   (240 - zh * vs->zoom) / 2 - vs->view_y * vs->zoom, vs->zoom);

	C3D_FrameRate(main_renderer.vsync ? 60.0f : 0.0f);
	C3D_FrameEnd(0);
	in_frame = 0;
	frame_id++;
	collect_dead();
	/* the next draw starts a new frame and rebinds the current target */
	batch_tex = NULL;
	batch_blend = -1;
	sdlctr_input_frame();
}

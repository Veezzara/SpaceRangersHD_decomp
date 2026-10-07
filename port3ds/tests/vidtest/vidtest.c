/* Checks the SDL renderer on the 3DS: orientation of render targets and
 * uploaded textures, clipping, blending and readback. Writes
 * sdmc:/vidtest.log ending with "RESULT: PASS" and shows a test picture. */
#include <stdio.h>
#include <string.h>
#include "sdlctr.h"

SDL_Window *SDL_CreateWindow(const char *, int, int, int, int, uint32_t);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *, int, uint32_t);
int SDL_InitSubSystem(uint32_t);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, uint32_t, int, int, int);
int SDL_SetRenderTarget(SDL_Renderer *, SDL_Texture *);
int SDL_SetRenderDrawColor(SDL_Renderer *, uint8_t, uint8_t, uint8_t, uint8_t);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *, int);
int SDL_RenderClear(SDL_Renderer *);
int SDL_RenderFillRect(SDL_Renderer *, const SDL_Rect *);
int SDL_RenderReadPixels(SDL_Renderer *, const SDL_Rect *, uint32_t, void *, int);
int SDL_UpdateTexture(SDL_Texture *, const SDL_Rect *, const void *, int);
int SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
int SDL_RenderSetClipRect(SDL_Renderer *, const SDL_Rect *);
int SDL_SetTextureBlendMode(SDL_Texture *, int);
int SDL_RenderGeometry(SDL_Renderer *, SDL_Texture *, const SDL_Vertex *, int, const int *, int);
void SDL_RenderPresent(SDL_Renderer *);
int SDL_PollEvent(SDL_Event *);
int SDL_SetTextureScaleMode(SDL_Texture *, int);

static FILE *logf;
static int failures;

static void check(int ok, const char *what, uint32_t got)
{
	fprintf(logf, "%s %s (got %08lX)\n", ok ? "ok  " : "FAIL", what, (unsigned long)got);
	fflush(logf);
	if (!ok) failures++;
}

static uint32_t px[64 * 64];

static uint32_t at(int x, int y) { return px[y * 64 + x]; }

int main(void)
{
	SDL_Renderer *r;
	SDL_Texture *target, *img, *screen;
	static uint32_t pixels[32 * 32];
	SDL_Rect rc;
	int x, y, frames;
	SDL_Vertex tri[3];

	logf = fopen("sdmc:/vidtest.log", "w");
	SDL_InitSubSystem(SDL_INIT_VIDEO);
	r = SDL_CreateRenderer(SDL_CreateWindow("t", 0, 0, 1024, 768, 0), -1, SDL_RENDERER_PRESENTVSYNC);

	target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 64, 64);
	SDL_SetRenderTarget(r, target);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
	rc = (SDL_Rect){ 0, 0, 16, 8 };
	SDL_RenderFillRect(r, &rc);

	/* uploaded texture: top half green, bottom half blue */
	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++)
			pixels[y * 32 + x] = y < 16 ? 0xFF00FF00u : 0xFF0000FFu;
	img = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
	SDL_UpdateTexture(img, NULL, pixels, 32 * 4);
	SDL_SetTextureBlendMode(img, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(img, 0);
	rc = (SDL_Rect){ 0, 32, 32, 32 };
	SDL_RenderCopy(r, img, NULL, &rc);

	/* clipped white fill */
	rc = (SDL_Rect){ 40, 0, 8, 8 };
	SDL_RenderSetClipRect(r, &rc);
	SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
	SDL_RenderFillRect(r, NULL);
	SDL_RenderSetClipRect(r, NULL);

	/* blended half transparent yellow over black */
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, 255, 255, 0, 128);
	rc = (SDL_Rect){ 48, 48, 8, 8 };
	SDL_RenderFillRect(r, &rc);

	/* untextured geometry triangle in the right half */
	memset(tri, 0, sizeof(tri));
	tri[0].position = (SDL_FPoint){ 32, 16 };
	tri[1].position = (SDL_FPoint){ 64, 16 };
	tri[2].position = (SDL_FPoint){ 64, 32 };
	for (x = 0; x < 3; x++) tri[x].color = (SDL_Color){ 0, 0, 255, 255 };
	SDL_RenderGeometry(r, NULL, tri, 3, NULL, 0);

	SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, px, 64 * 4);
	check(at(2, 2) == 0xFFFF0000u, "fill at top-left is red", at(2, 2));
	check(at(2, 12) == 0xFF000000u, "below the fill is black", at(2, 12));
	check(at(20, 2) == 0xFF000000u, "right of the fill is black", at(20, 2));
	check(at(10, 36) == 0xFF00FF00u, "copied texture top is green", at(10, 36));
	check(at(10, 60) == 0xFF0000FFu, "copied texture bottom is blue", at(10, 60));
	check(at(44, 4) == 0xFFFFFFFFu, "inside the clip rectangle is white", at(44, 4));
	check(at(36, 4) == 0xFF000000u, "left of the clip rectangle is black", at(36, 4));
	check(at(44, 12) == 0xFF000000u, "below the clip rectangle is black", at(44, 12));
	check(((at(50, 50) >> 16) & 0xFF) >= 120 && ((at(50, 50) >> 16) & 0xFF) <= 136, "50% blend", at(50, 50));
	check(at(62, 18) == 0xFF0000FFu, "triangle drawn", at(62, 18));
	check(at(34, 30) == 0xFF000000u, "outside the triangle", at(34, 30));

	/* a game screen with corner markers, shown on both screens */
	screen = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1024, 768);
	SDL_SetRenderTarget(r, screen);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 20, 30, 60, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawColor(r, 255, 0, 0, 255); rc = (SDL_Rect){ 0, 0, 200, 100 }; SDL_RenderFillRect(r, &rc);
	SDL_SetRenderDrawColor(r, 0, 255, 0, 255); rc = (SDL_Rect){ 824, 0, 200, 100 }; SDL_RenderFillRect(r, &rc);
	SDL_SetRenderDrawColor(r, 0, 0, 255, 255); rc = (SDL_Rect){ 0, 668, 200, 100 }; SDL_RenderFillRect(r, &rc);
	SDL_SetRenderDrawColor(r, 255, 255, 255, 255); rc = (SDL_Rect){ 824, 668, 200, 100 }; SDL_RenderFillRect(r, &rc);
	rc = (SDL_Rect){ 300, 200, 64, 64 };
	SDL_RenderCopy(r, target, NULL, &rc);
	{
		uint32_t row[4];
		rc = (SDL_Rect){ 1020, 700, 4, 1 };
		SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ARGB8888, row, 16);
		check(row[0] == 0xFFFFFFFFu, "1024x768 target bottom-right corner", row[0]);
	}
	fprintf(logf, failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	fclose(logf);

	for (frames = 0; frames < 100000; frames++) {
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (e.type == SDL_QUIT) frames = 100000;
		SDL_SetRenderTarget(r, NULL);
#ifndef SHOW
#define SHOW screen
#endif
		SDL_RenderCopy(r, SHOW, NULL, NULL);
		SDL_RenderPresent(r);
		SDL_SetRenderTarget(r, screen);
	}
	return 0;
}

/*
 * assetconv: downscales the images of Space Rangers HD packages for the 3DS.
 *
 *   assetconv [-s shift] [-m min-area] [-j jobs] [-z level] [-v] in.pkg out.pkg
 *
 * The HD art is drawn 1:1 in the game's 1024x768 space, which the 3DS shows
 * at 400x240 (or zoomed in). Storing it at 1 / (1 << shift) of its size cuts
 * the memory, loading and decoding time of the images by 4x (shift 1) or 16x
 * (shift 2), at the cost of detail when zoomed in.
 *
 * Downscaled images are ordinary images of the stored size, marked so that the
 * game (port3ds patches, TgiGR.ScaleShift) keeps reporting their original
 * size and position:
 *
 *   GI header Version = 0x53430000 + shift (originals have 1)
 *   GI header bytes 56..63 = logical Left, Top (int16), Width, Height (uint16)
 *   GAI header bytes 40..47 = "SC", shift, 0, 0, 0, 0, 0
 *
 * Converted:
 *   - .hai ship sprites (palette images, mapped back to their palettes);
 *   - .gi images of formats 0, 1, 2 and 3 with at least min-area pixels
 *     (format 0 stays raw, the others become format 2);
 *   - .gai animations whose frames are all of those formats;
 *   - playback .gai animations (frames of formats 5/6, deltas against the
 *     previous frame, starting from a first image <name>.gi of format 0):
 *     the frames are composed, downscaled and encoded again as exact deltas.
 * Everything else is copied as it is.
 *
 * Decoding and RLE encoding use okgf (https://github.com/pakompom/okgf), the
 * same code the game draws with.
 */
#include "okgf.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ------------------------------------------------------------------ util */

static int verbose;

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "assetconv: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}

static void *xcalloc(size_t n)
{
	void *p = calloc(1, n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t rds32(const uint8_t *p) { return (int32_t)rd32(p); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

typedef struct {
	uint8_t *p;
	size_t n, cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
	if (b->n + extra <= b->cap) return;
	b->cap = (b->n + extra) * 3 / 2 + 4096;
	b->p = realloc(b->p, b->cap);
	if (!b->p) die("out of memory");
}

static uint8_t *buf_grow(Buf *b, size_t n)
{
	buf_reserve(b, n);
	b->n += n;
	return b->p + b->n - n;
}

static void buf_add(Buf *b, const void *p, size_t n) { memcpy(buf_grow(b, n), p, n); }
static void buf_byte(Buf *b, uint8_t v) { *buf_grow(b, 1) = v; }
static void buf_u32(Buf *b, uint32_t v) { wr32(buf_grow(b, 4), v); }

/* ----------------------------------------------------------------- image */

/* Straight (not premultiplied) BGRA pixels; l, t place the image. */
typedef struct {
	int l, t, w, h;
	uint8_t *px;
} Image;

static void image_alloc(Image *im, int l, int t, int w, int h)
{
	im->l = l;
	im->t = t;
	im->w = w;
	im->h = h;
	im->px = xcalloc((size_t)w * h * 4 + 4);
}

/* Box filter over (1 << shift)^2 cells aligned at the image's top left, with
   alpha weighting; cells cut by the right and bottom edges average the pixels
   they have. */
static void downscale(const Image *src, int shift, Image *dst)
{
	int f = 1 << shift;
	int sw = (src->w + f - 1) >> shift, sh = (src->h + f - 1) >> shift;
	image_alloc(dst, src->l, src->t, sw, sh);
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			uint32_t n = 0, a = 0, cb = 0, cg = 0, cr = 0, pb = 0, pg = 0, pr = 0;
			for (int yy = y << shift; yy < (y + 1) << shift && yy < src->h; yy++)
				for (int xx = x << shift; xx < (x + 1) << shift && xx < src->w; xx++) {
					const uint8_t *p = src->px + ((size_t)yy * src->w + xx) * 4;
					n++;
					a += p[3];
					cb += p[0];
					cg += p[1];
					cr += p[2];
					pb += p[0] * p[3];
					pg += p[1] * p[3];
					pr += p[2] * p[3];
				}
			uint8_t *d = dst->px + ((size_t)y * sw + x) * 4;
			if (a) {
				d[0] = (uint8_t)((pb + a / 2) / a);
				d[1] = (uint8_t)((pg + a / 2) / a);
				d[2] = (uint8_t)((pr + a / 2) / a);
			} else {
				d[0] = (uint8_t)((cb + n / 2) / n);
				d[1] = (uint8_t)((cg + n / 2) / n);
				d[2] = (uint8_t)((cr + n / 2) / n);
			}
			d[3] = (uint8_t)((a + n / 2) / n);
		}
}

/* -------------------------------------------------------------------- gi */

#define GI_HEADER 64
#define GI_PLANE 32
#define SCALE_MARKER 0x53430000u

typedef struct {
	const uint8_t *p;
	size_t n;
	int l, t, r, b, format, planes;
	uint32_t amask;
} Gi;

static int gi_parse(const uint8_t *p, size_t n, Gi *gi)
{
	if (n < GI_HEADER || p[0] != 'g' || p[1] != 'i' || p[2] != 0) return 0;
	gi->p = p;
	gi->n = n;
	gi->l = rds32(p + 8);
	gi->t = rds32(p + 12);
	gi->r = rds32(p + 16);
	gi->b = rds32(p + 20);
	gi->amask = rd32(p + 36);
	gi->format = rds32(p + 40);
	gi->planes = rds32(p + 44);
	if (gi->planes < 0 || gi->planes > 8 || GI_HEADER + (size_t)gi->planes * GI_PLANE > n) return 0;
	if (gi->r < gi->l || gi->b < gi->t) return 0;
	for (int i = 0; i < gi->planes; i++) {
		uint32_t off = rd32(p + GI_HEADER + i * GI_PLANE), size = rd32(p + GI_HEADER + i * GI_PLANE + 4);
		if (off && ((size_t)off > n || size > n - off)) return 0;
	}
	return 1;
}

static int gi_scaled(const Gi *gi) { return (rd32(gi->p + 4) & 0xFFFF0000u) == SCALE_MARKER; }

typedef struct {
	uint32_t off, size;
	int l, t;
} Plane;

static Plane gi_plane(const Gi *gi, int i)
{
	const uint8_t *q = gi->p + GI_HEADER + i * GI_PLANE;
	Plane pl = {rd32(q), rd32(q + 4), rds32(q + 8), rds32(q + 12)};
	return pl;
}

/* Address of a plane's top left in an image of the GI's bounds. */
static uint8_t *plane_dest(const Image *im, const Gi *gi, Plane pl)
{
	return im->px + ((size_t)(pl.t - gi->t) * im->w + (pl.l - gi->l)) * 4;
}

static int rle_fits(const Gi *gi, Plane pl, int bpp_in)
{
	/* The RLE header gives the size of the commands; the drawers trust it. */
	if (pl.size < 16 || rd32(gi->p + pl.off) > pl.size - 16) return 0;
	int w = rds32(gi->p + pl.off + 4), h = rds32(gi->p + pl.off + 8);
	(void)bpp_in;
	return pl.l >= gi->l && pl.t >= gi->t && w >= 0 && h >= 0 && pl.l + w <= gi->r && pl.t + h <= gi->b;
}

/* Decodes formats 0-3 to straight BGRA, as TgiGR.DecodeToGraphBuf does. */
static int gi_decode(const Gi *gi, Image *im)
{
	int w = gi->r - gi->l, h = gi->b - gi->t;
	if (gi->format < 0 || gi->format > 3 || gi->planes < 1) return 0;
	image_alloc(im, gi->l, gi->t, w, h);
	if (gi->format == 0) {
		Plane pl = gi_plane(gi, 0);
		if (!pl.off) return 1;
		size_t need = (size_t)w * h * (gi->amask ? 4 : 2);
		if (pl.size < need) return 0;
		if (gi->amask)
			memcpy(im->px, gi->p + pl.off, need);
		else
			OKGF_Convert565toBGRA(gi->p + pl.off, w * 2, im->px, w * 4, w, h);
		return 1;
	}
	static const int order[3][3] = {{0, -1, -1}, {2, 1, 0}, {0, 1, -1}};
	const int *planes = order[gi->format - 1];
	for (int k = 0; k < 3; k++) {
		int i = planes[k];
		if (i < 0 || i >= gi->planes) continue;
		Plane pl = gi_plane(gi, i);
		if (!pl.off) continue;
		if (!rle_fits(gi, pl, 0)) return 0;
		const OkgfRleHeader *rle = (const OkgfRleHeader *)(gi->p + pl.off);
		uint8_t *d = plane_dest(im, gi, pl);
		if (gi->format == 1 || (gi->format == 2 && i == 0))
			OKGR_TransBuf_Draw_RGBA(d, w * 4, rle);
		else if (gi->format == 2 && i == 1)
			OKGR_TransAlphaBuf_Draw_RGBA(d, w * 4, rle);
		else if (gi->format == 2)
			OKGR_AlphaBuf_Draw_RGBA(d, w * 4, rle);
		else if (i == 0)
			OKGR_AlphaIndexed_Draw_RGBA(d, w * 4, rle);
		else
			OKGR_AlphaIndexed_AlphaDraw_RGBA(d, w * 4, rle);
	}
	return 1;
}

static void gi_header(Buf *out, int shift, const Image *logical, int sw, int sh, int format,
                      int planes, uint32_t amask)
{
	uint8_t *h = buf_grow(out, GI_HEADER);
	memset(h, 0, GI_HEADER);
	h[0] = 'g';
	h[1] = 'i';
	wr32(h + 4, SCALE_MARKER | (uint32_t)shift);
	wr32(h + 16, (uint32_t)sw);
	wr32(h + 20, (uint32_t)sh);
	if (format == 0 && amask) {
		wr32(h + 24, 0xFF0000);
		wr32(h + 28, 0xFF00);
		wr32(h + 32, 0xFF);
		wr32(h + 36, 0xFF000000u);
	} else if (format == 5) {
		wr32(h + 24, 0xFF0000);
		wr32(h + 28, 0xFF00);
		wr32(h + 32, 0xFF);
		wr32(h + 36, 0xFF000000u);
	} else {
		wr32(h + 24, 0xF800);
		wr32(h + 28, 0x7E0);
		wr32(h + 32, 0x1F);
	}
	wr32(h + 40, (uint32_t)format);
	wr32(h + 44, (uint32_t)planes);
	wr16(h + 56, (uint16_t)(int16_t)logical->l);
	wr16(h + 58, (uint16_t)(int16_t)logical->t);
	wr16(h + 60, (uint16_t)logical->w);
	wr16(h + 62, (uint16_t)logical->h);
	memset(buf_grow(out, (size_t)planes * GI_PLANE), 0, (size_t)planes * GI_PLANE);
}

static void gi_set_plane(Buf *out, size_t base, int i, uint32_t off, uint32_t size, int w, int h)
{
	uint8_t *q = out->p + base + GI_HEADER + i * GI_PLANE;
	wr32(q, off);
	wr32(q + 4, size);
	wr32(q + 8, 0);
	wr32(q + 12, 0);
	wr32(q + 16, (uint32_t)w);
	wr32(q + 20, (uint32_t)h);
}

static uint16_t to565(const uint8_t *p)
{
	unsigned r = (p[2] * 31 + 127) / 255, g = (p[1] * 63 + 127) / 255, b = (p[0] * 31 + 127) / 255;
	return (uint16_t)(r << 11 | g << 5 | b);
}

/* Writes a downscaled image: format 0 (raw 565 or BGRA) or 2 (RLE planes). */
static void gi_encode(Buf *out, const Image *small, const Image *logical, int shift, int format,
                      uint32_t amask)
{
	size_t base = out->n;
	int w = small->w, h = small->h;
	if (format == 0) {
		gi_header(out, shift, logical, w, h, 0, 1, amask);
		size_t off = out->n - base, size = (size_t)w * h * (amask ? 4 : 2);
		uint8_t *d = buf_grow(out, size);
		if (amask)
			memcpy(d, small->px, size);
		else
			for (size_t i = 0; i < (size_t)w * h; i++) wr16(d + 2 * i, to565(small->px + 4 * i));
		gi_set_plane(out, base, 0, (uint32_t)off, (uint32_t)size, w, h);
		return;
	}
	gi_header(out, shift, logical, w, h, 2, 3, 0);
	int32_t (*build[3])(const void *, int32_t, int32_t, int32_t, void *) = {
	    OKGR_TransBuf_BuildFromRGBA_16, OKGR_TransAlphaBuf_BuildFromRGBA_16, OKGR_AlphaBuf_BuildFromRGBA};
	for (int i = 0; i < 3; i++) {
		int32_t size = build[i](small->px, w * 4, w, h, NULL);
		size_t off = out->n - base;
		buf_reserve(out, (size_t)size);
		build[i](small->px, w * 4, w, h, out->p + out->n);
		out->n += (size_t)size;
		gi_set_plane(out, base, i, (uint32_t)off, (uint32_t)size, w, h);
	}
}

/* --------------------------------------------------------------- options */

static int opt_shift = 1;
static long opt_min_area = 64 * 64;
static int opt_level = 6;

typedef struct {
	long gi_in, gi_done, gai_in, gai_done, pb_in, pb_done, hai_in, hai_done;
	uint64_t bytes_in, bytes_out;
} Stats;

static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
static Stats stats;

static void count(long *field)
{
	pthread_mutex_lock(&stats_lock);
	++*field;
	pthread_mutex_unlock(&stats_lock);
}

/* Converts one image; returns 0 to keep the original. */
static int convert_gi(const uint8_t *p, size_t n, Buf *out)
{
	Gi gi;
	Image full, small;
	if (!gi_parse(p, n, &gi) || gi_scaled(&gi)) return 0;
	if ((long)(gi.r - gi.l) * (gi.b - gi.t) < opt_min_area) return 0;
	if (!gi_decode(&gi, &full)) return 0;
	downscale(&full, opt_shift, &small);
	gi_encode(out, &small, &full, opt_shift, gi.format == 0 ? 0 : 2, gi.format == 0 ? gi.amask : 0);
	free(full.px);
	free(small.px);
	return 1;
}

/* ------------------------------------------------------------------- gai */

#define GAI_HEADER 48

typedef struct {
	const uint8_t *p;
	size_t n;
	int l, t, r, b, frames;
	uint32_t flags, seq_off, seq_size;
} Gai;

static int gai_parse(const uint8_t *p, size_t n, Gai *g)
{
	if (n < GAI_HEADER || memcmp(p, "gai", 3)) return 0;
	g->p = p;
	g->n = n;
	g->l = rds32(p + 8);
	g->t = rds32(p + 12);
	g->r = rds32(p + 16);
	g->b = rds32(p + 20);
	g->frames = rds32(p + 24);
	g->flags = rd32(p + 28);
	g->seq_off = rd32(p + 32);
	g->seq_size = rd32(p + 36);
	if (g->frames < 0 || GAI_HEADER + (size_t)g->frames * 8 > n) return 0;
	if (g->seq_size && ((size_t)g->seq_off > n || g->seq_size > n - g->seq_off)) return 0;
	return 1;
}

/* A frame's GI, inflated if stored compressed ('ZL01', size, zlib stream).
   *owned is set when the caller must free the result. NULL for no frame. */
static const uint8_t *gai_frame(const Gai *g, int i, size_t *size, uint8_t **owned, int *bad)
{
	uint32_t off = rd32(g->p + GAI_HEADER + 8 * i), n = rd32(g->p + GAI_HEADER + 8 * i + 4);
	*owned = NULL;
	if (!off) return NULL;
	if ((size_t)off > g->n || n > g->n - off) {
		*bad = 1;
		return NULL;
	}
	const uint8_t *p = g->p + off;
	if (n >= 8 && !memcmp(p, "ZL01", 4)) {
		uLongf raw = rd32(p + 4);
		uint8_t *d = xmalloc(raw);
		if (uncompress(d, &raw, p + 8, n - 8) != Z_OK) {
			free(d);
			*bad = 1;
			return NULL;
		}
		*owned = d;
		*size = raw;
		return d;
	}
	*size = n;
	return p;
}

/* Rebuilds an animation around new frames: header (marked), frame table,
   the original sequence table, frames. frames[i].n == 0 means no frame. */
static void gai_write(Buf *out, const Gai *g, Buf *frames, int shift)
{
	size_t base = out->n;
	uint8_t *h = buf_grow(out, GAI_HEADER);
	memcpy(h, g->p, GAI_HEADER);
	memset(h + 40, 0, 8);
	h[40] = 'S';
	h[41] = 'C';
	h[42] = (uint8_t)shift;
	size_t table = out->n;
	buf_grow(out, (size_t)g->frames * 8);
	wr32(out->p + base + 32, (uint32_t)(out->n - base));
	buf_add(out, g->p + g->seq_off, g->seq_size);
	for (int i = 0; i < g->frames; i++) {
		uint32_t off = frames[i].n ? (uint32_t)(out->n - base) : 0;
		wr32(out->p + table + 8 * i, off);
		wr32(out->p + table + 8 * i + 4, (uint32_t)frames[i].n);
		buf_add(out, frames[i].p, frames[i].n);
	}
}

/* Animations of independent frames: every frame is downscaled on its own. */
static int convert_gai_plain(const Gai *g, Buf *out)
{
	Buf *frames = xcalloc(sizeof(Buf) * (size_t)(g->frames + 1));
	int ok = 1, any = 0;
	for (int i = 0; i < g->frames && ok; i++) {
		size_t n = 0;
		uint8_t *owned;
		int bad = 0;
		const uint8_t *p = gai_frame(g, i, &n, &owned, &bad);
		Gi gi;
		Image full, small;
		if (bad) ok = 0;
		if (!p) continue;
		if (!gi_parse(p, n, &gi) || gi_scaled(&gi) || !gi_decode(&gi, &full)) {
			ok = 0;
		} else {
			if (full.w && full.h) {
				downscale(&full, opt_shift, &small);
				gi_encode(&frames[i], &small, &full, opt_shift, gi.format == 0 ? 0 : 2,
				          gi.format == 0 ? gi.amask : 0);
				free(small.px);
				any = 1;
			} else {
				buf_add(&frames[i], p, n); /* an empty frame stays as it is */
			}
			free(full.px);
		}
		free(owned);
	}
	if (ok && any) {
		long area = (long)(g->r - g->l) * (g->b - g->t);
		ok = area >= opt_min_area;
	}
	if (ok && any) gai_write(out, g, frames, opt_shift);
	for (int i = 0; i < g->frames; i++) free(frames[i].p);
	free(frames);
	return ok && any;
}

/* ---------------------------------------------------- playback animations */

/* The sequence table must play the frames in order (0, 1, 2...) for the
   playback composition to match the frame order. */
static int gai_sequence_in_order(const Gai *g)
{
	const uint8_t *s = g->p + g->seq_off;
	if (g->seq_size < 8) return 0;
	uint32_t count = rd32(s);
	if (count < 1 || 8 + (size_t)count * 8 > g->seq_size) return 0;
	for (uint32_t k = 0; k < count; k++) {
		uint32_t at = rd32(s + 8 + 8 * k);
		if ((size_t)at + 4 > g->seq_size) return 0;
		uint32_t frames = rd32(s + at);
		if ((size_t)at + 4 + (size_t)frames * 8 > g->seq_size) return 0;
		for (uint32_t f = 0; f < frames; f++)
			if (rd32(s + at + 4 + 8 * f) != f) return 0;
	}
	return 1;
}

/* Encodes the change from prev to next (BGRA, w x h) inside box as an F5
   delta stream (okgf delta.c, OKGR_F5_DrawRGBA): per row and channel, runs
   of skipped pixels and of exact 8-bit deltas. */
static void f5_encode(Buf *out, const uint8_t *prev, const uint8_t *next, int w, int bl, int bt,
                      int bw, int bh)
{
	size_t base = out->n;
	uint8_t *h = buf_grow(out, 16);
	memset(h, 0, 16);
	wr32(h + 4, (uint32_t)bw);
	wr32(h + 8, (uint32_t)bh);
	h[12] = 0x88; /* 8-bit precision for all four channels */
	h[13] = 0x88;
	size_t index = out->n;
	buf_grow(out, (size_t)bh * 4);
	size_t stream = out->n;
	for (int y = 0; y < bh; y++) {
		wr32(out->p + index + 4 * y, (uint32_t)(out->n - base));
		const uint8_t *pr = prev + ((size_t)(bt + y) * w + bl) * 4;
		const uint8_t *nx = next + ((size_t)(bt + y) * w + bl) * 4;
		for (int c = 0; c < 4; c++) {
			int x = 0, at = 0; /* at: pixel the decoder's offset points to */
			while (x < bw) {
				int d = (uint8_t)(nx[4 * x + c] - pr[4 * x + c]);
				if (!d) {
					x++;
					continue;
				}
				/* Skip to x. */
				int skip = x - at;
				while (skip > 0) {
					if (skip < 63) {
						buf_byte(out, (uint8_t)skip);
						skip = 0;
					} else {
						int k = skip > 65535 ? 65535 : skip;
						buf_byte(out, 63);
						buf_byte(out, (uint8_t)k);
						buf_byte(out, (uint8_t)(k >> 8));
						skip -= k;
					}
				}
				/* A run of up to 16 changed pixels with deltas of one sign. */
				int sub = d > 128, n = 0, maxv = 0;
				while (n < 16 && x + n < bw) {
					int e = (uint8_t)(nx[4 * (x + n) + c] - pr[4 * (x + n) + c]);
					if (!e || (e > 128) != sub) break;
					int v = (sub ? 256 - e : e) - 1;
					if (v > maxv) maxv = v;
					n++;
				}
				int mode = maxv < 2 ? 0 : maxv < 4 ? 1 : maxv < 16 ? 2 : 3, bits = 1 << mode;
				buf_byte(out, (uint8_t)(128 | (sub ? 4 + mode : mode) << 4 | (n - 1)));
				unsigned packed = 0, used = 0;
				for (int i = 0; i < n; i++) {
					int e = (uint8_t)(nx[4 * (x + i) + c] - pr[4 * (x + i) + c]);
					unsigned v = (unsigned)((sub ? 256 - e : e) - 1);
					packed |= v << used;
					used += (unsigned)bits;
					if (used == 8) {
						buf_byte(out, (uint8_t)packed);
						packed = 0;
						used = 0;
					}
				}
				if (used) buf_byte(out, (uint8_t)packed);
				x += n;
				at = x;
			}
			buf_byte(out, 0); /* next channel; after the fourth, next row */
		}
	}
	buf_byte(out, 64);
	wr32(out->p + base, (uint32_t)(out->n - stream));
}

static int find_change(const uint8_t *a, const uint8_t *b, int w, int h, int *l, int *t, int *r, int *bt)
{
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int y = 0; y < h; y++) {
		const uint32_t *pa = (const uint32_t *)(a + (size_t)y * w * 4), *pb = (const uint32_t *)(b + (size_t)y * w * 4);
		for (int x = 0; x < w; x++)
			if (pa[x] != pb[x]) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				y1 = y;
			}
	}
	if (x1 < 0) return 0;
	*l = x0;
	*t = y0;
	*r = x1 + 1;
	*bt = y1 + 1;
	return 1;
}

/* Playback animations draw the first image, then each frame as a delta over
   the previous result (TgaiGI.Draw, CachedPlaybackGraphBuf). The frames are
   composed at full size, downscaled, and encoded again as exact deltas
   between the downscaled results. first is the animation's first image. */
static int convert_gai_playback(const Gai *g, const uint8_t *first, size_t first_n, Buf *out, Buf *first_out)
{
	Gi fgi;
	Image canvas, small_prev, small_next;
	if (!gi_parse(first, first_n, &fgi) || gi_scaled(&fgi) || fgi.format != 0 || !fgi.amask) return 0;
	if (fgi.l != g->l || fgi.t != g->t || fgi.r != g->r || fgi.b != g->b) return 0;
	if ((long)(g->r - g->l) * (g->b - g->t) < opt_min_area) return 0;
	if (!gai_sequence_in_order(g)) return 0;
	if (!gi_decode(&fgi, &canvas)) return 0;
	int w = canvas.w;
	downscale(&canvas, opt_shift, &small_prev);
	gi_encode(first_out, &small_prev, &canvas, opt_shift, 0, fgi.amask);
	Buf *frames = xcalloc(sizeof(Buf) * (size_t)(g->frames + 1));
	int ok = 1;
	for (int i = 0; i < g->frames && ok; i++) {
		size_t n = 0;
		uint8_t *owned;
		int bad = 0;
		const uint8_t *p = gai_frame(g, i, &n, &owned, &bad);
		Gi gi;
		if (bad) ok = 0;
		if (!p) continue;
		if (!gi_parse(p, n, &gi) || gi_scaled(&gi) || (gi.format != 5 && gi.format != 6) || gi.planes < 1) {
			ok = 0;
		} else {
			Plane pl = gi_plane(&gi, 0);
			int fx = gi.l - g->l, fy = gi.t - g->t;
			if (!pl.off || fx < 0 || fy < 0 || gi.r > g->r || gi.b > g->b) {
				ok = pl.off == 0;
			} else {
				uint8_t *d = canvas.px + ((size_t)fy * w + fx) * 4;
				if (gi.format == 5)
					OKGR_F5_DrawRGBA(d, w * 4, (const OkgfF5Header *)(gi.p + pl.off));
				else
					OKGR_F6_DrawRGBA(d, w * 4, (const OkgfF6Header *)(gi.p + pl.off));
				downscale(&canvas, opt_shift, &small_next);
				int bl, bt, br, bb;
				if (find_change(small_prev.px, small_next.px, small_next.w, small_next.h, &bl, &bt, &br, &bb)) {
					Image logical = {g->l + (bl << opt_shift), g->t + (bt << opt_shift), 0, 0, NULL};
					logical.w = ((br - bl) << opt_shift);
					logical.h = ((bb - bt) << opt_shift);
					if (logical.l + logical.w > g->r) logical.w = g->r - logical.l;
					if (logical.t + logical.h > g->b) logical.h = g->b - logical.t;
					gi_header(&frames[i], opt_shift, &logical, br - bl, bb - bt, 5, 1, 0);
					size_t off = frames[i].n;
					f5_encode(&frames[i], small_prev.px, small_next.px, small_next.w, bl, bt, br - bl, bb - bt);
					gi_set_plane(&frames[i], 0, 0, (uint32_t)off, (uint32_t)(frames[i].n - off), br - bl, bb - bt);
					/* Check: the encoded delta reproduces the downscaled frame. */
					OKGR_F5_DrawRGBA(small_prev.px + ((size_t)bt * small_prev.w + bl) * 4, small_prev.w * 4,
					                 (const OkgfF5Header *)(frames[i].p + off));
					if (memcmp(small_prev.px, small_next.px, (size_t)small_next.w * small_next.h * 4))
						die("F5 encoder mismatch in frame %d", i);
				}
				free(small_next.px);
			}
		}
		free(owned);
	}
	if (ok) gai_write(out, g, frames, opt_shift);
	for (int i = 0; i < g->frames; i++) free(frames[i].p);
	free(frames);
	free(canvas.px);
	free(small_prev.px);
	if (!ok) first_out->n = 0;
	return ok;
}

/* ------------------------------------------------------------------- hai */

/* Rotating ship sprites (EC_CacheHSAI): a 52-byte header, then per frame an
   8-bit index plane and a 256-color RGBA palette. The game draws them on a
   quad of the size the ship's settings give, so a smaller sprite needs no
   change in the game. Frames are downscaled in RGBA and mapped back to their
   palettes. The red mask (unused for palettes) marks converted sprites. */
#define HAI_HEADER 52

static int nearest_color(const uint8_t *pal, const uint8_t *c, int32_t *cache_key, uint8_t *cache_val)
{
	uint32_t key = (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
	uint32_t h = (key * 2654435761u) >> 20; /* 4096 slots */
	if (cache_key[h] == (int32_t)key && cache_key[h] != -1) return cache_val[h];
	long best = -1;
	int bi = 0;
	for (int i = 0; i < 256; i++) {
		const uint8_t *p = pal + 4 * i;
		long d = 0;
		/* Compare premultiplied colors and alpha. */
		for (int k = 0; k < 3; k++) {
			long e = (long)p[k] * p[3] - (long)c[k] * c[3];
			d += (e / 255) * (e / 255);
		}
		long ea = (long)p[3] - c[3];
		d += 4 * ea * ea;
		if (best < 0 || d < best) {
			best = d;
			bi = i;
		}
	}
	cache_key[h] = (int32_t)key;
	cache_val[h] = (uint8_t)bi;
	return bi;
}

static int convert_hai(const uint8_t *p, size_t n, Buf *out)
{
	if (n < HAI_HEADER) return 0;
	int w = rds32(p + 4), h = rds32(p + 8), pitch = rds32(p + 12);
	uint32_t frames = rd32(p + 16), stride = rd32(p + 20), pal = rd32(p + 24), bpp = rd32(p + 28);
	uint32_t palbytes = rd32(p + 48);
	if ((rd32(p + 32) & 0xFFFF0000u) == SCALE_MARKER) return 0;
	if (w <= 0 || h <= 0 || pitch != w || bpp != 8 || pal != 1 || palbytes != 1024) return 0;
	if (stride != (uint32_t)w * h + 1024 || HAI_HEADER + (uint64_t)frames * stride > n) return 0;
	if ((long)w * h < opt_min_area) return 0;
	int f = 1 << opt_shift, sw = (w + f - 1) >> opt_shift, sh = (h + f - 1) >> opt_shift;
	uint32_t sstride = (uint32_t)sw * sh + 1024;
	uint8_t *hd = buf_grow(out, HAI_HEADER);
	memcpy(hd, p, HAI_HEADER);
	wr32(hd + 4, (uint32_t)sw);
	wr32(hd + 8, (uint32_t)sh);
	wr32(hd + 12, (uint32_t)sw);
	wr32(hd + 20, sstride);
	wr32(hd + 32, SCALE_MARKER | (uint32_t)opt_shift);
	Image full, small;
	int32_t *cache_key = xmalloc(4096 * sizeof(int32_t));
	uint8_t *cache_val = xmalloc(4096);
	image_alloc(&full, 0, 0, w, h);
	for (uint32_t fr = 0; fr < frames; fr++) {
		const uint8_t *idx = p + HAI_HEADER + (size_t)fr * stride, *palette = idx + (size_t)w * h;
		for (size_t i = 0; i < (size_t)w * h; i++) memcpy(full.px + 4 * i, palette + 4 * idx[i], 4);
		downscale(&full, opt_shift, &small);
		memset(cache_key, 0xFF, 4096 * sizeof(int32_t));
		uint8_t *d = buf_grow(out, sstride);
		for (size_t i = 0; i < (size_t)sw * sh; i++) d[i] = (uint8_t)nearest_color(palette, small.px + 4 * i, cache_key, cache_val);
		memcpy(d + (size_t)sw * sh, palette, 1024);
		free(small.px);
	}
	free(full.px);
	free(cache_key);
	free(cache_val);
	return 1;
}

/* ------------------------------------------------------------------- pkg */

#define REC 158
#define BLOCK 65536

typedef struct Folder Folder;

typedef struct {
	uint8_t rec[REC];
	char path[1024], lower[1024];
	uint32_t stored, size, target;
	int32_t kind;
	Folder *child;
	/* Conversion result: the new contents, or NULL to copy the original. */
	uint8_t *data;
	size_t data_n, raw_n;
	int pair; /* index of the paired first image (playback animations), or -1 */
	int skip; /* an image that must stay as it is */
	uint32_t out_target, out_stored;
} Entry;

struct Folder {
	int count;
	Entry *e;
	uint32_t out_offset;
};

typedef struct {
	uint8_t *data;
	size_t n;
	Folder *root;
	Entry **files;
	int nfiles, capfiles;
	Folder **folders;
	int nfolders, capfolders;
} Pkg;

static Folder *read_folder(Pkg *pkg, uint32_t off, const char *path, int depth)
{
	if (depth > 64 || (size_t)off + 12 > pkg->n) die("bad folder at %u", off);
	Folder *f = xcalloc(sizeof(Folder));
	f->count = (int)rd32(pkg->data + off + 4);
	if (rd32(pkg->data + off + 8) != REC || (size_t)off + 12 + (size_t)f->count * REC > pkg->n)
		die("bad folder at %u", off);
	f->e = xcalloc(sizeof(Entry) * (size_t)(f->count + 1));
	if (pkg->nfolders == pkg->capfolders) {
		pkg->capfolders = pkg->capfolders * 2 + 16;
		pkg->folders = realloc(pkg->folders, sizeof(Folder *) * (size_t)pkg->capfolders);
	}
	pkg->folders[pkg->nfolders++] = f;
	for (int i = 0; i < f->count; i++) {
		Entry *e = &f->e[i];
		memcpy(e->rec, pkg->data + off + 12 + (size_t)i * REC, REC);
		e->stored = rd32(e->rec);
		e->size = rd32(e->rec + 4);
		e->kind = rds32(e->rec + 134);
		e->target = rd32(e->rec + 150);
		e->pair = -1;
		char name[64];
		memcpy(name, e->rec + 71, 63);
		name[63] = 0;
		snprintf(e->path, sizeof e->path, "%s/%s", path, name);
		for (size_t k = 0; k <= strlen(e->path); k++) {
			unsigned char ch = (unsigned char)e->path[k];
			e->lower[k] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
		}
		if (e->kind == 3 && rd32(e->rec + 142) == 0) {
			e->child = read_folder(pkg, e->target, e->path, depth + 1);
		} else {
			if ((size_t)e->target > pkg->n || e->stored > pkg->n - e->target) die("bad entry %s", e->path);
			if (pkg->nfiles == pkg->capfiles) {
				pkg->capfiles = pkg->capfiles * 2 + 64;
				pkg->files = realloc(pkg->files, sizeof(Entry *) * (size_t)pkg->capfiles);
			}
			pkg->files[pkg->nfiles++] = e;
		}
	}
	return f;
}

static void pkg_open(Pkg *pkg, const char *fn)
{
	FILE *fp = fopen(fn, "rb");
	if (!fp) die("%s: %s", fn, strerror(errno));
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	memset(pkg, 0, sizeof *pkg);
	pkg->n = (size_t)n;
	pkg->data = xmalloc(pkg->n);
	if (fread(pkg->data, 1, pkg->n, fp) != pkg->n) die("%s: read error", fn);
	fclose(fp);
	if (pkg->n < 4) die("%s: not a package", fn);
	pkg->root = read_folder(pkg, rd32(pkg->data), "", 0);
}

/* The uncompressed contents of a file entry. */
static uint8_t *pkg_read(const Pkg *pkg, const Entry *e)
{
	uint8_t *out = xmalloc(e->size);
	if (e->kind != 2) {
		if (e->stored < 4 || e->size > e->stored - 4) die("bad entry %s", e->path);
		memcpy(out, pkg->data + e->target + 4, e->size);
		return out;
	}
	size_t got = 0, o = e->target + 4, end = e->target + e->stored;
	while (got < e->size) {
		if (o + 4 > end) die("bad blocks in %s", e->path);
		uint32_t n = rd32(pkg->data + o);
		const uint8_t *b = pkg->data + o + 4;
		if (n < 8 || n > end - o - 4 || memcmp(b, "ZL02", 4)) die("bad block in %s", e->path);
		uLongf raw = rd32(b + 4);
		if (raw > e->size - got) die("bad block size in %s", e->path);
		if (uncompress(out + got, &raw, b + 8, n - 8) != Z_OK) die("inflate failed in %s", e->path);
		got += raw;
		o += 4 + n;
	}
	return out;
}

/* Stored form of new contents: u32 size of the rest, then 64 KiB blocks of
   [u32 n]["ZL02"][u32 raw size][zlib stream]. */
static void encode_chain(const uint8_t *raw, size_t n, Buf *out)
{
	buf_u32(out, 0);
	for (size_t i = 0; i < n; i += BLOCK) {
		size_t len = n - i < BLOCK ? n - i : BLOCK;
		uLongf z = compressBound(len);
		size_t at = out->n;
		buf_grow(out, 12 + z);
		memcpy(out->p + at + 4, "ZL02", 4);
		wr32(out->p + at + 8, (uint32_t)len);
		if (compress2(out->p + at + 12, &z, raw + i, len, opt_level) != Z_OK) die("deflate failed");
		wr32(out->p + at, (uint32_t)(8 + z));
		out->n = at + 12 + z;
	}
	wr32(out->p, (uint32_t)(out->n - 4));
}

/* -------------------------------------------------------------- the work */

static Pkg pkg;
static int next_job;
static pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;

static int has_ext(const Entry *e, const char *ext)
{
	size_t a = strlen(e->lower), b = strlen(ext);
	return a > b && !strcmp(e->lower + a - b, ext);
}

static void store(Entry *e, Buf *b)
{
	Buf chain = {0};
	encode_chain(b->p, b->n, &chain);
	e->raw_n = b->n;
	e->data = chain.p;
	e->data_n = chain.n;
}

static void convert_entry(Entry *e)
{
	if (e->skip) return;
	int gi = has_ext(e, ".gi"), gai = has_ext(e, ".gai"), hai = has_ext(e, ".hai");
	if (!gi && !gai && !hai) return;
	uint8_t *raw = pkg_read(&pkg, e);
	Buf out = {0};
	if (hai) {
		count(&stats.hai_in);
		if (convert_hai(raw, e->size, &out)) {
			count(&stats.hai_done);
			store(e, &out);
		}
	} else if (gi) {
		pthread_mutex_lock(&stats_lock);
		stats.gi_in++;
		pthread_mutex_unlock(&stats_lock);
		if (convert_gi(raw, e->size, &out)) {
			count(&stats.gi_done);
			store(e, &out);
		}
	} else {
		Gai g;
		if (gai_parse(raw, e->size, &g)) {
			if (!g.flags) {
				count(&stats.gai_in);
				if (convert_gai_plain(&g, &out)) {
					count(&stats.gai_done);
					store(e, &out);
				}
			} else {
				count(&stats.pb_in);
				if (e->pair >= 0) {
					Entry *fe = pkg.files[e->pair];
					uint8_t *first = pkg_read(&pkg, fe);
					Buf fout = {0};
					if (convert_gai_playback(&g, first, fe->size, &out, &fout)) {
						count(&stats.pb_done);
						store(e, &out);
						store(fe, &fout);
					} else if (verbose) {
						fprintf(stderr, "kept %s\n", e->path);
					}
					free(fout.p);
					free(first);
				} else if (verbose) {
					fprintf(stderr, "kept %s (no first image)\n", e->path);
				}
			}
		}
	}
	free(out.p);
	free(raw);
}

static void *worker(void *arg)
{
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&job_lock);
		int i = next_job++;
		pthread_mutex_unlock(&job_lock);
		if (i >= pkg.nfiles) return NULL;
		convert_entry(pkg.files[i]);
	}
}

/* Pairs each playback animation with its first image, <name>.gi in the same
   folder. Such an image is converted together with its animation, or not at
   all: a downscaled first image under full-size deltas would not match. */
static void pair_playback(void)
{
	for (int i = 0; i < pkg.nfiles; i++) {
		Entry *e = pkg.files[i];
		if (!has_ext(e, ".gai")) continue;
		uint8_t head[GAI_HEADER];
		uint8_t *raw = pkg_read(&pkg, e);
		int pb = e->size >= GAI_HEADER && !memcmp(raw, "gai", 3) && rd32(raw + 28);
		memcpy(head, raw, e->size < GAI_HEADER ? e->size : GAI_HEADER);
		free(raw);
		if (!pb) continue;
		char want[1024];
		snprintf(want, sizeof want, "%.*s.gi", (int)(strlen(e->lower) - 4), e->lower);
		for (int k = 0; k < pkg.nfiles; k++)
			if (!strcmp(pkg.files[k]->lower, want)) {
				e->pair = k;
				pkg.files[k]->skip = 1;
			}
	}
}

static void write_pkg(const char *fn)
{
	FILE *fp = fopen(fn, "wb");
	if (!fp) die("%s: %s", fn, strerror(errno));
	uint32_t off = 4;
	for (int i = 0; i < pkg.nfolders; i++) {
		pkg.folders[i]->out_offset = off;
		off += 12 + REC * (uint32_t)pkg.folders[i]->count;
	}
	for (int i = 0; i < pkg.nfiles; i++) {
		Entry *e = pkg.files[i];
		e->out_target = off;
		e->out_stored = e->data ? (uint32_t)e->data_n : e->stored;
		if ((uint64_t)off + e->out_stored > 0xFFFFFFFFu) die("%s: package too large", fn);
		off += e->out_stored;
	}
	uint8_t w[4];
	wr32(w, 4);
	fwrite(w, 1, 4, fp);
	for (int i = 0; i < pkg.nfolders; i++) {
		Folder *f = pkg.folders[i];
		uint8_t h[12];
		wr32(h, 170);
		wr32(h + 4, (uint32_t)f->count);
		wr32(h + 8, REC);
		fwrite(h, 1, 12, fp);
		for (int k = 0; k < f->count; k++) {
			Entry *e = &f->e[k];
			uint8_t rec[REC];
			memcpy(rec, e->rec, REC);
			if (e->child) {
				wr32(rec + 150, e->child->out_offset);
			} else {
				wr32(rec, e->out_stored);
				wr32(rec + 150, e->out_target);
				if (e->data) {
					/* Converted entries are stored compressed. */
					wr32(rec + 4, (uint32_t)e->raw_n);
					wr32(rec + 134, 2);
					wr32(rec + 138, 2);
				}
			}
			wr32(rec + 154, 0);
			fwrite(rec, 1, REC, fp);
		}
	}
	for (int i = 0; i < pkg.nfiles; i++) {
		Entry *e = pkg.files[i];
		if (e->data)
			fwrite(e->data, 1, e->data_n, fp);
		else
			fwrite(pkg.data + e->target, 1, e->stored, fp);
		stats.bytes_out += e->out_stored;
	}
	if (fclose(fp)) die("%s: write error", fn);
}

static void usage(void)
{
	fprintf(stderr,
	        "usage: assetconv [-s shift] [-m min-area] [-j jobs] [-z level] [-v] in.pkg out.pkg\n"
	        "  -s shift     downscale by 1 << shift (default 1: half size)\n"
	        "  -m min-area  leave images of fewer pixels as they are (default 4096)\n"
	        "  -j jobs      threads (default 4)\n"
	        "  -z level     zlib level of converted files (default 6)\n"
	        "  -v           list what is kept and why\n");
	exit(2);
}

int main(int argc, char **argv)
{
	int jobs = 4, i;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		const char *o = argv[i];
		if (!strcmp(o, "-v")) {
			verbose = 1;
			continue;
		}
		if (i + 1 >= argc) usage();
		if (!strcmp(o, "-s")) opt_shift = atoi(argv[++i]);
		else if (!strcmp(o, "-m")) opt_min_area = atol(argv[++i]);
		else if (!strcmp(o, "-j")) jobs = atoi(argv[++i]);
		else if (!strcmp(o, "-z")) opt_level = atoi(argv[++i]);
		else usage();
	}
	if (argc - i != 2 || opt_shift < 1 || opt_shift > 4 || jobs < 1 || opt_level < 1 || opt_level > 9) usage();
	pkg_open(&pkg, argv[i]);
	stats.bytes_in = pkg.n;
	pair_playback();
	pthread_t *t = xmalloc(sizeof(pthread_t) * (size_t)jobs);
	for (int k = 0; k < jobs; k++) pthread_create(&t[k], NULL, worker, NULL);
	for (int k = 0; k < jobs; k++) pthread_join(t[k], NULL);
	write_pkg(argv[i + 1]);
	printf("%s: images %ld/%ld, animations %ld/%ld, playback animations %ld/%ld, ship sprites %ld/%ld, "
	       "%.1f -> %.1f MiB\n",
	       argv[i], stats.gi_done, stats.gi_in, stats.gai_done, stats.gai_in, stats.pb_done, stats.pb_in,
	       stats.hai_done, stats.hai_in,
	       stats.bytes_in / 1048576.0, (stats.bytes_out + 4) / 1048576.0);
	return 0;
}

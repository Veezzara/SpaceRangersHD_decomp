/* The libvorbisfile functions used by the game, implemented on Tremor
 * (integer Vorbis decoder from the 3DS portlibs).
 *
 * Tremor's own ov_* symbols are renamed to tremor_ov_* by the build script.
 * The game embeds an OggVorbis_File sized for libvorbisfile; here it only
 * holds a pointer to Tremor's decoder state, which has a different layout. */
#include <stdlib.h>
#include <string.h>
/* declare Tremor's functions under their renamed symbols */
#define ov_open_callbacks tremor_ov_open_callbacks
#define ov_clear tremor_ov_clear
#define ov_info tremor_ov_info
#define ov_read tremor_ov_read
#include <tremor/ivorbisfile.h>
#undef ov_open_callbacks
#undef ov_clear
#undef ov_info
#undef ov_read

static OggVorbis_File *state(void *vf) { return *(OggVorbis_File **)vf; }

int ov_open_callbacks(void *datasource, void *vf, const char *initial, long ibytes, ov_callbacks callbacks)
{
	OggVorbis_File *t = calloc(1, sizeof(*t));
	int r;
	*(OggVorbis_File **)vf = t;
	if (!t) return OV_EFAULT;
	r = tremor_ov_open_callbacks(datasource, t, initial, ibytes, callbacks);
	if (r < 0) {
		free(t);
		*(OggVorbis_File **)vf = NULL;
	}
	return r;
}

int ov_clear(void *vf)
{
	OggVorbis_File *t = state(vf);
	if (t) {
		tremor_ov_clear(t);
		free(t);
		*(OggVorbis_File **)vf = NULL;
	}
	return 0;
}

vorbis_info *ov_info(void *vf, int link)
{
	OggVorbis_File *t = state(vf);
	return t ? tremor_ov_info(t, link) : NULL;
}

/* Tremor always produces signed 16-bit little-endian samples. */
long ov_read(void *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream)
{
	OggVorbis_File *t = state(vf);
	if (!t) return OV_EINVAL;
	if (bigendianp || word != 2 || !sgned) return OV_EIMPL;
	return tremor_ov_read(t, buffer, length, bitstream);
}

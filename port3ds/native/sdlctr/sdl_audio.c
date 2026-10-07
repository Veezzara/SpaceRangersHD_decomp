/* SDL2 subset for the 3DS: audio output through the DSP (ndsp).
 * Requires the DSP firmware dump (sdmc:/3ds/dspfirm.cdc) like every
 * homebrew that plays sound. Float samples are converted to 16 bit. */
#include <stdlib.h>
#include <string.h>
#include "sdlctr.h"

#define NUM_BUFFERS 4
#define AUDIO_DEVICE_ID 1

static struct {
	int open, paused, quit;
	SDL_AudioSpec spec;
	ndspWaveBuf wave[NUM_BUFFERS];
	int16_t *pcm;          /* linear memory, NUM_BUFFERS * frames * channels */
	float *mix;            /* callback buffer */
	SDL_mutex *lock;
	LightEvent signal;
	Thread thread;
} audio;

SDL_mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex *m);
int SDL_LockMutex(SDL_mutex *m);
int SDL_UnlockMutex(SDL_mutex *m);
int fpcctr_get_default_core(void);

static void dsp_callback(void *data)
{
	(void)data;
	LightEvent_Signal(&audio.signal);
}

static void fill_buffer(ndspWaveBuf *wb)
{
	int frames = audio.spec.samples, ch = audio.spec.channels;
	int n = frames * ch, i;
	int16_t *out = (int16_t *)wb->data_pcm16;
	if (audio.paused) {
		memset(out, 0, n * sizeof(int16_t));
	} else if (audio.spec.format == AUDIO_F32SYS) {
		memset(audio.mix, 0, n * sizeof(float));
		SDL_LockMutex(audio.lock);
		audio.spec.callback(audio.spec.userdata, (uint8_t *)audio.mix, n * sizeof(float));
		SDL_UnlockMutex(audio.lock);
		for (i = 0; i < n; i++) {
			float s = audio.mix[i] * 32767.0f;
			if (s > 32767.0f) s = 32767.0f;
			if (s < -32768.0f) s = -32768.0f;
			out[i] = (int16_t)s;
		}
	} else {
		memset(out, 0, n * sizeof(int16_t));
		SDL_LockMutex(audio.lock);
		audio.spec.callback(audio.spec.userdata, (uint8_t *)out, n * sizeof(int16_t));
		SDL_UnlockMutex(audio.lock);
	}
	DSP_FlushDataCache(out, n * sizeof(int16_t));
	wb->nsamples = frames;
	ndspChnWaveBufAdd(0, wb);
}

static void audio_thread(void *arg)
{
	(void)arg;
	while (!audio.quit) {
		int i;
		for (i = 0; i < NUM_BUFFERS; i++)
			if (audio.wave[i].status == NDSP_WBUF_DONE || audio.wave[i].status == NDSP_WBUF_FREE)
				fill_buffer(&audio.wave[i]);
		LightEvent_WaitTimeout(&audio.signal, 20 * 1000000LL);
	}
}

uint32_t SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                             SDL_AudioSpec *obtained, int allowed_changes)
{
	float mix[12];
	int i, frames, ch, core;
	s32 prio = 0x30;
	(void)device; (void)allowed_changes;
	if (iscapture) { sdlctr_set_error("audio capture is not supported"); return 0; }
	if (audio.open) { sdlctr_set_error("audio device already open"); return 0; }
	if (desired->format != AUDIO_F32SYS && desired->format != AUDIO_S16SYS) {
		sdlctr_set_error("unsupported audio format %x", desired->format);
		return 0;
	}
	if (R_FAILED(ndspInit())) {
		sdlctr_set_error("cannot initialise the DSP (is sdmc:/3ds/dspfirm.cdc present?)");
		return 0;
	}
	audio.spec = *desired;
	if (audio.spec.channels != 1) audio.spec.channels = 2;
	if (audio.spec.samples == 0) audio.spec.samples = 1024;
	frames = audio.spec.samples;
	ch = audio.spec.channels;
	audio.spec.silence = 0;
	audio.spec.size = frames * ch * (audio.spec.format == AUDIO_F32SYS ? 4 : 2);
	audio.pcm = linearAlloc(NUM_BUFFERS * frames * ch * sizeof(int16_t));
	audio.mix = malloc(frames * ch * sizeof(float));
	if (!audio.pcm || !audio.mix) {
		ndspExit();
		sdlctr_set_error("out of memory for audio buffers");
		return 0;
	}
	ndspSetOutputMode(ch == 2 ? NDSP_OUTPUT_STEREO : NDSP_OUTPUT_MONO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, (float)audio.spec.freq);
	ndspChnSetFormat(0, ch == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	memset(mix, 0, sizeof(mix));
	mix[0] = mix[1] = 1.0f;
	ndspChnSetMix(0, mix);
	memset(audio.wave, 0, sizeof(audio.wave));
	for (i = 0; i < NUM_BUFFERS; i++) {
		audio.wave[i].data_pcm16 = audio.pcm + i * frames * ch;
		audio.wave[i].nsamples = frames;
		audio.wave[i].status = NDSP_WBUF_FREE;
	}
	audio.lock = SDL_CreateMutex();
	LightEvent_Init(&audio.signal, RESET_ONESHOT);
	audio.paused = 1;
	audio.quit = 0;
	ndspSetCallback(dsp_callback, NULL);
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	/* mixing runs Pascal code; keep it off the main core where possible */
	core = fpcctr_get_default_core() == 2 ? 2 : 1;
	audio.thread = threadCreate(audio_thread, NULL, 64 * 1024, prio - 1, core, false);
	if (!audio.thread)
		audio.thread = threadCreate(audio_thread, NULL, 64 * 1024, prio - 1, -2, false);
	if (!audio.thread) {
		ndspExit();
		sdlctr_set_error("cannot start the audio thread");
		return 0;
	}
	audio.open = 1;
	if (obtained) *obtained = audio.spec;
	sdlctr_log("audio: %d Hz, %d channels, %d frames, format %x, core %d",
	           audio.spec.freq, ch, frames, audio.spec.format, core);
	return AUDIO_DEVICE_ID;
}

void SDL_PauseAudioDevice(uint32_t dev, int pause)
{
	if (dev == AUDIO_DEVICE_ID && audio.open) audio.paused = pause != 0;
}

void SDL_LockAudioDevice(uint32_t dev)
{
	if (dev == AUDIO_DEVICE_ID && audio.open) SDL_LockMutex(audio.lock);
}

void SDL_UnlockAudioDevice(uint32_t dev)
{
	if (dev == AUDIO_DEVICE_ID && audio.open) SDL_UnlockMutex(audio.lock);
}

void SDL_CloseAudioDevice(uint32_t dev)
{
	if (dev != AUDIO_DEVICE_ID || !audio.open) return;
	audio.quit = 1;
	LightEvent_Signal(&audio.signal);
	threadJoin(audio.thread, U64_MAX);
	threadFree(audio.thread);
	ndspSetCallback(NULL, NULL);
	ndspChnReset(0);
	ndspExit();
	linearFree(audio.pcm);
	free(audio.mix);
	SDL_DestroyMutex(audio.lock);
	memset(&audio, 0, sizeof(audio));
}

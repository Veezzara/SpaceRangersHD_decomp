/* SDL2 subset for the 3DS: initialisation, errors, synchronisation, misc. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "sdlctr.h"

static char error_text[256];
static LightLock log_lock = 1;
static int log_ready;

int sdlctr_set_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(error_text, sizeof(error_text), fmt, ap);
	va_end(ap);
	sdlctr_log("SDL error: %s", error_text);
	return -1;
}

void sdlctr_log(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	FILE *f;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	svcOutputDebugString(line, strlen(line));
	LightLock_Lock(&log_lock);
	if (!log_ready) {
		mkdir("sdmc:/3ds", 0777);
		mkdir("sdmc:/3ds/SpaceRangersHD", 0777);
	}
	f = fopen("sdmc:/3ds/SpaceRangersHD/sdl.log", log_ready ? "a" : "w");
	if (f) {
		log_ready = 1;
		fprintf(f, "%u %s\n", (unsigned)sdlctr_ticks(), line);
		fclose(f);
	}
	LightLock_Unlock(&log_lock);
}

uint32_t sdlctr_ticks(void)
{
	static u64 start;
	u64 now = svcGetSystemTick();
	if (!start) start = now;
	return (uint32_t)((now - start) / (SYSCLOCK_ARM11 / 1000));
}

const char *SDL_GetError(void) { return error_text; }
void SDL_SetMainReady(void) {}
const char *SDL_GetHint(const char *name) { (void)name; return NULL; }
uint64_t SDL_GetPerformanceFrequency(void) { return SYSCLOCK_ARM11; }
uint64_t SDL_GetPerformanceCounter(void) { return svcGetSystemTick(); }
uint32_t SDL_GetTicks(void) { return sdlctr_ticks(); }
void SDL_free(void *p) { free(p); }

int SDL_GetCPUCount(void)
{
	bool is_new = false;
	APT_CheckNew3DS(&is_new);
	return is_new ? 3 : 2;
}

int SDL_GetSystemRAM(void)
{
	return (int)(osGetMemRegionSize(MEMREGION_APPLICATION) / (1024 * 1024));
}

/* --------------------------------------------------------------- mutex */

struct SDL_mutex {
	LightLock lock;
	volatile u32 owner; /* thread tag of the owner, 0 if free */
	int count;
};

static u32 thread_tag(void)
{
	/* the TLS area is unique per thread */
	return (u32)getThreadLocalStorage();
}

SDL_mutex *SDL_CreateMutex(void)
{
	SDL_mutex *m = calloc(1, sizeof(*m));
	if (!m) { sdlctr_set_error("out of memory"); return NULL; }
	LightLock_Init(&m->lock);
	return m;
}

void SDL_DestroyMutex(SDL_mutex *m) { free(m); }

int SDL_LockMutex(SDL_mutex *m)
{
	u32 me;
	if (!m) return 0;
	me = thread_tag();
	if (m->owner == me) {
		m->count++;
		return 0;
	}
	LightLock_Lock(&m->lock);
	m->owner = me;
	m->count = 1;
	return 0;
}

int SDL_TryLockMutex(SDL_mutex *m)
{
	u32 me;
	if (!m) return 0;
	me = thread_tag();
	if (m->owner == me) {
		m->count++;
		return 0;
	}
	if (LightLock_TryLock(&m->lock) != 0) return SDL_MUTEX_TIMEDOUT;
	m->owner = me;
	m->count = 1;
	return 0;
}

int SDL_UnlockMutex(SDL_mutex *m)
{
	if (!m) return 0;
	if (m->owner != thread_tag()) return sdlctr_set_error("unlocking a mutex not owned");
	if (--m->count == 0) {
		m->owner = 0;
		LightLock_Unlock(&m->lock);
	}
	return 0;
}

/* --------------------------------------------------------------- cond */

struct SDL_cond {
	CondVar cv;
};

SDL_cond *SDL_CreateCond(void)
{
	SDL_cond *c = calloc(1, sizeof(*c));
	if (!c) { sdlctr_set_error("out of memory"); return NULL; }
	CondVar_Init(&c->cv);
	return c;
}

void SDL_DestroyCond(SDL_cond *c) { free(c); }
int SDL_CondSignal(SDL_cond *c) { if (c) CondVar_Signal(&c->cv); return 0; }
int SDL_CondBroadcast(SDL_cond *c) { if (c) CondVar_Broadcast(&c->cv); return 0; }

int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, uint32_t ms)
{
	int count, timed_out = 0;
	if (!c || !m) return sdlctr_set_error("invalid condition wait");
	/* release a recursive mutex completely while waiting */
	count = m->count;
	m->owner = 0;
	m->count = 0;
	if (ms == SDL_MUTEX_MAXWAIT)
		CondVar_Wait(&c->cv, &m->lock);
	else
		timed_out = CondVar_WaitTimeout(&c->cv, &m->lock, (s64)ms * 1000000LL) != 0;
	m->owner = thread_tag();
	m->count = count;
	return timed_out ? SDL_MUTEX_TIMEDOUT : 0;
}

int SDL_CondWait(SDL_cond *c, SDL_mutex *m) { return SDL_CondWaitTimeout(c, m, SDL_MUTEX_MAXWAIT); }

/* --------------------------------------------------------------- misc */

static char *clipboard;

char *SDL_GetClipboardText(void) { return strdup(clipboard ? clipboard : ""); }

int SDL_SetClipboardText(const char *text)
{
	free(clipboard);
	clipboard = strdup(text ? text : "");
	return 0;
}

int SDL_OpenURL(const char *url)
{
	sdlctr_log("OpenURL ignored: %s", url ? url : "");
	return sdlctr_set_error("opening URLs is not supported");
}

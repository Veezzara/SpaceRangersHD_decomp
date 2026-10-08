/*
    This file is part of the Free Pascal run time library.

    Nintendo 3DS (CTR) support routines.

    The Pascal RTL for the 3DS is built on top of devkitARM's newlib and
    libctru. Everything whose binary layout is defined by C headers
    (struct stat, DIR, libctru locks/events, thread handles, TLS) is
    wrapped here, so the Pascal side only deals with plain integers and
    opaque buffers whose sizes are checked at compile time below.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.
*/

#include <3ds.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <malloc.h>

/* Sizes of the opaque Pascal buffers (see sysosh.inc). */
#define FPCCTR_LOCK_SIZE  16
#define FPCCTR_EVENT_SIZE 16

_Static_assert(sizeof(RecursiveLock) <= FPCCTR_LOCK_SIZE, "TRTLCriticalSection too small");
_Static_assert(sizeof(LightEvent) <= FPCCTR_EVENT_SIZE, "event buffer too small");

/* libctru gives the main thread 32 KiB of stack by default. Pascal code
   (and code translated from Delphi in particular) keeps large records and
   arrays on the stack; Windows gives 1 MiB. Overflowing the stack silently
   corrupts the heap, so reserve 2 MiB. */
u32 __stacksize__ = 2 * 1024 * 1024;
extern u32 __ctru_heap_size;

/* Memory statistics: [0] heap arena, [1] heap in use, [2] heap capacity
   (libctru __ctru_heap_size), [3] free linear memory, [4] free VRAM, [5] application region size,
   [6] main thread stack size. */
void fpcctr_meminfo(u32 *out)
{
	struct mallinfo mi = mallinfo();
	out[0] = mi.arena;
	out[1] = mi.uordblks;
	out[2] = __ctru_heap_size;
	out[3] = linearSpaceFree();
	out[4] = vramSpaceFree();
	out[5] = osGetMemRegionSize(MEMREGION_APPLICATION);
	out[6] = __stacksize__;
}

/* ---------------------------------------------------------------- errno */

int fpcctr_errno(void) { return errno; }
void fpcctr_set_errno(int e) { errno = e; }

/* ---------------------------------------------------------------- files */

/* Pascal side passes FPC-neutral flags: low 2 bits = access mode
   (0 read, 1 write, 2 read/write), then create/trunc/append/excl. */
#define FPCCTR_O_CREAT  0x10
#define FPCCTR_O_TRUNC  0x20
#define FPCCTR_O_APPEND 0x40
#define FPCCTR_O_EXCL   0x80

int fpcctr_open(const char *path, int flags, int mode)
{
	int f;
	switch (flags & 3) {
	case 0: f = O_RDONLY; break;
	case 1: f = O_WRONLY; break;
	default: f = O_RDWR; break;
	}
	if (flags & FPCCTR_O_CREAT) f |= O_CREAT;
	if (flags & FPCCTR_O_TRUNC) f |= O_TRUNC;
	if (flags & FPCCTR_O_APPEND) f |= O_APPEND;
	if (flags & FPCCTR_O_EXCL) f |= O_EXCL;
	return open(path, f, mode);
}

int fpcctr_close(int fd) { return close(fd); }
int fpcctr_read(int fd, void *buf, int len) { return read(fd, buf, len); }
int fpcctr_write(int fd, const void *buf, int len) { return write(fd, buf, len); }

long long fpcctr_lseek(int fd, long long offset, int whence)
{
	off_t r = lseek(fd, (off_t)offset, whence);
	return (long long)r;
}

int fpcctr_ftruncate(int fd, long long size) { return ftruncate(fd, (off_t)size); }
int fpcctr_isatty(int fd) { return isatty(fd); }
int fpcctr_unlink(const char *path) { return unlink(path); }
int fpcctr_rename(const char *from, const char *to) { return rename(from, to); }
int fpcctr_mkdir(const char *path) { return mkdir(path, 0777); }
int fpcctr_rmdir(const char *path) { return rmdir(path); }
int fpcctr_chdir(const char *path) { return chdir(path); }

int fpcctr_getcwd(char *buf, int size)
{
	return getcwd(buf, size) ? 0 : -1;
}

/* Kind: 0 = missing, 1 = regular file, 2 = directory, 3 = other. */
typedef struct {
	long long size;
	long long mtime;  /* seconds since the Unix epoch */
	int kind;
	int readonly;
} fpcctr_statinfo;

static void fill_stat(const struct stat *st, fpcctr_statinfo *info)
{
	info->size = st->st_size;
	info->mtime = st->st_mtime;
	if (S_ISDIR(st->st_mode)) info->kind = 2;
	else if (S_ISREG(st->st_mode)) info->kind = 1;
	else info->kind = 3;
	info->readonly = (st->st_mode & S_IWUSR) ? 0 : 1;
}

int fpcctr_stat(const char *path, fpcctr_statinfo *info)
{
	struct stat st;
	memset(info, 0, sizeof(*info));
	if (stat(path, &st) != 0) return -1;
	fill_stat(&st, info);
	return 0;
}

int fpcctr_fstat(int fd, fpcctr_statinfo *info)
{
	struct stat st;
	memset(info, 0, sizeof(*info));
	if (fstat(fd, &st) != 0) return -1;
	fill_stat(&st, info);
	return 0;
}

void *fpcctr_opendir(const char *path) { return opendir(path); }
int fpcctr_closedir(void *dir) { return closedir((DIR *)dir); }

const char *fpcctr_readdir(void *dir)
{
	struct dirent *e = readdir((DIR *)dir);
	return e ? e->d_name : NULL;
}

/* ---------------------------------------------------------------- time */

/* Monotonic milliseconds and microseconds from the ARM11 tick counter. */
unsigned long long fpcctr_ticks_us(void)
{
	return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000ULL);
}

unsigned long long fpcctr_ticks_ms(void)
{
	return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000ULL);
}

unsigned long long fpcctr_ticks_raw(void) { return svcGetSystemTick(); }
unsigned long long fpcctr_ticks_per_second(void) { return SYSCLOCK_ARM11; }

void fpcctr_sleep_ns(long long ns) { svcSleepThread(ns); }

/* Local wall-clock time; fields: year, month(1-12), day, hour, minute,
   second, millisecond, day of week (0 = Sunday). */
void fpcctr_localtime(int *fields)
{
	struct timeval tv;
	struct tm tm;
	time_t t;
	gettimeofday(&tv, NULL);
	t = tv.tv_sec;
	localtime_r(&t, &tm);
	fields[0] = tm.tm_year + 1900;
	fields[1] = tm.tm_mon + 1;
	fields[2] = tm.tm_mday;
	fields[3] = tm.tm_hour;
	fields[4] = tm.tm_min;
	fields[5] = tm.tm_sec;
	fields[6] = tv.tv_usec / 1000;
	fields[7] = tm.tm_wday;
}

long long fpcctr_time(void) { return (long long)time(NULL); }

/* Converts Unix seconds to local broken-down time (same field layout). */
void fpcctr_unix_to_local(long long secs, int *fields)
{
	struct tm tm;
	time_t t = (time_t)secs;
	localtime_r(&t, &tm);
	fields[0] = tm.tm_year + 1900;
	fields[1] = tm.tm_mon + 1;
	fields[2] = tm.tm_mday;
	fields[3] = tm.tm_hour;
	fields[4] = tm.tm_min;
	fields[5] = tm.tm_sec;
	fields[6] = 0;
	fields[7] = tm.tm_wday;
}

/* ---------------------------------------------------------------- locks */

void fpcctr_lock_init(void *p) { RecursiveLock_Init((RecursiveLock *)p); }
void fpcctr_lock_enter(void *p) { RecursiveLock_Lock((RecursiveLock *)p); }
int fpcctr_lock_tryenter(void *p) { return RecursiveLock_TryLock((RecursiveLock *)p) == 0; }
void fpcctr_lock_leave(void *p) { RecursiveLock_Unlock((RecursiveLock *)p); }

/* ---------------------------------------------------------------- events */

/* manual != 0 creates a sticky (manual reset) event. */
void fpcctr_event_init(void *p, int manual, int initial)
{
	LightEvent *ev = (LightEvent *)p;
	LightEvent_Init(ev, manual ? RESET_STICKY : RESET_ONESHOT);
	if (initial) LightEvent_Signal(ev);
}

void fpcctr_event_set(void *p) { LightEvent_Signal((LightEvent *)p); }
void fpcctr_event_reset(void *p) { LightEvent_Clear((LightEvent *)p); }
void fpcctr_event_wait(void *p) { LightEvent_Wait((LightEvent *)p); }

/* Returns 1 when signalled, 0 on timeout. timeout_ms < 0 waits forever. */
int fpcctr_event_wait_timeout(void *p, int timeout_ms)
{
	LightEvent *ev = (LightEvent *)p;
	if (timeout_ms < 0) {
		LightEvent_Wait(ev);
		return 1;
	}
	if (timeout_ms == 0) return LightEvent_TryWait(ev) ? 1 : 0;
	return LightEvent_WaitTimeout(ev, (s64)timeout_ms * 1000000LL) == 0;
}

void *fpcctr_alloc_event(int manual, int initial)
{
	LightEvent *ev = (LightEvent *)malloc(sizeof(LightEvent));
	if (ev) fpcctr_event_init(ev, manual, initial);
	return ev;
}

void fpcctr_free(void *p) { free(p); }

/* ---------------------------------------------------------------- threads */

/* Per-thread Pascal threadvar block and a unique per-thread id. */
static __thread void *fpcctr_tls_block;
static __thread int fpcctr_tls_marker;

void *fpcctr_tls_get(void) { return fpcctr_tls_block; }
void fpcctr_tls_set(void *p) { fpcctr_tls_block = p; }
void *fpcctr_thread_self(void) { return &fpcctr_tls_marker; }

/* Default core for new Pascal threads: -2 = the application's default
   core. The game may set this to 2 on a New 3DS to use the extra core. */
static int fpcctr_default_core = -2;
void fpcctr_set_default_core(int core) { fpcctr_default_core = core; }
int fpcctr_get_default_core(void) { return fpcctr_default_core; }

int fpcctr_main_priority(void)
{
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	return prio;
}

/* priority_delta is added to the main thread priority (lower = higher
   priority on the 3DS; valid range 0x18..0x3F). */
void *fpcctr_thread_create(void (*entry)(void *), void *arg, int stack_size,
                           int priority_delta, int core)
{
	int prio = fpcctr_main_priority() + priority_delta;
	Thread t;
	if (prio < 0x18) prio = 0x18;
	if (prio > 0x3F) prio = 0x3F;
	if (stack_size < 64 * 1024) stack_size = 64 * 1024;
	if (core == -100) core = fpcctr_default_core;
	t = threadCreate(entry, arg, (size_t)stack_size, prio, core, false);
	if (!t && core != -2)
		t = threadCreate(entry, arg, (size_t)stack_size, prio, -2, false);
	return t;
}

/* Returns 0 on success (thread finished), non-zero on timeout/error. */
int fpcctr_thread_join(void *t, long long timeout_ns)
{
	if (!t) return -1;
	return R_FAILED(threadJoin((Thread)t, timeout_ns < 0 ? U64_MAX : (u64)timeout_ns)) ? -1 : 0;
}

int fpcctr_thread_exitcode(void *t) { return t ? threadGetExitCode((Thread)t) : 0; }
void fpcctr_thread_free(void *t) { if (t) threadFree((Thread)t); }
void fpcctr_thread_exit(int rc) { threadExit(rc); }
void fpcctr_yield(void) { svcSleepThread(0); }

int fpcctr_cpu_count(void)
{
	bool isNew = false;
	APT_CheckNew3DS(&isNew);
	return isNew ? 3 : 2;
}

int fpcctr_is_new3ds(void)
{
	bool isNew = false;
	APT_CheckNew3DS(&isNew);
	return isNew ? 1 : 0;
}

/* ---------------------------------------------------------------- misc */

void fpcctr_exit(int code) { exit(code); }

const char *fpcctr_getenv(const char *name) { return getenv(name); }

void fpcctr_thread_detach(void *t) { if (t) threadDetach((Thread)t); }

/* Sets the priority relative to the main thread (see thread_create). */
int fpcctr_thread_set_priority(void *t, int priority_delta)
{
	int prio = fpcctr_main_priority() + priority_delta;
	Handle h = t ? threadGetHandle((Thread)t) : CUR_THREAD_HANDLE;
	if (prio < 0x18) prio = 0x18;
	if (prio > 0x3F) prio = 0x3F;
	return R_SUCCEEDED(svcSetThreadPriority(h, prio)) ? 1 : 0;
}

int fpcctr_thread_get_priority(void *t)
{
	s32 prio = 0x30;
	Handle h = t ? threadGetHandle((Thread)t) : CUR_THREAD_HANDLE;
	svcGetThreadPriority(&prio, h);
	return prio - fpcctr_main_priority();
}

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
#include <sys/iosupport.h>
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

/* OS-level blocks for the Pascal heap manager. The application heap is
   only about 87 MiB and gets fragmented by the game's multi-MiB buffers,
   while the linear (GPU) heap usually has room to spare. When malloc
   fails, fall back to linear memory as long as enough of it stays free for
   textures and the GPU command and vertex buffers. */
#define LINEAR_RESERVE (10u * 1024 * 1024)
extern u32 __ctru_linear_heap, __ctru_linear_heap_size;
static u32 linear_fallback_bytes, os_alloc_failures, os_alloc_last_failed;
void fpcctr_trace(const char *what, const char *path, u32 size);

static int is_linear(const void *p)
{
	u32 a = (u32)p;
	return a >= __ctru_linear_heap && a < __ctru_linear_heap + __ctru_linear_heap_size;
}

void *fpcctr_os_alloc(size_t size)
{
	void *p = malloc(size);
	if (!p && linearSpaceFree() >= size + LINEAR_RESERVE) {
		p = linearAlloc(size);
		if (p) linear_fallback_bytes += size;
	}
	if (!p) {
		os_alloc_failures++;
		os_alloc_last_failed = size;
	}
	if (size >= 1024 * 1024)
		fpcctr_trace(!p ? "allocfail" : is_linear(p) ? "alloclin" : "alloc", NULL, size);
	return p;
}

void fpcctr_os_free(void *p, size_t size)
{
	if (!p) return;
	if (size >= 1024 * 1024) fpcctr_trace("free", NULL, size);
	if (is_linear(p)) {
		linear_fallback_bytes -= size;
		linearFree(p);
	} else
		free(p);
}

/* [0] bytes spilled to linear memory, [1] failed allocations, [2] size of
   the last failed one. */
void fpcctr_os_alloc_stats(u32 *out)
{
	out[0] = linear_fallback_bytes;
	out[1] = os_alloc_failures;
	out[2] = os_alloc_last_failed;
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

/* Virtual file descriptors.
 *
 * The 3DS limits how many files a process can keep open, while the game
 * keeps every data package open for its whole run. Files opened through
 * the RTL get virtual descriptors (VFD_BASE + index); at most MAX_REAL of
 * them hold a real descriptor at a time. The least recently used one is
 * closed when needed and transparently reopened at the same position. */
#define VFD_BASE 1000
#define MAX_VFD 1024
/* The FS service fails at about 22 open files (0xD860466C), and logs and
   libraries need some too. */
#define MAX_REAL 8

typedef struct {
	char *path;
	int flags;      /* newlib flags for reopening */
	int mode;
	int fd;         /* real descriptor or -1 */
	off_t pos;      /* position while closed */
	u32 last_use;
} vfile;

static vfile vfiles[MAX_VFD];
static int real_open_count;
static u32 use_clock;
static LightLock vfile_lock = 1;
static struct {
	int opened, peak_open, peak_real, evictions, reopen_failures, failures, rdonly_fallbacks;
	int last_errno;
	char last_path[256];
} fstats;

static void note_failure(const char *path, int err)
{
	fstats.failures++;
	fstats.last_errno = err;
	strncpy(fstats.last_path, path, sizeof(fstats.last_path) - 1);
}

static int real_open(const char *path, int flags, int mode)
{
	int fd = open(path, flags, mode);
	if (fd < 0 && (flags & O_ACCMODE) == O_RDWR &&
	    (errno == EACCES || errno == EPERM || errno == EROFS)) {
		/* read-only media or attribute: the game opens data read/write */
		fd = open(path, (flags & ~O_ACCMODE) | O_RDONLY, mode);
		if (fd >= 0) fstats.rdonly_fallbacks++;
	}
	return fd;
}

/* Errors that do not mean "out of file handles". */
static int is_definite_error(int err)
{
	return err == ENOENT || err == ENOTDIR || err == EEXIST || err == EISDIR || err == ENAMETOOLONG;
}

static int evict_one(void)
{
	int i, victim = -1;
	u32 oldest = 0xFFFFFFFFu;
	for (i = 0; i < MAX_VFD; i++)
		if (vfiles[i].path && vfiles[i].fd >= 0 && vfiles[i].last_use < oldest) {
			oldest = vfiles[i].last_use;
			victim = i;
		}
	if (victim < 0) return 0;
	vfiles[victim].pos = lseek(vfiles[victim].fd, 0, SEEK_CUR);
	close(vfiles[victim].fd);
	vfiles[victim].fd = -1;
	real_open_count--;
	fstats.evictions++;
	return 1;
}

/* Returns the real descriptor of a virtual one, reopening it if needed;
   -1 with errno set on failure. Called with vfile_lock held. */
static int real_fd(int vfd)
{
	vfile *v;
	int fd;
	if (vfd < VFD_BASE) return vfd;
	if (vfd >= VFD_BASE + MAX_VFD || !vfiles[vfd - VFD_BASE].path) {
		errno = EBADF;
		return -1;
	}
	v = &vfiles[vfd - VFD_BASE];
	v->last_use = ++use_clock;
	if (v->fd >= 0) return v->fd;
	if (real_open_count >= MAX_REAL) evict_one();
	fd = real_open(v->path, v->flags & ~(O_CREAT | O_TRUNC | O_EXCL), v->mode);
	while (fd < 0 && !is_definite_error(errno) && evict_one())
		fd = real_open(v->path, v->flags & ~(O_CREAT | O_TRUNC | O_EXCL), v->mode);
	if (fd < 0) {
		fstats.reopen_failures++;
		note_failure(v->path, errno);
		return -1;
	}
	if (!(v->flags & O_APPEND)) lseek(fd, v->pos, SEEK_SET);
	v->fd = fd;
	real_open_count++;
	if (real_open_count > fstats.peak_real) fstats.peak_real = real_open_count;
	return fd;
}

/* Bumped whenever a directory entry may have appeared, gone or been renamed,
   so callers can cache directory listings (each FS call is an IPC round trip). */
static volatile unsigned int fs_generation;

unsigned int fpcctr_fs_generation(void) { return fs_generation; }

/* Debug aid: when trace_open.txt exists next to the game data, every open is
   appended to open.log together with the heap in use. */
void fpcctr_trace(const char *what, const char *path, u32 size)
{
	static int state; /* 0 unknown, 1 off, 2 on */
	static FILE *log;
	struct stat st;
	if (state == 1) return;
	if (state == 0) {
		state = 1;
		if (stat("sdmc:/3ds/SpaceRangersHD/trace_open.txt", &st) == 0 &&
		    (log = fopen("sdmc:/3ds/SpaceRangersHD/open.log", "w")) != NULL)
			state = 2;
		if (state == 1) return;
	}
	fprintf(log, "%lu %u %s %u %s\n", (unsigned long)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000ULL)),
		(unsigned)(mallinfo().uordblks / 1024), what, (unsigned)(size / 1024), path ? path : "-");
	fflush(log);
}

int fpcctr_open(const char *path, int flags, int mode)
{
	int f, fd, i, err, created = 0;
	struct stat st;
	switch (flags & 3) {
	case 0: f = O_RDONLY; break;
	case 1: f = O_WRONLY; break;
	default: f = O_RDWR; break;
	}
	if (flags & FPCCTR_O_CREAT) f |= O_CREAT;
	if (flags & FPCCTR_O_TRUNC) f |= O_TRUNC;
	if (flags & FPCCTR_O_APPEND) f |= O_APPEND;
	if (flags & FPCCTR_O_EXCL) f |= O_EXCL;
	if ((f & O_CREAT) && stat(path, &st) != 0) created = 1;
	LightLock_Lock(&vfile_lock);
	for (i = 0; i < MAX_VFD && vfiles[i].path; i++)
		;
	if (i == MAX_VFD) {
		LightLock_Unlock(&vfile_lock);
		note_failure(path, EMFILE);
		errno = EMFILE;
		return -1;
	}
	if (real_open_count >= MAX_REAL) evict_one();
	fd = real_open(path, f, mode);
	while (fd < 0 && !is_definite_error(errno) && evict_one())
		fd = real_open(path, f, mode);
	if (fd < 0) {
		err = errno;
		note_failure(path, err);
		LightLock_Unlock(&vfile_lock);
		errno = err;
		return -1;
	}
	if (created) fs_generation++;
	fpcctr_trace("open", path, 0);
	vfiles[i].path = strdup(path);
	vfiles[i].flags = f;
	vfiles[i].mode = mode;
	vfiles[i].fd = fd;
	vfiles[i].pos = 0;
	vfiles[i].last_use = ++use_clock;
	real_open_count++;
	fstats.opened++;
	if (real_open_count > fstats.peak_real) fstats.peak_real = real_open_count;
	{
		int n = 0, j;
		for (j = 0; j < MAX_VFD; j++) if (vfiles[j].path) n++;
		if (n > fstats.peak_open) fstats.peak_open = n;
	}
	LightLock_Unlock(&vfile_lock);
	return VFD_BASE + i;
}

int fpcctr_close(int vfd)
{
	vfile *v;
	int r = 0;
	if (vfd < VFD_BASE) return close(vfd);
	LightLock_Lock(&vfile_lock);
	if (vfd >= VFD_BASE + MAX_VFD || !vfiles[vfd - VFD_BASE].path) {
		LightLock_Unlock(&vfile_lock);
		errno = EBADF;
		return -1;
	}
	v = &vfiles[vfd - VFD_BASE];
	if (v->fd >= 0) {
		r = close(v->fd);
		real_open_count--;
	}
	free(v->path);
	memset(v, 0, sizeof(*v));
	LightLock_Unlock(&vfile_lock);
	return r;
}

int fpcctr_read(int vfd, void *buf, int len)
{
	int fd, r;
	LightLock_Lock(&vfile_lock);
	fd = real_fd(vfd);
	r = fd < 0 ? -1 : read(fd, buf, len);
	LightLock_Unlock(&vfile_lock);
	return r;
}

int fpcctr_write(int vfd, const void *buf, int len)
{
	int fd, r;
	LightLock_Lock(&vfile_lock);
	fd = real_fd(vfd);
	r = fd < 0 ? -1 : write(fd, buf, len);
	LightLock_Unlock(&vfile_lock);
	return r;
}

long long fpcctr_lseek(int vfd, long long offset, int whence)
{
	int fd;
	off_t r;
	LightLock_Lock(&vfile_lock);
	fd = real_fd(vfd);
	r = fd < 0 ? -1 : lseek(fd, (off_t)offset, whence);
	LightLock_Unlock(&vfile_lock);
	return (long long)r;
}

int fpcctr_ftruncate(int vfd, long long size)
{
	int fd, r;
	LightLock_Lock(&vfile_lock);
	fd = real_fd(vfd);
	r = fd < 0 ? -1 : ftruncate(fd, (off_t)size);
	LightLock_Unlock(&vfile_lock);
	return r;
}

/* [0] files opened, [1] open now, [2] peak open, [3] real descriptors now,
   [4] peak real, [5] evictions, [6] reopen failures, [7] open failures,
   [8] read-only fallbacks, [9] errno of the last failure. */
const char *fpcctr_file_stats(int *out)
{
	int i, n = 0;
	LightLock_Lock(&vfile_lock);
	for (i = 0; i < MAX_VFD; i++) if (vfiles[i].path) n++;
	out[0] = fstats.opened; out[1] = n; out[2] = fstats.peak_open; out[3] = real_open_count;
	out[4] = fstats.peak_real; out[5] = fstats.evictions; out[6] = fstats.reopen_failures;
	out[7] = fstats.failures; out[8] = fstats.rdonly_fallbacks; out[9] = fstats.last_errno;
	LightLock_Unlock(&vfile_lock);
	return fstats.last_path;
}

int fpcctr_isatty(int fd) { return fd < VFD_BASE ? isatty(fd) : 0; }
static int bump(int r) { fs_generation++; return r; }
int fpcctr_unlink(const char *path) { return bump(unlink(path)); }
int fpcctr_rename(const char *from, const char *to) { return bump(rename(from, to)); }
int fpcctr_mkdir(const char *path) { return bump(mkdir(path, 0777)); }
int fpcctr_rmdir(const char *path) { return bump(rmdir(path)); }
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

int fpcctr_fstat(int vfd, fpcctr_statinfo *info)
{
	struct stat st;
	int fd, r;
	memset(info, 0, sizeof(*info));
	LightLock_Lock(&vfile_lock);
	fd = real_fd(vfd);
	r = fd < 0 ? -1 : fstat(fd, &st);
	LightLock_Unlock(&vfile_lock);
	if (r != 0) return -1;
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

/* Directory entry plus its type and size. The SD archive reports both with
   the entry itself, so this avoids a stat() per entry: on the 3DS each stat
   opens and closes the file through the FS service, which made scanning a
   data folder take seconds. The modification time is not available here. */
const char *fpcctr_readdir_info(void *dir, fpcctr_statinfo *info)
{
	DIR *d = (DIR *)dir;
	struct stat st;
	const devoptab_t *dev;
	memset(info, 0, sizeof(*info));
	if (!d || !d->dirData) return NULL;
	dev = devoptab_list[d->dirData->device];
	if (!dev || !dev->dirnext_r) return NULL;
	memset(&st, 0, sizeof(st));
	if (dev->dirnext_r(_REENT, d->dirData, d->fileData.d_name, &st) != 0)
		return NULL;
	d->position++;
	fill_stat(&st, info);
	info->readonly = 0;
	return d->fileData.d_name;
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

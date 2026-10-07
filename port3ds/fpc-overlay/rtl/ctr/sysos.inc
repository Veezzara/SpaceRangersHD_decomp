{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{ Bindings to the C support layer (fpcctr.c), newlib and libctru }

{$L fpcctr.o}

type
  TCtrStatInfo = record
    size: Int64;
    mtime: Int64;
    kind: LongInt;      { 0 missing, 1 file, 2 directory, 3 other }
    readonly: LongInt;
  end;
  PCtrStatInfo = ^TCtrStatInfo;

const
  CTR_O_CREAT  = $10;
  CTR_O_TRUNC  = $20;
  CTR_O_APPEND = $40;
  CTR_O_EXCL   = $80;

  { newlib errno values }
  CTR_EPERM   = 1;
  CTR_ENOENT  = 2;
  CTR_EINTR   = 4;
  CTR_EIO     = 5;
  CTR_EBADF   = 9;
  CTR_EAGAIN  = 11;
  CTR_ENOMEM  = 12;
  CTR_EACCES  = 13;
  CTR_EFAULT  = 14;
  CTR_EBUSY   = 16;
  CTR_EEXIST  = 17;
  CTR_EXDEV   = 18;
  CTR_ENOTDIR = 20;
  CTR_EISDIR  = 21;
  CTR_EINVAL  = 22;
  CTR_ENFILE  = 23;
  CTR_EMFILE  = 24;
  CTR_ENOSPC  = 28;
  CTR_ESPIPE  = 29;
  CTR_EROFS   = 30;
  CTR_EPIPE   = 32;
  CTR_ENAMETOOLONG = 91;
  CTR_ENOTEMPTY = 90;
  CTR_ELOOP   = 92;

function ctr_errno: LongInt; cdecl; external name 'fpcctr_errno';
procedure ctr_set_errno(e: LongInt); cdecl; external name 'fpcctr_set_errno';
function ctr_open(path: PAnsiChar; flags, mode: LongInt): LongInt; cdecl; external name 'fpcctr_open';
function ctr_close(fd: LongInt): LongInt; cdecl; external name 'fpcctr_close';
function ctr_read(fd: LongInt; buf: Pointer; len: LongInt): LongInt; cdecl; external name 'fpcctr_read';
function ctr_write(fd: LongInt; buf: Pointer; len: LongInt): LongInt; cdecl; external name 'fpcctr_write';
function ctr_lseek(fd: LongInt; offset: Int64; whence: LongInt): Int64; cdecl; external name 'fpcctr_lseek';
function ctr_ftruncate(fd: LongInt; size: Int64): LongInt; cdecl; external name 'fpcctr_ftruncate';
function ctr_isatty(fd: LongInt): LongInt; cdecl; external name 'fpcctr_isatty';
function ctr_unlink(path: PAnsiChar): LongInt; cdecl; external name 'fpcctr_unlink';
function ctr_rename(src, dst: PAnsiChar): LongInt; cdecl; external name 'fpcctr_rename';
function ctr_mkdir(path: PAnsiChar): LongInt; cdecl; external name 'fpcctr_mkdir';
function ctr_rmdir(path: PAnsiChar): LongInt; cdecl; external name 'fpcctr_rmdir';
function ctr_chdir(path: PAnsiChar): LongInt; cdecl; external name 'fpcctr_chdir';
function ctr_getcwd(buf: PAnsiChar; size: LongInt): LongInt; cdecl; external name 'fpcctr_getcwd';
function ctr_stat(path: PAnsiChar; info: PCtrStatInfo): LongInt; cdecl; external name 'fpcctr_stat';
function ctr_fstat(fd: LongInt; info: PCtrStatInfo): LongInt; cdecl; external name 'fpcctr_fstat';
function ctr_opendir(path: PAnsiChar): Pointer; cdecl; external name 'fpcctr_opendir';
function ctr_closedir(dir: Pointer): LongInt; cdecl; external name 'fpcctr_closedir';
function ctr_readdir(dir: Pointer): PAnsiChar; cdecl; external name 'fpcctr_readdir';

function ctr_ticks_ms: QWord; cdecl; external name 'fpcctr_ticks_ms';
function ctr_ticks_us: QWord; cdecl; external name 'fpcctr_ticks_us';
function ctr_ticks_raw: QWord; cdecl; external name 'fpcctr_ticks_raw';
function ctr_ticks_per_second: QWord; cdecl; external name 'fpcctr_ticks_per_second';
procedure ctr_sleep_ns(ns: Int64); cdecl; external name 'fpcctr_sleep_ns';
procedure ctr_localtime(fields: PLongInt); cdecl; external name 'fpcctr_localtime';
procedure ctr_unix_to_local(secs: Int64; fields: PLongInt); cdecl; external name 'fpcctr_unix_to_local';
function ctr_time: Int64; cdecl; external name 'fpcctr_time';

procedure ctr_lock_init(p: Pointer); cdecl; external name 'fpcctr_lock_init';
procedure ctr_lock_enter(p: Pointer); cdecl; external name 'fpcctr_lock_enter';
function ctr_lock_tryenter(p: Pointer): LongInt; cdecl; external name 'fpcctr_lock_tryenter';
procedure ctr_lock_leave(p: Pointer); cdecl; external name 'fpcctr_lock_leave';

function ctr_alloc_event(manual, initial: LongInt): Pointer; cdecl; external name 'fpcctr_alloc_event';
procedure ctr_event_set(p: Pointer); cdecl; external name 'fpcctr_event_set';
procedure ctr_event_reset(p: Pointer); cdecl; external name 'fpcctr_event_reset';
procedure ctr_event_wait(p: Pointer); cdecl; external name 'fpcctr_event_wait';
function ctr_event_wait_timeout(p: Pointer; timeout_ms: LongInt): LongInt; cdecl; external name 'fpcctr_event_wait_timeout';
procedure ctr_free(p: Pointer); cdecl; external name 'fpcctr_free';

type
  TCtrThreadEntry = procedure(arg: Pointer); cdecl;

function ctr_tls_get: Pointer; cdecl; external name 'fpcctr_tls_get';
procedure ctr_tls_set(p: Pointer); cdecl; external name 'fpcctr_tls_set';
function ctr_thread_self: Pointer; cdecl; external name 'fpcctr_thread_self';
function ctr_thread_create(entry: TCtrThreadEntry; arg: Pointer; stack_size, priority_delta, core: LongInt): Pointer; cdecl; external name 'fpcctr_thread_create';
function ctr_thread_join(t: Pointer; timeout_ns: Int64): LongInt; cdecl; external name 'fpcctr_thread_join';
function ctr_thread_exitcode(t: Pointer): LongInt; cdecl; external name 'fpcctr_thread_exitcode';
procedure ctr_thread_free(t: Pointer); cdecl; external name 'fpcctr_thread_free';
procedure ctr_thread_exit(rc: LongInt); cdecl; external name 'fpcctr_thread_exit';
procedure ctr_yield; cdecl; external name 'fpcctr_yield';
function ctr_cpu_count: LongInt; cdecl; external name 'fpcctr_cpu_count';
procedure ctr_exit(code: LongInt); cdecl; external name 'fpcctr_exit';
function ctr_getenv(name: PAnsiChar): PAnsiChar; cdecl; external name 'fpcctr_getenv';

function ctr_malloc(size: PtrUInt): Pointer; cdecl; external name 'malloc';
procedure ctr_cfree(p: Pointer); cdecl; external name 'free';

procedure Errno2InOutRes;
var
  e: LongInt;
begin
  e:=ctr_errno;
  case e of
    0: InOutRes:=0;
    CTR_ENFILE,
    CTR_EMFILE: InOutRes:=4;
    CTR_ENOENT: InOutRes:=2;
    CTR_EBADF: InOutRes:=6;
    CTR_ENOMEM,
    CTR_EFAULT: InOutRes:=217;
    CTR_EINVAL: InOutRes:=218;
    CTR_EPIPE,
    CTR_EINTR,
    CTR_EIO,
    CTR_EAGAIN,
    CTR_ENOSPC: InOutRes:=101;
    CTR_ENAMETOOLONG,
    CTR_ELOOP,
    CTR_ENOTDIR: InOutRes:=3;
    CTR_EPERM,
    CTR_EROFS,
    CTR_EEXIST,
    CTR_EISDIR,
    CTR_ENOTEMPTY,
    CTR_EACCES: InOutRes:=5;
    CTR_EBUSY: InOutRes:=162;
  else
    InOutRes:=e;
  end;
end;

procedure ctr_thread_detach(t: Pointer); cdecl; external name 'fpcctr_thread_detach';
function ctr_thread_set_priority(t: Pointer; delta: LongInt): LongInt; cdecl; external name 'fpcctr_thread_set_priority';
function ctr_thread_get_priority(t: Pointer): LongInt; cdecl; external name 'fpcctr_thread_get_priority';

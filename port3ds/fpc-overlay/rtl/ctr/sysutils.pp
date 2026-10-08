{
    This file is part of the Free Pascal run time library.

    Sysutils unit for the Nintendo 3DS (CTR).

    File system access goes through newlib (sdmc:/ and romfs:/ devices
    provided by libctru), wrapped by fpcctr.c.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{$IFNDEF FPC_DOTTEDUNITS}
unit sysutils;
{$ENDIF FPC_DOTTEDUNITS}

interface

{$MODE objfpc}
{$MODESWITCH OUT}
{$IFDEF UNICODERTL}
{$MODESWITCH UNICODESTRINGS}
{$ELSE}
{$H+}
{$ENDIF}
{$modeswitch typehelpers}
{$modeswitch advancedrecords}

{$DEFINE HAS_OSERROR}
{$DEFINE HAS_SLEEP}
{$DEFINE HAS_GETTICKCOUNT64}
{$DEFINE HAS_TEMPDIR}
{$DEFINE FINDHANDLE_IS_POINTER}
{ used OS file system APIs use ansistring }
{$define SYSUTILS_HAS_ANSISTR_FILEUTIL_IMPL}
{ OS has an ansistring/single byte environment variable API }
{$define SYSUTILS_HAS_ANSISTR_ENVVAR_IMPL}

{ Include platform independent interface part }
{$i sysutilh.inc}

implementation

{$IFDEF FPC_DOTTEDUNITS}
uses
  System.SysConst;
{$ELSE FPC_DOTTEDUNITS}
uses
  sysconst;
{$ENDIF FPC_DOTTEDUNITS}

{ Include platform independent implementation part }
{$i sysutils.inc}

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

function ctr_errno: LongInt; cdecl; external name 'fpcctr_errno';
function ctr_open(path: PAnsiChar; flags, mode: LongInt): LongInt; cdecl; external name 'fpcctr_open';
function ctr_close(fd: LongInt): LongInt; cdecl; external name 'fpcctr_close';
function ctr_read(fd: LongInt; buf: Pointer; len: LongInt): LongInt; cdecl; external name 'fpcctr_read';
function ctr_write(fd: LongInt; buf: Pointer; len: LongInt): LongInt; cdecl; external name 'fpcctr_write';
function ctr_lseek(fd: LongInt; offset: Int64; whence: LongInt): Int64; cdecl; external name 'fpcctr_lseek';
function ctr_ftruncate(fd: LongInt; size: Int64): LongInt; cdecl; external name 'fpcctr_ftruncate';
function ctr_unlink(path: PAnsiChar): LongInt; cdecl; external name 'fpcctr_unlink';
function ctr_rename(src, dst: PAnsiChar): LongInt; cdecl; external name 'fpcctr_rename';
function ctr_stat(path: PAnsiChar; info: PCtrStatInfo): LongInt; cdecl; external name 'fpcctr_stat';
function ctr_fstat(fd: LongInt; info: PCtrStatInfo): LongInt; cdecl; external name 'fpcctr_fstat';
function ctr_opendir(path: PAnsiChar): Pointer; cdecl; external name 'fpcctr_opendir';
function ctr_closedir(dir: Pointer): LongInt; cdecl; external name 'fpcctr_closedir';
function ctr_readdir(dir: Pointer): PAnsiChar; cdecl; external name 'fpcctr_readdir';
function ctr_readdir_info(dir: Pointer; info: PCtrStatInfo): PAnsiChar; cdecl; external name 'fpcctr_readdir_info';
function ctr_ticks_ms: QWord; cdecl; external name 'fpcctr_ticks_ms';
procedure ctr_sleep_ns(ns: Int64); cdecl; external name 'fpcctr_sleep_ns';
procedure ctr_localtime(fields: PLongInt); cdecl; external name 'fpcctr_localtime';
procedure ctr_unix_to_local(secs: Int64; fields: PLongInt); cdecl; external name 'fpcctr_unix_to_local';
function ctr_getenv(name: PAnsiChar): PAnsiChar; cdecl; external name 'fpcctr_getenv';
function ctr_strerror(errnum: LongInt): PAnsiChar; cdecl; external name 'strerror';

{ Converts a Unix timestamp to a DOS packed local date (the "file date"
  format used by non-Unix targets of SysUtils). }
function UnixToFileDate(secs: Int64): Int64;
var
  f: array[0..7] of LongInt;
begin
  ctr_unix_to_local(secs,@f[0]);
  Result:=DateTimeToFileDate(EncodeDate(f[0],f[1],f[2])+EncodeTime(f[3],f[4],f[5],0));
end;

{****************************************************************************
                              File Functions
****************************************************************************}

function FileOpen(const FileName: RawByteString; Mode: Integer): THandle;
var
  SystemFileName: RawByteString;
  flags: LongInt;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  case (Mode and (fmOpenRead or fmOpenWrite or fmOpenReadWrite)) of
    fmOpenWrite: flags:=1;
    fmOpenReadWrite: flags:=2;
  else
    flags:=0;
  end;
  Result:=ctr_open(PAnsiChar(SystemFileName),flags,&666);
end;

function FileGetDate(Handle: THandle): Int64;
var
  info: TCtrStatInfo;
begin
  if ctr_fstat(Handle,@info)<>0 then
    Result:=-1
  else
    Result:=UnixToFileDate(info.mtime);
end;

function FileSetDate(Handle: THandle; Age: Int64): LongInt;
begin
  { not supported by the 3DS file systems through newlib }
  Result:=-1;
end;

function FileCreate(const FileName: RawByteString): THandle;
var
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  Result:=ctr_open(PAnsiChar(SystemFileName),2 or CTR_O_CREAT or CTR_O_TRUNC,&666);
end;

function FileCreate(const FileName: RawByteString; Rights: Integer): THandle;
begin
  Result:=FileCreate(FileName);
end;

function FileCreate(const FileName: RawByteString; ShareMode: Integer; Rights: Integer): THandle;
begin
  Result:=FileCreate(FileName);
end;

function FileRead(Handle: THandle; out Buffer; Count: LongInt): LongInt;
begin
  Result:=ctr_read(Handle,@Buffer,Count);
end;

function FileWrite(Handle: THandle; const Buffer; Count: LongInt): LongInt;
begin
  Result:=ctr_write(Handle,@Buffer,Count);
end;

function FileSeek(Handle: THandle; FOffset, Origin: LongInt): LongInt;
begin
  Result:=LongInt(ctr_lseek(Handle,FOffset,Origin));
end;

function FileSeek(Handle: THandle; FOffset: Int64; Origin: LongInt): Int64;
begin
  Result:=ctr_lseek(Handle,FOffset,Origin);
end;

procedure FileClose(Handle: THandle);
begin
  ctr_close(Handle);
end;

function FileTruncate(Handle: THandle; Size: Int64): Boolean;
begin
  Result:=ctr_ftruncate(Handle,Size)=0;
end;

function DeleteFile(const FileName: RawByteString): Boolean;
var
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  Result:=ctr_unlink(PAnsiChar(SystemFileName))=0;
end;

function RenameFile(const OldName, NewName: RawByteString): Boolean;
var
  OldSystemFileName, NewSystemFileName: RawByteString;
begin
  OldSystemFileName:=ToSingleByteFileSystemEncodedFileName(OldName);
  NewSystemFileName:=ToSingleByteFileSystemEncodedFileName(NewName);
  Result:=ctr_rename(PAnsiChar(OldSystemFileName),PAnsiChar(NewSystemFileName))=0;
end;

function FileAge(const FileName: RawByteString): Int64;
var
  info: TCtrStatInfo;
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  if (ctr_stat(PAnsiChar(SystemFileName),@info)<>0) or (info.kind=2) then
    Result:=-1
  else
    Result:=UnixToFileDate(info.mtime);
end;

function FileGetSymLinkTarget(const FileName: RawByteString; out SymLinkRec: TRawbyteSymLinkRec): Boolean;
begin
  Result:=False;
end;

function FileExists(const FileName: RawByteString; FollowLink: Boolean): Boolean;
var
  info: TCtrStatInfo;
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  Result:=(ctr_stat(PAnsiChar(SystemFileName),@info)=0) and (info.kind<>2);
end;

function DirectoryExists(const Directory: RawByteString; FollowLink: Boolean): Boolean;
var
  info: TCtrStatInfo;
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(Directory);
  { stat() of "dir/" fails on some devices }
  while (Length(SystemFileName)>1) and (SystemFileName[Length(SystemFileName)] in AllowDirectorySeparators) and
        (SystemFileName[Length(SystemFileName)-1]<>':') do
    SetLength(SystemFileName,Length(SystemFileName)-1);
  Result:=(ctr_stat(PAnsiChar(SystemFileName),@info)=0) and (info.kind=2);
end;

function InfoToAttr(const info: TCtrStatInfo; const Name: RawByteString): LongInt;
begin
  Result:=0;
  if info.kind=2 then
    Result:=Result or faDirectory
  else
    Result:=Result or faArchive;
  if info.readonly<>0 then
    Result:=Result or faReadOnly;
  if (Length(Name)>0) and (Name[1]='.') and (Name<>'.') and (Name<>'..') then
    Result:=Result or faHidden;
end;

function FileGetAttr(const FileName: RawByteString): LongInt;
var
  info: TCtrStatInfo;
  SystemFileName: RawByteString;
begin
  SystemFileName:=ToSingleByteFileSystemEncodedFileName(FileName);
  if ctr_stat(PAnsiChar(SystemFileName),@info)<>0 then
    Result:=-1
  else
    Result:=InfoToAttr(info,ExtractFileName(SystemFileName));
end;

function FileSetAttr(const Filename: RawByteString; Attr: LongInt): LongInt;
begin
  Result:=-1;
end;

{****************************************************************************
                           FindFirst / FindNext
****************************************************************************}

type
  PCtrFindData = ^TCtrFindData;
  TCtrFindData = record
    Dir: Pointer;
    DirName: RawByteString;   { with trailing separator, may be empty }
    Mask: RawByteString;
    SearchAttr: LongInt;
    Single: Boolean;           { no wildcards: only one entry }
  end;

function CtrUpCase(c: AnsiChar): AnsiChar; inline;
begin
  if c in ['a'..'z'] then
    Result:=AnsiChar(Ord(c)-32)
  else
    Result:=c;
end;

{ Case-insensitive wildcard match ('*', '?'), as on FAT and Windows. }
function CtrMatch(const Pattern, Name: RawByteString): Boolean;
var
  p, n, starP, starN: SizeInt;
begin
  p:=1; n:=1; starP:=0; starN:=0;
  while n<=Length(Name) do
    begin
      if (p<=Length(Pattern)) and ((Pattern[p]='?') or (CtrUpCase(Pattern[p])=CtrUpCase(Name[n]))) then
        begin
          inc(p); inc(n);
        end
      else if (p<=Length(Pattern)) and (Pattern[p]='*') then
        begin
          starP:=p; starN:=n; inc(p);
        end
      else if starP<>0 then
        begin
          p:=starP+1; inc(starN); n:=starN;
        end
      else
        exit(False);
    end;
  while (p<=Length(Pattern)) and (Pattern[p]='*') do
    inc(p);
  { Windows semantics: "*.*" also matches names without an extension }
  if (p=Length(Pattern)) and (Pattern[p]='.') then
    inc(p);
  Result:=p>Length(Pattern);
end;

function CtrFillSearchRecInfo(const info: TCtrStatInfo; const Name: RawByteString; SearchAttr: LongInt; var Rslt: TAbstractSearchRec): Boolean;
var
  attr: LongInt;
begin
  Result:=False;
  attr:=InfoToAttr(info,Name);
  { the generic code passes the requested attributes; exclude entries with
    special attributes that were not asked for }
  if (attr and (faDirectory or faHidden or faSysFile) and not SearchAttr)<>0 then
    exit;
  Rslt.Attr:=attr;
  Rslt.Size:=info.size;
  if info.mtime<>0 then
    Rslt.Time:=UnixToFileDate(info.mtime)
  else
    Rslt.Time:=0;
  Result:=True;
end;

function CtrFillSearchRec(const FullName, Name: RawByteString; SearchAttr: LongInt; var Rslt: TAbstractSearchRec): Boolean;
var
  info: TCtrStatInfo;
begin
  Result:=(ctr_stat(PAnsiChar(FullName),@info)=0) and
    CtrFillSearchRecInfo(info,Name,SearchAttr,Rslt);
end;

function InternalFindNext(var Rslt: TAbstractSearchRec; var Name: RawByteString): LongInt;
var
  data: PCtrFindData;
  entry: PAnsiChar;
  entryName: RawByteString;
  info: TCtrStatInfo;
begin
  Result:=-1;
  data:=PCtrFindData(Rslt.FindHandle);
  if (data=nil) or (data^.Dir=nil) then
    exit;
  repeat
    { the entry carries its type and size; a stat per entry is very slow }
    entry:=ctr_readdir_info(data^.Dir,@info);
    if entry=nil then
      exit;
    entryName:=entry;
    if CtrMatch(data^.Mask,entryName) and
       CtrFillSearchRecInfo(info,entryName,data^.SearchAttr,Rslt) then
      begin
        Name:=entryName;
        SetCodePage(Name,DefaultFileSystemCodePage,false);
        Result:=0;
        exit;
      end;
  until false;
end;

function InternalFindFirst(const Path: RawByteString; Attr: LongInt; out Rslt: TAbstractSearchRec; var Name: RawByteString): LongInt;
var
  data: PCtrFindData;
  SystemPath, dirName: RawByteString;
  i: SizeInt;
begin
  Result:=-1;
  FillChar(Rslt,SizeOf(Rslt),0);
  if Path='' then
    exit;
  SystemPath:=ToSingleByteFileSystemEncodedFileName(Path);
  New(data);
  FillChar(data^,SizeOf(data^),0);
  Rslt.FindHandle:=data;
  data^.SearchAttr:=Attr or faArchive or faReadOnly;
  i:=Length(SystemPath);
  while (i>0) and not (SystemPath[i] in AllowDirectorySeparators) and (SystemPath[i]<>':') do
    dec(i);
  data^.DirName:=Copy(SystemPath,1,i);
  data^.Mask:=Copy(SystemPath,i+1,Length(SystemPath));
  if (Pos('*',data^.Mask)=0) and (Pos('?',data^.Mask)=0) then
    begin
      data^.Single:=True;
      if CtrFillSearchRec(SystemPath,data^.Mask,data^.SearchAttr,Rslt) then
        begin
          Name:=data^.Mask;
          SetCodePage(Name,DefaultFileSystemCodePage,false);
          Result:=0;
        end;
    end
  else
    begin
      dirName:=data^.DirName;
      if dirName='' then
        dirName:='.';
      data^.Dir:=ctr_opendir(PAnsiChar(dirName));
      if data^.Dir<>nil then
        Result:=InternalFindNext(Rslt,Name);
    end;
  if Result<>0 then
    InternalFindClose(Rslt.FindHandle);
end;

procedure InternalFindClose(var Handle: Pointer);
var
  data: PCtrFindData;
begin
  data:=PCtrFindData(Handle);
  if data=nil then
    exit;
  if data^.Dir<>nil then
    ctr_closedir(data^.Dir);
  Dispose(data);
  Handle:=nil;
end;

{****************************************************************************
                              Disk Functions
****************************************************************************}

function DiskFree(Drive: Byte): Int64;
begin
  Result:=-1;
end;

function DiskSize(Drive: Byte): Int64;
begin
  Result:=-1;
end;

{****************************************************************************
                              Misc Functions
****************************************************************************}

procedure SysBeep;
begin
end;

procedure Sleep(Milliseconds: Cardinal);
begin
  ctr_sleep_ns(Int64(Milliseconds)*1000000);
end;

function GetTickCount64: QWord;
begin
  Result:=ctr_ticks_ms;
end;

{****************************************************************************
                              Locale Functions
****************************************************************************}

procedure GetLocalTime(var SystemTime: TSystemTime);
var
  f: array[0..7] of LongInt;
begin
  ctr_localtime(@f[0]);
  SystemTime.Year:=f[0];
  SystemTime.Month:=f[1];
  SystemTime.Day:=f[2];
  SystemTime.Hour:=f[3];
  SystemTime.Minute:=f[4];
  SystemTime.Second:=f[5];
  SystemTime.MilliSecond:=f[6];
  SystemTime.DayOfWeek:=f[7];
end;

function SysErrorMessage(ErrorCode: Integer): String;
begin
  Result:=ctr_strerror(ErrorCode);
end;

{****************************************************************************
                              OS utility functions
****************************************************************************}

function GetTempDir(Global: Boolean): String;
begin
  if Assigned(OnGetTempDir) then
    Result:=OnGetTempDir(Global)
  else
    Result:='sdmc:/tmp/';
end;

function GetEnvironmentVariable(const EnvVar: AnsiString): AnsiString;
var
  p: PAnsiChar;
begin
  p:=ctr_getenv(PAnsiChar(EnvVar));
  if p=nil then
    Result:=''
  else
    Result:=p;
end;

function GetEnvironmentVariableCount: Integer;
begin
  Result:=0;
end;

function GetEnvironmentString(Index: Integer): {$ifdef FPC_RTL_UNICODE}UnicodeString{$else}AnsiString{$endif};
begin
  Result:='';
end;

function ExecuteProcess(const Path: RawByteString; const ComLine: RawByteString; Flags: TExecuteFlags=[]): Integer;
begin
  raise EOSError.Create('ExecuteProcess is not supported on the 3DS');
end;

function ExecuteProcess(const Path: RawByteString; const ComLine: array of RawByteString; Flags: TExecuteFlags=[]): Integer;
begin
  raise EOSError.Create('ExecuteProcess is not supported on the 3DS');
end;

function ExecuteProcess(const Path: UnicodeString; const ComLine: UnicodeString; Flags: TExecuteFlags=[]): Integer;
begin
  raise EOSError.Create('ExecuteProcess is not supported on the 3DS');
end;

function ExecuteProcess(const Path: UnicodeString; const ComLine: array of UnicodeString; Flags: TExecuteFlags=[]): Integer;
begin
  raise EOSError.Create('ExecuteProcess is not supported on the 3DS');
end;

function GetLastOSError: Integer;
begin
  Result:=ctr_errno;
end;

{****************************************************************************
                              Initialization code
****************************************************************************}

Initialization
  InitExceptions;
Finalization
  FreeTerminateProcs;
  DoneExceptions;
end.

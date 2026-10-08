unit Ctru;
{$MODE delphi}
// Minimal libctru bindings and process start-up for the Nintendo 3DS.
interface

type
  Result3DS = LongInt;

const
  // Game data lives on the SD card; see docs in port3ds/README.md.
  CtrDataDir = 'sdmc:/3ds/SpaceRangersHD';

  GFX_TOP = 0;
  GFX_BOTTOM = 1;
  GFX_LEFT = 0;
  GSPGPU_EVENT_VBLANK0 = 2;

  KEY_A = 1 shl 0;
  KEY_B = 1 shl 1;
  KEY_SELECT = 1 shl 2;
  KEY_START = 1 shl 3;
  KEY_DRIGHT = 1 shl 4;
  KEY_DLEFT = 1 shl 5;
  KEY_DUP = 1 shl 6;
  KEY_DDOWN = 1 shl 7;
  KEY_R = 1 shl 8;
  KEY_L = 1 shl 9;
  KEY_X = 1 shl 10;
  KEY_Y = 1 shl 11;
  KEY_ZL = 1 shl 14;
  KEY_ZR = 1 shl 15;
  KEY_TOUCH = 1 shl 20;
  KEY_CSTICK_RIGHT = 1 shl 24;
  KEY_CSTICK_LEFT = 1 shl 25;
  KEY_CSTICK_UP = 1 shl 26;
  KEY_CSTICK_DOWN = 1 shl 27;
  KEY_CPAD_RIGHT = 1 shl 28;
  KEY_CPAD_LEFT = 1 shl 29;
  KEY_CPAD_UP = 1 shl 30;
  KEY_CPAD_DOWN = LongWord(1) shl 31;

type
  TTouchPosition = record
    px, py: Word;
  end;

  TCirclePosition = record
    dx, dy: SmallInt;
  end;

procedure gfxInitDefault; cdecl; external;
procedure gfxExit; cdecl; external;
function consoleInit(screen: LongInt; console: Pointer): Pointer; cdecl; external;
function aptMainLoop: Boolean; cdecl; external;
procedure hidScanInput; cdecl; external;
function hidKeysDown: LongWord; cdecl; external;
function hidKeysHeld: LongWord; cdecl; external;
function hidKeysUp: LongWord; cdecl; external;
procedure hidTouchRead(var pos: TTouchPosition); cdecl; external;
procedure hidCircleRead(var pos: TCirclePosition); cdecl; external;
procedure hidCstickRead(var pos: TCirclePosition); cdecl; external;
procedure gspWaitForEvent(id: LongInt; nextEvent: Boolean); cdecl; external;
procedure osSetSpeedupEnable(enable: Boolean); cdecl; external;
function osGetTime: QWord; cdecl; external;
function APT_SetAppCpuTimeLimit(percent: LongWord): Result3DS; cdecl; external;
function romfsMountSelf(name: PAnsiChar): Result3DS; cdecl; external;
function romfsUnmount(name: PAnsiChar): Result3DS; cdecl; external;
function svcOutputDebugString(str: PAnsiChar; length: LongInt): Result3DS; cdecl; external;

procedure CtrStartup;
procedure CtrShutdown;
// Logs heap and system memory use (see fpcctr_meminfo).
procedure CtrLogMemory(const Context: AnsiString);
// Appends a line to sdmc:/3ds/SpaceRangersHD/ctr.log and the debug output.
procedure CtrLog(const Msg: AnsiString);
function CtrRomfsMounted: Boolean;

implementation

uses
  SysUtils;

// Native libraries of the port (see tools/buildnative.sh) and portlibs.
{$LINKLIB SDL2}
{$LINKLIB citro3d}
{$LINKLIB okgf}
{$LINKLIB vorbisfile}
{$LINKLIB ogg}
{$LINKLIB png16}
{$LINKLIB jpeg}
{$LINKLIB z}

var
  RomfsOk: Boolean;
  LogLock: TRTLCriticalSection;
  LogReady: Boolean;
  MonitorStop: Boolean;
  MonitorThread: TThreadID;
  StartTicks: QWord;

procedure fpcctr_meminfo(Info: PLongWord); cdecl; external;
function fpcctr_file_stats(Stats: PLongInt): PAnsiChar; cdecl; external;

procedure CtrLogFiles(const Context: AnsiString);
var
  Stats: array[0..9] of LongInt;
  LastPath: PAnsiChar;
begin
  LastPath := fpcctr_file_stats(@Stats[0]);
  CtrLog(Format('%s files: opened %d, open %d (peak %d), real %d (peak %d), evictions %d, reopen failures %d, ' +
    'open failures %d, read-only fallbacks %d',
    [Context, Stats[0], Stats[1], Stats[2], Stats[3], Stats[4], Stats[5], Stats[6], Stats[7], Stats[8]]));
  if Stats[7] > 0 then
    CtrLog(Format('%s last failed open: errno %d, %s', [Context, Stats[9], AnsiString(LastPath)]));
end;

procedure CtrLogMemory(const Context: AnsiString);
var
  Info: array[0..6] of LongWord;
begin
  fpcctr_meminfo(@Info[0]);
  CtrLog(Format('%s: heap %d KiB used, arena %d KiB, capacity %d KiB; linear %d KiB, VRAM %d KiB; app region %d KiB; stack %d KiB',
    [Context, Info[1] div 1024, Info[0] div 1024, Info[2] div 1024, Info[3] div 1024,
     Info[4] div 1024, Info[5] div 1024, Info[6] div 1024]));
end;

// Logs memory every two seconds, so that a run that ends abruptly still
// shows how far it got and how memory evolved.
function MemoryMonitor(Parameter: Pointer): PtrInt;
var
  Counter: Integer;
begin
  Counter := 0;
  while not MonitorStop do
  begin
    Sleep(100);
    Inc(Counter);
    if Counter mod 20 = 0 then
    begin
      CtrLogMemory(Format('t=%ds', [(GetTickCount64 - StartTicks) div 1000]));
      CtrLogFiles(Format('t=%ds', [(GetTickCount64 - StartTicks) div 1000]));
    end;
  end;
  Result := 0;
end;

procedure RedirectStdIO;
begin
  // Runtime error reports and the game's own fatal-error output go here.
  {$I-}
  AssignFile(Output, CtrDataDir + '/stdout.log');
  Rewrite(Output);
  AssignFile(StdErr, CtrDataDir + '/stderr.log');
  Rewrite(StdErr);
  ErrOutput := StdErr;
  {$I+}
  if IOResult <> 0 then
    ;
end;

procedure CtrLog(const Msg: AnsiString);
var
  f: TextFile;
  line: AnsiString;
begin
  line := FormatDateTime('hh:nn:ss.zzz', Now) + ' ' + Msg;
  svcOutputDebugString(PAnsiChar(line), Length(line));
  if not LogReady then
    Exit;
  EnterCriticalSection(LogLock);
  try
    AssignFile(f, CtrDataDir + '/ctr.log');
    {$I-}
    Append(f);
    if IOResult <> 0 then
      Rewrite(f);
    if IOResult = 0 then
    begin
      WriteLn(f, line);
      CloseFile(f);
    end;
    {$I+}
  finally
    LeaveCriticalSection(LogLock);
  end;
end;

function CtrRomfsMounted: Boolean;
begin
  Result := RomfsOk;
end;

procedure CtrStartup;
var
  f: TextFile;
begin
  // 804 MHz and the L2 cache on New 3DS; no effect on old models.
  osSetSpeedupEnable(True);
  // Allow a worker thread on the system core.
  APT_SetAppCpuTimeLimit(30);
  RomfsOk := romfsMountSelf('romfs') >= 0;
  ForceDirectories(CtrDataDir);
  InitCriticalSection(LogLock);
  AssignFile(f, CtrDataDir + '/ctr.log');
  {$I-}
  Rewrite(f);
  if IOResult = 0 then
    CloseFile(f);
  {$I+}
  LogReady := True;
  // On the New 3DS run worker threads on the extra core.
  if CtrIsNew3DS then
    CtrSetDefaultThreadCore(2);
  ChDir(CtrDataDir);
  RedirectStdIO;
  StartTicks := GetTickCount64;
  CtrLog('Space Rangers HD for 3DS starting; New3DS=' + BoolToStr(CtrIsNew3DS, True) +
    ' romfs=' + BoolToStr(RomfsOk, True) + ' cpus=' + IntToStr(CPUCount));
  CtrLogMemory('start');
  MonitorThread := BeginThread(@MemoryMonitor, nil);
end;

procedure CtrShutdown;
begin
  MonitorStop := True;
  CtrLogMemory('exit');
  CtrLogFiles('exit');
  CtrLog(Format('shutdown: ExitCode=%d ErrorAddr=%p', [ExitCode, ErrorAddr]));
  {$I-}
  Flush(Output);
  Flush(StdErr);
  {$I+}
  if RomfsOk then
    romfsUnmount('romfs');
end;

end.

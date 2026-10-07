{
    This file is part of the Free Pascal run time library.

    System unit for the Nintendo 3DS (CTR).

    The 3DS port runs as a homebrew application on top of devkitARM's
    newlib and libctru. libctru's crt0 calls main(), which is the Pascal
    main program itself.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}
unit System;

interface

{$define FPC_IS_SYSTEM}
{$define HAS_CMDLINE}
{$define DISABLE_NO_THREAD_MANAGER}

{ newlib/libctru supply the C library; FPC objects must not use the
  Linux syscall based helpers }
{$define FPC_USE_LIBC}

{$define FPC_LOAD_SOFTFPU}

{$i systemh.inc}

{$ifdef FPC_LOAD_SOFTFPU}
{$define fpc_softfpu_interface}
{$i softfpu.pp}
{$undef fpc_softfpu_interface}
{$endif FPC_LOAD_SOFTFPU}

const
  LineEnding = #10;
  LFNSupport = true;
  DirectorySeparator = '/';
  DriveSeparator = ':';
  ExtensionSeparator = '.';
  PathSeparator = ';';
  AllowDirectorySeparators : set of AnsiChar = ['\','/'];
  AllowDriveSeparators : set of AnsiChar = [':'];
  maxExitCode = 255;
  MaxPathLen = 1024;
  AllFilesMask = '*';

  UnusedHandle    = -1;
  StdInputHandle  = 0;
  StdOutputHandle = 1;
  StdErrorHandle  = 2;

  FileNameCaseSensitive : boolean = false;
  FileNameCasePreserving: boolean = true;
  CtrlZMarksEOF: boolean = false; (* #26 not considered as end of file *)

  sLineBreak = LineEnding;
  DefaultTextLineBreakStyle : TTextLineBreakStyle = tlbsLF;

var
  argc: LongInt = 0;
  argv: PPAnsiChar = nil;
  envp: PPAnsiChar = nil;

  { set up by libctru's crt0 before main() is called }
  ctr_system_argc: LongInt; external name '__system_argc';
  ctr_system_argv: PPAnsiChar; external name '__system_argv';

function get_cmdline:PAnsiChar;
property cmdline:PAnsiChar read get_cmdline;

{ Core used for threads created by BeginThread/TThread: -2 = default
  application core, 2 = the extra New 3DS core. }
procedure CtrSetDefaultThreadCore(core: LongInt);
function CtrIsNew3DS: Boolean;

implementation

const
  calculated_cmdline: PAnsiChar = nil;
  ARG_MAX = 65536;

{$ifdef FPC_LOAD_SOFTFPU}
{$define fpc_softfpu_implementation}
{$i softfpu.pp}
{$undef fpc_softfpu_implementation}

{ we get these functions and types from the softfpu code }
{$define FPC_SYSTEM_HAS_float64}
{$define FPC_SYSTEM_HAS_float32}
{$define FPC_SYSTEM_HAS_flag}
{$define FPC_SYSTEM_HAS_extractFloat64Frac0}
{$define FPC_SYSTEM_HAS_extractFloat64Frac1}
{$define FPC_SYSTEM_HAS_extractFloat64Exp}
{$define FPC_SYSTEM_HAS_extractFloat64Sign}
{$define FPC_SYSTEM_HAS_ExtractFloat32Frac}
{$define FPC_SYSTEM_HAS_extractFloat32Exp}
{$define FPC_SYSTEM_HAS_extractFloat32Sign}
{$endif FPC_LOAD_SOFTFPU}

{$define HAS_GETCPUCOUNT}

{ C types used by the libc based generic routines (cgeneric.inc) }
type
  cint = longint;
  cuint = longword;
  clong = longint;
  culong = longword;
  size_t = ptruint;
  ssize_t = ptrint;
const
  clib = 'c';

{$i system.inc}

procedure ctr_set_default_core(core: LongInt); cdecl; external name 'fpcctr_set_default_core';
function ctr_is_new3ds: LongInt; cdecl; external name 'fpcctr_is_new3ds';

procedure CtrSetDefaultThreadCore(core: LongInt);
begin
  ctr_set_default_core(core);
end;

function CtrIsNew3DS: Boolean;
begin
  CtrIsNew3DS:=ctr_is_new3ds<>0;
end;

{*****************************************************************************
                       Misc. System Dependent Functions
*****************************************************************************}

procedure System_exit;
begin
  ctr_exit(ExitCode);
end;

function GetProcessID: SizeUInt;
begin
  GetProcessID:=1;
end;

Function ParamCount: Longint;
Begin
  Paramcount:=argc-1;
End;

function paramstr(l: longint) : shortstring;
begin
  if (l>=0) and (l<argc) and (argv<>nil) and (argv[l]<>nil) then
    paramstr:=strpas(argv[l])
  else
    paramstr:='';
end;

Procedure Randomize;
Begin
  randseed:=longint(ctr_ticks_raw xor QWord(ctr_time));
End;

{*****************************************************************************
                                    cmdline
*****************************************************************************}

procedure SetupCmdLine;
var
  i, len, size: longint;
  p: PAnsiChar;
begin
  size:=1;
  for i:=0 to argc-1 do
    inc(size,strlen(argv[i])+3);
  p:=SysGetMem(size);
  p^:=#0;
  len:=0;
  for i:=0 to argc-1 do
    begin
      if i>0 then
        begin
          p[len]:=' ';
          inc(len);
        end;
      move(argv[i]^,p[len],strlen(argv[i]));
      inc(len,strlen(argv[i]));
    end;
  p[len]:=#0;
  calculated_cmdline:=p;
end;

function get_cmdline:PAnsiChar;
begin
  if calculated_cmdline=nil then
    setupcmdline;
  get_cmdline:=calculated_cmdline;
end;

{*****************************************************************************
                         SystemUnit Initialization
*****************************************************************************}

procedure SysInitStdIO;
begin
  OpenStdIO(Input,fmInput,StdInputHandle);
  OpenStdIO(Output,fmOutput,StdOutputHandle);
  OpenStdIO(ErrOutput,fmOutput,StdErrorHandle);
  OpenStdIO(StdOut,fmOutput,StdOutputHandle);
  OpenStdIO(StdErr,fmOutput,StdErrorHandle);
end;

function CheckInitialStkLen(stklen : SizeUInt) : SizeUInt;
begin
  CheckInitialStkLen:=stklen;
end;

begin
  argc:=ctr_system_argc;
  argv:=ctr_system_argv;
  IsConsole:=TRUE;
  StackLength:=CheckInitialStkLen(InitialStkLen);
  StackBottom:=Sptr-StackLength;
  fpc_cpucodeinit;
  { Setup heap }
  InitHeap;
  SysInitExceptions;
  initunicodestringmanager;
  { Setup stdin, stdout and stderr }
  SysInitStdIO;
  { Reset IO Error }
  InOutRes:=0;
  { threading }
  InitSystemThreads;
end.

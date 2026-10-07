{ Self-test of the Free Pascal RTL on the Nintendo 3DS.
  Writes results to the top screen console and to sdmc:/rtltest.log;
  the last line is "RESULT: PASS" or "RESULT: FAIL (<n>)". }
program rtltest;

{$mode objfpc}{$H+}

uses
  ctrwstring, SysUtils, Classes, SyncObjs, Math, DateUtils, StrUtils, Contnrs;

type
  TGfxScreen = LongInt;

procedure gfxInitDefault; cdecl; external;
procedure gfxExit; cdecl; external;
function consoleInit(screen: TGfxScreen; console: Pointer): Pointer; cdecl; external;
function aptMainLoop: Boolean; cdecl; external;
procedure hidScanInput; cdecl; external;
function hidKeysDown: LongWord; cdecl; external;
procedure gspWaitForEvent(id: LongInt; nextEvent: Boolean); cdecl; external;

const
  GFX_TOP = 0;
  GSPGPU_EVENT_VBlank0 = 2;
  KEY_START = 1 shl 3;

var
  Log: TextFile;
  Failures: Integer = 0;
  Tests: Integer = 0;

procedure Say(const S: string);
begin
  WriteLn(S);
  WriteLn(Log, S);
  Flush(Log);
end;

procedure Check(Cond: Boolean; const Name: string);
begin
  Inc(Tests);
  if Cond then
    Say('ok   ' + Name)
  else
    begin
      Inc(Failures);
      Say('FAIL ' + Name);
    end;
end;

threadvar
  TV: Integer;

type
  TWorker = class(TThread)
  public
    Sum: Int64;
    Base: Integer;
    TVOk: Boolean;
    procedure Execute; override;
  end;

var
  SharedCounter: Integer = 0;
  Lock: TCriticalSection;

procedure TWorker.Execute;
var
  i: Integer;
begin
  TV := Base;
  for i := 1 to 100000 do
    Inc(Sum, i mod 7);
  for i := 1 to 1000 do
    begin
      Lock.Acquire;
      Inc(SharedCounter);
      Lock.Release;
    end;
  Sleep(5);
  TVOk := TV = Base;
end;

procedure TestThreads;
var
  w: array[0..3] of TWorker;
  i: Integer;
  ok: Boolean;
  ev: TEvent;
  t0: QWord;
begin
  Lock := TCriticalSection.Create;
  TV := 12345;
  for i := 0 to High(w) do
    begin
      w[i] := TWorker.Create(True);
      w[i].Base := i + 1;
    end;
  for i := 0 to High(w) do
    w[i].Start;
  ok := True;
  for i := 0 to High(w) do
    begin
      w[i].WaitFor;
      if (w[i].Sum <> 300000) or not w[i].TVOk then
        Say(Format('  thread %d: sum=%d tv=%s', [i, w[i].Sum, BoolToStr(w[i].TVOk, True)]));
      ok := ok and (w[i].Sum = 300000) and w[i].TVOk;
      w[i].Free;
    end;
  Check(ok, 'TThread run/WaitFor, threadvars per thread');
  Check(SharedCounter = 4000, 'TCriticalSection counter = ' + IntToStr(SharedCounter));
  Check(TV = 12345, 'main thread threadvar kept');
  Lock.Free;

  ev := TEvent.Create(nil, True, False, '');
  t0 := GetTickCount64;
  Check(ev.WaitFor(50) = wrTimeout, 'TEvent timeout');
  Check(GetTickCount64 - t0 >= 40, 'TEvent waited ~50ms (' + IntToStr(GetTickCount64 - t0) + 'ms)');
  ev.SetEvent;
  Check(ev.WaitFor(1000) = wrSignaled, 'TEvent signalled');
  ev.Free;
  Check(CPUCount >= 2, 'CPUCount = ' + IntToStr(CPUCount));
end;

procedure TestExceptions;
var
  caught: Boolean;
  p: PInteger;
begin
  caught := False;
  try
    raise Exception.Create('boom');
  except
    on E: Exception do caught := E.Message = 'boom';
  end;
  Check(caught, 'raise/except');
  caught := False;
  try
    try
      StrToInt('x');
    finally
      caught := True;
    end;
  except
    on E: EConvertError do caught := caught and True;
  end;
  Check(caught, 'try/finally + EConvertError');
  caught := False;
  try
    p := nil;
    if Random(2) < 5 then
      raise EAccessViolation.Create('simulated');
    p^ := 1;
  except
    on E: EAccessViolation do caught := True;
  end;
  Check(caught, 'EAccessViolation class');
end;

procedure TestStrings;
var
  ws: WideString;
  a: AnsiString;
  r: RawByteString;
  sl: TStringList;
begin
  ws := #$041A#$043E#$0441#$043C#$0438#$0447#$0435#$0441#$043A#$0438#$0435; { "Космические" }
  r := '';
  SetLength(r, 0);
  a := AnsiString(ws);
  Check(Length(ws) = 11, 'WideString length');
  r := RawByteString(UTF8Encode(ws));
  Check(Length(r) = 22, 'UTF8Encode length = ' + IntToStr(Length(r)));
  Check(UTF8Decode(r) = ws, 'UTF8 round trip');
  SetLength(r, 0);
  r := RawByteString(UTF8Encode(ws));
  SetCodePage(r, CP_UTF8, False);
  SetCodePage(r, 1251, True);
  Check((Length(r) = 11) and (Byte(r[1]) = $CA), 'cp1251 conversion (first byte $' + IntToHex(Byte(r[1]), 2) + ')');
  Check(WideString(r) = ws, 'cp1251 round trip');
  Check(WideUpperCase(WideString('abc') + #$0451#$0436) = WideString('ABC') + #$0401#$0416, 'WideUpperCase cyrillic');
  Check(WideCompareText(#$0430#$0431, #$0410#$0411) = 0, 'WideCompareText cyrillic');
  Check(DefaultSystemCodePage = CP_UTF8, 'DefaultSystemCodePage = ' + IntToStr(DefaultSystemCodePage));
  Check(Format('%d-%s-%.2f', [42, 'x', 3.14159]) = '42-x-3.14', 'Format');
  Check(FloatToStr(0.5) = '0.5', 'FloatToStr');
  Check(StrToFloat('2.25') = 2.25, 'StrToFloat');
  Check(ReverseString('abc') = 'cba', 'StrUtils');
  Say('  TStringList');
  sl := TStringList.Create;
  sl.CommaText := 'b,a,c';
  Say('  sort');
  sl.Sort;
  Check(sl.CommaText = 'a,b,c', 'TStringList sort');
  sl.Free;
end;

procedure TestMath;
var
  d: Double;
  e: Extended;
  i64: Int64;
begin
  d := Sqrt(2.0);
  Check(Abs(d * d - 2.0) < 1e-12, 'Sqrt');
  Check(Abs(Sin(Pi / 6) - 0.5) < 1e-12, 'Sin');
  Check(Round(2.5) = 2, 'banker''s Round');
  Check(Trunc(-2.7) = -2, 'Trunc');
  d := 3.0;
  e := 1.0 / d; { a constant 1.0/3.0 is folded to Single precision by FPC }
  Check(SizeOf(Extended) = 8, 'Extended is Double on ARM (size ' + IntToStr(SizeOf(Extended)) + ')');
  d := e * 3 - 1;
  Check(Abs(d) < 1e-15, 'Extended arithmetic (' + FloatToStr(d) + ', e=' + FloatToStr(e) + ')');
  i64 := High(Int64) div 3;
  Check(i64 * 3 + 1 = High(Int64), 'Int64 div/mul');
  Check(Power(2, 10) = 1024, 'Power');
  Check(Max(3, 7) = 7, 'Max');
  Check(DaysBetween(EncodeDate(2026, 1, 1), EncodeDate(2026, 3, 1)) = 59, 'DateUtils');
end;

type
  TV3 = record X, Y, Z: Double; end;

function abitest_hfa(a, b, c: TV3; r: Double): Double; cdecl; external;
function abitest_many(a, b, c, d, e, f, g, h, i: Double; k: LongInt; j: Double): Double; cdecl; external;
function abitest_mixed(a: LongInt; b: Single; c: Int64; d: Double; e: Single; f: LongInt): Double; cdecl; external;
function abitest_ret_hfa(x: Double): TV3; cdecl; external;

{$L abitest.o}

procedure TestABI;
var
  a, b, c, r: TV3;
begin
  a.X := 1; a.Y := 2; a.Z := 3;
  b.X := 4; b.Y := 5; b.Z := 6;
  c.X := 7; c.Y := 8; c.Z := 9;
  Check(abitest_hfa(a, b, c, 0.5) = 1 + 5 * 10 + 9 * 100 + 0.5 * 1000, 'AAPCS-VFP: 3 HFA + stacked double');
  Check(abitest_many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) = 120936, 'AAPCS-VFP: 10 doubles + int');
  Check(abitest_mixed(1, 2.5, 3, 4.25, 5.5, 6) = 1 + 2.5 * 10 + 3 * 100 + 4.25 * 1000 + 5.5 * 10000 + 6 * 100000, 'AAPCS-VFP: mixed int/float');
  r := abitest_ret_hfa(2);
  Check((r.X = 2) and (r.Y = 4) and (r.Z = 6), 'AAPCS-VFP: HFA return');
end;

procedure TestFiles;
const
  Dir = 'sdmc:/rtltest.tmp';
var
  fs: TFileStream;
  s: string;
  sr: TSearchRec;
  n: Integer;
  ms: TMemoryStream;
begin
  ForceDirectories(Dir + '/sub');
  Check(DirectoryExists(Dir + '/sub'), 'ForceDirectories/DirectoryExists');
  fs := TFileStream.Create(Dir + '/Alpha.TXT', fmCreate);
  s := 'hello file';
  fs.WriteBuffer(s[1], Length(s));
  fs.Free;
  fs := TFileStream.Create(Dir + '/beta.txt', fmCreate);
  fs.Free;
  Check(FileExists(Dir + '/Alpha.TXT'), 'FileExists');
  ms := TMemoryStream.Create;
  try
    ms.LoadFromFile(Dir + '/Alpha.TXT');
    Check(ms.Size = 10, 'TMemoryStream.LoadFromFile (size ' + IntToStr(ms.Size) + ')');
    { FAT on the console ignores case; emulators may not }
    if FileExists(Dir + '/alpha.txt') then
      Say('info sdmc: is case-insensitive')
    else
      Say('info sdmc: is case-sensitive (emulator host file system)');
  except
    on E: Exception do Check(False, 'LoadFromFile: ' + E.Message);
  end;
  ms.Free;
  n := 0;
  if FindFirst(Dir + '/*.txt', faAnyFile, sr) = 0 then
    begin
      repeat
        Inc(n);
      until FindNext(sr) <> 0;
      FindClose(sr);
    end;
  Check(n = 2, 'FindFirst *.txt found ' + IntToStr(n));
  n := 0;
  if FindFirst(Dir + '/*', faDirectory, sr) = 0 then
    begin
      repeat
        if (sr.Attr and faDirectory) <> 0 then Inc(n);
      until FindNext(sr) <> 0;
      FindClose(sr);
    end;
  Check(n >= 1, 'FindFirst directories');
  Check(FileAge(Dir + '/Alpha.TXT') <> -1, 'FileAge');
  Check(RenameFile(Dir + '/beta.txt', Dir + '/gamma.txt'), 'RenameFile');
  Check(DeleteFile(Dir + '/gamma.txt') and DeleteFile(Dir + '/Alpha.TXT'), 'DeleteFile');
  Check(RemoveDir(Dir + '/sub') and RemoveDir(Dir), 'RemoveDir');
end;

procedure TestHeap;
var
  lst: TObjectList;
  i: Integer;
  p: Pointer;
begin
  lst := TObjectList.Create(True);
  for i := 1 to 20000 do
    lst.Add(TStringList.Create);
  lst.Free;
  GetMem(p, 16 * 1024 * 1024);
  FillChar(p^, 16 * 1024 * 1024, 1);
  FreeMem(p);
  Check(True, 'heap: 20000 objects + 16 MiB block');
end;

var
  t0: QWord;
begin
  gfxInitDefault;
  consoleInit(GFX_TOP, nil);
  AssignFile(Log, 'sdmc:/rtltest.log');
  Rewrite(Log);
  Say('FPC ' + {$I %FPCVERSION%} + ' RTL self-test on 3DS');
  Say('Time: ' + DateTimeToStr(Now));
  t0 := GetTickCount64;
  TestExceptions;
  TestStrings;
  TestMath;
  TestABI;
  TestFiles;
  TestHeap;
  TestThreads;
  Say(Format('%d tests in %d ms', [Tests, GetTickCount64 - t0]));
  if Failures = 0 then
    Say('RESULT: PASS')
  else
    Say('RESULT: FAIL (' + IntToStr(Failures) + ')');
  CloseFile(Log);
  WriteLn('Press START to exit');
  while aptMainLoop do
    begin
      hidScanInput;
      if (hidKeysDown and KEY_START) <> 0 then
        Break;
      gspWaitForEvent(GSPGPU_EVENT_VBlank0, True);
    end;
  gfxExit;
end.

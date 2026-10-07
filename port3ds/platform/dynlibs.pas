unit dynlibs;
{$MODE objfpc}{$H+}
// The 3DS has no dynamic libraries. Every load fails, so optional
// components (Xvid video, the planetary battle engine, Steam) report
// themselves as unavailable and the game continues without them.
interface

type
  TLibHandle = PtrInt;

const
  NilHandle = TLibHandle(0);
  SharedSuffix = 'so';

function LoadLibrary(const Name: RawByteString): TLibHandle;
function LoadLibrary(const Name: UnicodeString): TLibHandle;
function SafeLoadLibrary(const Name: RawByteString): TLibHandle;
function GetProcedureAddress(Lib: TLibHandle; const ProcName: AnsiString): Pointer;
function GetProcAddress(Lib: TLibHandle; const ProcName: AnsiString): Pointer;
function UnloadLibrary(Lib: TLibHandle): Boolean;
function FreeLibrary(Lib: TLibHandle): Boolean;
function GetLoadErrorStr: AnsiString;

implementation

function LoadLibrary(const Name: RawByteString): TLibHandle;
begin
  Result := NilHandle;
end;

function LoadLibrary(const Name: UnicodeString): TLibHandle;
begin
  Result := NilHandle;
end;

function SafeLoadLibrary(const Name: RawByteString): TLibHandle;
begin
  Result := NilHandle;
end;

function GetProcedureAddress(Lib: TLibHandle; const ProcName: AnsiString): Pointer;
begin
  Result := nil;
end;

function GetProcAddress(Lib: TLibHandle; const ProcName: AnsiString): Pointer;
begin
  Result := nil;
end;

function UnloadLibrary(Lib: TLibHandle): Boolean;
begin
  Result := Lib = NilHandle;
end;

function FreeLibrary(Lib: TLibHandle): Boolean;
begin
  Result := UnloadLibrary(Lib);
end;

function GetLoadErrorStr: AnsiString;
begin
  Result := 'dynamic libraries are not available on the Nintendo 3DS';
end;

end.

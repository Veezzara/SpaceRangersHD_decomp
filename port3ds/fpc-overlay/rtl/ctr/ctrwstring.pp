{
    This file is part of the Free Pascal run time library.

    Unicode string manager for the Nintendo 3DS (CTR).

    Self-contained: converts between UTF-16 and UTF-8, Windows-1251,
    Windows-1252, ISO-8859-1, CP866 and ASCII, and implements case
    mapping and comparison for Latin, Latin-1, Greek and Cyrillic.
    Comparisons are ordinal (after case folding for case-insensitive
    ones). Use it as the first unit of a program, like cwstring.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}
unit ctrwstring;

{$mode objfpc}
{$inline on}

interface

procedure SetCtrWideStringManager;

implementation

const
  { upper half (0x80..0xFF) of the single byte code pages }
  CP1251_High: array[$80..$FF] of Word = (
    $0402,$0403,$201A,$0453,$201E,$2026,$2020,$2021,$20AC,$2030,$0409,$2039,$040A,$040C,$040B,$040F,
    $0452,$2018,$2019,$201C,$201D,$2022,$2013,$2014,$FFFD,$2122,$0459,$203A,$045A,$045C,$045B,$045F,
    $00A0,$040E,$045E,$0408,$00A4,$0490,$00A6,$00A7,$0401,$00A9,$0404,$00AB,$00AC,$00AD,$00AE,$0407,
    $00B0,$00B1,$0406,$0456,$0491,$00B5,$00B6,$00B7,$0451,$2116,$0454,$00BB,$0458,$0405,$0455,$0457,
    $0410,$0411,$0412,$0413,$0414,$0415,$0416,$0417,$0418,$0419,$041A,$041B,$041C,$041D,$041E,$041F,
    $0420,$0421,$0422,$0423,$0424,$0425,$0426,$0427,$0428,$0429,$042A,$042B,$042C,$042D,$042E,$042F,
    $0430,$0431,$0432,$0433,$0434,$0435,$0436,$0437,$0438,$0439,$043A,$043B,$043C,$043D,$043E,$043F,
    $0440,$0441,$0442,$0443,$0444,$0445,$0446,$0447,$0448,$0449,$044A,$044B,$044C,$044D,$044E,$044F);

  CP1252_High: array[$80..$9F] of Word = (
    $20AC,$FFFD,$201A,$0192,$201E,$2026,$2020,$2021,$02C6,$2030,$0160,$2039,$0152,$FFFD,$017D,$FFFD,
    $FFFD,$2018,$2019,$201C,$201D,$2022,$2013,$2014,$02DC,$2122,$0161,$203A,$0153,$FFFD,$017E,$0178);

  CP866_High: array[$80..$FF] of Word = (
    $0410,$0411,$0412,$0413,$0414,$0415,$0416,$0417,$0418,$0419,$041A,$041B,$041C,$041D,$041E,$041F,
    $0420,$0421,$0422,$0423,$0424,$0425,$0426,$0427,$0428,$0429,$042A,$042B,$042C,$042D,$042E,$042F,
    $0430,$0431,$0432,$0433,$0434,$0435,$0436,$0437,$0438,$0439,$043A,$043B,$043C,$043D,$043E,$043F,
    $2591,$2592,$2593,$2502,$2524,$2561,$2562,$2556,$2555,$2563,$2551,$2557,$255D,$255C,$255B,$2510,
    $2514,$2534,$252C,$251C,$2500,$253C,$255E,$255F,$255A,$2554,$2569,$2566,$2560,$2550,$256C,$2567,
    $2568,$2564,$2565,$2559,$2558,$2552,$2553,$256B,$256A,$2518,$250C,$2588,$2584,$258C,$2590,$2580,
    $0440,$0441,$0442,$0443,$0444,$0445,$0446,$0447,$0448,$0449,$044A,$044B,$044C,$044D,$044E,$044F,
    $0401,$0451,$0404,$0454,$0407,$0457,$040E,$045E,$00B0,$2219,$00B7,$221A,$2116,$00A4,$25A0,$00A0);

type
  TByteToWide = array[0..255] of Word;
  PByteToWide = ^TByteToWide;

var
  Table1251, Table1252, Table28591, Table866, TableASCII: TByteToWide;

procedure BuildTables;
var
  i: Integer;
begin
  for i:=0 to 127 do
    begin
      Table1251[i]:=i; Table1252[i]:=i; Table28591[i]:=i; Table866[i]:=i; TableASCII[i]:=i;
    end;
  for i:=128 to 255 do
    begin
      Table1251[i]:=CP1251_High[i];
      if i<$A0 then
        Table1252[i]:=CP1252_High[i]
      else
        Table1252[i]:=i;
      Table28591[i]:=i;
      Table866[i]:=CP866_High[i];
      TableASCII[i]:=$FFFD;
    end;
end;

function TableFor(cp: TSystemCodePage): PByteToWide;
begin
  case cp of
    1251: Result:=@Table1251;
    1252: Result:=@Table1252;
    28591: Result:=@Table28591;
    866: Result:=@Table866;
    20127: Result:=@TableASCII;
  else
    Result:=nil;
  end;
end;

function IsUTF8(cp: TSystemCodePage): Boolean; inline;
begin
  Result:=(cp=CP_UTF8) or (cp=CP_ACP) or (cp=CP_OEMCP) or (cp=CP_NONE);
end;

function ResolveCP(cp: TSystemCodePage): TSystemCodePage;
begin
  if (cp=CP_ACP) or (cp=CP_OEMCP) then
    Result:=DefaultSystemCodePage
  else
    Result:=cp;
end;

{ reverse lookup: UTF-16 code unit -> single byte, '?' when unmappable }
function WideToByte(t: PByteToWide; c: Word): AnsiChar;
var
  i: Integer;
begin
  if c<$80 then
    exit(AnsiChar(c));
  for i:=128 to 255 do
    if t^[i]=c then
      exit(AnsiChar(i));
  Result:='?';
end;

procedure Unicode2AnsiMove(source: PUnicodeChar; var dest: RawByteString; cp: TSystemCodePage; len: SizeInt);
var
  t: PByteToWide;
  i: SizeInt;
  p: PAnsiChar;
begin
  cp:=ResolveCP(cp);
  t:=TableFor(cp);
  if t=nil then
    begin
      { UTF-8 and anything unknown }
      dest:='';
      SetLength(dest,len*3);
      if len>0 then
        SetLength(dest,UnicodeToUtf8(PAnsiChar(dest),Length(dest)+1,source,len)-1)
      else
        SetLength(dest,0);
      SetCodePage(dest,CP_UTF8,False);
      exit;
    end;
  SetLength(dest,len);
  p:=PAnsiChar(dest);
  for i:=0 to len-1 do
    p[i]:=WideToByte(t,Word(source[i]));
  SetCodePage(dest,cp,False);
end;

procedure Ansi2UnicodeMove(source: PAnsiChar; cp: TSystemCodePage; var dest: UnicodeString; len: SizeInt);
var
  t: PByteToWide;
  i: SizeInt;
  p: PUnicodeChar;
begin
  cp:=ResolveCP(cp);
  t:=TableFor(cp);
  if t=nil then
    begin
      SetLength(dest,len);
      if len>0 then
        SetLength(dest,Utf8ToUnicode(PUnicodeChar(dest),len+1,source,len)-1);
      exit;
    end;
  SetLength(dest,len);
  p:=PUnicodeChar(dest);
  for i:=0 to len-1 do
    p[i]:=UnicodeChar(t^[Byte(source[i])]);
end;

function UpperChar(c: Word): Word; inline;
begin
  case c of
    $61..$7A: Result:=c-32;
    $E0..$F6, $F8..$FE: Result:=c-32;
    $FF: Result:=$178;
    $0100..$017F:
      if (c>=$0139) and (c<=$0148) or (c>=$0179) and (c<=$017E) then
        begin
          if (c and 1)=0 then Result:=c-1 else Result:=c;
        end
      else if (c and 1)=1 then Result:=c-1 else Result:=c;
    $03B1..$03C1, $03C3..$03CB: Result:=c-32;
    $0430..$044F: Result:=c-32;
    $0450..$045F: Result:=c-80;
    $0460..$0481, $048A..$04BF: if (c and 1)=1 then Result:=c-1 else Result:=c;
  else
    Result:=c;
  end;
end;

function LowerChar(c: Word): Word; inline;
begin
  case c of
    $41..$5A: Result:=c+32;
    $C0..$D6, $D8..$DE: Result:=c+32;
    $0100..$017F:
      if c=$0178 then Result:=$FF
      else if (c>=$0139) and (c<=$0148) or (c>=$0179) and (c<=$017E) then
        begin
          if (c and 1)=1 then Result:=c+1 else Result:=c;
        end
      else if (c and 1)=0 then Result:=c+1 else Result:=c;
    $0391..$03A1, $03A3..$03AB: Result:=c+32;
    $0410..$042F: Result:=c+32;
    $0400..$040F: Result:=c+80;
    $0460..$0481, $048A..$04BF: if (c and 1)=0 then Result:=c+1 else Result:=c;
  else
    Result:=c;
  end;
end;

function UpperUnicodeString(const S: UnicodeString): UnicodeString;
var
  i: SizeInt;
begin
  Result:=S;
  UniqueString(Result);
  for i:=1 to Length(Result) do
    Result[i]:=UnicodeChar(UpperChar(Word(Result[i])));
end;

function LowerUnicodeString(const S: UnicodeString): UnicodeString;
var
  i: SizeInt;
begin
  Result:=S;
  UniqueString(Result);
  for i:=1 to Length(Result) do
    Result[i]:=UnicodeChar(LowerChar(Word(Result[i])));
end;

function CompareUnicode(p1, p2: PUnicodeChar; l1, l2: SizeInt; IgnoreCase: Boolean): PtrInt;
var
  i, n: SizeInt;
  a, b: Word;
begin
  n:=l1;
  if l2<n then
    n:=l2;
  for i:=0 to n-1 do
    begin
      a:=Word(p1[i]);
      b:=Word(p2[i]);
      if IgnoreCase then
        begin
          a:=UpperChar(a);
          b:=UpperChar(b);
        end;
      if a<>b then
        exit(PtrInt(a)-PtrInt(b));
    end;
  Result:=l1-l2;
end;

function CompareUnicodeString(const s1, s2: UnicodeString; Options: TCompareOptions): PtrInt;
begin
  Result:=CompareUnicode(PUnicodeChar(s1),PUnicodeChar(s2),Length(s1),Length(s2),coIgnoreCase in Options);
end;

function CharLengthPChar(const Str: PAnsiChar): PtrInt;
var
  p: PAnsiChar;
  l: PtrInt;
begin
  Result:=0;
  if Str=nil then
    exit;
  if not IsUTF8(DefaultSystemCodePage) and (TableFor(DefaultSystemCodePage)<>nil) then
    exit(strlen(Str));
  p:=Str;
  while p^<>#0 do
    begin
      l:=Utf8CodePointLen(p,4,false);
      if l<=0 then
        l:=1;
      inc(p,l);
      inc(Result);
    end;
end;

function CodePointLength(const Str: PAnsiChar; MaxLookAhead: PtrInt): PtrInt;
begin
  if Str^=#0 then
    exit(0);
  if not IsUTF8(DefaultSystemCodePage) and (TableFor(DefaultSystemCodePage)<>nil) then
    exit(1);
  Result:=Utf8CodePointLen(Str,MaxLookAhead,false);
end;

function UpperAnsiString(const s: AnsiString): AnsiString;
begin
  Result:=AnsiString(UpperUnicodeString(UnicodeString(s)));
end;

function LowerAnsiString(const s: AnsiString): AnsiString;
begin
  Result:=AnsiString(LowerUnicodeString(UnicodeString(s)));
end;

function CompareStrAnsiString(const S1, S2: AnsiString): PtrInt;
begin
  Result:=CompareUnicodeString(UnicodeString(S1),UnicodeString(S2),[]);
end;

function CompareTextAnsiString(const S1, S2: AnsiString): PtrInt;
begin
  Result:=CompareUnicodeString(UnicodeString(S1),UnicodeString(S2),[coIgnoreCase]);
end;

function StrCompAnsi(S1, S2: PAnsiChar): PtrInt;
begin
  Result:=CompareStrAnsiString(AnsiString(S1),AnsiString(S2));
end;

function StrICompAnsi(S1, S2: PAnsiChar): PtrInt;
begin
  Result:=CompareTextAnsiString(AnsiString(S1),AnsiString(S2));
end;

function StrLCompAnsi(S1, S2: PAnsiChar; MaxLen: PtrUInt): PtrInt;
var
  a, b: AnsiString;
begin
  SetString(a,S1,strlen(S1));
  SetString(b,S2,strlen(S2));
  if PtrUInt(Length(a))>MaxLen then SetLength(a,MaxLen);
  if PtrUInt(Length(b))>MaxLen then SetLength(b,MaxLen);
  Result:=CompareStrAnsiString(a,b);
end;

function StrLICompAnsi(S1, S2: PAnsiChar; MaxLen: PtrUInt): PtrInt;
var
  a, b: AnsiString;
begin
  SetString(a,S1,strlen(S1));
  SetString(b,S2,strlen(S2));
  if PtrUInt(Length(a))>MaxLen then SetLength(a,MaxLen);
  if PtrUInt(Length(b))>MaxLen then SetLength(b,MaxLen);
  Result:=CompareTextAnsiString(a,b);
end;

function StrLowerAnsi(Str: PAnsiChar): PAnsiChar;
var
  s: AnsiString;
begin
  s:=LowerAnsiString(AnsiString(Str));
  if strlen(Str)=PtrUInt(Length(s)) then
    Move(PAnsiChar(s)^,Str^,Length(s));
  Result:=Str;
end;

function StrUpperAnsi(Str: PAnsiChar): PAnsiChar;
var
  s: AnsiString;
begin
  s:=UpperAnsiString(AnsiString(Str));
  if strlen(Str)=PtrUInt(Length(s)) then
    Move(PAnsiChar(s)^,Str^,Length(s));
  Result:=Str;
end;

function GetStandardCodePage(const stdcp: TStandardCodePageEnum): TSystemCodePage;
begin
  Result:=CP_UTF8;
end;

procedure SetCtrWideStringManager;
var
  m: TUnicodeStringManager;
begin
  GetUnicodeStringManager(m);
  with m do
    begin
      Wide2AnsiMoveProc:=@Unicode2AnsiMove;
      Ansi2WideMoveProc:=@Ansi2UnicodeMove;
      UpperWideStringProc:=@UpperUnicodeString;
      LowerWideStringProc:=@LowerUnicodeString;
      CompareWideStringProc:=@CompareUnicodeString;
      CharLengthPCharProc:=@CharLengthPChar;
      CodePointLengthProc:=@CodePointLength;
      UpperAnsiStringProc:=@UpperAnsiString;
      LowerAnsiStringProc:=@LowerAnsiString;
      CompareStrAnsiStringProc:=@CompareStrAnsiString;
      CompareTextAnsiStringProc:=@CompareTextAnsiString;
      StrCompAnsiStringProc:=@StrCompAnsi;
      StrICompAnsiStringProc:=@StrICompAnsi;
      StrLCompAnsiStringProc:=@StrLCompAnsi;
      StrLICompAnsiStringProc:=@StrLICompAnsi;
      StrLowerAnsiStringProc:=@StrLowerAnsi;
      StrUpperAnsiStringProc:=@StrUpperAnsi;
      Unicode2AnsiMoveProc:=@Unicode2AnsiMove;
      Ansi2UnicodeMoveProc:=@Ansi2UnicodeMove;
      UpperUnicodeStringProc:=@UpperUnicodeString;
      LowerUnicodeStringProc:=@LowerUnicodeString;
      CompareUnicodeStringProc:=@CompareUnicodeString;
      GetStandardCodePageProc:=@GetStandardCodePage;
    end;
  SetUnicodeStringManager(m);
end;

initialization
  BuildTables;
  SetCtrWideStringManager;
end.

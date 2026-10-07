{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{*****************************************************************************
                           Directory Handling
*****************************************************************************}

Procedure Do_MkDir(s: rawbytestring);
begin
  if ctr_mkdir(PAnsiChar(s))<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Procedure Do_RmDir(s: rawbytestring);
begin
  if (s='.') then
    begin
      InOutRes:=16;
      exit;
    end;
  if ctr_rmdir(PAnsiChar(s))<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Procedure Do_ChDir(s: rawbytestring);
begin
  if ctr_chdir(PAnsiChar(s))<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

procedure do_getdir(drivenr : byte;var dir : rawbytestring);
var
  buf: array[0..1023] of AnsiChar;
begin
  dir:='';
  if ctr_getcwd(@buf[0],sizeof(buf))=0 then
    begin
      dir:=buf;
      SetCodePage(dir,DefaultFileSystemCodePage,false);
    end
  else
    Errno2InOutRes;
end;

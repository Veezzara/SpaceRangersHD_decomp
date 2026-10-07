{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target: low level file functions.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

Procedure Do_Close(Handle:thandle);
begin
  ctr_close(Handle);
end;

Procedure Do_Erase(p: PAnsiChar; pchangeable: boolean);
var
  info: TCtrStatInfo;
begin
  if (ctr_stat(p,@info)=0) and (info.kind=2) then
    begin
      InOutRes:=2;
      exit;
    end;
  if ctr_unlink(p)<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

procedure do_truncate (handle:thandle;fpos:int64);
begin
  if ctr_ftruncate(handle,fpos)<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Procedure Do_Rename(p1,p2:PAnsiChar; p1changeable, p2changeable: boolean);
begin
  if ctr_rename(p1,p2)<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Function Do_Write(Handle:thandle;Addr:Pointer;Len:Longint):longint;
var
  res: longint;
begin
  res:=ctr_write(Handle,Addr,Len);
  if res<0 then
    begin
      Errno2InOutRes;
      res:=0;
    end
  else
    InOutRes:=0;
  Do_Write:=res;
end;

Function Do_Read(Handle:thandle;Addr:Pointer;Len:Longint):Longint;
var
  res: longint;
begin
  res:=ctr_read(Handle,Addr,Len);
  if res<0 then
    begin
      Errno2InOutRes;
      res:=0;
    end
  else
    InOutRes:=0;
  Do_Read:=res;
end;

function Do_FilePos(Handle: thandle):Int64;
begin
  do_filepos:=ctr_lseek(Handle,0,1);
  if do_filepos<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Procedure Do_Seek(Handle:thandle;Pos:Int64);
begin
  if ctr_lseek(Handle,pos,0)<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Function Do_SeekEnd(Handle:thandle):Int64;
begin
  do_seekend:=ctr_lseek(Handle,0,2);
  if do_seekend<0 then
    Errno2InOutRes
  else
    InOutRes:=0;
end;

Function Do_FileSize(Handle:thandle):Int64;
var
  info: TCtrStatInfo;
begin
  if ctr_fstat(Handle,@info)=0 then
    begin
      Do_FileSize:=info.size;
      InOutRes:=0;
    end
  else
    begin
      Do_FileSize:=0;
      Errno2InOutRes;
    end;
end;

Procedure Do_Open(var f; p: PAnsiChar; flags: longint; pchangeable: boolean);
{
  FileRec and textrec have both Handle and mode as the first items so
  they could use the same routine for opening/creating.
  when (flags and $100)   the file will be append
  when (flags and $1000)  the file will be truncate/rewritten
  when (flags and $10000) there is no check for close (needed for textfiles)
}
var
  oflags : longint;
begin
  { close first if opened }
  if ((flags and $10000)=0) then
   begin
     case FileRec(f).mode of
      fminput,fmoutput,fminout : Do_Close(FileRec(f).Handle);
      fmclosed : ;
     else
      begin
        inoutres:=102; {not assigned}
        exit;
      end;
     end;
   end;
  { reset file Handle }
  FileRec(f).Handle:=UnusedHandle;
  { We do the conversion of filemodes here, concentrated on 1 place }
  case (flags and 3) of
   0 : begin
         oflags:=0;
         FileRec(f).mode:=fminput;
       end;
   1 : begin
         oflags:=1;
         FileRec(f).mode:=fmoutput;
       end;
   2 : begin
         oflags:=2;
         FileRec(f).mode:=fminout;
       end;
  end;
  if (flags and $1000)=$1000 then
   oflags:=oflags or (CTR_O_CREAT or CTR_O_TRUNC)
  else
   if (flags and $100)=$100 then
    oflags:=oflags or CTR_O_APPEND;
  { empty name is special }
  if p[0]=#0 then
   begin
     case FileRec(f).mode of
       fminput :
         FileRec(f).Handle:=StdInputHandle;
       fminout, { this is set by rewrite }
       fmoutput :
         FileRec(f).Handle:=StdOutputHandle;
       fmappend :
         begin
           FileRec(f).Handle:=StdOutputHandle;
           FileRec(f).mode:=fmoutput; {fool fmappend}
         end;
     end;
     exit;
   end;
  { real open call }
  FileRec(f).Handle:=ctr_open(p,oflags,&666);
  if (FileRec(f).Handle<0) and
     (ctr_errno=CTR_EROFS) and ((oflags and 3)=2) then
   begin
     oflags:=oflags and not 3;
     FileRec(f).Handle:=ctr_open(p,oflags,&666);
   end;
  if FileRec(f).Handle<0 then
   begin
     Errno2InOutRes;
     FileRec(f).mode:=fmclosed;
   end
  else
   InOutRes:=0;
end;

function do_isdevice(handle:thandle):boolean;
begin
  do_isdevice:=(handle<=2) or (ctr_isatty(handle)=1);
end;

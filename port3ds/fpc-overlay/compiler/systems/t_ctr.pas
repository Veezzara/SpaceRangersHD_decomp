{
    This unit implements support import,export,link routines
    for the Nintendo 3DS (CTR) target.

    Linking is delegated to the devkitARM gcc driver with 3dsx.specs,
    so that libctru's crt0, newlib and libgcc are pulled in the same way
    as for C homebrew. The resulting ELF is turned into a .3dsx by the
    build scripts (3dsxtool), because that step needs SMDH/RomFS input.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 ****************************************************************************
}
unit t_ctr;

{$i fpcdefs.inc}

interface


implementation

    uses
       SysUtils,
       cutils,cfileutl,cclasses,
       globtype,globals,systems,verbose,cscript,fmodule,i_ctr,link,import,fppu,symsym,ogbase;

    type
       { External C libraries are linked statically; just remember them
         for the linker command line. }
       TImportLibCTR=class(timportlib)
          procedure generatelib;override;
       end;

       TLinkerCTR=class(texternallinker)
       private
          Function  WriteResponseFile: Boolean;
       public
          constructor Create; override;
          procedure SetDefaultInfo; override;
          function  MakeExecutable:boolean; override;
       end;


{*****************************************************************************
                               TIMPORTLIBCTR
*****************************************************************************}

procedure TImportLibCTR.generatelib;
var
  i : longint;
  ImportLibrary : TImportLibrary;
begin
  for i:=0 to current_module.ImportLibraryList.Count-1 do
    begin
      ImportLibrary:=TImportLibrary(current_module.ImportLibraryList[i]);
      current_module.linkothersharedlibs.add(ImportLibrary.Name,link_always);
    end;
end;


{*****************************************************************************
                                  TLINKERCTR
*****************************************************************************}

Constructor TLinkerCTR.Create;
begin
  Inherited Create;
  SharedLibFiles.doubles:=true;
  StaticLibFiles.doubles:=true;
end;


procedure TLinkerCTR.SetDefaultInfo;
begin
  with Info do
   begin
     ExeCmd[1]:='gcc -specs=3dsx.specs -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -g $OPT $GCSECTIONS $STRIP $MAP -o $EXE @$RES';
   end;
end;


Function TLinkerCTR.WriteResponseFile: Boolean;
Var
  linkres  : TLinkRes;
  HPath    : TCmdStrListItem;
  s        : TCmdStr;
begin
  WriteResponseFile:=False;

  { Open link.res file; gcc reads it as an @file (one argument per line) }
  LinkRes:=TLinkRes.Create(outputexedir+Info.ResName,false);

  { Library search paths }
  HPath:=TCmdStrListItem(current_module.locallibrarysearchpath.First);
  while assigned(HPath) do
   begin
     s:=HPath.Str;
     if s<>'' then
       LinkRes.Add(maybequoted('-L'+s));
     HPath:=TCmdStrListItem(HPath.Next);
   end;
  HPath:=TCmdStrListItem(LibrarySearchPath.First);
  while assigned(HPath) do
   begin
     s:=HPath.Str;
     if s<>'' then
       LinkRes.Add(maybequoted('-L'+s));
     HPath:=TCmdStrListItem(HPath.Next);
   end;

  { No startup object: the Pascal main program is the C main() called
    by libctru's crt0 (3dsx.specs). }

  { Object files }
  while not ObjectFiles.Empty do
   begin
     s:=ObjectFiles.GetFirst;
     if s<>'' then
       LinkRes.AddFileName(maybequoted(s));
   end;

  { Pascal static libraries; group them since units reference each other }
  if not StaticLibFiles.Empty then
   begin
     LinkRes.Add('-Wl,--start-group');
     while not StaticLibFiles.Empty do
      begin
        s:=StaticLibFiles.GetFirst;
        LinkRes.AddFileName(maybequoted(s));
      end;
     LinkRes.Add('-Wl,--end-group');
   end;

  { C libraries requested via $linklib / external declarations }
  LinkRes.Add('-Wl,--start-group');
  while not SharedLibFiles.Empty do
   begin
     s:=SharedLibFiles.GetFirst;
     if (s<>'c') and (s<>'gcc') and (s<>'m') then
       LinkRes.Add('-l'+s);
   end;
  LinkRes.Add('-lctru');
  LinkRes.Add('-lm');
  LinkRes.Add('-Wl,--end-group');

  linkres.writetodisk;
  linkres.free;

  WriteResponseFile:=True;
end;


function TLinkerCTR.MakeExecutable:boolean;
var
  binstr,
  cmdstr  : TCmdStr;
  success : boolean;
  GCSectionsStr,
  MapStr,
  StripStr: string;
begin
  StripStr:='';
  MapStr:='';
  GCSectionsStr:='';

  if (cs_link_strip in current_settings.globalswitches) and
     not(cs_link_separate_dbg_file in current_settings.globalswitches) then
   StripStr:='-s';
  if (cs_link_map in current_settings.globalswitches) then
   MapStr:='-Wl,-Map,'+maybequoted(ChangeFileExt(current_module.exefilename,'.map'));
  { 3dsx.specs already passes --gc-sections }
  if not(cs_link_nolink in current_settings.globalswitches) then
   Message1(exec_i_linking,current_module.exefilename);

  WriteResponseFile();

  SplitBinCmd(Info.ExeCmd[1],binstr,cmdstr);
  Replace(cmdstr,'$OPT',Info.ExtraOptions);
  Replace(cmdstr,'$EXE',maybequoted(ScriptFixFileName(current_module.exefilename)));
  Replace(cmdstr,'$RES',maybequoted(ScriptFixFileName(outputexedir+Info.ResName)));
  Replace(cmdstr,'$STRIP',StripStr);
  Replace(cmdstr,'$GCSECTIONS',GCSectionsStr);
  Replace(cmdstr,'$MAP',MapStr);

  success:=DoExec(FindUtil(utilsprefix+BinStr),cmdstr,true,false);

  if (success) and not(cs_link_nolink in current_settings.globalswitches) then
   DeleteFile(outputexedir+Info.ResName);

  MakeExecutable:=success;
end;


{*****************************************************************************
                                     Initialize
*****************************************************************************}

initialization
  RegisterLinker(ld_ctr,TLinkerCTR);
  RegisterImport(system_arm_ctr,TImportLibCTR);
  RegisterTarget(system_arm_ctr_info);
end.

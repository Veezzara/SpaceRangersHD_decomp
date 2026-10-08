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
      OS Memory allocation / deallocation

      Large blocks come from newlib's malloc, which in turn grows the
      libctru application heap, and from the linear heap when that fails
      (see fpcctr_os_alloc).
 ****************************************************************************}

function ctr_os_alloc(size: ptruint): pointer; cdecl; external name 'fpcctr_os_alloc';
procedure ctr_os_free(p: pointer; size: ptruint); cdecl; external name 'fpcctr_os_free';

function SysOSAlloc(size: ptruint): pointer;
begin
  result:=ctr_os_alloc(size);
end;

{$define HAS_SYSOSFREE}

procedure SysOSFree(p: pointer; size: ptruint);
begin
  ctr_os_free(p,size);
end;

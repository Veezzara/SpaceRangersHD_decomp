{
    This file is part of the Free Component Library (FCL)
    Copyright (c) 1999-2000 by Michael Van Canneyt and Florian Klaempfl

    Classes unit for the Nintendo 3DS (CTR)

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{$mode objfpc}
{$h+}
{$modeswitch advancedrecords}
{$if FPC_FULLVERSION>=30301}
{$modeswitch FUNCTIONREFERENCES}
{$define FPC_HAS_REFERENCE_PROCEDURE}
{$ifndef CPULLVM}
{$if DEFINED(CPUARM) or DEFINED(CPUAARCH64)}
   {$define FPC_USE_INTRINSICS}
{$endif}
{$if defined(CPUPOWERPC) or defined(CPUPOWERPC64)}
   {$define FPC_USE_INTRINSICS}
{$endif}
{$if defined(CPURISCV32) or defined(CPURISCV64)}
   {$define FPC_USE_INTRINSICS}
{$endif}
{$endif}
{$endif}
{ determine the type of the resource/form file }
{$define Win16Res}

{$IFNDEF FPC_DOTTEDUNITS}
unit Classes;
{$ENDIF FPC_DOTTEDUNITS}
{$INLINE ON}

interface

{$ifdef NO_FPC_USE_INTRINSICS}
  {$undef FPC_USE_INTRINSICS}
{$endif}
{$IFDEF FPC_DOTTEDUNITS}
uses
  System.SysUtils,
  System.Types,
  System.TypInfo,
{$ifdef FPC_TESTGENERICS}
  System.FGL,
{$endif}
  System.RtlConsts,
{$ifdef FPC_USE_INTRINSICS}
  System.Intrinsics,
{$endif}
  System.SortBase;
{$ELSE FPC_DOTTEDUNITS}
uses
  sysutils,
  types,
  typinfo,
{$ifdef FPC_TESTGENERICS}
  fgl,
{$endif}
  rtlconsts,
{$ifdef FPC_USE_INTRINSICS}
  intrinsics,
{$endif}
  sortbase;
{$ENDIF FPC_DOTTEDUNITS}

{ Also set FPC_USE_INTRINSICS for i386 and x86_64,
  but only after _USES clause as there
  is not intinsics unit for those CPUs }
{$IF FPC_FULLVERSION>=30301}
{$ifndef CPULLVM}
{$if defined(CPUI386) or defined(CPUX86_64)}
   {$define FPC_USE_INTRINSICS}
{$endif}
{$endif}
{$endif}

{$i classesh.inc}

implementation

{ OS - independent class implementations are in /inc directory. }
{$i classes.inc}


initialization
  CommonInit;
finalization
  CommonCleanup;

  if ThreadsInited then
     DoneThreads;
end.

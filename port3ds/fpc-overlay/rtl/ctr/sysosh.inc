{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{Platform specific information}
type
  THandle = LongInt;
  TThreadID = PtrUInt;
  TOSTimestamp = Int64;

  { opaque libctru RecursiveLock, see fpcctr.c }
  PRTLCriticalSection = ^TRTLCriticalSection;
  TRTLCriticalSection = record
    data: array[0..3] of LongWord;
  end;

{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{ The 3DS file systems (sdmc:, romfs:) take UTF-8 paths }
{$define FPCRTL_FILESYSTEM_UTF8}

{ The OS supports a single byte file system operations API that we use }
{$define FPCRTL_FILESYSTEM_SINGLE_BYTE_API}

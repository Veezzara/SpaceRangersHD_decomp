unit GameCtrStartup;
{$MODE delphi}
// First unit initialised on the Nintendo 3DS: brings up the services the
// rest of the platform layer relies on before any game unit runs, and
// explains where the game data belongs when it is missing.
interface

implementation

uses
  SysUtils,
  SDL2,
  Ctru;

procedure CheckGameData;
const
  Message =
    'Space Rangers HD game data was not found.'#10#10 +
    'Copy the contents of the installed game folder (Rangers.exe is not needed) to'#10 +
    CtrDataDir + '/'#10 +
    'on the SD card, so that ' + CtrDataDir + '/install.txt exists.';
begin
  if FileExists(CtrDataDir + '/install.txt') or FileExists(CtrDataDir + '/Install.txt') then
    Exit;
  CtrLog('game data missing');
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  SDL_ShowSimpleMessageBox(0, 'Space Rangers HD', Message, nil);
  Halt(1);
end;

initialization
  CtrStartup;
  CheckGameData;
finalization
  CtrShutdown;
end.

unit GameCtrStartup;
{$MODE delphi}
// First unit initialised on the Nintendo 3DS: brings up the services the
// rest of the platform layer relies on before any game unit runs.
interface

implementation

uses
  Ctru;

initialization
  CtrStartup;
finalization
  CtrShutdown;
end.

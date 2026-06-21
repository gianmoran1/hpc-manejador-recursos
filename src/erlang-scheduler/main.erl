-module(main).
-export([main/0]).

main() ->
  spawn(loggerScheduler, iniciar, ["scheduler.log", self()]),
  receive
    {okLogger} ->
      conectarConBackoff(1000);
    {errorLogger, Razon} ->
      io:format("Error al iniciar el logger: ~p~n", [Razon])
  end.

% Intenta conectar al agente C. Si falla o se pierde la conexion, reintenta
% con backoff exponencial hasta un maximo de 30 segundos entre intentos.
conectarConBackoff(Delay) ->
  case tcpClient:empezarConexion("127.0.0.1", 8080) of
    {okConnect, Socket} ->
      PidScheduler = spawn(scheduler, iniciar, [Socket, self()]),
      spawn(tcpClient, recibirRespuesta, [Socket, PidScheduler]),
      PidScheduler ! arrancar,
      io:format("Sistema iniciado con exito.~n"),
      receive
        {cerrarSistema, Razon} ->
          tcpClient:terminarConexion(Socket),
          NuevoDelay = min(Delay * 2, 30000),
          loggerScheduler:log(
            io_lib:format("[RECONEXION] Desconexion: ~p. Reintentando en ~w ms.", [Razon, NuevoDelay])),
          io:format("Conexion perdida (~p). Reintentando en ~w ms...~n", [Razon, NuevoDelay]),
          timer:sleep(NuevoDelay),
          conectarConBackoff(NuevoDelay)
      end;
    {errorConnect, Razon} ->
      NuevoDelay = min(Delay * 2, 30000),
      io:format("No se pudo conectar (~p). Reintentando en ~w ms...~n", [Razon, NuevoDelay]),
      timer:sleep(NuevoDelay),
      conectarConBackoff(NuevoDelay)
  end.

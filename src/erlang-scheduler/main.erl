-module(main).
-export([main/0]).

% Funcion para dar inicio al sistema.
main() ->
  spawn(loggerScheduler, iniciar, ["scheduler.log", self()]),
  receive
    {okLogger} ->
      case tcpClient:empezarConexion("127.0.0.1", 8080) of
        {okConnect, Socket} ->
          PidScheduler = spawn(scheduler, iniciar, [Socket, self()]),
          % Nace el oyente de las respuestas del agente de C.
          spawn(tcpClient, recibirRespuesta, [Socket, PidScheduler]),
          % Como hay oyente, envio al scheduler que arranque.
          PidScheduler ! arrancar,
          io:format("Sistema iniciado con éxito~n"),
          receive
            {cerrarSistema, Razon} ->
              loggerScheduler:cerrar(),
              tcpClient:terminarConexion(Socket),
              io:format("Cerrando el sistema, razón: ~p~n", [Razon])
          end;
        {errorConnect, Razon} ->
          loggerScheduler:cerrar(),
          io:format("Error al iniciar el sistema: ~p~n", [Razon])
      end;
    {errorLogger, Razon} ->
      io:format("Error al iniciar el logger: ~p~n", [Razon])
  end.

-module(main).
-export([iniciarSistema/0]).

iniciarSistema() ->
  spawn(logger, iniciar, ["scheduler.log"]),
  case tcpClient:empezarConexion("127.0.0.1", 8080) of
    {okConnect, Socket} ->
      PidScheduler = spawn(scheduler, iniciarScheduler, [Socket]),
      % Nace el oyente de las respuestas del agente.
      spawn(tcpClient, recibirRespuesta, [Socket, PidScheduler]),
      % Como hay oyente, envio al scheduler que arranque.
      PidScheduler ! arrancar,
      io:format("Sistema iniciado con éxito~n");
    
    {errorConnect, Razon} ->
      io:format("Error al iniciar el sistema: ~p~n", [Razon])
  end.
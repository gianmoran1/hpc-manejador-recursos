-module(main).
-export([iniciarSistema/0]).

iniciarSistema() ->
  {ok, Socket} = tcpClient:empezarConexion("127.0.0.1", 8080),
  
  PidScheduler = spawn(scheduler, iniciarLoop, [Socket]),
  
  spawn(tcpClient, recibirRespuesta, [Socket, PidScheduler]),
  
  PidScheduler ! arrancar,
  
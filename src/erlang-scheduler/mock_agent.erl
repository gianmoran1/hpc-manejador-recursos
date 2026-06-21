-module(mock_agent).
-export([start/0]).

% Inicia el servidor simulado en el puerto 8080.
start() ->
  io:format("Iniciando simulador del agente C en puerto 8080...~n"),
  Opciones = [binary, {packet, line}, {active, false}, {reuseaddr, true}],
  case gen_tcp:listen(8080, Opciones) of
    {ok, ListenSocket} ->
      accept(ListenSocket);
    {error, Razon} ->
      io:format("No se pudo abrir el puerto: ~p~n", [Razon])
  end.

accept(ListenSocket) ->
  case gen_tcp:accept(ListenSocket) of
    {ok, Socket} ->
      io:format("Cliente Erlang conectado!~n"),
      loop(Socket),
      accept(ListenSocket);
    {error, _} ->
      ok
  end.

loop(Socket) ->
  case gen_tcp:recv(Socket, 0) of
    {ok, Bin} ->
      Str = binary_to_list(Bin),
      Tokens = string:tokens(Str, " \n"),
      procesar(Socket, Tokens),
      loop(Socket);
    {error, closed} ->
      io:format("Conexion cerrada por el cliente.~n");
    {error, _} ->
      ok
  end.

% Simula la respuesta a GET_NODES enviando dos nodos ficticios.
procesar(Socket, ["GET_NODES"]) ->
  io:format("[Agente] Recibido: GET_NODES~n"),
  Nodos = "NODES 127.0.0.1:9001:cpu:4:mem:8:gpu:2;"
          "127.0.0.1:9002:cpu:6:mem:12:gpu:1\n",
  gen_tcp:send(Socket, Nodos);

% Simula respuestas aleatorias para probar todos tus ruteos.
procesar(Socket, ["JOB_REQUEST", JobIdStr | _Reqs]) ->
  io:format("[Agente] Recibido: JOB_REQUEST para Job ~s~n", [JobIdStr]),
  case rand:uniform(3) of
    1 ->
      gen_tcp:send(Socket, io_lib:format("JOB_GRANTED ~s\n", [JobIdStr]));
    2 ->
      gen_tcp:send(Socket, io_lib:format("JOB_DENIED ~s\n", [JobIdStr]));
    3 ->
      % Demoramos un segundo la respuesta para simular la traba.
      timer:sleep(1000),
      gen_tcp:send(Socket, io_lib:format("JOB_TIMEOUT ~s\n", [JobIdStr]))
  end;

procesar(_Socket, ["JOB_RELEASE", JobIdStr]) ->
  io:format("[Agente] Recibido: JOB_RELEASE para Job ~s~n", [JobIdStr]);

procesar(_Socket, Cmd) ->
  io:format("[Agente] Comando desconocido: ~p~n", [Cmd]).
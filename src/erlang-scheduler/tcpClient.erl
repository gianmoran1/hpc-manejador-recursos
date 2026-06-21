-module(tcpClient).
-export([empezarConexion/2, terminarConexion/1,
         solicitudNodos/1, solicitudTrabajo/3, liberarTrabajo/2, estadoTrabajo/2,
         recibirRespuesta/2, procesarMensaje/1]).

% Dada la direccion IP y el puerto, la funcion inicia la conexion
% al agente de C y devuelve el Socket si tiene exito.
empezarConexion(Direccion, Puerto) ->
  % Los datos recibidos son binarios, se entregan como vienen (crudos),
  % usamos conexion pasiva.
  Opciones = [binary, {packet, line}, {active, false}],
  case gen_tcp:connect(Direccion, Puerto, Opciones) of
    {ok, Socket} ->
      {okConnect, Socket};
    {error, Razon} ->
      {errorConnect, Razon}
  end.

% Dado el socket del agente de C, la funcion termina la conexion.
terminarConexion(Socket) ->
  gen_tcp:close(Socket).

% Pide al agente C la lista de nodos participantes en la red.
solicitudNodos(Socket) ->
  gen_tcp:send(Socket, "GET_NODES\n").

% Dado el socket del agente de C, un id de trabajo y la lista de requerimientos
% con la forma [{"IP", "recurso", cantidad}], la funcion solicita al agente
% de C la solicitud de trabajo.
solicitudTrabajo(Socket, JobId, ListaRequerimientos) ->
  ListaRequerimientosStr = [io_lib:format("@~s:~s:~w", [IP, Recurso, Cantidad]) 
                            || {IP, Recurso, Cantidad} <- ListaRequerimientos],
  Requerimientos = lists:flatten(lists:join(" ", ListaRequerimientosStr)),
  
  Mensaje = io_lib:format("JOB_REQUEST ~w ~s\n", [JobId, Requerimientos]),
  gen_tcp:send(Socket, Mensaje).

% Dado el socket del agente de C y un id de trabajo, la funcion pide que se
% libere el trabajo con ese id.
liberarTrabajo(Socket, JobId) ->
  Mensaje = io_lib:format("JOB_RELEASE ~w\n", [JobId]),
  gen_tcp:send(Socket, Mensaje).

% Dado el socket del agente de C y un id de trabajo, la funcion pide
% el estado actual del trabajo con ese id.
estadoTrabajo(Socket, JobId) ->
  Mensaje = io_lib:format("JOB_STATUS ~w\n", [JobId]),
  gen_tcp:send(Socket, Mensaje).

% Dado el socket del agente y el pid del scheduler propiamente dicho, la 
% funcion espera las respuestas del agente de C.
recibirRespuesta(Socket, PidScheduler) ->
  % Con {packet, line}, recv devuelve exactamente una linea por llamada.
  case gen_tcp:recv(Socket, 0) of
    {ok, Binarios} ->
      % Convertimos el binario a un string.
      StringRecibido = binary_to_list(Binarios),
      % Separamos el string usando los espacios y el \n como delimitadores.
      Tokens = string:tokens(StringRecibido, " \n"),
      Resultado = procesarMensaje(Tokens),
      PidScheduler ! Resultado, % Al scheduler le enviamos el resultado
      recibirRespuesta(Socket, PidScheduler);
    {error, Reason} ->
      PidScheduler ! {errorRecvAgente, Reason}
  end.

% Dada la lista de tokens, la funcion evalua el comando y devuelve cual fue la
% respuesta.
procesarMensaje(Tokens) ->
  case Tokens of
    ["NODES", StrNodos] ->
      {nodos, StrNodos};

    ["JOB_GRANTED", JobIdStr] ->
      JobId = list_to_integer(JobIdStr),
      {granted, JobId};
      
    ["JOB_DENIED", JobIdStr] ->
      JobId = list_to_integer(JobIdStr),
      {denied, JobId};
      
    ["JOB_TIMEOUT", JobIdStr] ->
      JobId = list_to_integer(JobIdStr),
      {timeout, JobId};
      
    ComandoDesconocido ->
      % Cualquier lista que no coincida con los formatos esperados.
      {comandoDesconocido, ComandoDesconocido}
  end.

-module(scheduler).
-export([iniciarScheduler/1]).

iniciarScheduler(Socket) ->
  receive
    arrancar ->
      tcpClient:solicitudNodos(Socket),
      loopScheduler(Socket, [], {0,0,0}, #{})
  end.

% Estado: Socket, ListaNodos, RecursosTotales {Cpu,Mem,Gpu}, #{JobId => WorkerPid}
loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso) ->
  receive
    {nodos, StrNodos} ->
      case parseoNodos(StrNodos) of
        error ->
          io:format("Error al parsear nodos.~n"),
          loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);
        {ok, ListaParseada} ->
          Totales = calcularTotales(ListaParseada),
          io:format("Nodos parseados: ~w~n", [ListaParseada]),
          erlang:send_after(1000, self(), generar_trabajo),
          loopScheduler(Socket, ListaParseada, Totales, JobsEnCurso)
      end;

    generar_trabajo when ListaNodos =:= [] ->
      io:format("Sin nodos disponibles, ignorando generacion de trabajo.~n"),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);

    generar_trabajo ->
      JobId = erlang:unique_integer([positive, monotonic]),
      WorkerPid = spawn(job_worker, iniciar, [Socket, JobId, self(), RecursosTotales]),
      erlang:send_after(3000, self(), generar_trabajo),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso#{JobId => WorkerPid});

    % El worker manda {CantCpu, CantMem, CantGpu} en el orden convenido.
    {solicitar_recursos, JobId, {CantCpu, CantMem, CantGpu}} ->
      case asignarNodos({CantCpu, CantMem, CantGpu}, ListaNodos) of
        [] ->
          rutear(JobId, {denied, JobId}, JobsEnCurso);
        ReqsConcretos ->
          tcpClient:solicitudTrabajo(Socket, JobId, ReqsConcretos)
      end,
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);

    {granted, JobId} ->
      rutear(JobId, {granted, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);

    {denied, JobId} ->
      rutear(JobId, {denied, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);

    {timeout, JobId} ->
      rutear(JobId, {timeout, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso);

    {job_terminado, JobId} ->
      loopScheduler(Socket, ListaNodos, RecursosTotales, maps:remove(JobId, JobsEnCurso))
  end.

rutear(JobId, Mensaje, JobsEnCurso) ->
  case maps:find(JobId, JobsEnCurso) of
    {ok, Pid} -> Pid ! Mensaje;
    error     -> io:format("Respuesta para Job desconocido: ~w~n", [JobId])
  end.

calcularTotales(ListaNodos) ->
  lists:foldl(fun({_IP, _Puerto, {Cpu, Mem, Gpu}}, {AccCpu, AccMem, AccGpu}) ->
    {AccCpu + Cpu, AccMem + Mem, AccGpu + Gpu}
  end, {0, 0, 0}, ListaNodos).

% Dado {CantCpu, CantMem, CantGpu}, para cada posicion no-cero busca un nodo
% con suficiente capacidad y lo asigna al azar.
% Devuelve [{IP, "cpu"|"mem"|"gpu", Cantidad}] para solicitudTrabajo.

%CORREGIR, si pido 7 de cpu y tengo 2 maquinas con 5 y 5 no crea la solicitud, Candidatos = []
asignarNodos({CantCpu, CantMem, CantGpu}, ListaNodos) ->
  asignarRecurso("cpu", CantCpu, fun({_,_,{Max,_,_}}) -> Max end, ListaNodos) ++
  asignarRecurso("mem", CantMem, fun({_,_,{_,Max,_}}) -> Max end, ListaNodos) ++
  asignarRecurso("gpu", CantGpu, fun({_,_,{_,_,Max}}) -> Max end, ListaNodos).

asignarRecurso(_Tipo, 0, _GetMax, _ListaNodos) -> [];
asignarRecurso(Tipo, Cantidad, GetMax, ListaNodos) ->
  Candidatos = [IP || {IP, _Puerto, _} = Nodo <- ListaNodos, GetMax(Nodo) >= Cantidad],
  case Candidatos of
    [] -> [];
    _  ->
      IP = lists:nth(rand:uniform(length(Candidatos)), Candidatos),
      [{IP, Tipo, Cantidad}]
  end.

% Dado "IP:PUERTO:res1:val1:res2:val2:...;...", devuelve
% {ok, [{IP, Puerto, {Cpu, Mem, Gpu}}]} o error.
parseoNodos(StrNodos) ->
  NodosStr = string:tokens(StrNodos, ";"),
  try
    Lista = [parsearNodo(N) || N <- NodosStr],
    {ok, Lista}
  catch
    _:_ -> error
  end.

parsearNodo(NodoStr) ->
  [IP, PuertoStr | RecursosTokens] = string:tokens(NodoStr, ":"),
  {IP, list_to_integer(PuertoStr), parsearRecursos(RecursosTokens)}.

% Construye {Cpu, Mem, Gpu} desde los tokens, usando 0 para recursos ausentes.
parsearRecursos(Tokens) ->
  Mapa = parsearRecursosMap(Tokens, #{}),
  {maps:get("cpu", Mapa, 0), maps:get("mem", Mapa, 0), maps:get("gpu", Mapa, 0)}.

parsearRecursosMap([], Acc) -> Acc;
parsearRecursosMap([Tipo, CantStr | Resto], Acc) ->
  parsearRecursosMap(Resto, Acc#{Tipo => list_to_integer(CantStr)}).

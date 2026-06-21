-module(scheduler).
-export([iniciar/2, loopActualizadorNodos/1]).

% Dado el socket del agente de C y el pid del main, la funcion inicia el 
% scheduler, este espera a que este creado el oyente del agente de C.
iniciar(Socket, PidMain) ->
  receive
    arrancar ->
      tcpClient:solicitudNodos(Socket),
      % Si el scheduler muere, el generador de trabajos y el actualizador
      % tambien.
      spawn_link(jobWorker, loopGeneradorTrabajos, [self()]),
      spawn_link(?MODULE, loopActualizadorNodos, [self()]),
      loopScheduler(Socket, [], {0,0,0}, #{}, PidMain)
  end.

% Dado el Pid del scheduler la funcion pide la lista de nodos cada 15 segundos 
% para mantener el cluster actualizado.
loopActualizadorNodos(PidScheduler) ->
  timer:sleep(15000),
  PidScheduler ! actualizarNodos,
  loopActualizadorNodos(PidScheduler).

% La funcion maneja todo el ciclo de vida del scheduler.
% Estado: ListaNodos, RecursosTotales {Cpu,Mem,Gpu}, #{JobId => WorkerPid}
loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain) ->
  receive
    % Interaccion con nodos:
    actualizarNodos ->
      tcpClient:solicitudNodos(Socket),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {nodos, StrNodos} ->
      case manejoRecursos:parsearNodos(StrNodos) of
        error ->
          loggerScheduler:log("Error en el parseo de nodos, usamos la lista vieja."),
          loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);
        {ok, ListaParseada} ->
          Totales = manejoRecursos:calcularRecursosTotales(ListaParseada),
          loopScheduler(Socket, ListaParseada, Totales, JobsEnCurso, PidMain)
      end;

    % Interacciones con el trabajo simulado:
    generarTrabajo ->
      case ListaNodos of
        [] -> 
          loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);
        _  ->
          case RecursosTotales of
            {0,0,0} -> % Si no hay recursos o la lista es vacia ignoramos.
              loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);
            _ ->
              JobId = erlang:unique_integer([positive, monotonic]),
              WorkerPid = spawn(jobWorker, iniciarJobWorker, 
                                [Socket, JobId, self(), RecursosTotales]),
              loopScheduler(Socket, ListaNodos, RecursosTotales, 
                            JobsEnCurso#{JobId => WorkerPid}, PidMain)
          end
      end;

    % El worker manda {CantCpu, CantMem, CantGpu} en el orden convenido.
    {solicitarRecursos, JobId, {CantCpu, CantMem, CantGpu}} ->
      case manejoRecursos:asignarNodos({CantCpu, CantMem, CantGpu}, ListaNodos) of
        [] ->
          enviarMensajeJob(JobId, {denied, JobId}, JobsEnCurso);
        ReqsConcretos ->
          tcpClient:solicitudTrabajo(Socket, JobId, ReqsConcretos)
      end,
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {jobTerminado, JobId} ->
      loopScheduler(Socket, ListaNodos, RecursosTotales, 
                    maps:remove(JobId, JobsEnCurso), PidMain);

    % Respuestas desde el agente de C:
    {granted, JobId} ->
      enviarMensajeJob(JobId, {granted, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {denied, JobId} ->
      enviarMensajeJob(JobId, {denied, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {timeout, JobId} ->
      enviarMensajeJob(JobId, {timeout, JobId}, JobsEnCurso),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {comandoDesconocido, StrComando} ->
      Str = io_lib:format("Comando desconocido del agente: ~s", [StrComando]),
      loggerScheduler:log(Str),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {errorRecvAgente, Razon} ->
      % Se cayo la conexion con el oyente, terminamos el programa
      PidMain ! {cerrarSistema, Razon}
  end.

% Dado el identificador del trabajo (JobId), un mensaje y el mapa de trabajos 
% en curso (JobsEnCurso), la funcion busca el PID del proceso trabajador 
% asociado a ese JobId y le reenvia el mensaje. Si no lo encuentra, loguea
% que el trabajo es ausente.
enviarMensajeJob(JobId, {Tipo, JobId} = Mensaje, JobsEnCurso) ->
  case maps:find(JobId, JobsEnCurso) of
    {ok, Pid} -> 
      Pid ! Mensaje;
    error -> 
      Str = io_lib:format("Ignorando ~p: Job ~w ausente", [Tipo, JobId]),
      loggerScheduler:log(Str)
  end.

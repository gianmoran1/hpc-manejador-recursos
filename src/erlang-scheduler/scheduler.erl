-module(scheduler).
-export([iniciar/2, loopActualizadorNodos/1]).

-define(MAX_JOBS, 10).

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
        _ ->
          case RecursosTotales of
            {0,0,0} ->
              loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);
            _ when map_size(JobsEnCurso) >= ?MAX_JOBS ->
              loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);
            _ ->
              JobId = erlang:unique_integer([positive, monotonic]),
              {WorkerPid, _Ref} = spawn_monitor(jobWorker, iniciarJobWorker,
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

    % Worker bajo de forma inesperada (crash): limpiamos el mapa.
    {'DOWN', _Ref, process, _Pid, normal} ->
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {'DOWN', _Ref, process, Pid, Razon} ->
      JobsLimpios = maps:filter(fun(_Id, P) -> P =/= Pid end, JobsEnCurso),
      Str = io_lib:format("Worker ~p termino con error: ~p", [Pid, Razon]),
      loggerScheduler:log(Str),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsLimpios, PidMain);

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
      Str = io_lib:format("Comando desconocido del agente: ~p", [StrComando]),
      loggerScheduler:log(Str),
      loopScheduler(Socket, ListaNodos, RecursosTotales, JobsEnCurso, PidMain);

    {errorRecvAgente, Razon} ->
      % Matamos todos los workers: quedarian colgados en receive sin respuesta.
      maps:foreach(fun(_JobId, Pid) -> exit(Pid, shutdown) end, JobsEnCurso),
      PidMain ! {cerrarSistema, Razon},
      exit(self(), cerrarSistema)
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

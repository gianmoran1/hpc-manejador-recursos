-module(jobWorker).
-export([loopGeneradorTrabajos/1, iniciarJobWorker/4]).

% Dado el Pid del scheduler, la funcion se encarga de generar trabajos cada 
% cierto tiempo.
loopGeneradorTrabajos(PidScheduler) ->
  timer:sleep(rand:uniform(5000) + 2000),
  PidScheduler ! generarTrabajo,
  loopGeneradorTrabajos(PidScheduler).

% Dado el Socket del agente de C, el ID del Job, el Pid del scheduler y los
% recursos totales del cluster, la funcion inicia el job simulado.
% RecursosTotales = {TotalCpu, TotalMem, TotalGpu}, convenido con el scheduler.
iniciarJobWorker(Socket, JobId, PidScheduler, RecursosTotales) ->
  ejecutarTrabajoSimulado(Socket, JobId, PidScheduler, RecursosTotales),
  PidScheduler ! {jobTerminado, JobId}.

% Dado el Socket del agente de C, el ID del Job, el Pid del scheduler y los
% recursos totales del cluster, la funcion ejecuta el job simulado
% con una solicitud aleatoria de recursos y ademas espera la respuesta
% del agente para escribir en el log.
ejecutarTrabajoSimulado(Socket, JobId, PidScheduler, RecursosTotales) ->
  Cantidades = generarCantidades(RecursosTotales),
  PidScheduler ! {solicitarRecursos, JobId, Cantidades},
  receive
    {granted, JobId} ->
      loggerScheduler:log(io_lib:format("[GRANTED] JobId:~w", [JobId])),
      timer:sleep(rand:uniform(5000) + 2000),
      tcpClient:liberarTrabajo(Socket, JobId);

    {denied, JobId} ->
      loggerScheduler:log(io_lib:format("[DENIED] JobId:~w", [JobId]));

    {timeout, JobId} ->
      % Posible deadlock evitado con timeout.
      loggerScheduler:log(io_lib:format("[TIMEOUT] JobId:~w", [JobId])),
      tcpClient:liberarTrabajo(Socket, JobId),
      timer:sleep(rand:uniform(3000)),
      % Intentamos ejecutar el trabajo nuevamente.
      ejecutarTrabajoSimulado(Socket, JobId, PidScheduler, RecursosTotales)
  end.

% Genera {CantCpu, CantMem, CantGpu} donde cada valor es 0 (no pedir)
% o entre 1 y la mitad del total global. Garantiza al menos un valor > 0.
generarCantidades({TotalCpu, TotalMem, TotalGpu}) ->
  Cantidades = {genCant(TotalCpu), genCant(TotalMem), genCant(TotalGpu)},
  case Cantidades of
    {0, 0, 0} -> generarCantidades({TotalCpu, TotalMem, TotalGpu});
    _         -> Cantidades
  end.

genCant(0) -> 0;
genCant(Total) ->
  case rand:uniform(2) of
    1 -> 0;
    2 -> rand:uniform(max(1, Total div 2))
  end.

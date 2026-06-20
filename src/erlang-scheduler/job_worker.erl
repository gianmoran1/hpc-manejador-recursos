-module(job_worker).
-export([iniciar/4]).

% RecursosTotales = {TotalCpu, TotalMem, TotalGpu}, convenido con el scheduler.
iniciar(Socket, JobId, PidScheduler, RecursosTotales) ->
  ejecutar(Socket, JobId, PidScheduler, RecursosTotales),
  PidScheduler ! {job_terminado, JobId}.

ejecutar(Socket, JobId, PidScheduler, RecursosTotales) ->
  Cantidades = generarCantidades(RecursosTotales),
  io:format("Solicitando Job ~w con cantidades: ~w~n", [JobId, Cantidades]),
  PidScheduler ! {solicitar_recursos, JobId, Cantidades},
  receive
    {granted, JobId} ->
      logger:log(io_lib:format("[GRANTED] job_id=~w", [JobId])),
      timer:sleep(rand:uniform(5000) + 2000),
      tcpClient:liberarTrabajo(Socket, JobId);

    {denied, JobId} ->
      logger:log(io_lib:format("[DENIED] job_id=~w", [JobId]));

    {timeout, JobId} ->
      logger:log(io_lib:format("[DEADLOCK_AVOIDED] job_id=~w", [JobId])),
      tcpClient:liberarTrabajo(Socket, JobId),
      timer:sleep(rand:uniform(3000)),
      ejecutar(Socket, JobId, PidScheduler, RecursosTotales)
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

-module(logger).
-export([iniciar/1, log/1]).

% Abre el archivo y se registra globalmente como scheduler_logger.
% Es el unico proceso que escribe al archivo, serializando accesos concurrentes.
iniciar(Archivo) ->
  {ok, Fd} = file:open(Archivo, [append]),
  register(scheduler_logger, self()),
  loop(Fd).

loop(Fd) ->
  receive
    {log, Linea} ->
      io:fwrite(Fd, "~s~n", [Linea]),
      loop(Fd);
    cerrar ->
      file:close(Fd)
  end.

log(Linea) ->
  scheduler_logger ! {log, Linea}.

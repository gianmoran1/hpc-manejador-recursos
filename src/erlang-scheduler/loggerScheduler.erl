-module(loggerScheduler).
-export([iniciar/2, log/1, cerrar/0]).

% Dado un nombre de archivo y el PidPadre, abre el archivo y se registra 
% globalmente como schedulerLogger. Le envia un mensaje al PidPadre indicando 
% si tuvo exito o fallo.
iniciar(Archivo, PidPadre) ->
  case file:open(Archivo, [append]) of
    {ok, Fd} ->
      register(schedulerLogger, self()),
      PidPadre ! {okLogger},
      loopLogger(Fd);
    {error, Razon} ->
      PidPadre ! {errorLogger, Razon}
  end.

% Dado el file descriptor de el archivo .log, la funcion que espera mensajes.
loopLogger(Fd) ->
  receive
    {log, Linea} ->
      io:fwrite(Fd, "~s~n", [Linea]),
      loopLogger(Fd);
    cerrar ->
      file:close(Fd)
  end.

% Dado un string, la funcion la escribe en el archivo .log.
log(Linea) ->
  schedulerLogger ! {log, Linea}.

% La funcion cierra el archivo .log.
cerrar() ->
  schedulerLogger ! cerrar.

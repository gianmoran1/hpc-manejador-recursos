-module(scheduler).
-export([iniciarScheduler/1]).

iniciarScheduler(Socket) ->
  receive
    % Esperamos hasta que se cree el oyente.
    arrancar ->
      tcpClient:solicitudNodos(Socket),
      loopScheduler(Socket, []) % Arranca con una lista de nodos vacia.
  end.

loopScheduler(Socket, ListaNodos) ->
  receive
    {nodos, StrNodos} ->
      case parseoNodos(StrNodos) of
        error -> 
          io:format("Error al parsear nodos.~n"),
          loopScheduler(Socket, ListaNodos);
        {ok, ListaParseada} ->
          io:format("Nodos parseados. Arrancando simulador...~n"),
          % ¡La chispa inicial! Nos mandamos un mensaje a nosotros 
          % mismos en 1 segundo para crear el primer job.
          erlang:send_after(1000, self(), generar_trabajo),
          loopScheduler(Socket, ListaParseada)
      end;

    % 2. Evento para inventar un trabajo nuevo
    generar_trabajo ->
      JobId = erlang:unique_integer([positive]),
      
      % TODO: Aca deberias elegir nodos al azar de tu ListaNodos 
      % y armar tu lista de requerimientos real. Por ahora hardcodeamos:
      Requerimientos = [{"192.168.1.10", "cpu", 2}],
      
      io:format("Simulador: Pidiendo recursos para Job ~w~n", [JobId]),
      tcpClient:solicitudTrabajo(Socket, JobId, Requerimientos),
      
      % Programamos el proximo trabajo para dentro de 3 segundos
      erlang:send_after(3000, self(), generar_trabajo),
      loopScheduler(Socket, ListaNodos);

    % 3. El agente C nos dice que conseguimos los recursos
    {granted, JobId} ->
      io:format("Exito: Job ~w concedido. Usando recursos...~n", [JobId]),
      
      % Simulamos que el trabajo tarda 5 segundos en ejecutarse 
      erlang:send_after(5000, self(), {terminar_trabajo, JobId}),
      loopScheduler(Socket, ListaNodos);

    % 4. Pasaron los 5 segundos, hay que devolver los recursos
    {terminar_trabajo, JobId} ->
      io:format("Fin: Liberando recursos del Job ~w~n", [JobId]),
      tcpClient:liberarTrabajo(Socket, JobId),
      loopScheduler(Socket, ListaNodos);

    % 5. ESTRATEGIA ANTI-DEADLOCK: El agente C nos avisa de un timeout
    {timeout, JobId} ->
      io:format("ALERTA: Posible deadlock en Job ~w. Abortando...~n", [JobId]),
      % Nuestra estrategia es soltar todo lo que teniamos para romper la espera circular 
      tcpClient:liberarTrabajo(Socket, JobId),
      loopScheduler(Socket, ListaNodos);

    % 6. Nos rechazaron de entrada
    {denied, JobId} ->
      io:format("Rechazado: No hay recursos para el Job ~w~n", [JobId]),
      loopScheduler(Socket, ListaNodos)
  end.

% Funcion stub para que no te tire error al compilar. 
% Despues la programas bien.
parseoNodos(_StrNodos) ->
  {ok, [{"192.168.1.10", "cpu", 4}]}.
-module(manejoRecursos).
-export([calcularRecursosTotales/1, parsearNodos/1, asignarNodos/2]).

% Dada la lista de nodos parseada, suma todos los recursos del cluster.
% Devuelve una unica tupla {TotalCpu, TotalMem, TotalGpu}.
calcularRecursosTotales(ListaNodos) ->
  lists:foldl(fun({_IP, _Puerto, {Cpu, Mem, Gpu}}, {AccCpu, AccMem, AccGpu}) ->
                  {AccCpu + Cpu, AccMem + Mem, AccGpu + Gpu}
              end,
              {0, 0, 0},
              ListaNodos).

% Dado un string de nodos con la forma "IP:PUERTO:res1:val1:res2:val2:...;...", 
% en caso de exito en el paseo devuelve una lista 
% [{IP, Puerto, {Cpu, Mem, Gpu}}] o error.
parsearNodos(StrNodos) ->
  try
    ListaNodosStr = string:tokens(StrNodos, ";"),
    Lista = [parsearNodo(N) || N <- ListaNodosStr],
    {ok, Lista}
  catch
    % Cualquier excepcion en el parseo es capturada y se envia error.
    _:_ -> error
  end.

% Dado un nodo individual de la forma: "IP:PUERTO:res1:val1:res2:val2:...", la
% funcion lo parsea y devuelve en una tupla.
parsearNodo(NodoStr) ->
  [IP, PuertoStr | RecursosTokens] = string:tokens(NodoStr, ":"),
  {IP, list_to_integer(PuertoStr), parsearRecursos(RecursosTokens)}.

% Dada una lista de strings con recursos y sus cantidades, la funcion construye 
% {Cpu, Mem, Gpu} desde los tokens, usando 0 para recursos ausentes.
parsearRecursos(Tokens) ->
  Mapa = parsearRecursosMap(Tokens, #{}),
  {maps:get("cpu", Mapa, 0), maps:get("mem", Mapa, 0), maps:get("gpu", Mapa, 0)}.

% Cuando la lista de tokens esta vacia la fucion devuelve el mapa final con 
% todos los recursos almacenados.
parsearRecursosMap([], MapaRecursos) -> 
  MapaRecursos;
% Si la lista no es vacia extrae el Tipo de recurso y su cantidad. Los agrega 
% al mapa y sigue iterando con el resto de la lista.
parsearRecursosMap([Tipo, CantStr | Resto], MapaRecursos) ->
  % Aniado al mapa construido el recurso convirtiendo el string a entero.
  parsearRecursosMap(Resto, MapaRecursos#{Tipo => list_to_integer(CantStr)}).

% Dado {CantCpu, CantMem, CantGpu}, busca nodos para satisfacer la demanda.
% Si no puede satisfacerla completamente para algun recurso, devuelve [].
% En caso de exito, devuelve [{IP, "cpu"|"mem"|"gpu", Cantidad}] 
% para solicitudTrabajo.
asignarNodos({CantCpu, CantMem, CantGpu}, ListaNodos) ->
  ReqCpu = asignarRecurso("cpu", CantCpu, 
                          fun({_, _, {Max, _, _}}) -> Max end, ListaNodos),
  ReqMem = asignarRecurso("mem", CantMem, 
                          fun({_, _, {_, Max, _}}) -> Max end, ListaNodos),
  ReqGpu = asignarRecurso("gpu", CantGpu, 
                          fun({_, _, {_, _, Max}}) -> Max end, ListaNodos),
  
  % Si alguna de las listas devolvio un atomo 'insuficiente', fallamos todo.
  case (ReqCpu =:= insuficiente) orelse 
       (ReqMem =:= insuficiente) orelse 
       (ReqGpu =:= insuficiente) of
    true  -> [];
    false -> ReqCpu ++ ReqMem ++ ReqGpu
  end.

% Si pedimos 0 de un recurso, no necesitamos asignar nada.
asignarRecurso(_Tipo, 0, _GetMax, _ListaNodos) -> 
  [];
asignarRecurso(Tipo, CantidadPedida, GetMax, ListaNodos) ->
  % Mezclamos la lista de nodos para no pedirle siempre al mismo primero.
  NodosMezclados = [X || {_,X} <- lists:sort([{rand:uniform(), N} || N <- ListaNodos])],
  asignarVoraz(Tipo, CantidadPedida, GetMax, NodosMezclados, []).

% Caso Base: Ya cubrimos toda la demanda.
asignarVoraz(_Tipo, 0, _GetMax, _Nodos, Acumulador) -> 
  Acumulador;
% Caso Fallo: Nos quedamos sin nodos y todavia falta cubrir demanda.
asignarVoraz(_Tipo, _Falta, _GetMax, [], _Acumulador) -> 
  insuficiente;
% Caso Recursivo: Evaluamos el nodo actual.
asignarVoraz(Tipo, Falta, GetMax, [{IP, _Puerto, _} = Nodo | RestoNodos], Acc) ->
  Disponible = GetMax(Nodo),
  case Disponible of
    0 -> 
      % Este nodo no tiene este recurso, pasamos al siguiente.
      asignarVoraz(Tipo, Falta, GetMax, RestoNodos, Acc);
    _ ->
      % Tomamos lo que falta o lo maximo que el nodo puede dar, lo que sea menor.
      Tomar = min(Falta, Disponible),
      NuevoAcc = [{IP, Tipo, Tomar} | Acc],
      asignarVoraz(Tipo, Falta - Tomar, GetMax, RestoNodos, NuevoAcc)
  end.
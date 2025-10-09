# LAB03 – Pub/Sub TCP (Broker, Publisher y Subscriber) #
Documentación de la implementación en C de un sistema Publish/Subscribe sobre TCP usando sockets BSD en Linux (WSL).
Componentes: broker_tcp.c, publisher_tcp.c, subscriber_tcp.c.

## Arquitectura general ##
<img width="441" height="81" alt="image" src="https://github.com/user-attachments/assets/7bec76b4-a8db-44a3-bc54-dd266130f7c0" />

- Broker: servidor TCP concurrente. Acepta conexiones, reconoce el rol de cada cliente (PUB o SUB), registra suscripciones por topic y reenvía mensajes.
- Publisher: cliente TCP que se conecta al broker y envía eventos PUB: <topic>|<mensaje>.
- Subscriber: cliente TCP que se conecta, anuncia rol, se suscribe SUB: <topic> y recibe MSG: del broker.

## Cómo refleja TCP el comportamiento del sistema ##

- Conexión confiable: cada cliente hace connect() → el broker hace accept() → TCP realiza el 3-way handshake (SYN, SYN/ACK, ACK).
- Entrega garantizada y ordenada: los eventos viajan dentro de segmentos con flag PSH; el broker contesta ACK (a nivel aplicación) tras procesarlos. TCP asegura orden y retransmisión.
- Canales independientes: cada cliente tiene su socket propio (publisher/subscriber ↔ broker); el broker usa select() para atender muchos a la vez sin hilos.
- Cierre limpio: close() → TCP envía FIN/ACK.

## Compilación y ejecución ##

**1. Desde la carpeta tcp:**
   
gcc -Wall -O2 -o broker_tcp     broker_tcp.c

gcc -Wall -O2 -o publisher_tcp  publisher_tcp.c

gcc -Wall -O2 -o subscriber_tcp subscriber_tcp.c

**2. Broker**

./broker_tcp 9000

**3. Subscribers**

./subscriber_tcp 127.0.0.1 9000 Real_Azul_vs_Atletico_Rojo

./subscriber_tcp 127.0.0.1 9000 Verde_CF_vs_Universidad_FC

**4. Publishers (simulador de partido)**

./publisher_tcp 127.0.0.1 9000 "Real Azul" "Atletico Rojo" 20 150

./publisher_tcp 127.0.0.1 9000 "Verde CF"  "Universidad FC" 20 150


## Broker (broker_tcp.c) – Diseño y funciones clave

### 1. Creación del socket y configuración de `SO_REUSEADDR`

<img width="681" height="156" alt="image" src="https://github.com/user-attachments/assets/03fd3102-28f3-46f4-8260-bd583ba1ad71" />

En este bloque se crea el **socket TCP** que usará el *broker* para aceptar conexiones entrantes.  
La llamada `socket(AF_INET, SOCK_STREAM, 0)` inicializa un *endpoint* de comunicación utilizando **IPv4** y el protocolo **TCP**, que es orientado a flujo y confiable.  
Si la función falla (`listener < 0`), se termina el programa, ya que el servidor no podría operar sin un socket válido.  

Luego se activa la opción `SO_REUSEADDR` mediante `setsockopt()`, lo que permite **reutilizar el puerto 9000** inmediatamente después de cerrar el programa, incluso si el socket anterior sigue en estado `TIME_WAIT`.  

Esto refleja una característica importante del **comportamiento de TCP**:  
el sistema mantiene las conexiones cerradas durante un breve tiempo para evitar que paquetes antiguos interfieran con nuevas sesiones.  
Al habilitar `SO_REUSEADDR`, el *broker* puede reiniciarse sin esperar ese intervalo.

###  2. Asociación del socket con dirección y puerto (`bind()`)

<img width="773" height="169" alt="image" src="https://github.com/user-attachments/assets/9accddda-4e16-4594-a860-b8d40a442073" />

Este bloque asocia el socket del *broker* con una dirección IP y un puerto específicos.  
Se utiliza la estructura `sockaddr_in` para configurar los parámetros de red y la función `bind()` para vincular el socket con el sistema operativo.

Primero se limpia la estructura con `memset()` para evitar datos residuales.  
Luego se define:
- `sin_family = AF_INET`, indicando que se usará la familia de direcciones **IPv4**.  
- `sin_addr.s_addr = htonl(INADDR_ANY)`, lo que permite aceptar conexiones desde **cualquier interfaz de red** (equivale a 0.0.0.0).  
- `sin_port = htons(port)`, asignando el **puerto TCP 9000**. La conversión `htons()` asegura el orden correcto de bytes (*big-endian*), tal como exige el protocolo TCP/IP.

La llamada `bind()` vincula estos valores al socket.  
A partir de este punto, el servidor queda **reservando el puerto 9000** para futuras conexiones.

**Comportamiento TCP reflejado:**  
El socket pasa del estado **CLOSED** a **BOUND** dentro de la máquina.  
Aunque aún no hay conexiones activas, el sistema ya sabe que este proceso será el encargado de aceptar peticiones entrantes en ese puerto.


### 3. Escucha de conexiones entrantes (`listen()`)

<img width="710" height="484" alt="image" src="https://github.com/user-attachments/assets/e6234b37-cb3b-4e03-b6b0-b6f82a155b15" />

**Escucha de conexiones (`listen`)**  
Al invocar `listen(..., 16)` el socket del broker pasa de **BOUND** a **LISTEN**.  
El segundo parámetro (=16) define el **backlog**, es decir, el tamaño máximo de la cola de peticiones **SYN** pendientes que el kernel mantiene mientras el servidor hace `accept()`.  
Desde este momento el SO puede completar el *three-way handshake* y dejar conexiones listas para que el proceso las acepte.

**Estructuras de estado**  
Para manejar múltiples clientes simultáneamente el broker reserva e inicializa:

- `clients[]` → descriptor de socket por cliente (con `-1` indicando “libre”).  
- `roles[]` → rol de cada conexión: `UNKNOWN`, `PUB` o `SUB`.  
- `topics[][]` → *topic* suscrito por cada cliente (cadena vacía = sin suscripción).

El bucle de inicialización pone **todas las entradas** en “vacías”: `clients[i] = -1`, `roles[i] = UNKNOWN`, `topics[i][0] = '\0'`.

**Preparación para multiplexación con `select()`**  
Se configuran las máscaras de descriptores:

- `fd_set master, readfds` → conjuntos de fds; `master` es el conjunto base.  
- `FD_ZERO(&master)` → limpia la máscara.  
- `FD_SET(listener, &master)` → añade el **socket de escucha** para vigilar nuevas conexiones.  
- `int fdmax = listener` → guarda el **mayor fd** observado (requerido por `select`).

**Relación con TCP**  
- Con `listen`, el kernel acepta **handshakes TCP** y encola conexiones a la espera de que el proceso haga `accept()`.  
- Con `select`, cuando el **socket de escucha** está “listo para lectura”, significa que **hay una conexión establecida** en la cola; cuando un **socket de cliente** está “listo para lectura”, significa **datos disponibles** o **cierre del peer**.  
- Estas estructuras permiten al broker **multiplexar** muchas conexiones TCP en un solo hilo.

### 4 Bucle principal del broker
### 4.1 Bucle principal: “esperar y atender” con `select()`

<img width="478" height="191" alt="image" src="https://github.com/user-attachments/assets/e9159304-758b-43b8-845f-2ef971bed650" />

**Qué hace este bloque:**  
- Entra a un bucle infinito y **se queda esperando** hasta que ocurra algo en cualquiera de los sockets (nueva conexión o datos de un cliente).
- Cuando algo pasa, **sabe exactamente en qué socket fue** y lo atiende.

**Piezas clave:**

- `readfds = master`  
  Hacemos una **copia** del conjunto de sockets que estamos vigilando. `select()` modifica esa copia, por eso no usamos `master` directo.

- `ready = select(fdmax + 1, &readfds, NULL, NULL, NULL)`  
  `select()` **bloquea** el programa hasta que haya actividad de **lectura** en al menos un socket.  
  - Si vuelve con un número `ready > 0`, significa “hay `ready` sockets con algo para leer”.

- `for (fd = 0; fd <= fdmax && ready > 0; fd++) { … }`  
  Recorremos todos los descriptores **y solo procesamos** los que `select()` marcó como “listos”.

- `FD_ISSET(fd, &readfds)`  
  Pregunta: **¿este socket tiene algo?**  
  - Si **sí** y es el **socket de escucha**, significa **nueva conexión** pendiente → la atenderemos con `accept()`.  
  - Si **sí** y es un **socket de cliente**, significa **datos recibidos** o que el cliente **cerró** → lo leeremos con `recv()`.

**Que hace `select()` aquí:**  
Nos permite **vigilar muchos sockets a la vez** en un solo hilo: el broker **no se bloquea** esperando a un cliente en particular; **reacciona** al que tenga actividad.


### 4.2 Cuando `select()` señala al *listener*: aceptar una nueva conexión (`accept()`)

<img width="548" height="459" alt="image" src="https://github.com/user-attachments/assets/9ab5ec5a-9971-45cf-b53a-47fdb480e6a3" />

**Qué pasa aquí:**  
Si el descriptor “listo” es el **socket de escucha**, significa que hay un cliente que terminó el **handshake TCP** y está esperando ser atendido.  
El broker llama a `accept()` para **extraer** esa conexión de la cola y obtener un **nuevo socket** exclusivo para ese cliente.

**Pasos del bloque:**

- `accept(listener, …)`  
  Crea un **socket de cliente** (`cfd`) distinto del `listener`.  
  - Si falla, se registra el error y se continúa (no detiene el servidor).

- `inet_ntop(...)` + `ntohs(cli.sin_port)`  
  Convierte la IP y el puerto del cliente a texto para el log.  
  (Útil para depurar y ver quién se conectó).

- Buscar hueco en las tablas de estado  
  Recorre `clients[]` para encontrar una posición libre (`-1`):
  - `clients[i] = cfd` → guarda el **fd** del cliente.  
  - `roles[i] = UNKNOWN` → aún no sabemos si será **PUB** o **SUB**.  
  - `topics[i][0] = '\0'` → sin topic asignado por ahora.  
  - `FD_SET(cfd, &master)` → añade el nuevo socket al conjunto vigilado por `select()`.  
  - `fdmax = max(fdmax, cfd)` → actualiza el mayor fd para futuras llamadas a `select()`.

- Sin espacio disponible  
  Si no hay lugares libres en `clients[]`, se **cierra** el socket (`close(cfd)`) y se informa “capacidad llena”.

**Idea clave:**  
- El *listener* solo **recibe** conexiones.  
- Cada cliente real trabaja con **su propio socket (cfd)**.  
- Al agregar `cfd` a `master`, el broker podrá detectar **datos** de ese cliente en iteraciones futuras de `select()`.

**Cómo encaja con TCP:**  
- `accept()` se llama **después** del handshake (SYN → SYN/ACK → ACK).  
- A partir de aquí, el cliente y el broker tienen un **canal dedicado** para intercambiar mensajes de aplicación.

### 4.3 Recepción de datos o cierre de conexión (`recv()`)

<img width="526" height="327" alt="image" src="https://github.com/user-attachments/assets/8b485977-897e-44c2-826d-88d60fcfe133" />

**Qué pasa en este bloque:**  
Si el descriptor “listo” **no es el listener**, significa que un cliente existente (PUB o SUB) ha enviado datos o ha cerrado su conexión.  
Aquí el broker usa `recv()` para **leer** los datos o detectar un cierre.

**Explicación paso a paso:**

- `recv(fd, buf, sizeof(buf) - 1, 0)`  
  Intenta leer datos del socket del cliente y los guarda en `buf`.  
  - Si devuelve un número positivo → se recibieron bytes.  
  - Si devuelve `0` → el cliente **cerró la conexión**.  
  - Si devuelve negativo → ocurrió un **error** en la lectura.

- Si `n <= 0`  
  El cliente ya **no está disponible**.  
  - Se imprime el mensaje de desconexión.  
  - Se elimina el socket del conjunto `master` con `FD_CLR(fd, &master)`.  
  - Se cierra el descriptor con `close(fd)` para liberar recursos.

- Limpieza del registro del cliente  
  Se busca el índice correspondiente en `clients[]` y se reinician sus valores:
  - `clients[i] = -1` → marca la posición como libre.  
  - `roles[i] = UNKNOWN` → borra su rol previo.  
  - `topics[i][0] = '\0'` → borra su topic suscrito.

**Comportamiento TCP que refleja:**  
- Cuando el cliente termina su conexión, TCP realiza el cierre con el intercambio **FIN / ACK**, y `recv()` devuelve `0`.  
- Este bloque maneja correctamente ese evento, manteniendo actualizado el estado del broker y dejando espacio libre para nuevos clientes.  

### 4.4 Procesamiento de los datos recibidos (`recv()` completo)

<img width="528" height="314" alt="image" src="https://github.com/user-attachments/assets/78115a4e-d2c4-4f6c-8428-2fc21237d614" />

**Qué hace este bloque:**  
Cuando `recv()` devuelve datos, el broker debe analizarlos.  
TCP entrega los mensajes como un flujo continuo, por lo que este bloque se encarga de **dividirlo en líneas completas** y procesarlas una a una.

**Pasos del bloque:**

- `buf[n] = '\0'`  
  Se agrega el carácter nulo al final del texto recibido para poder tratarlo como una cadena.

- **Identificación del cliente:**  
  Se busca el índice correspondiente en `clients[]` para saber qué conexión envió el mensaje.  
  Si no se encuentra, se registra una advertencia y se continúa (caso raro).

- **Separación de líneas:**  
  Se usa `strtok_r()` con el separador `"\r\n"` para dividir el texto recibido en múltiples líneas.  
  Esto es importante porque en una misma llamada de `recv()` pueden llegar varios mensajes.

- **Filtrado de líneas vacías:**  
  Se ignoran las cadenas vacías para evitar procesar saltos de línea residuales.

- **Impresión de mensajes recibidos:**  
  Se muestra en consola el contenido de cada línea con su descriptor de cliente (`fd`).

- **Identificación del tipo de mensaje:**  
  - Si la línea es `"ROLE: PUB"`, el cliente se marca como **publicador** (`roles[idx] = PUB`) y se responde con un `ACK`.  
  - Si la línea es `"ROLE: SUB"`, se marca como **suscriptor** (`roles[idx] = SUB`) y también se confirma con `ACK`.

**Relación con el funcionamiento de TCP:**  
TCP no garantiza que los mensajes lleguen en el mismo tamaño o agrupación con que se enviaron.  
Por eso, este bloque “rearma” el contenido recibido en mensajes completos, línea a línea, permitiendo al broker **interpretar correctamente los comandos** enviados por cada cliente.

**En resumen:**  
Este código transforma el flujo TCP en **mensajes discretos** y clasifica a cada cliente según su rol (publicador o suscriptor), asegurando que el broker siempre entienda qué tipo de cliente se acaba de conectar.

### 4.5 Procesamiento de comandos `SUB` y `PUB`

<img width="461" height="479" alt="image" src="https://github.com/user-attachments/assets/8e764f8a-6e8d-4e0d-bdd2-48088d34f9a6" />

Este bloque permite al broker interpretar y manejar los comandos enviados por los clientes, diferenciando entre suscriptores (SUB) y publicadores (PUB). Cada mensaje recibido se analiza y se actúa en consecuencia.

#### Comando SUB: <topic>
Cuando un cliente envía un mensaje que comienza con `SUB:`, el broker interpreta que desea suscribirse a un tema específico.  
Mediante `strncmp(line, "SUB: ", 5)` se detecta el comando y con `snprintf(topics[idx], ...)` se guarda el nombre del topic en el que el cliente desea recibir mensajes.  
Luego, el broker confirma la suscripción enviando un mensaje de respuesta `"SUBOK"` al cliente.

Este proceso aprovecha la confiabilidad del protocolo TCP: los datos llegan completos y en el mismo orden en que fueron enviados, permitiendo registrar correctamente las suscripciones sin pérdidas ni duplicaciones.

#### Comando PUB: <topic>|<mensaje>
Cuando un cliente identificado como publicador envía un mensaje que comienza con `PUB:`, el broker lo procesa como una publicación.  
Primero, el código separa el nombre del topic y el contenido del mensaje usando `strchr(payload, '|')`. El texto antes del separador representa el topic, y el texto después el mensaje.  
Luego, se construye una cadena con el formato `"MSG: <topic> <mensaje>"`, que se reenviará a todos los suscriptores del mismo topic.

El broker recorre la lista de clientes:
- Verifica si el cliente está activo (`clients[i] > 0`),
- Comprueba que su rol sea `SUB`,
- Comprueba que el topic coincida (`strcmp(topics[i], topic) == 0`).

A los clientes que cumplan estas condiciones se les envía el mensaje mediante `send()`. Finalmente, se imprime en consola cuántos suscriptores lo recibieron y se envía un `"ACK"` al publicador para confirmar que su publicación fue procesada.

#### Relación con el funcionamiento de TCP
El comportamiento de este bloque depende directamente de las características del protocolo TCP:
- TCP garantiza **entrega confiable**, asegurando que todos los bytes del mensaje se reciban correctamente.
- TCP mantiene el **orden** de los datos, lo que permite que las publicaciones lleguen a los suscriptores en la misma secuencia en que fueron enviadas.
- TCP conserva una **conexión persistente**, de modo que los publicadores y suscriptores pueden enviar y recibir mensajes continuamente sin necesidad de reconectarse.

En conjunto, estas propiedades permiten que el broker funcione como un sistema de mensajería estable y seguro, distribuyendo información en tiempo real sin pérdidas, duplicaciones ni desorden en las entregas.

## Publisher (publisher_tcp.c) – Diseño y funciones clave

### 1. Publisher – Funciones auxiliares

<img width="401" height="298" alt="image" src="https://github.com/user-attachments/assets/f902e50b-379d-4910-b6c3-06c409a1693f" />

#### `sanitize(char *s)`
Normaliza el texto de un topic o nombre de equipo para que sea consistente al compararlo:
- Recorre la cadena y reemplaza los espacios `' '` por guiones bajos `'_'`.
- Elimina saltos de línea (`'\r'` o `'\n'`) cortando la cadena en ese punto (`'\0'`).
Esto evita discrepancias al usar `strcmp()` entre lo que publica el publisher y lo que suscribe el subscriber.

#### `send_line(int fd, const char *line)`
Envía una línea de texto por el socket TCP y trata de leer una respuesta rápida:
- Calcula el largo con `strlen` y usa `send(fd, line, L, 0)` para transmitir la línea completa.
- Intenta leer un posible `ACK` del broker con `recv(fd, resp, ..., MSG_DONTWAIT)`.  
  El flag `MSG_DONTWAIT` evita bloquearse si todavía no llegó la respuesta.
- Si llega algo, se termina en `'\0'` para poder imprimirlo (opcional).

Relación con TCP:
- `send()` escribe en el flujo confiable de TCP; el sistema se encarga de segmentar, retransmitir y mantener el orden.
- `recv(..., MSG_DONTWAIT)` refleja que la aplicación no necesita esperar bloqueada por la respuesta: TCP entregará los bytes tan pronto como estén disponibles.

#### `rnd(int a, int b)`
Genera un entero pseudoaleatorio en el rango `[a, b]`.  
Se usa para simular eventos del partido (goles, tarjetas, etc.) con probabilidades controladas.

### 2. Publisher – Bloque principal de inicialización (`main` - argumentos y configuración)

<img width="637" height="288" alt="image" src="https://github.com/user-attachments/assets/46a46bb7-c98d-467d-aeaa-8477654b4ecf" />

Este bloque configura los parámetros iniciales del publicador a partir de los argumentos de ejecución que el usuario pasa por consola.  
El programa se invoca con la siguiente estructura:

./publisher_tcp <ip_broker> <puerto> "EquipoA" "EquipoB" [duracion_min=90] [ms_por_min=300]


**Validación de argumentos:**  
Si el número de argumentos es menor a 5, el programa muestra el formato correcto de uso y termina con `EXIT_FAILURE`.  
Esto evita que se ejecute sin la información mínima requerida.

**Asignación de variables básicas:**
- `ip` y `port` definen la dirección y el puerto del broker TCP.  
- `teamA` y `teamB` guardan los nombres de los dos equipos del partido.  
- `duration` indica cuántos minutos simulará el juego (por defecto 90).  
- `ms_per_min` define el tiempo en milisegundos que dura un “minuto” simulado (por defecto 300 ms).

**Creación del topic:**
Se genera un nombre de topic con el formato `"EquipoA_vs_EquipoB"`.  
Antes, se normalizan los nombres de los equipos con `sanitize()`, que reemplaza espacios por guiones bajos y elimina saltos de línea, garantizando que el topic no contenga caracteres problemáticos.  
El nombre se crea con `snprintf(topic, sizeof(topic), "%s_vs_%s", teamA, teamB)`.

**Relación con TCP:**
En este punto todavía no se ha abierto la conexión, pero toda esta configuración prepara los datos que luego se enviarán al broker mediante un socket TCP.  
Esta inicialización es importante porque define el *contexto lógico* de la sesión: una sola conexión TCP se usará para publicar todos los eventos relacionados con este partido (topic).


### 3. Publisher – Conexión TCP y envío inicial

<img width="435" height="324" alt="image" src="https://github.com/user-attachments/assets/323769d5-669c-4e5e-870f-2d8c2720764e" />

Este bloque establece la conexión TCP con el broker y envía los primeros mensajes de identificación y estado.

**Creación del socket TCP**
Se crea un socket con `socket(AF_INET, SOCK_STREAM, 0)`.  
- `AF_INET` indica que se usará la familia de direcciones IPv4.  
- `SOCK_STREAM` define que la conexión será TCP (orientada a flujo y confiable).  
Si la creación falla, se invoca `die("socket")` y el programa termina.

**Configuración del destino (`struct sockaddr_in`)**
Se prepara la estructura `srv` con los datos del broker:
- `srv.sin_family = AF_INET` define que es una conexión IPv4.  
- `srv.sin_port = htons(port)` convierte el puerto al formato de red (big-endian).  
- `inet_pton(AF_INET, ip, &srv.sin_addr)` convierte la dirección IP de texto a formato binario.

**Establecimiento de la conexión**
`connect(fd, (struct sockaddr*)&srv, sizeof(srv))` inicia la conexión TCP con el broker.  
Si es exitosa, se imprime un mensaje confirmando la conexión con la IP, puerto y el topic asignado.  
En este punto, el *three-way handshake* de TCP ya se completó y la sesión está establecida.

**Identificación del rol**
El publicador envía su rol al broker con `send_line(fd, "ROLE: PUB\n")`.  
Esto le indica al broker que esta conexión será usada para publicar mensajes y no para suscribirse.

**Inicialización de la simulación**
Se establece una semilla aleatoria con `srand((unsigned)time(NULL) ^ getpid())` para variar los eventos del partido.  
Luego se envía el primer mensaje:
```c
snprintf(out, sizeof(out), "PUB: %s|Inicio del partido\n", topic);
send_line(fd, out);
```
### 4. Publisher – Simulación del partido y envío de eventos

<img width="541" height="470" alt="image" src="https://github.com/user-attachments/assets/46b3a4d5-64d7-49b9-8c4f-5bec083e6c4b" />

Este bloque implementa el ciclo principal que simula el desarrollo del partido.  
Mediante un bucle que avanza minuto a minuto, el publicador genera eventos aleatorios y los envía al broker a través de la conexión TCP establecida.

En cada iteración del ciclo, se usa la función `rnd(a, b)` para decidir de forma aleatoria qué tipo de evento ocurre.  
Entre los eventos posibles se incluyen goles, tarjetas amarillas, tarjetas rojas, cambios de jugador, tiros de esquina u otras situaciones del partido.  
Cada evento se formatea en una cadena de texto con información sobre el minuto y el equipo correspondiente, y se envía con la función `send_line()`, que escribe el mensaje en el socket TCP.

Gracias a este mecanismo, el broker recibe los eventos en tiempo real y los retransmite a todos los suscriptores interesados en el mismo topic.  
El uso de TCP garantiza que todos los mensajes lleguen de manera confiable y en el mismo orden en que fueron generados, manteniendo la coherencia de la simulación.

En resumen, este bloque transforma la simulación del juego en un flujo continuo de mensajes transmitidos por una conexión TCP persistente, asegurando una comunicación estable entre publicador, broker y suscriptores durante toda la duración del partido.

### 5. Publisher – Cierre de la simulación y fin de la conexión

<img width="541" height="124" alt="image" src="https://github.com/user-attachments/assets/4a09b66a-f03b-46b3-90c7-6837227ecfe0" />

Al finalizar el ciclo de simulación, el publicador envía un último mensaje al broker con el resultado final del partido.  
El mensaje se construye con `snprintf()` y se envía con `send_line()`, usando el formato:
```
PUB: <topic>|Final del partido – Resultado <EquipoA> <golesA> - <golesB> <EquipoB>
```

Esto permite que todos los suscriptores reciban el resumen final del encuentro antes de que la conexión se cierre.

Después del envío, el socket se libera mediante `close(fd)`, finalizando la comunicación con el broker.  
Por último, se imprime un mensaje local confirmando que el partido ha terminado y mostrando el marcador final.

Este bloque refleja el cierre natural de una sesión TCP:  
el publicador envía sus últimos datos, el broker los recibe, y luego la conexión se cierra de forma ordenada.  
Gracias al protocolo TCP, el cierre asegura que todos los mensajes pendientes sean entregados antes de liberar los recursos.

## Suscriber (suscriber_tcp.c) – Diseño y funciones clave

### 1. Subscriber – Inicialización y conexión

<img width="521" height="272" alt="image" src="https://github.com/user-attachments/assets/25b0f733-447b-409a-86dc-dfb6f8f79d2b" />

Este bloque configura los parámetros básicos del suscriptor y establece la conexión TCP con el broker.

**Validación de argumentos**
El programa espera exactamente 4 argumentos:

```
./subscriber_tcp <ip_broker> <puerto> <partido>
```

Si no se cumplen, se imprime el formato correcto de uso y el programa finaliza.  
Así se asegura que el suscriptor tenga la información mínima necesaria: adónde conectarse y a qué topic (partido) suscribirse.

**Creación del socket TCP**
`socket(AF_INET, SOCK_STREAM, 0)` crea un socket de tipo **TCP** (flujo confiable).  
Si falla, el programa finaliza, ya que no podría comunicarse con el broker.

**Configuración de la dirección del broker**
Se completa una estructura `sockaddr_in`:
- `sin_family = AF_INET` para IPv4,
- `sin_port = htons(port)` convierte el puerto al formato de red,
- `inet_pton(AF_INET, ip, &srv.sin_addr)` pasa la IP de texto a binario.

**Establecimiento de la conexión**
`connect(fd, (struct sockaddr*)&srv, sizeof(srv))` inicia la conexión con el broker.  
Si es exitosa, el *three-way handshake* de TCP (SYN, SYN/ACK, ACK) ya se ha completado y el canal de comunicación queda listo para enviar/recibir datos.  
Se imprime un mensaje indicando que el suscriptor está conectado a la IP y al puerto del broker.

Esta fase refleja el uso típico de TCP en un cliente: crear socket, configurar la dirección remota y conectarse, obteniendo un flujo confiable por el que se intercambiarán los mensajes de control y los eventos publicados.

### 2. Subscriber – Identificación y suscripción

<img width="361" height="137" alt="image" src="https://github.com/user-attachments/assets/913efc7a-d8d5-495a-ac19-856a762728bd" />

Después de establecer la conexión con el broker, el suscriptor debe identificarse y especificar el topic al que quiere recibir mensajes.

**Envío del rol**
Se construye un mensaje `"ROLE: SUB\n"` y se envía con `send(fd, role, strlen(role), 0)`.  
Esto le indica al broker que esta conexión será utilizada para **recibir** publicaciones, no para enviarlas.  
Si el envío falla, el programa se detiene, ya que sin este paso el broker no podrá clasificar correctamente la conexión.

**Envío de la suscripción**
Luego se genera otro mensaje con el formato `"SUB: <topic>\n"`, donde `<topic>` corresponde al partido o tema de interés.  
Se usa `snprintf()` para construir la cadena y `send()` para transmitirla al broker.  
Finalmente, se imprime en consola una confirmación de la suscripción.

**Relación con el funcionamiento de TCP**
Ambos mensajes se envían a través de la misma conexión TCP ya establecida.  
TCP garantiza que:
- Los bytes se entregan **en orden**, por lo que el broker siempre procesará primero el rol y luego la suscripción.
- La entrega es **confiable**, asegurando que los comandos no se pierdan ni se corrompan.

Gracias a esto, el broker puede mantener la sesión abierta y lista para enviar mensajes a este suscriptor cada vez que un publicador publique un evento en el topic correspondiente.

### 3. Subscriber – Recepción de mensajes del broker

<img width="383" height="155" alt="image" src="https://github.com/user-attachments/assets/b2a6535b-e959-4644-972b-765dd45f13f4" />

En este bloque, el suscriptor entra en un bucle permanente donde espera mensajes provenientes del broker.  
Una vez que la suscripción ha sido registrada, la conexión TCP se mantiene abierta para recibir cualquier publicación nueva que el broker reenvíe.

**Recepción de datos**
El comando `recv(fd, buf, sizeof(buf) - 1, 0)` lee los datos que llegan por el socket y los guarda en el búfer `buf`.  
- Si devuelve un número positivo, significa que llegaron datos.  
- Si devuelve `0`, la conexión fue cerrada por el broker.  
- Si devuelve un valor negativo, ocurrió un error en la recepción.

Al recibir datos válidos, se agrega el carácter nulo `'\0'` para terminar la cadena y poder imprimirla de forma legible en consola.  
Luego se muestra en pantalla el mensaje recibido, que usualmente tendrá el formato:

```
[SUB] <- MSG: <topic> <mensaje>
```

**Cierre ordenado**
Si `recv()` devuelve `0` o un error, el suscriptor sale del bucle, cierra el socket con `close(fd)` y termina el programa.

**Relación con el comportamiento de TCP**
- TCP mantiene una conexión **persistente** y **bidireccional**: el broker puede enviar mensajes en cualquier momento sin que el cliente deba solicitar cada uno.  
- Los datos llegan **en orden y sin pérdidas**, garantizando que los eventos del publicador sean recibidos por los suscriptores en la misma secuencia en la que fueron emitidos.  
- Cuando el broker cierra la conexión, el suscriptor lo detecta automáticamente porque `recv()` devuelve `0`.

Este mecanismo permite que el suscriptor funcione como un receptor en tiempo real de todos los mensajes relacionados con el topic al que se unió.

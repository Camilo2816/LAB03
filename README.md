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



<img width="526" height="327" alt="image" src="https://github.com/user-attachments/assets/8b485977-897e-44c2-826d-88d60fcfe133" />



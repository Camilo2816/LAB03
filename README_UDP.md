# LAB03 – Pub/Sub TCP (Broker, Publisher y Subscriber) #
Documentación de la implementación en C de un sistema Publish/Subscribe sobre UDP usando sockets BSD en Linux (VM Ubuntu).
Componentes: broker_udp.c, publisher_udp.c, subscriber_udp.c.

## Arquitectura general ##
<img width="441" height="81" alt="image" src="https://github.com/user-attachments/assets/7bec76b4-a8db-44a3-bc54-dd266130f7c0" />

- Broker: recibe datagramas PUB: <topic>|<mensaje> del publisher y los reenvía con formato MSG: <topic>|<mensaje> a todos los suscriptores registrados para ese <topic>.

- Publisher: envía datagramas al broker con publicaciones para un <topic>.

-Subscriber: registra su interés con un datagrama SUB: <topic> y luego recibe datagramas MSG: que coincidan con ese <topic>.

## Cómo refleja TCP el comportamiento del sistema ##

- Sin conexión: no existe 3-way handshake. El broker hace bind() y escucha datagramas; publisher/subscriber usan sendto() directo.

- Entrega no garantizada y posible desorden: los datagramas pueden perderse, duplicarse o llegar fuera de orden; la aplicación debe tolerarlo.

- Menor overhead: cabecera UDP 8 bytes (vs TCP 20 bytes). En Wireshark verás únicamente UDP (sin SYN/ACK/FIN).

- Estado ligero en el broker: mantiene un mapa topic → conjunto de endpoints (IP:PUERTO) para reenviar, pero no hay estado de conexión por cliente.

## Compilación y ejecución ##

**1. Desde la carpeta UDP:**
   
gcc -Wall -O2 -o broker_udp     broker_udp.c

gcc -Wall -O2 -o publisher_udp  publisher_udp.c

gcc -Wall -O2 -o subscriber_udp subscriber_udp.c


**Broker (por defecto en puerto 5001)**

./broker_udp

**3. Subscribers**

./subscriber_udp 127.0.0.1 5001 Real_Azul_vs_Atletico_Rojo

./subscriber_udp 127.0.0.1 5001 Verde_CF_vs_Universidad_FC

**4. Publishers (simulador de partido)**

./publisher_udp 127.0.0.1 5001 Real_Azul_vs_Atletico_Rojo 10

./publisher_udp 127.0.0.1 9000 Verde_CF_vs_Universidad_FC 10

**Capturas en Wireshark (UDP)**

- Capturas en Wireshark (UDP)
- Interfaz: lo (Loopback).
- Filtro: udp.port == 5001.
- Guarda la traza como: captures/udp_pubsub.pcap.


## Broker (broker_tcp.c) – Diseño y funciones clave

### 1.1 Socket y bind()

<img width="625" height="371" alt="image" src="https://github.com/user-attachments/assets/c428436c-1f77-40b1-aa39-150d7d7c2d4e" />


En este bloque se crea el socket UDP que usará el broker para recibir datagramas.
La llamada socket(AF_INET, SOCK_DGRAM, 0) inicializa un endpoint IPv4 de tipo datagrama (UDP). Si la creación falla, se aborta porque el servidor no podría operar.

Luego se configura la dirección local:

- sin_family = AF_INET (IPv4),
- sin_addr.s_addr = INADDR_ANY (0.0.0.0 para escuchar en todas las interfaces),
- sin_port = htons(PUERTO) para usar el puerto 5001.

Con bind() se asocia el socket al puerto indicado.
A diferencia de TCP, UDP no usa listen() ni accept(): tras el bind, el broker ya puede recibir datagramas con recvfrom().

Comportamiento UDP reflejado: no hay 3-way handshake ni estado de conexión. El broker queda listo para recibir paquetes sin establecer sesiones.

###  2. Broker UDP — bucle principal con recvfrom() / sendto()

<img width="692" height="794" alt="image" src="https://github.com/user-attachments/assets/9a17bfb6-de59-4af1-a95c-5b7bea4a827b" />


Aquí el broker entra en un bucle infinito:

Recibe un datagrama con recvfrom(), que entrega:
- el payload (texto del mensaje),
- la dirección de origen del cliente (IP:puerto).

Clasifica el mensaje:
- Si empieza con SUB: → extrae el <topic> y registra al remitente en la tabla interna de suscriptores (arreglo subs[] con activo, tema y addr).
Esto hace que el broker sepa a qué dirección reenviar futuros mensajes de ese topic.
- Si empieza con PUB: → separa <topic> y <mensaje> (usando el carácter |).
Luego construye MSG:<topic>|<mensaje> y lo reenvía con sendto() a cada suscriptor cuyo tema coincida exactamente.

Detalles importantes del diseño:
- El estado es ligero: el broker no mantiene sockets por cliente; solo guarda (topic, IP:puerto) de los SUB.

- El fan-out se hace con un sendto() por cada suscriptor del topic.

- Como UDP no garantiza entrega ni orden, el broker no reintenta ni reordena: solo reenvía lo que recib


### 3. Publisher UDP — envío de N eventos

<img width="814" height="654" alt="image" src="https://github.com/user-attachments/assets/cce537d5-36f2-43c5-a07f-ea79aa277d22" />
El subscriber crea su socket UDP, arma la dirección del broker y envía una vez:

Así, el broker registra su (IP:puerto) dentro del conjunto de ese <topic>.

Luego queda en un bucle con recvfrom() esperando datagramas MSG:<topic>|<mensaje> y los imprime.

Por qué es relevante: como UDP no garantiza orden/entrega, si la aplicación exige consistencia (p. ej., marcador siempre creciente) debe implementar número de secuencia o timestamp en el payload y aplicar solo si es más nuevo. Tu implementación base ya permite explicarlo, aunque no aplica reordenamiento (y está OK para el lab).

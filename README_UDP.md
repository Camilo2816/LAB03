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


### 3. Publisher UDP - Diseño y funciones clave

1. Creación del socket y configuración de la dirección del broker

<img width="550" height="210" alt="image" src="https://github.com/user-attachments/assets/060cb0eb-6bb8-408f-acae-1ab2dc554cb7" />


En esta primera parte se crea un socket UDP con socket(AF_INET, SOCK_DGRAM, 0) y se configura la dirección del broker.
El inet_pton() convierte la IP del broker (por ejemplo, 127.0.0.1) a formato binario.

A diferencia del publisher TCP, aquí no se realiza connect() porque UDP no establece conexiones persistentes.
Los mensajes se envían directamente con sendto() cada vez que sea necesario.

2. Envío de mensajes simulados

<img width="860" height="208" alt="image" src="https://github.com/user-attachments/assets/fb59a96c-52fb-4a1a-a6e5-c9b75cd30d37" />


Este fragmento simula el envío de varios eventos de un partido.
Cada evento es un datagrama independiente enviado con sendto().
El formato PUB:<topic>|Evento UDP #i permite al broker reconocer el tema y reenviar el mensaje a los suscriptores correctos.

Comportamiento UDP:
- Los mensajes se envían sin confirmación.
- Si uno se pierde, el publisher no se entera.
- El retardo (usleep) facilita observar el flujo en Wireshark.








### 3. Subscriber UDP - Diseño y funciones clave 

<img width="612" height="398" alt="image" src="https://github.com/user-attachments/assets/d2a29835-11ee-4f8b-ae28-a3fd02122612" />

En esta parte, el subscriber crea un socket UDP y envía un único datagrama al broker con el formato SUB:<topic>.
De esta manera, el broker agrega su dirección (IP y puerto) a la lista de suscriptores del tema correspondiente.

Comportamiento UDP:
- No hay handshake ni confirmación: el broker simplemente asume que el mensaje fue recibido.
- Este proceso es mucho más rápido que en TCP, pero también más vulnerable a pérdidas.

2. Recepción de mensajes del broker

<img width="630" height="205" alt="image" src="https://github.com/user-attachments/assets/a3875162-677d-4b93-ba75-99d64143a440" />

Después de suscribirse, el subscriber entra en un bucle infinito escuchando mensajes MSG: reenviados por el broker.
Cada datagrama recibido se imprime en consola, mostrando los eventos del partido o topic suscrito.

Relación con UDP:
- Cada recvfrom() recibe un datagrama completo (no parte de un flujo).
- Si el broker envía varios mensajes seguidos, podrían llegar desordenados o perderse.
- Para evitar inconsistencias, la aplicación podría agregar números de secuencia o timestamps.

Ejemplo de salida esperada:

<img width="593" height="50" alt="image" src="https://github.com/user-attachments/assets/eb401036-716a-45ee-83a2-ed28675340c1" />

### Capturas Wireshark (UDP)
1. Filtro y arranque de captura

En Wireshark, selecciona la interfaz lo (loopback) y aplica el filtro:

<img width="196" height="42" alt="image" src="https://github.com/user-attachments/assets/5e1afe89-d587-469c-9658-ec7dbf04e395" />

Al iniciar el broker y los clientes, se observarán directamente los datagramas UDP, sin el clásico handshake de TCP.

2. Suscripción (SUB:)

El subscriber envía un único datagrama con:

<img width="689" height="74" alt="image" src="https://github.com/user-attachments/assets/a3f4c224-f4ab-41f6-9d7d-12b8269a8e97" />

En Wireshark se verá un solo paquete que viaja desde el SUB hacia el broker.

El broker imprime en consola:

<img width="703" height="93" alt="image" src="https://github.com/user-attachments/assets/5fddebe2-edd8-41a2-b210-0530025d9e9e" />

3. Publicación y replicación

Cuando el publisher envía:

<img width="576" height="50" alt="image" src="https://github.com/user-attachments/assets/8b9931ac-f1f6-480b-9ac2-627dcb9931e2" />

El broker genera:

<img width="475" height="42" alt="image" src="https://github.com/user-attachments/assets/413beccb-e2cf-44e8-83bf-0ccad4b89583" />

y lo reenvía a todos los suscriptores del mismo tema.

En Wireshark se ven los datagramas PUB entrando al broker y los MSG saliendo hacia cada SUB, evidenciando el proceso de fan-out.


### Conclusión general

La implementación UDP demuestra un sistema ligero, rápido y sin conexión persistente, ideal para aplicaciones donde la baja latencia es más importante que la entrega confiable de cada mensaje, como transmisiones en vivo o notificaciones efímeras.

Sin embargo, esta simplicidad tiene un costo: al no garantizar orden ni entrega, el protocolo requiere que la aplicación gestione por sí misma mecanismos de control, como numeración de mensajes, timestamps o reenvíos, si se desea mantener consistencia entre los suscriptores.

Por otro lado, el enfoque TCP —implementado en la parte anterior del laboratorio— ofrece una comunicación más segura y ordenada, a costa de un mayor consumo de recursos y tiempos de establecimiento de conexión.

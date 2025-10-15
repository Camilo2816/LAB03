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

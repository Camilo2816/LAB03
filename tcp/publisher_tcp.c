// publisher_tcp.c — Simulador de partido: publica eventos para <EquipoA_vs_EquipoB>
// Compilar: gcc -Wall -O2 -o publisher_tcp publisher_tcp.c
// Uso:
//   ./publisher_tcp <ip_broker> <puerto> "<EquipoA>" "<EquipoB>" [duracion_min=90] [ms_por_min=300]
// Ejemplo:
//   ./publisher_tcp 127.0.0.1 9000 "Real Azul" "Atletico Rojo" 30 150
//
// Protocolo que ya tienes:
//   1) ROLE: PUB
//   2) PUB: <topic>|<mensaje>
//
// Nota: lee los ACKs del broker pero no bloquea si no llegan.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>

static void die(const char *m){ perror(m); exit(EXIT_FAILURE); }

static void sanitize(char *s) {
    // Reemplaza espacios por '_' y quita saltos de línea
    for (char *p=s; *p; ++p) {
        if (*p==' ') *p = '_';
        if (*p=='\r' || *p=='\n') *p = '\0';
    }
}

static void send_line(int fd, const char *line) {
    size_t L = strlen(line);
    if (send(fd, line, L, 0) < 0) die("send");
    // lee ACK si llega (no es crítico si no llega)
    char resp[128];
    ssize_t r = recv(fd, resp, sizeof(resp)-1, MSG_DONTWAIT);
    if (r > 0) { resp[r] = 0; /* printf("[PUB] <- %s", resp); */ }
}

static int rnd(int a, int b) {
    // entero en [a, b]
    return a + (rand() % (b - a + 1));
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
            "Uso: %s <ip_broker> <puerto> \"<EquipoA>\" \"<EquipoB>\" [duracion_min=90] [ms_por_min=300]\n",
            argv[0]);
        return EXIT_FAILURE;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    char teamA[128], teamB[128];
    snprintf(teamA, sizeof(teamA), "%s", argv[3]);
    snprintf(teamB, sizeof(teamB), "%s", argv[4]);
    int duration = (argc >= 6) ? atoi(argv[5]) : 90;       // minutos simulados
    int ms_per_min = (argc >= 7) ? atoi(argv[6]) : 300;    // cuánto tarda un "minuto" simulado

    // tema (topic): EquipoA_vs_EquipoB (espacios -> _)
    sanitize(teamA);
    sanitize(teamB);
    char topic[300];
    snprintf(topic, sizeof(topic), "%s_vs_%s", teamA, teamB);

    // socket TCP
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) <= 0) die("inet_pton");

    if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) die("connect");
    printf("[PUB] Conectado a %s:%d (topic=%s)\n", ip, port, topic);

    // rol
    send_line(fd, "ROLE: PUB\n");

    // semilla aleatoria
    srand((unsigned)time(NULL) ^ getpid());

    int golesA = 0, golesB = 0;
    char out[512];

    // inicio
    snprintf(out, sizeof(out), "PUB: %s|Inicio del partido\n", topic);
    send_line(fd, out);

    // bucle del "minutero" simulado
    for (int min = 1; min <= duration; ++min) {
        // Probabilidades (ajústalas si quieres):
        // Nota: todas son independientes; puede haber 0..N eventos en el mismo minuto.
        if (rnd(1,100) <= 6) { // gol 6%
            int lado = rnd(0,1);
            if (lado==0) {
                golesA++;
                snprintf(out, sizeof(out), "PUB: %s|Gol de %s al %d' (marcador %d-%d)\n",
                         topic, teamA, min, golesA, golesB);
            } else {
                golesB++;
                snprintf(out, sizeof(out), "PUB: %s|Gol de %s al %d' (marcador %d-%d)\n",
                         topic, teamB, min, golesA, golesB);
            }
            send_line(fd, out);
        }

        if (rnd(1,100) <= 10) { // amarilla 10%
            int lado = rnd(0,1);
            int dorsal = rnd(2,11);
            snprintf(out, sizeof(out), "PUB: %s|Tarjeta amarilla #%d para %s al %d'\n",
                     topic, dorsal, lado?teamB:teamA, min);
            send_line(fd, out);
        }

        if (rnd(1,100) <= 2) { // roja 2%
            int lado = rnd(0,1);
            int dorsal = rnd(2,11);
            snprintf(out, sizeof(out), "PUB: %s|Tarjeta roja #%d para %s al %d'\n",
                     topic, dorsal, lado?teamB:teamA, min);
            send_line(fd, out);
        }

        if (rnd(1,100) <= 12) { // falta 12%
            int lado = rnd(0,1);
            snprintf(out, sizeof(out), "PUB: %s|Falta de %s al %d'\n",
                     topic, lado?teamB:teamA, min);
            send_line(fd, out);
        }

        if (rnd(1,100) <= 8) { // tiro de esquina 8%
            int lado = rnd(0,1);
            snprintf(out, sizeof(out), "PUB: %s|Tiro de esquina para %s al %d'\n",
                     topic, lado?teamB:teamA, min);
            send_line(fd, out);
        }

        if (rnd(1,100) <= 5) { // cambio 5%
            int sale = rnd(2,11);
            int entra = rnd(12,30);
            int lado = rnd(0,1);
            snprintf(out, sizeof(out), "PUB: %s|Cambio en %s: #%d por #%d al %d'\n",
                     topic, lado?teamB:teamA, sale, entra, min);
            send_line(fd, out);
        }

        // cada 15' manda un estado
        if (min % 15 == 0) {
            snprintf(out, sizeof(out), "PUB: %s|Marcador parcial %s %d - %d %s al %d'\n",
                     topic, teamA, golesA, golesB, teamB, min);
            send_line(fd, out);
        }

        // avanzar el "minuto" simulado
        usleep(ms_per_min * 1000);
    }

    // final
    snprintf(out, sizeof(out), "PUB: %s|Final del partido — Resultado %s %d - %d %s\n",
             topic, teamA, golesA, golesB, teamB);
    send_line(fd, out);

    close(fd);
    printf("[PUB] Partido terminado (%s %d - %d %s)\n", teamA, golesA, golesB, teamB);
    return 0;
}

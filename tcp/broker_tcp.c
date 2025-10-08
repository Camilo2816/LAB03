// broker_tcp.c — Broker TCP con select(), roles PUB/SUB, suscripciones y reenvío.
// Maneja múltiples líneas por recv() para no perder "ROLE: SUB" + "SUB: topic" juntas.
// Compilar: gcc -Wall -O2 -o broker_tcp broker_tcp.c
// Ejecutar: ./broker_tcp 9000

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS  FD_SETSIZE
#define BUF_SIZE     1024

enum Role { UNKNOWN = 0, PUB = 1, SUB = 2 };

static void die(const char *m){ perror(m); exit(EXIT_FAILURE); }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);

    // 1) socket() + SO_REUSEADDR
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) die("socket");
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2) bind()
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    addr.sin_port = htons(port);
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) die("bind");

    // 3) listen()
    if (listen(listener, 16) < 0) die("listen");
    printf("[BROKER] Escuchando en 0.0.0.0:%d (select)\n", port);

    // Estado de clientes
    int clients[MAX_CLIENTS];
    enum Role roles[MAX_CLIENTS];
    char topics[MAX_CLIENTS][128];

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
        roles[i] = UNKNOWN;
        topics[i][0] = '\0';
    }

    fd_set master, readfds;
    FD_ZERO(&master);
    FD_SET(listener, &master);
    int fdmax = listener;

    // 4) bucle principal
    for (;;) {
        readfds = master;
        int ready = select(fdmax + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            die("select");
        }

        for (int fd = 0; fd <= fdmax && ready > 0; fd++) {
            if (!FD_ISSET(fd, &readfds)) continue;
            ready--;

            if (fd == listener) {
                // —— Nueva conexión ——
                struct sockaddr_in cli; socklen_t clen = sizeof(cli);
                int cfd = accept(listener, (struct sockaddr*)&cli, &clen);
                if (cfd < 0) { perror("accept"); continue; }

                char ip[64];
                inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                printf("[BROKER] Nueva conexión %s:%d (cfd=%d)\n",
                       ip, ntohs(cli.sin_port), cfd);

                int placed = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] < 0) {
                        clients[i] = cfd;
                        roles[i]   = UNKNOWN;
                        topics[i][0] = '\0';
                        FD_SET(cfd, &master);
                        if (cfd > fdmax) fdmax = cfd;
                        placed = 1;
                        break;
                    }
                }
                if (!placed) {
                    fprintf(stderr, "[BROKER] Capacidad llena. Cerrando cfd=%d\n", cfd);
                    close(cfd);
                }

            } else {
                // —— Datos de un cliente existente ——
                char buf[BUF_SIZE];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    if (n < 0) perror("recv");
                    printf("[BROKER] Cliente fd=%d desconectado\n", fd);
                    FD_CLR(fd, &master);
                    close(fd);
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (clients[i] == fd) {
                            clients[i] = -1;
                            roles[i]   = UNKNOWN;
                            topics[i][0] = '\0';
                            break;
                        }
                    }
                    continue;
                }

                buf[n] = '\0';

                // ——— NUEVO: procesar TODAS las líneas recibidas en este recv() ———
                int idx = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i] == fd) { idx = i; break; }
                if (idx == -1) { fprintf(stderr, "[WARN] fd desconocido\n"); continue; }

                char *save = NULL;
                for (char *line = strtok_r(buf, "\r\n", &save);
                     line != NULL;
                     line = strtok_r(NULL, "\r\n", &save)) {

                    if (*line == '\0') continue; // saltar líneas vacías
                    printf("[BROKER] fd=%d -> \"%s\"\n", fd, line);

                    if (strcmp(line, "ROLE: PUB") == 0) {
                        roles[idx] = PUB;
                        send(fd, "ACK\n", 4, 0);

                    } else if (strcmp(line, "ROLE: SUB") == 0) {
                        roles[idx] = SUB;
                        send(fd, "ACK\n", 4, 0);

                    } else if (strncmp(line, "SUB: ", 5) == 0 && roles[idx] == SUB) {
                        snprintf(topics[idx], sizeof(topics[idx]), "%s", line + 5);
                        printf("[BROKER] fd=%d suscrito a \"%s\"\n", fd, topics[idx]);
                        send(fd, "SUBOK\n", 6, 0);

                    } else if (strncmp(line, "PUB: ", 5) == 0 && roles[idx] == PUB) {
                        // formato: PUB: <topic>|<mensaje>
                        char *payload = line + 5;
                        char *bar = strchr(payload, '|');
                        if (!bar) { send(fd, "ERR\n", 4, 0); continue; }
                        *bar = '\0';
                        const char *topic = payload;
                        const char *msg   = bar + 1;

                        char out[BUF_SIZE];
                        snprintf(out, sizeof(out), "MSG: %s|%s\n", topic, msg);

                        int delivered = 0;
                        for (int i = 0; i < MAX_CLIENTS; i++) {
                            if (clients[i] >= 0 &&
                                roles[i] == SUB &&
                                topics[i][0] != '\0' &&
                                strcmp(topics[i], topic) == 0) {
                                send(clients[i], out, strlen(out), 0);
                                delivered++;
                            }
                        }
                        printf("[BROKER] PUB topic=\"%s\" -> entregados=%d\n", topic, delivered);
                        send(fd, "ACK\n", 4, 0);

                    } else {
                        // comando desconocido
                        send(fd, "ACK\n", 4, 0);
                    }
                }
                // ——————————————————————————————————————————————————————————————
            }
        }
    }

    close(listener);
    return 0;
}

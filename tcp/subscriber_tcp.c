// subscriber_tcp.c — se declara SUB, envía SUB:<topic> y recibe mensajes
// Compilar: gcc -Wall -O2 -o subscriber_tcp subscriber_tcp.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static void die(const char *m){ perror(m); exit(EXIT_FAILURE); }

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <partido>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) <= 0) die("inet_pton");

    if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) die("connect");
    printf("[SUB] Conectado a %s:%d\n", ip, port);

    // 1️⃣ enviar rol
    const char *role = "ROLE: SUB\n";
    if (send(fd, role, strlen(role), 0) < 0) die("send role");

    // 2️⃣ enviar suscripción
    char line[256];
    snprintf(line, sizeof(line), "SUB: %s\n", topic);
    if (send(fd, line, strlen(line), 0) < 0) die("send sub");
    printf("[SUB] Enviada suscripción a \"%s\"\n", topic);

    // 3️⃣ escuchar mensajes del broker
    char buf[1024];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) { if (n < 0) perror("recv"); break; }
        buf[n] = '\0';
        printf("[SUB] <- %s", buf);
    }

    close(fd);
    return 0;
}

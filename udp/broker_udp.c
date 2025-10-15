#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PUERTO 5001
#define BUF 2048
#define MAX_SUBS 100
#define MAX_TEMA 128

typedef struct {
    int activo;
    char tema[MAX_TEMA];
    struct sockaddr_in addr;
} Subs;

int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    Subs subs[MAX_SUBS];
    char buffer[BUF];
    socklen_t len = sizeof(cliaddr);

    // Inicializar lista de subscriptores
    memset(subs, 0, sizeof(subs));

    // Crear socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PUERTO);

    // Enlazar puerto
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("[BROKER UDP] Escuchando en puerto %d...\n", PUERTO);

    // Bucle principal
    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, BUF - 1, 0,
                             (struct sockaddr *)&cliaddr, &len);
        if (n < 0) continue;

        buffer[n] = '\0';

        if (strncmp(buffer, "SUB:", 4) == 0) {
            const char *tema = buffer + 4;

            // Buscar espacio o actualizar
            int i;
            for (i = 0; i < MAX_SUBS; i++) {
                if (!subs[i].activo) {
                    subs[i].activo = 1;
                    strncpy(subs[i].tema, tema, MAX_TEMA - 1);
                    subs[i].addr = cliaddr;
                    printf("[BROKER UDP] Nueva suscripción '%s'\n", tema);
                    break;
                }
            }
        } else if (strncmp(buffer, "PUB:", 4) == 0) {
            char *p = strchr(buffer + 4, '|');
            if (!p) continue;
            *p = '\0';
            const char *tema = buffer + 4;
            const char *msg = p + 1;

            char out[BUF];
            snprintf(out, sizeof(out), "MSG:%s|%s", tema, msg);

            // reenviar a todos los subscriptores de ese tema
            for (int i = 0; i < MAX_SUBS; i++) {
                if (subs[i].activo && strcmp(subs[i].tema, tema) == 0) {
                    sendto(sockfd, out, strlen(out), 0,
                           (struct sockaddr *)&subs[i].addr, sizeof(subs[i].addr));
                }
            }
        }
    }

    close(sockfd);
    return 0;
}

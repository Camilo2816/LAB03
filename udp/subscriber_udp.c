#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUF 2048

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <topic>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &servaddr.sin_addr);

    // Enviar suscripción
    char submsg[256];
    snprintf(submsg, sizeof(submsg), "SUB:%s", topic);
    sendto(sockfd, submsg, strlen(submsg), 0,
           (struct sockaddr *)&servaddr, sizeof(servaddr));
    printf("[SUB UDP] Suscrito a '%s'\n", topic);

    // Escuchar mensajes
    char buf[BUF];
    for (;;) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (n > 0) {
            buf[n] = '\0';
            printf("[SUB UDP] <- %s\n", buf);
        }
    }

    close(sockfd);
    return 0;
}

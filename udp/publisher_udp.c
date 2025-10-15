#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <topic> <mensajes>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];
    int count = atoi(argv[4]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &servaddr.sin_addr);

    for (int i = 1; i <= count; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "PUB:%s|Evento UDP #%d", topic, i);
        sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        printf("[PUB UDP] %s\n", msg);
        usleep(300000); // 300 ms entre mensajes
    }

    close(sockfd);
    return 0;
}

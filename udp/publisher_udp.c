// publisher_udp.c — Simulador de partido en UDP
// Compilar: gcc -Wall -O2 -o publisher_udp publisher_udp.c
// Uso:
//   ./publisher_udp <ip_broker> <puerto> "<EquipoA>" "<EquipoB>" [duracion_min=20] [ms_por_min=300]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static void die(const char *m){ perror(m); exit(EXIT_FAILURE); }

static void sanitize(char *s) {
    for (char *p=s; *p; ++p) {
        if (*p==' ') *p = '_';
        if (*p=='\r' || *p=='\n') { *p = '\0'; break; }
    }
}

static int rnd(int a, int b) { return a + (rand() % (b - a + 1)); }

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
            "Uso:\n  %s <ip_broker> <puerto> \"<EquipoA>\" \"<EquipoB>\" [duracion_min=20] [ms_por_min=300]\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    char teamA[128], teamB[128];
    snprintf(teamA, sizeof(teamA), "%s", argv[3]);
    snprintf(teamB, sizeof(teamB), "%s", argv[4]);

    int duration  = (argc >= 6) ? atoi(argv[5]) : 20;   // minutos simulados (para pruebas)
    int ms_per_min = (argc >= 7) ? atoi(argv[6]) : 300; // cuánto dura un “minuto” simulado

    // topic: EquipoA_vs_EquipoB (sin espacios)
    char tA[128], tB[128];
    snprintf(tA, sizeof(tA), "%s", teamA);
    snprintf(tB, sizeof(tB), "%s", teamB);
    sanitize(tA); sanitize(tB);

    char topic[300];
    snprintf(topic, sizeof(topic), "%s_vs_%s", tA, tB);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) die("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0) die("inet_pton");

    srand((unsigned)time(NULL) ^ getpid());

    int golesA = 0, golesB = 0;
    char msg[512];

    // Inicio del partido
    snprintf(msg, sizeof(msg), "PUB:%s|Inicio del partido", topic);
    sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

    for (int min = 1; min <= duration; ++min) {
        // Gol ~6%
        if (rnd(1,100) <= 6) {
            int lado = rnd(0,1);
            if (lado==0) { golesA++; 
                snprintf(msg, sizeof(msg), "PUB:%s|Gol de %s al %d' (marcador %d-%d)",
                         topic, teamA, min, golesA, golesB);
            } else { golesB++;
                snprintf(msg, sizeof(msg), "PUB:%s|Gol de %s al %d' (marcador %d-%d)",
                         topic, teamB, min, golesA, golesB);
            }
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Amarilla ~10%
        if (rnd(1,100) <= 10) {
            int lado = rnd(0,1);
            int dorsal = rnd(2,11);
            snprintf(msg, sizeof(msg), "PUB:%s|Tarjeta amarilla #%d para %s al %d'",
                     topic, dorsal, lado?teamB:teamA, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Roja ~2%
        if (rnd(1,100) <= 2) {
            int lado = rnd(0,1);
            int dorsal = rnd(2,11);
            snprintf(msg, sizeof(msg), "PUB:%s|Tarjeta roja #%d para %s al %d'",
                     topic, dorsal, lado?teamB:teamA, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Falta ~12%
        if (rnd(1,100) <= 12) {
            int lado = rnd(0,1);
            snprintf(msg, sizeof(msg), "PUB:%s|Falta de %s al %d'",
                     topic, lado?teamB:teamA, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Tiro de esquina ~8%
        if (rnd(1,100) <= 8) {
            int lado = rnd(0,1);
            snprintf(msg, sizeof(msg), "PUB:%s|Tiro de esquina para %s al %d'",
                     topic, lado?teamB:teamA, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Cambio ~5%
        if (rnd(1,100) <= 5) {
            int sale = rnd(2,11), entra = rnd(12,30), lado = rnd(0,1);
            snprintf(msg, sizeof(msg), "PUB:%s|Cambio en %s: #%d por #%d al %d'",
                     topic, lado?teamB:teamA, sale, entra, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        // Parcial cada 5 minutos simulados
        if (min % 5 == 0) {
            snprintf(msg, sizeof(msg), "PUB:%s|Parcial %s %d - %d %s al %d'",
                     topic, teamA, golesA, golesB, teamB, min);
            sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        usleep(ms_per_min * 1000);
    }

    // Final
    snprintf(msg, sizeof(msg), "PUB:%s|Final del partido — Resultado %s %d - %d %s",
             topic, teamA, golesA, golesB, teamB);
    sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

    close(sockfd);
    return 0;
}


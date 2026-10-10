/*
 * Cada mensaje lleva un numero de secuencia que permite a los suscriptores detectar perdida, duplicados y desorden
 *
 * gcc -Wall -Wextra -o publisher_udp publisher_udp.c
 *
 * Para probar
 * ./publisher_udp <ip_broker> <puerto> <tema> <pubid> [n_msgs=10] [intervalo_ms=500]
 * ./publisher_udp 127.0.0.1 9090 PartidoA P1 10 500
 * ./publisher_udp 127.0.0.1 9090 PartidoB P2 10 500
 *
 * Con un intervalo_ms=0 se envia en rafaga que puede servir para provocar perdidas en UDP, por si se van a probar perdidas.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

static const char *EVENTOS[] = {
    "Gol de Equipo A",
    "Tarjeta amarilla al numero 10 de Equipo B",
    "Cambio: jugador 10 entra por jugador 20",
    "Gol de Equipo B",
    "Tiro de esquina para Equipo A",
    "Falta peligrosa de Equipo B",
    "Tarjeta roja al numero 5 de Equipo A",
    "Penal para Equipo B",
    "Atajada espectacular del arquero de Equipo A",
    "Fuera de lugar de Equipo B"
};
#define N_EVENTOS (sizeof(EVENTOS) / sizeof(EVENTOS[0]))

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <ip_broker> <puerto> <tema> <pubid> [n_msgs=10] [intervalo_ms=500]\n",
                argv[0]);
        return 1;
    }

    const char *ip_broker = argv[1];
    int puerto            = atoi(argv[2]);
    const char *tema      = argv[3];
    const char *pubid     = argv[4];
    int n_msgs            = (argc > 5) ? atoi(argv[5]) : 10;
    int intervalo_ms      = (argc > 6) ? atoi(argv[6]) : 500;

    if (strchr(tema, '|') || strchr(pubid, '|')) {
        fprintf(stderr, "El tema y el pubid no pueden contener '|'\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in broker;
    memset(&broker, 0, sizeof(broker));
    broker.sin_family = AF_INET;
    broker.sin_port   = htons(puerto);
    if (inet_pton(AF_INET, ip_broker, &broker.sin_addr) != 1) {
        fprintf(stderr, "IP invalida: %s\n", ip_broker);
        close(sock);
        return 1;
    }
    printf("[PUB %s] Publicando %d mensajes en el tema '%s' hacia %s:%d\n",
           pubid, n_msgs, tema, ip_broker, puerto);

    for (int seq = 1; seq <= n_msgs; seq++) {
        char texto[256];
        int minuto = (seq * 9) % 90 + 1;
        snprintf(texto, sizeof(texto), "%s al minuto %d",
                 EVENTOS[(seq - 1) % N_EVENTOS], minuto);
        char msg[BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "PUB|%s|%s|%d|%s",
                           tema, pubid, seq, texto);
        if (sendto(sock, msg, len, 0,
                   (struct sockaddr *)&broker, sizeof(broker)) < 0) {
            perror("sendto");
        } else {
            printf("[PUB %s] enviado #%d: %s\n", pubid, seq, texto);
        }
        if (intervalo_ms > 0) usleep((useconds_t)intervalo_ms * 1000);
    }

    printf("[PUB %s] Fin: %d mensajes enviados.\n",
           pubid, n_msgs);
    close(sock);
    return 0;
}
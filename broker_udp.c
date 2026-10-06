/*
 * Broker UDP
 *
 * Mensajes de texto con campos separados por |
 * 
 * Suscriptor - Broker : SUB|tema, suscribirse a un partido
 * Suscriptor - Broker : UNSUB|tema, cancelar suscripcion
 * Broker - Suscriptor : OK|SUB|tema, confirmacion de la suscripcion
 * Publicador - Broker : PUB|tema|pubid|seq|texto
 * Broker - Suscriptor : MSG|tema|pubid|seq|texto
 *
 *  gcc -Wall -Wextra -o broker_udp broker_udp.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PUERTO_DEFECTO 9090
#define BUF_SIZE 1024
#define MAX_SUBS 128   
#define TEMA_LEN 64

typedef struct {
    struct sockaddr_in addr;
    char tema[TEMA_LEN];
    int activa;
} Suscripcion;

static Suscripcion tabla[MAX_SUBS];
static volatile sig_atomic_t corriendo = 1;

static unsigned long recibidos_pub = 0;
static unsigned long reenviados    = 0;

static void manejar_sigint(int sig) {
    (void)sig;
    corriendo = 0;
}

static int dividir(char *s, char *campos[], int max) {
    int n = 0;
    campos[n++] = s;
    while (n < max && (s = strchr(s, '|')) != NULL) {
        *s = '\0';
        campos[n++] = ++s;
    }
    return n;
}

static int misma_direccion(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int buscar(const struct sockaddr_in *addr, const char *tema) {
    for (int i = 0; i < MAX_SUBS; i++) {
        if (tabla[i].activa && misma_direccion(&tabla[i].addr, addr) &&
            strcmp(tabla[i].tema, tema) == 0)
            return i;
    }
    return -1;
}

static int agregar(const struct sockaddr_in *addr, const char *tema) {
    if (buscar(addr, tema) >= 0) return 0;          
    for (int i = 0; i < MAX_SUBS; i++) {
        if (!tabla[i].activa) {
            tabla[i].addr = *addr;
            strncpy(tabla[i].tema, tema, TEMA_LEN - 1);
            tabla[i].tema[TEMA_LEN - 1] = '\0';
            tabla[i].activa = 1;
            return 0;
        }
    }
    return -1;
}

static void eliminar(const struct sockaddr_in *addr, const char *tema) {
    int i = buscar(addr, tema);
    if (i >= 0) tabla[i].activa = 0;
}

int main(int argc, char *argv[]) {
    int puerto = (argc > 1) ? atoi(argv[1]) : PUERTO_DEFECTO;
    if (puerto <= 0 || puerto > 65535) {
        fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(puerto);
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_sigint;
    sigaction(SIGINT, &sa, NULL);

    printf("[BROKER UDP] Escuchando en el puerto %d (Ctrl+C para salir)\n", puerto);

    char buf[BUF_SIZE];
    while (corriendo) {
        struct sockaddr_in cliente;
        socklen_t clen = sizeof(cliente);

        ssize_t n = recvfrom(sock, buf, BUF_SIZE - 1, 0,
                             (struct sockaddr *)&cliente, &clen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }
        buf[n] = '\0';
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cliente.sin_addr, ip, sizeof(ip));
        int pto = ntohs(cliente.sin_port);
        char copia[BUF_SIZE];
        strcpy(copia, buf);
        char *campos[5];
        int nc = dividir(copia, campos, 5);

        if (strcmp(campos[0], "SUB") == 0 && nc >= 2) {
            if (agregar(&cliente, campos[1]) == 0) {
                printf("[BROKER] SUB   %s:%d -> tema '%s'\n", ip, pto, campos[1]);
                char ack[BUF_SIZE];
                snprintf(ack, sizeof(ack), "OK|SUB|%s", campos[1]);
                sendto(sock, ack, strlen(ack), 0,
                       (struct sockaddr *)&cliente, clen);
            } else {
                fprintf(stderr, "[BROKER] Tabla de suscripciones llena\n");
            }

        } else if (strcmp(campos[0], "UNSUB") == 0 && nc >= 2) {
            eliminar(&cliente, campos[1]);
            printf("[BROKER] UNSUB %s:%d -> tema '%s'\n", ip, pto, campos[1]);

        } else if (strcmp(campos[0], "PUB") == 0 && nc == 5) {
            recibidos_pub++;
            printf("[BROKER] PUB   de %s:%d | tema '%s' | %s #%s | %s\n",
                   ip, pto, campos[1], campos[2], campos[3], campos[4]);

            char salida[BUF_SIZE];
            int len = snprintf(salida, sizeof(salida), "MSG|%s|%s|%s|%s",
                               campos[1], campos[2], campos[3], campos[4]);
            if (len >= (int)sizeof(salida)) len = sizeof(salida) - 1;

            int enviados = 0;
            for (int i = 0; i < MAX_SUBS; i++) {
                if (tabla[i].activa && strcmp(tabla[i].tema, campos[1]) == 0) {
                    if (sendto(sock, salida, len, 0,
                               (struct sockaddr *)&tabla[i].addr,
                               sizeof(tabla[i].addr)) >= 0) {
                        enviados++;
                        reenviados++;
                    }
                }
            }
            printf("[BROKER]       reenviado a %d suscriptor(es)\n", enviados);

        } else {
            printf("[BROKER] Datagrama no reconocido de %s:%d: \"%s\"\n", ip, pto, buf);
        }
    }

    printf("\n[BROKER] Cerrando. PUB recibidos: %lu | MSG reenviados: %lu\n",
           recibidos_pub, reenviados);
    close(sock);
    return 0;
}
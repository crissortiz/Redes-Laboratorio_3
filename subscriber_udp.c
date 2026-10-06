/* 
 * Como udp no garantiza nada, el suscriptor lleva las estadisticas por publicador usando el numero de secuencia
 * Al presionar Ctrl+C se cancelan las suscripciones y se imprime el resumen
 *
 * Para probar
 * gcc -Wall -Wextra -o subscriber_udp subscriber_udp.c
 * ./subscriber_udp <ip_broker> <puerto> <tema1> [tema2]
 * ./subscriber_udp 127.0.0.1 9090 PartidoA
 * ./subscriber_udp 127.0.0.1 9090 PartidoA PartidoB
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define MAX_TEMAS 16
#define MAX_FUENTES 32      
#define MAX_SEQ 4096    
#define REINTENTOS 3   
#define ESPERA_ACK_S 1

typedef struct {
    char tema[64];
    char pubid[32];
    int  min_seq, max_seq;
    unsigned long unicos, duplicados, desorden;
    unsigned char visto[MAX_SEQ];
} Fuente;

static Fuente fuentes[MAX_FUENTES];
static int n_fuentes = 0;
static volatile sig_atomic_t corriendo = 1;
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

static Fuente *obtener_fuente(const char *tema, const char *pubid) {
    for (int i = 0; i < n_fuentes; i++)
        if (strcmp(fuentes[i].tema, tema) == 0 && strcmp(fuentes[i].pubid, pubid) == 0)
            return &fuentes[i];
    if (n_fuentes >= MAX_FUENTES) return NULL;
    Fuente *f = &fuentes[n_fuentes++];
    memset(f, 0, sizeof(*f));
    strncpy(f->tema, tema, sizeof(f->tema) - 1);
    strncpy(f->pubid, pubid, sizeof(f->pubid) - 1);
    f->min_seq = f->max_seq = -1;
    return f;
}

static void procesar_msg(char *buf) {
    char *c[5];
    if (dividir(buf, c, 5) != 5) return;
    const char *tema = c[1], *pubid = c[2], *texto = c[4];
    int seq = atoi(c[3]);
    const char *marca = "";
    Fuente *f = obtener_fuente(tema, pubid);
    if (f && seq >= 0 && seq < MAX_SEQ) {
        if (f->visto[seq]) {
            f->duplicados++;
            marca = "  [DUPLICADO]";
        } else {
            f->visto[seq] = 1;
            f->unicos++;
            if (f->max_seq < 0) {
                f->min_seq = f->max_seq = seq;
            } else if (seq > f->max_seq) {
                if (seq > f->max_seq + 1) marca = "  [HUECO: posible perdida]";
                f->max_seq = seq;
            } else {
                f->desorden++;
                marca = "  [FUERA DE ORDEN]";
            }
            if (seq < f->min_seq) f->min_seq = seq;
        }
    }
    printf("[%s] %s #%d: %s%s\n", tema, pubid, seq, texto, marca);
}

static int esperar_ack(int sock, const char *tema) {
    char esperado[BUF_SIZE];
    snprintf(esperado, sizeof(esperado), "OK|SUB|%s", tema);

    for (;;) {
        char buf[BUF_SIZE];
        ssize_t n = recvfrom(sock, buf, BUF_SIZE - 1, 0, NULL, NULL);
        if (n < 0) return 0;              
        buf[n] = '\0';
        if (strcmp(buf, esperado) == 0) return 1;
        if (strncmp(buf, "MSG|", 4) == 0) procesar_msg(buf);
    }
}

static void imprimir_resumen(void) {
    printf("\n===== RESUMEN DEL SUSCRIPTOR (UDP) =====\n");
    for (int i = 0; i < n_fuentes; i++) {
        Fuente *f = &fuentes[i];
        long esperados = (f->max_seq >= 0) ? (f->max_seq - f->min_seq + 1) : 0;
        long perdidos  = esperados - (long)f->unicos;
        printf("tema '%s' | %s | recibidos: %lu | perdidos: %ld | "
               "desordenados: %lu | duplicados: %lu\n",
               f->tema, f->pubid, f->unicos, perdidos, f->desorden, f->duplicados);
    }
    if (n_fuentes == 0) printf("No se recibio ningun mensaje.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }
    const char *ip_broker = argv[1];
    int puerto = atoi(argv[2]);
    int n_temas = argc - 3;
    if (n_temas > MAX_TEMAS) n_temas = MAX_TEMAS;
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

    struct timeval tv = { ESPERA_ACK_S, 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_sigint;
    sigaction(SIGINT, &sa, NULL);

    for (int t = 0; t < n_temas; t++) {
        const char *tema = argv[3 + t];
        char sub[BUF_SIZE];
        int len = snprintf(sub, sizeof(sub), "SUB|%s", tema);
        int ok = 0;
        for (int intento = 1; intento <= REINTENTOS && !ok && corriendo; intento++) {
            sendto(sock, sub, len, 0, (struct sockaddr *)&broker, sizeof(broker));
            ok = esperar_ack(sock, tema);
            if (!ok) printf("[SUB] Sin respuesta del broker para '%s' (intento %d/%d)\n",
                            tema, intento, REINTENTOS);
        }
        if (ok) printf("[SUB] Suscrito al tema '%s'\n", tema);
        else {
            fprintf(stderr, "[SUB] No se pudo suscribir a '%s'. ¿Esta corriendo el broker?\n", tema);
            close(sock);
            return 1;
        }
    }
    printf("[SUB] Esperando noticias... (Ctrl+C para salir)\n");

    char buf[BUF_SIZE];
    while (corriendo) {
        ssize_t n = recvfrom(sock, buf, BUF_SIZE - 1, 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("recvfrom");
            break;
        }
        buf[n] = '\0';
        if (strncmp(buf, "MSG|", 4) == 0) procesar_msg(buf);
    }
    for (int t = 0; t < n_temas; t++) {
        char unsub[BUF_SIZE];
        int len = snprintf(unsub, sizeof(unsub), "UNSUB|%s", argv[3 + t]);
        sendto(sock, unsub, len, 0, (struct sockaddr *)&broker, sizeof(broker));
    }

    imprimir_resumen();
    close(sock);
    return 0;
}
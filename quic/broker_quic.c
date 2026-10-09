/*
 * broker_quic.c
 *
 * Cada cliente abre UN stream bidireccional y por ahi viajan sus mensajes y las respuestas del broker.
 *
 * ./broker_quic [puerto] [cert.pem] [key.pem] por defecto 4433, cert.pem, key.pem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <picoquic.h>
#include <picosocks.h>
#include <picoquic_utils.h>
#include <picoquic_packet_loop.h>

#define PUERTO_DEFECTO 4433
#define ALPN "pubsub-lab3"
#define MAX_CLIENTES 128
#define MAX_TEMAS 16
#define TEMA_LEN 64
#define LINEA_MAX 1024

typedef struct {
    picoquic_cnx_t *cnx;
    uint64_t stream_id;                
    char linea[LINEA_MAX];          
    size_t linea_len;
    int descartando;              
    char temas[MAX_TEMAS][TEMA_LEN];  
    int n_temas;
    int numero;                  
} Cliente;

static Cliente *clientes[MAX_CLIENTES];
static volatile sig_atomic_t corriendo = 1;
static unsigned long recibidos_pub = 0, reenviados = 0, conexiones = 0;

static void manejar_sigint(int sig) { (void)sig; corriendo = 0; }

static int dividir(char *s, char *campos[], int max) {
    int n = 0;
    campos[n++] = s;
    while (n < max && (s = strchr(s, '|')) != NULL) {
        *s = '\0';
        campos[n++] = ++s;
    }
    return n;
}

static void enviar(picoquic_cnx_t *cnx, uint64_t stream_id, const char *texto) {
    // Encola los bytes en el stream, picoquic los empaqueta y retransmite si hace falta
    picoquic_add_to_stream(cnx, stream_id, (const uint8_t *)texto, strlen(texto), 0);
}

static int esta_suscrito(const Cliente *c, const char *tema) {
    for (int i = 0; i < c->n_temas; i++)
        if (strcmp(c->temas[i], tema) == 0) return 1;
    return 0;
}

// Procesa una linea completa recibida de un cliente
static void procesar_linea(Cliente *c, uint64_t stream_id, char *linea) {
    char copia[LINEA_MAX];
    strcpy(copia, linea);
    char *f[5];
    int nf = dividir(copia, f, 5);
    if (strcmp(f[0], "SUB") == 0 && nf >= 2) {
        c->stream_id = stream_id;
        if (!esta_suscrito(c, f[1]) && c->n_temas < MAX_TEMAS) {
            strncpy(c->temas[c->n_temas], f[1], TEMA_LEN - 1);
            c->temas[c->n_temas][TEMA_LEN - 1] = '\0';
            c->n_temas++;
        }
        printf("[BROKER] SUB   conexion #%d -> tema '%s'\n", c->numero, f[1]);
        char ack[LINEA_MAX];
        snprintf(ack, sizeof(ack), "OK|SUB|%s\n", f[1]);
        enviar(c->cnx, stream_id, ack);

    } else if (strcmp(f[0], "UNSUB") == 0 && nf >= 2) {
        for (int i = 0; i < c->n_temas; i++) {
            if (strcmp(c->temas[i], f[1]) == 0) {
                memcpy(c->temas[i], c->temas[c->n_temas - 1], TEMA_LEN);
                c->n_temas--;
                break;
            }
        }
        printf("[BROKER] UNSUB conexion #%d -> tema '%s'\n", c->numero, f[1]);

    } else if (strcmp(f[0], "PUB") == 0 && nf == 5) {
        recibidos_pub++;
        printf("[BROKER] PUB   conexion #%d | tema '%s' | %s #%s | %s\n",
               c->numero, f[1], f[2], f[3], f[4]);

        char salida[LINEA_MAX + 8];
        snprintf(salida, sizeof(salida), "MSG|%s|%s|%s|%s\n", f[1], f[2], f[3], f[4]);
        int enviados = 0;
        for (int i = 0; i < MAX_CLIENTES; i++) {
            Cliente *s = clientes[i];
            if (s != NULL && esta_suscrito(s, f[1])) {
                enviar(s->cnx, s->stream_id, salida);
                enviados++;
                reenviados++;
            }
        }
        printf("[BROKER]       reenviado a %d suscriptor(es)\n", enviados);
    } else {
        printf("[BROKER] Linea no reconocida de conexion #%d: \"%s\"\n", c->numero, linea);
    }
}

// Acumula bytes del stream y procesa cada linea completa
static void procesar_bytes(Cliente *c, uint64_t stream_id, const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ch = (char)bytes[i];
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!c->descartando && c->linea_len > 0) {
                c->linea[c->linea_len] = '\0';
                procesar_linea(c, stream_id, c->linea);
            }
            c->linea_len = 0;
            c->descartando = 0;
        } else if (c->linea_len < LINEA_MAX - 1) {
            c->linea[c->linea_len++] = ch;
        } else {
            c->descartando = 1;
        }
    }
}

static void quitar_cliente(Cliente *c) {
    for (int i = 0; i < MAX_CLIENTES; i++)
        if (clientes[i] == c) clientes[i] = NULL;
    free(c);
}

static int broker_callback(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes,
                           size_t length, picoquic_call_back_event_t ev,
                           void *callback_ctx, void *v_stream_ctx) {
    (void)v_stream_ctx;
    Cliente *c = (Cliente *)callback_ctx;
    if (callback_ctx == NULL ||
        callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        c = (Cliente *)calloc(1, sizeof(Cliente));
        int libre = -1;
        for (int i = 0; i < MAX_CLIENTES && c != NULL; i++)
            if (clientes[i] == NULL) { libre = i; break; }
        if (c == NULL || libre < 0) {
            free(c);
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        c->cnx = cnx;
        c->numero = (int)(++conexiones);
        clientes[libre] = c;
        picoquic_set_callback(cnx, broker_callback, c);
        printf("[BROKER] Nueva conexion QUIC #%d\n", c->numero);
    }

    switch (ev) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        procesar_bytes(c, stream_id, bytes, length);
        if (ev == picoquic_callback_stream_fin) {
            // El cliente termino de enviar, se responde con FIN, asi el publicador sabe que todo lo suyo llego y puede cerrar
            picoquic_add_to_stream(cnx, stream_id, NULL, 0, 1);
        }
        break;
    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        printf("[BROKER] Conexion #%d cerrada\n", c->numero);
        quitar_cliente(c);
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int broker_loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo,
                          void *ctx, void *arg) {
    (void)quic; (void)ctx;
    switch (modo) {
    case picoquic_packet_loop_ready:
        if (arg != NULL) ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
        break;
    case picoquic_packet_loop_time_check: {
        packet_loop_time_check_arg_t *t = (packet_loop_time_check_arg_t *)arg;
        if (t->delta_t > 100000) t->delta_t = 100000;
        if (!corriendo) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        break;
    }
    case picoquic_packet_loop_after_receive:
    case picoquic_packet_loop_after_send:
        if (!corriendo) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); 
    int puerto            = (argc > 1) ? atoi(argv[1]) : PUERTO_DEFECTO;
    const char *cert      = (argc > 2) ? argv[2] : "cert.pem";
    const char *llave     = (argc > 3) ? argv[3] : "key.pem";

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_sigint;
    sigaction(SIGINT, &sa, NULL);
    static int contexto_defecto; 
    uint64_t ahora = picoquic_current_time();
    picoquic_quic_t *quic = picoquic_create(MAX_CLIENTES, cert, llave, NULL, ALPN,
                                            broker_callback, &contexto_defecto,
                                            NULL, NULL, NULL, ahora, NULL,
                                            NULL, NULL, 0);
    if (quic == NULL) {
        fprintf(stderr, "No se pudo crear el contexto QUIC. ¿Existen %s y %s?\n", cert, llave);
        return 1;
    }

    picoquic_set_key_log_file_from_env(quic);

    printf("[BROKER QUIC] Escuchando en UDP/%d (Ctrl+C para salir)\n", puerto);

    int ret = picoquic_packet_loop(quic, puerto, AF_INET, 0, 0, 0, broker_loop_cb, NULL);

    (void)ret;
    picoquic_free(quic);  
    printf("\n[BROKER] Cerrando. Conexiones: %lu | PUB recibidos: %lu | MSG reenviados: %lu\n",
           conexiones, recibidos_pub, reenviados);
    return 0;
}
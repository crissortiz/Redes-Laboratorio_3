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

#define ALPN "pubsub-lab3"
#define STREAM_ID 0
#define MAX_TEMAS 16
#define MAX_FUENTES 32
#define MAX_SEQ 4096
#define LINEA_MAX 1024

typedef struct {
    char tema[64], pubid[32];
    int min_seq, max_seq;
    unsigned long unicos, duplicados, desorden;
    unsigned char visto[MAX_SEQ];
} Fuente;

typedef struct {
    picoquic_cnx_t *cnx;
    char **temas;
    int n_temas;
    char linea[LINEA_MAX];
    size_t linea_len;
    int listo, cerrando, desconectado;
    Fuente fuentes[MAX_FUENTES];
    int n_fuentes;
} Sub;

static volatile sig_atomic_t corriendo = 1;
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

static Fuente *obtener_fuente(Sub *s, const char *tema, const char *pubid) {
    for (int i = 0; i < s->n_fuentes; i++)
        if (strcmp(s->fuentes[i].tema, tema) == 0 && strcmp(s->fuentes[i].pubid, pubid) == 0)
            return &s->fuentes[i];
    if (s->n_fuentes >= MAX_FUENTES) return NULL;
    Fuente *f = &s->fuentes[s->n_fuentes++];
    memset(f, 0, sizeof(*f));
    strncpy(f->tema, tema, sizeof(f->tema) - 1);
    strncpy(f->pubid, pubid, sizeof(f->pubid) - 1);
    f->min_seq = f->max_seq = -1;
    return f;
}

static void procesar_msg(Sub *s, char *linea) {
    char *c[5];
    if (dividir(linea, c, 5) != 5) return;
    const char *tema = c[1], *pubid = c[2], *texto = c[4];
    int seq = atoi(c[3]);
    const char *marca = "";

    Fuente *f = obtener_fuente(s, tema, pubid);
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

static void procesar_linea(Sub *s, char *linea) {
    if (strncmp(linea, "MSG|", 4) == 0) {
        procesar_msg(s, linea);
    } else if (strncmp(linea, "OK|SUB|", 7) == 0) {
        printf("[SUB] Suscrito al tema '%s'\n", linea + 7);
    }
}

static void imprimir_resumen(Sub *s) {
    printf("\n===== RESUMEN DEL SUSCRIPTOR (QUIC) =====\n");
    for (int i = 0; i < s->n_fuentes; i++) {
        Fuente *f = &s->fuentes[i];
        long esperados = (f->max_seq >= 0) ? (f->max_seq - f->min_seq + 1) : 0;
        printf("tema '%s' | %s | recibidos: %lu | perdidos: %ld | desordenados: %lu | duplicados: %lu\n",
               f->tema, f->pubid, f->unicos, esperados - (long)f->unicos, f->desorden, f->duplicados);
    }
    if (s->n_fuentes == 0) printf("No se recibio ningun mensaje.\n");
}

static int sub_callback(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes,
                        size_t length, picoquic_call_back_event_t ev,
                        void *callback_ctx, void *v_stream_ctx) {
    (void)stream_id; (void)v_stream_ctx;
    Sub *s = (Sub *)callback_ctx;

    switch (ev) {
    case picoquic_callback_ready:
        printf("[SUB] Conexion QUIC establecida (handshake completo)\n");
        s->listo = 1;
        for (int i = 0; i < s->n_temas; i++) {
            char sub[LINEA_MAX];
            int len = snprintf(sub, sizeof(sub), "SUB|%s\n", s->temas[i]);
            picoquic_add_to_stream(cnx, STREAM_ID, (const uint8_t *)sub, (size_t)len, 0);
        }
        break;
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        for (size_t i = 0; i < length; i++) {
            char ch = (char)bytes[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                s->linea[s->linea_len] = '\0';
                if (s->linea_len > 0) procesar_linea(s, s->linea);
                s->linea_len = 0;
            } else if (s->linea_len < LINEA_MAX - 1) {
                s->linea[s->linea_len++] = ch;
            }
        }
        break;
    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        if (!s->listo) fprintf(stderr, "[SUB] No se pudo conectar. ¿Esta corriendo el broker?\n");
        s->desconectado = 1;
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int sub_loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo,
                       void *ctx, void *arg) {
    (void)quic;
    Sub *s = (Sub *)ctx;
    switch (modo) {
    case picoquic_packet_loop_ready:
        if (arg != NULL) ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
        printf("[SUB] Esperando noticias (Ctrl+C para salir)\n");
        break;
    case picoquic_packet_loop_time_check: {
        packet_loop_time_check_arg_t *t = (packet_loop_time_check_arg_t *)arg;
        if (t->delta_t > 100000) t->delta_t = 100000;  
        if (s->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        if (!corriendo && !s->cerrando) {
            s->cerrando = 1;
            picoquic_close(s->cnx, 0);
        }
        break;
    }
    case picoquic_packet_loop_after_receive:
    case picoquic_packet_loop_after_send:
        if (s->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    static Sub s; 
    int puerto = atoi(argv[2]);
    s.temas = &argv[3];
    s.n_temas = argc - 3;
    if (s.n_temas > MAX_TEMAS) s.n_temas = MAX_TEMAS;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_sigint;
    sigaction(SIGINT, &sa, NULL);
    struct sockaddr_storage direccion;
    int es_nombre = 0;
    if (picoquic_get_server_address(argv[1], puerto, &direccion, &es_nombre) != 0) {
        fprintf(stderr, "No se pudo resolver %s:%d\n", argv[1], puerto);
        return 1;
    }

    uint64_t ahora = picoquic_current_time();
    picoquic_quic_t *quic = picoquic_create(1, NULL, NULL, NULL, ALPN, NULL, NULL,
                                            NULL, NULL, NULL, ahora, NULL, NULL, NULL, 0);
    if (quic == NULL) { fprintf(stderr, "No se pudo crear el contexto QUIC\n"); return 1; }
    picoquic_set_null_verifier(quic);        
    picoquic_set_key_log_file_from_env(quic);

    s.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
                                (struct sockaddr *)&direccion, ahora, 0,
                                es_nombre ? argv[1] : "localhost", ALPN, 1);
    if (s.cnx == NULL) { fprintf(stderr, "No se pudo crear la conexion\n"); picoquic_free(quic); return 1; }
    picoquic_set_callback(s.cnx, sub_callback, &s);
    if (picoquic_start_client_cnx(s.cnx) < 0) {
        fprintf(stderr, "No se pudo iniciar la conexion\n");
        picoquic_free(quic);
        return 1;
    }

    picoquic_packet_loop(quic, 0, direccion.ss_family, 0, 0, 0, sub_loop_cb, &s);
    imprimir_resumen(&s);
    picoquic_free(quic);
    return 0;
}
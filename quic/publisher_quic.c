#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <picoquic.h>
#include <picosocks.h>
#include <picoquic_utils.h>
#include <picoquic_packet_loop.h>

#define ALPN "pubsub-lab3"
#define STREAM_ID 0  

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

typedef struct {
    picoquic_cnx_t *cnx;
    const char *tema, *pubid;
    int n_msgs, sig_seq;
    uint64_t intervalo_us, proximo_envio;
    int listo;         
    int fin_enviado;     
    int desconectado;
} Pub;

static void enviar_siguiente(Pub *p) {
    char texto[256], msg[1024];
    int seq = p->sig_seq++;
    snprintf(texto, sizeof(texto), "%s al minuto %d",
             EVENTOS[(seq - 1) % N_EVENTOS], (seq * 9) % 90 + 1);
    int len = snprintf(msg, sizeof(msg), "PUB|%s|%s|%d|%s\n", p->tema, p->pubid, seq, texto);

    // Encola la linea en el stream 0, sin FIN todavia
    picoquic_add_to_stream(p->cnx, STREAM_ID, (const uint8_t *)msg, (size_t)len, 0);
    printf("[PUB %s] enviado #%d: %s\n", p->pubid, seq, texto);
}

static int pub_callback(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes,
                        size_t length, picoquic_call_back_event_t ev,
                        void *callback_ctx, void *v_stream_ctx) {
    (void)stream_id; (void)bytes; (void)length; (void)v_stream_ctx;
    Pub *p = (Pub *)callback_ctx;

    switch (ev) {
    case picoquic_callback_ready:
        printf("[PUB %s] Conexion QUIC establecida (handshake completo)\n", p->pubid);
        p->listo = 1;
        break;
    case picoquic_callback_stream_fin:
        // El broker cerro su lado y confirmo que recibio todo
        if (p->fin_enviado) picoquic_close(cnx, 0);
        break;
    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        if (!p->listo) fprintf(stderr, "[PUB %s] No se pudo conectar. ¿Esta corriendo el broker?\n", p->pubid);
        p->desconectado = 1;
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int pub_loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo,
                       void *ctx, void *arg) {
    (void)quic;
    Pub *p = (Pub *)ctx;
    switch (modo) {
    case picoquic_packet_loop_ready:
        if (arg != NULL) ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
        break;
    case picoquic_packet_loop_time_check: {
        packet_loop_time_check_arg_t *t = (packet_loop_time_check_arg_t *)arg;
        if (t->delta_t > 20000) t->delta_t = 20000;  
        if (p->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;

        if (p->listo && !p->fin_enviado) {
            if (p->proximo_envio == 0) p->proximo_envio = t->current_time;
            while (p->sig_seq <= p->n_msgs && t->current_time >= p->proximo_envio) {
                enviar_siguiente(p);
                p->proximo_envio += p->intervalo_us;
                if (p->intervalo_us > 0) break;
            }
            if (p->sig_seq > p->n_msgs) {
                picoquic_add_to_stream(p->cnx, STREAM_ID, NULL, 0, 1);
                p->fin_enviado = 1;
                printf("[PUB %s] Fin: %d mensajes enviados, esperando confirmacion del broker...\n",
                       p->pubid, p->n_msgs);
            }
        }
        break;
    }
    case picoquic_packet_loop_after_receive:
    case picoquic_packet_loop_after_send:
        // Conexion cerrada, el valor devuelto por time_check puede perderse por eso se revisa tambien aqui
        if (p->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> <pubid> [n_msgs=10] [intervalo_ms=500]\n", argv[0]);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    Pub p;
    memset(&p, 0, sizeof(p));
    int puerto  = atoi(argv[2]);
    p.tema      = argv[3];
    p.pubid     = argv[4];
    p.n_msgs    = (argc > 5) ? atoi(argv[5]) : 10;
    p.intervalo_us = (uint64_t)((argc > 6) ? atoi(argv[6]) : 500) * 1000;
    p.sig_seq   = 1;
    if (strchr(p.tema, '|') || strchr(p.pubid, '|')) {
        fprintf(stderr, "El tema y el pubid no pueden contener '|'\n");
        return 1;
    }

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

    p.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
                                (struct sockaddr *)&direccion, ahora, 0,
                                es_nombre ? argv[1] : "localhost", ALPN, 1);
    if (p.cnx == NULL) { fprintf(stderr, "No se pudo crear la conexion\n"); picoquic_free(quic); return 1; }
    picoquic_set_callback(p.cnx, pub_callback, &p);
    if (picoquic_start_client_cnx(p.cnx) < 0) {
        fprintf(stderr, "No se pudo iniciar la conexion\n");
        picoquic_free(quic);
        return 1;
    }
    printf("[PUB %s] Conectando a %s:%d, tema '%s'...\n", p.pubid, argv[1], puerto, p.tema);
    picoquic_packet_loop(quic, 0, direccion.ss_family, 0, 0, 0, pub_loop_cb, &p);
    picoquic_free(quic);
    printf("[PUB %s] Terminado.\n", p.pubid);
    return 0;
}
// broker implementado por santi :)

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 64
#define MAX_TOPICS 16
#define FRAME_SIZE 4096

// info de cada cliente
typedef struct {
    int fd;
    char input[FRAME_SIZE]; //guardar hasta completar el mensaje
    size_t used;

    // partidos suscrito
    char topics[MAX_TOPICS][64];
    int count;
}Client;

static Client clients[MAX_CLIENTS];

// enviando bytes por partes hasta terminar de enviar el msg
static int sendall(int fd, const char *s, size_t n){
    while(n){
        ssize_t k = send(fd, s, n, 0);
        if(k<0 && errno==EINTR){
            continue;
        }
        if(k<=0){
            return -1;
        }
        s += k;
        n -= (size_t)k;
    }
    return 0;
}

// cerrar y limpiar datos
static void drop(Client *c){
    close(c->fd);
    c->fd = -1;
    c->used = 0;
    c->count = 0;
}

//comprobar si sigue escuchando un partido
static int subscribed(Client *c, const char *topic){
    for (int i=0; i<c->count;i++){
        if(!strcmp(c->topics[i], topic)){
            return 1;
        }
    }
    return 0;
}

//mensajes
static int handle(Client *c, char *line){
    char *space = strchr(line, ' ');
    if(!space){
        return -1;
    }
    //separar comando
    *space++ = '\0';
    char *topic = space;

    if(!strcmp(line, "SUB")){
        if(!*topic || strlen(topic) >= 64 || strpbrk(topic, " \t\r")){
            return -1;
        }
        if(!subscribed(c,topic)){
            if(c->count == MAX_TOPICS){
                return -1;
            }
            strcpy(c->topics[c->count++], topic);
        }
    
        char ack[100];
        int n = snprintf(ack, sizeof ack, "OK SUB %s\n", topic);
        return sendall(c->fd, ack, (size_t)n);
    }
    if (strcmp(line, "PUB")){
        return -1;
    }
    space = strchr(topic, ' ');
    if(!space){
        return -1;
    }

    //separar topic y mensaje
    *space++ = '\0';

    if(!*topic || strlen(topic) >= 64 || strpbrk(topic, " \t\r") || !*space || strlen(space) > 3000 ){
        return -1;
    } 
    char out[FRAME_SIZE];
    int n = snprintf(out, sizeof out, "MSG %s %s\n", topic, space);
    printf("%s: %s\n", topic, space);
    fflush(stdout);

    //enviar a los interesados
    for(int i=0; i<MAX_CLIENTS; i++){
        Client *dest = &clients[i];
        if(dest->fd >= 0 && subscribed(dest,topic)){
            if(sendall(dest->fd, out, (size_t)n)<0){
                drop(dest);
            }
        }
    }
    return c->fd <0? -1: 0;
}

int main(int argc, char **argv){
    if(argc !=2){
        fprintf(stderr, "uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    //puerto
    char *end;
    long port = strtol(argv[1], &end, 10);
    if(!*argv[1] || *end || port<1 || port>65535){
        fprintf(stderr, "puerto no valido.\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    for(int i=0; i<MAX_CLIENTS; i++){
        clients[i].fd = -1;
    }

    //crear socket tcp
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if(server<0){
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    //timeout
    struct timeval timeout = {2,0};
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if(bind(server, (struct sockaddr*)&addr, sizeof addr)<0 || listen(server, 16) <0){
        perror("bind/listen");
        close(server);
        return 1;
    }
    if(server>= FD_SETSIZE){
        close(server);
        return 1;
    }
    printf("broker escuchando en puerto %ld\n", port);
    fflush(stdout);
    
    for(;;){
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(server, &reads);
        int maxfd = server;
        for(int i=0; i<MAX_CLIENTS; i++){
            if(clients[i].fd >=0){
                FD_SET(clients[i].fd, &reads);
                if(clients[i].fd > maxfd){
                    maxfd = clients[i].fd;
                }
            }
        }
        //esperar conexion o mensaje
        if(select(maxfd+1, &reads, NULL, NULL, NULL)<0){
            if(errno == EINTR){
                continue;
            }
            perror("select");
            break;
        }
        //priorizar existentes
        for(int i=0; i<MAX_CLIENTS; i++){
            Client *c = &clients[i];
            if(c->fd < 0 || !FD_ISSET(c->fd, &reads)){
                continue;
            }
            char chunk[1024];
            ssize_t n = recv(c->fd, chunk, sizeof chunk, 0);
            if(n<0 && errno==EINTR){
                continue;
            }
            if(n<=0){
                drop(c);
                continue;
            }

            for(ssize_t j=0; j<n && c->fd >=0; j++){
                if(chunk[j] == '\n'){
                    c->input[c->used] = '\0';
                    c->used = 0;
                    if(handle(c,c->input)<0 && c->fd >=0){
                        drop(c);
                    }
                }else if(c->used < sizeof c->input -1){
                    c->input[c->used++] = chunk[j];
            } else{
                    drop(c);
                }
            }
        }

        if (FD_ISSET(server, &reads)){
            int fd = accept(server, NULL, NULL);
            if(fd<0){
                continue;
        }
        int slot = -1;
        for(int i=0; i<MAX_CLIENTS; i++){
            if(clients[i].fd <0){
                slot = i;
                break;
            }
        }
        if(slot<0 || fd >= FD_SETSIZE){
            close(fd);
            continue;
        }
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        clients[slot].fd = fd;
        clients[slot].used = 0;
        clients[slot].count = 0;
    }
}
    for (int i=0; i<MAX_CLIENTS; i++){
        if(clients[i].fd >=0){
            drop(&clients[i]);
        }
    }
    close(server);
    return 1;

}


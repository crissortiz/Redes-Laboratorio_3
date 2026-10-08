//suscriptor tcp implementado por santi :)
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

//hacer el send hasta enviar todo
static int send_all(int fd, const char *s, size_t n){
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

//contectarse al broker
static int connect_to(const char *host, const char *port){
    struct addrinfo hints = {0};
    struct addrinfo *list;
    struct addrinfo *a;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, port, &hints, &list);
    if(err){
        fprintf(stderr, "%s\n", gai_strerror(err));
        return -1;
    }
    int fd = -1;
    for(a=list; a; a=a->ai_next){
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if(fd<0){
            continue;
        }
        if(connect(fd, a->ai_addr, a->ai_addrlen)==0){
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}
static int valid_topic(const char *s){
    return *s && strlen(s)<64 && !strpbrk(s, " \t\r\n");
}
int main(int argc, char **argv){
    if(argc<4 || argc>19){
        fprintf(stderr, "uso: %s IP PUERTO PARTIDO [PARTIDO...] (max. 16)\n", argv[0]);
        return 1;
    }
    for(int i=3; i<argc; i++){
        if(!valid_topic(argv[i])){
            fprintf(stderr, "partido no valido: %s\n", argv[i]);
            return 1;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    int fd = connect_to(argv[1], argv[2]);
    if(fd<0){
        perror("connect");
        return 1;
    }
    for(int i=3; i<argc; i++){
        char frame[80];
        int n = snprintf(frame, sizeof frame, "SUB %s\n", argv[i]);
        if(send_all(fd, frame, (size_t)n)<0){
            perror("send");
            close(fd);
            return 1;
        }
    }
    char line[4096];
    size_t used = 0;
    int status = 0;
    for(;;){
        char chunk[1024];
        ssize_t n = recv(fd, chunk, sizeof chunk, 0);
        if(n<0 && errno==EINTR){
            continue;
        }
        if(n<=0){
            if(n<0){
                perror("recv");
                status = 1;
            }
            break;
        }
        for(ssize_t i=0; i<n; i++){
            if(chunk[i]=='\n'){
                line[used] = '\0';
                puts(line);
                fflush(stdout);
                used = 0;
            } else if(used< sizeof line -1){
                line[used++] = chunk[i];
            } else{
                fprintf(stderr, "mensaje muy largo\n");
                close(fd);
                return 1;
            }
        }
    }
    fprintf(stderr, "conexion cerrada.\n");
    close(fd);
    return status;
}
//publicador tcp implementado por santi :)
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

//validar que el nombre es valido
static int valid_topics(const char *s){
    return *s && strlen(s)<64 && !strpbrk(s, " \t\r\n");
}

int main(int argc, char **argv){
    if(argc!=4 || !valid_topics(argv[3])){
        fprintf(stderr, "USO: %s IP PUERTO PARTIDO " "Sin espacios, max 63 caracteres\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    int fd = connect_to(argv[1], argv[2]);
    if(fd<0){
        perror("connect");
        return 1;
    }
    printf("conectado. escribe noticias pofa para %s; Ctrl+D para salir\n", argv[3]);
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int status = 0;

    //leer noticias por lineas
    while((n=getline(&line, &cap, stdin)) >= 0){
        while (n && (line[n-1]=='\n' || line[n-1]=='\r')){
            line[--n] = '\0';
        }
        if(!n){
            continue;
        }
        if(n>3000){
            fprintf(stderr, "noticia muy larga, max 3000 bytes\n");
            continue;
        }
        char frame[4096];
        int len = snprintf(frame, sizeof frame, "PUB %s %s\n", argv[3], line);
        if(send_all(fd, frame, (size_t)len)<0){
            perror("send");
            status = 1;
            break;
        }
    }
    free(line);
    close(fd);
    return status;
}


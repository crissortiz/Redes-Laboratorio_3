# Laboratorio 3: Sockets y Transporte en UDP

Este README documenta la parte UDP del sistema de noticias deportivas en tiempo real con un modelo publicador-suscriptor con un broker en el medio.

## Librerías externas

**No usamos ninguna librería externa,** todo el código usa solamente la librería estándar de C y los headers POSIX que vienen con Linux. No hay que instalar nada ni enlazar nada extra al compilar y por eso solo contamos con la tabla de abajo con lo que importamos y para que se usó.

## Qué importamos y para qué se usó


| Header | Funciones o tipos usados | Para qué |
|---|---|---|
| `<stdio.h>` | `printf`, `fprintf`, `snprintf`, `perror` | Imprimir en consola lo que pasa, mensajes de error y armar los mensajes del protocolo con `snprintf` que respeta el tamaño del buffer |
| `<stdlib.h>` | `atoi` | Convertir a entero los argumentos de la línea de comandos puerto, número de mensajes, intervalo, y el número de secuencia que viene dentro del mensaje |
| `<string.h>` | `memset`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strchr` | Limpiar las estructuras de direcciones, copiar y comparar temas y comandos y buscar el separador `\|` para partir los mensajes |
| `<errno.h>` | `errno`, `EINTR`, `EAGAIN`, `EWOULDBLOCK` | Distinguir si `recvfrom` falló por un error real, porque lo interrumpió Ctrl+C o porque se venció el timeout |
| `<signal.h>` | `sigaction`, `SIGINT`, `sig_atomic_t` | Capturar Ctrl+C para cerrar bien, el broker y el suscriptor imprimen sus estadísticas antes de terminar y el suscriptor cancela sus suscripciones |

### Headers POSIX / Linux (sockets)

| Header | Funciones o tipos usados | Para qué |
|---|---|---|
| `<sys/socket.h>` | `socket`, `bind`, `sendto`, `recvfrom`, `setsockopt`, `socklen_t`, `AF_INET`, `SOCK_DGRAM`, `SOL_SOCKET`, `SO_REUSEADDR`, `SO_RCVTIMEO` | Es el núcleo del laboratorio, explicado en la siguiente sección |
| `<netinet/in.h>` | `struct sockaddr_in`, `INADDR_ANY`, `htons`, `htonl`, `ntohs` | Guardar IP y puerto de cada extremo, y convertir los números entre el orden de bytes del computador (host) y el de la red (big endian) |
| `<arpa/inet.h>` | `inet_pton`, `inet_ntop` | Convertir la IP de texto a binario para el `sendto`, y de binario a texto para mostrar quién envió un datagrama |
| `<unistd.h>` | `close`, `usleep` | Cerrar el socket al terminar y esperar entre mensajes en el publicador. |
| `<sys/time.h>` | `struct timeval` | Definir el timeout de `recvfrom` en el suscriptor  |

## Protocolo que definimos

Como la guía no define el formato de los mensajes, udp diseña un texto con campos separados por `|`:

```
- Suscriptor - Broker : SUB|tema
- Suscriptor - Broker : UNSUB|tema
- Broker - Suscriptor : OK|SUB|tema
- Publicador - Broker : PUB|tema|pubid|seq|texto
- Broker - Suscriptor : MSG|tema|pubid|seq|texto
```

- El **tema** es el partido, por ejemplo PartidoA.
- El **pubid** identifica al periodista `P1`, `P2`.
- El **seq** es un número de secuencia que va aumentando por publicador, udp no numera los mensajes por nosotros, así que lo agregamos a nivel de aplicación para poder detectar pérdidas, duplicados y desorden en el suscriptor.
- El broker **no modifica el contenido**, solo cambia `PUB` por `MSG` y reenvía.
- Como el `SUB` puede perderse, el broker responde `OK|SUB|tema` y el suscriptor reintenta hasta 3 veces.

## Compilación

```bash
gcc -Wall -Wextra -o broker_udp broker_udp.c
gcc -Wall -Wextra -o publisher_udp publisher_udp.c
gcc -Wall -Wextra -o subscriber_udp subscriber_udp.c
```

## Ejecución 

Cada comando va en una terminal distinta y en este orden, primero el broker, despues los suscriptores y por utimo los publicadores, porque como el broker no guarda mensajes si se publica cuando nadie está suscrito, los mensajes se pierden.

Se pueden usar estos comandos para probarlo
```bash
./broker_udp 9090
./subscriber_udp 127.0.0.1 9090 PartidoA
./subscriber_udp 127.0.0.1 9090 PartidoA PartidoB
./publisher_udp 127.0.0.1 9090 PartidoA P1 10 500
./publisher_udp 127.0.0.1 9090 PartidoB P2 10 500
```

## Limitaciones conocidas

- El broker guarda máximo 128 suscripciones y cada suscriptor sigue máximo 16 temas y 32 publicadores distintos, estos límites estan fijos en el código para no usar memoria dinámica.
- Los mensajes tienen un máximo de 1024 bytes.
- El suscriptor no puede detectar la pérdida de los últimos mensajes de un publicador, porque la pérdida se detecta al ver un hueco en los `seq` y después de ellos no llega nada.
- El broker no se entera si un suscriptor se cae sin mandar `UNSUB` y le seguirá reenviando mensajes.
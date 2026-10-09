# Laboratorio 3  Bono QUIC

| Archivo | Rol |
|---|---|
| `broker_quic.c` | Recibe lo que publican los periodistas y lo reenvía a los hinchas suscritos al partido |
| `publisher_quic.c` | Periodista que envía N noticias de un partido al broker |
| `subscriber_quic.c` | Hincha que se suscribe a uno o varios partidos y muestra las noticias |
| `Makefile` | Compila los tres programas y genera el certificado |

## librería externa

Para TCP y UDP basto con la API de sockets del sistema operativo, con quic no se puede hacer lo mismo, quic no es una función del sistema operativo sino un protocolo completo que corre sobre udp y que incluye por dentro cifrado TLS 1.3 obligatorio, handshake, retransmisión, control de congestión y control de flujo por stream. Implementarlo desde cero en C con solo la librería estándar no es realista para un laboratorio, así que era mejor usar una implementación existente.

Elegimos **picoquic** https://github.com/private-octopus/picoquic, licencia MIT porque

- Está escrita en C igual que lo exige el laboratorio 
- Tiene un ciclo de paquetes ya hecho `picoquic_packet_loop` que maneja el socket udp, así que podemos concentrarnos en la lógica publicador-suscriptor.
- Trae un ejemplo (`sample/`) que usamos como referencia para saber en qué orden llamar las funciones.

### Dependencias de picoquic

| Dependencia | Para qué |
|---|---|
| **picotls** | Implementación de TLS 1.3 que usa picoquic. Se descarga sola al compilar picoquic con -DPICOQUIC_FETCH_PTLS=Y |
| **OpenSSL**  | Primitivas criptográficas que usa picotls. Se enlazan con `-lssl -lcrypto` |
| **cmake**, **git**, **gcc** | Para descargar y compilar picoquic |
| `-lpthread -lm` | Hilos y matemáticas que necesita picoquic al enlazar |

## Instalación de picoquic y compilación

```bash
# Herramientas
sudo apt install -y git cmake gcc make pkg-config libssl-dev

# Descargar y compilar la libreria
git clone https://github.com/private-octopus/picoquic.git
cd picoquic
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DPICOQUIC_FETCH_PTLS=Y
make -j4 picoquic-core
cd ../..

# Compilar 
cd quic
make                    
make cert.pem              
```

El paso `make cert.pem` ejecuta

```bash
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365 -subj "/CN=localhost"
```
P
ara MAC
```bash
xcode-select --install           
brew install cmake openssl@3 pkg-config

# Descargar y compilar la libreria 
git clone https://github.com/private-octopus/picoquic.git
cd picoquic
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DPICOQUIC_FETCH_PTLS=Y \
         -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
make -j4 picoquic-core
cd ../..

# Compilar
cd quic
make
make cert.pem
./broker_quic 4433           
```

QUIC obliga a usar TLS, por eso el broker necesita un certificado y una llave, el certificado es autofirmado.

## Ejecución 

```
./broker_quic 4433
./subscriber_quic 127.0.0.1 4433 PartidoA
./subscriber_quic 127.0.0.1 4433 PartidoA PartidoB
./publisher_quic 127.0.0.1 4433 PartidoA P1 10 500
./publisher_quic 127.0.0.1 4433 PartidoB P2 10 500
```

## Documentación de picoquic

los headers incluidos son `picoquic.h`, `picosocks.h`, `picoquic_utils.h`, `picoquic_packet_loop.h`, ademas se usan `sys/socket.h` y `netinet/in.h` para `AF_INET` y `struct sockaddr_storage`.

### Funciones

| Función | Usada en | Qué hace y por qué la llamamos |
|---|---|---|
| `picoquic_current_time()` | los 3 | Devuelve la hora actual en microsegundos. Picoquic mide todo el tiempo (timers, retransmisiones) en microsegundos y pide la hora al crear el contexto |
| `picoquic_create(max_conexiones, cert, llave, raiz, alpn, callback, ctx, ..., hora, ...)` | los 3 | Crea el **contexto QUIC** (`picoquic_quic_t`), que contiene todas las conexiones. En el broker se le pasan el certificado, la llave, el ALPN y el callback por defecto. En publicador y suscriptor se pasa `NULL` en certificado y llave porque son clientes, y `1` como máximo de conexiones |
| `picoquic_free(quic)` | los 3 | Libera el contexto. En el broker además cierra las conexiones que queden abiertas |
| `picoquic_set_key_log_file_from_env(quic)` | los 3 | Si existe la variable de entorno `SSLKEYLOGFILE`, guarda ahí las llaves de sesión TLS. Wireshark las usa para **descifrar** la captura de QUIC (ver sección de Wireshark) |
| `picoquic_set_null_verifier(quic)` | publicador, suscriptor | El cliente no valida el certificado del broker. Es necesario porque el nuestro es autofirmado. **No se debe hacer en un sistema real**: deja la conexión vulnerable a un intermediario falso |
| `picoquic_get_server_address(texto, puerto, &dir, &es_nombre)` | publicador, suscriptor | Convierte la IP o nombre  y el puerto en una `struct sockaddr_storage` lista para usar |
| `picoquic_create_cnx(quic, cid_nulo, cid_nulo, dir, hora, 0, sni, alpn, 1)` | publicador, suscriptor | Crea el contexto de **una conexión de cliente** hacia el broker. Los dos `picoquic_null_connection_id` hacen que picoquic escoja los identificadores de conexión, `sni` es el nombre de servidor que va en el handshake TLS; el último `1` indica que es cliente |
| `picoquic_start_client_cnx(cnx)` | publicador, suscriptor | Arranca el handshake QUIC/TLS: prepara el paquete *Initial* que irá al broker |
| `picoquic_set_callback(cnx, funcion, contexto)` | los 3 | Registra la función que picoquic llamará con cada evento de esa conexión y el puntero de contexto que recibirá|
| `picoquic_get_default_callback_context(quic)` y `picoquic_get_quic_ctx(cnx)` | broker | Cuando llega una conexión nueva, picoquic llama el callback con el contexto por defecto. Con estas dos funciones detectamos esa primera llamada y creamos la estructura `Cliente` de esa conexión |
| `picoquic_add_to_stream(cnx, stream_id, datos, largo, fin)` | los 3 | Pone bytes en la cola de un stream, picoquic los copia, los empaqueta y los retransmite si se pierden. Con `fin = 1` y `largo = 0` se cierra nuestro lado del stream |
| `picoquic_close(cnx, codigo)` | broker, publicador, suscriptor | Cierra la conexión enviando un `CONNECTION_CLOSE`. El código 0 es "sin error". El broker lo usa solo si no puede atender un cliente  |
| `picoquic_packet_loop` | los 3 | Es el ciclo principal, abre el socket UDP, lee paquetes, se los entrega a QUIC, envía los que QUIC prepara y se encarga de los timers. No retorna hasta que el callback pida terminar. El broker pasa su puerto; los clientes pasan 0. |
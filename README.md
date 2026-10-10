# Laboratorio 3: Análisis de Capa de Transporte y Sockets (TCP vs UDP)
**Infraestructura de Comunicaciones - Universidad de los Andes**

Sistema distribuido de transmisión de noticias deportivas en tiempo real basado en el patrón arquitectural **Publicador–Suscriptor (Pub-Sub)** con un **Broker** intermediario, implementado en lenguaje C sobre sockets POSIX nativos.

---

## 1. Arquitectura del Sistema

El sistema emula una plataforma de streaming deportivo en vivo con tres actores principales:
* **Publishers (Periodistas):** Emiten eventos en vivo de un partido específico (ej. *"Gol de Colombia al minuto 12"*).
* **Broker (Canal Central):** Recibe las publicaciones y las redistribuye exclusivamente a los suscriptores interesados en dicho partido, sin alterar el contenido del mensaje.
* **Subscribers (Aficionados):** Se suscriben a uno o varios partidos y reciben las notificaciones en tiempo real en su terminal.

```
+----------------+      PUB (Tema A)      +----------------+      MSG (Tema A)      +-------------------+
|  Publisher 1   | ---------------------> |                | ---------------------> |   Subscriber 1    |
+----------------+                        |                |                        | (Suscrito Tema A) |
                                          |     BROKER     |                        +-------------------+
+----------------+      PUB (Tema B)      | (TCP / UDP)    |      MSG (Tema A y B)  +-------------------+
|  Publisher 2   | ---------------------> |                | ---------------------> |   Subscriber 2    |
+----------------+                        +----------------+                        | (Temas A y B)     |
                                                                                    +-------------------+
```

---

## 2. Estructura Organizada del Proyecto

```text
Laboratorio_3/
├── tcp/                     # Módulo TCP (broker, publicador, suscriptor y Makefile)
│   ├── broker_tcp.c
│   ├── publisher_tcp.c
│   ├── subscriber_tcp.c
│   ├── readme_usartcp
│   └── Makefile
├── udp/                     # Módulo UDP (broker, publicador, suscriptor y Makefile)
│   ├── broker_udp.c
│   ├── publisher_udp.c
│   ├── subscriber_udp.c
│   ├── README_UDP.md
│   └── Makefile
├── quic/                    # Módulo Bono: Protocolo QUIC / HTTP/3 (Intacto)
│   ├── broker_quic.c
│   ├── publisher_quic.c
│   ├── subscriber_quic.c
│   ├── cert.pem, key.pem
│   ├── README_QUIC.md
│   └── Makefile
├── pruebas/                 # Suite de automatización y cargas de prueba
│   ├── test_tcp.sh
│   ├── test_udp.sh
│   ├── test_perdida_udp.sh
│   ├── eventos_partido_a.txt
│   ├── eventos_partido_b.txt
│   └── evidencia_perdida_pantallazo.txt
├── capturas/                # Archivos oficiales de tráfico de red para Wireshark
│   ├── tcp_pubsub.pcap (y .pcapng)
│   └── udp_pubsub.pcap (y .pcapng)
├── logs/                    # Registros de auditoría generados por las pruebas
│   ├── *.log
├── Makefile                 # Compilador global modular
├── README.md                # Documentación principal del sistema
├── RESPUESTAS_INFORME.md    # Respuestas detalladas a las 10 preguntas
└── COMANDOS_PRUEBAS.txt     # Guía rápida de comandos
```

---

## 3. Descripción de Archivos y su Importancia

A continuación se detalla la función de cada archivo dentro del repositorio y su relevancia para los objetivos del proyecto:

### 2.1. Implementación TCP (Confiable y Orientada a Conexión)

* **`broker_tcp.c`**
  * **Qué hace:** Servidor central TCP que escucha en el puerto `5050`. Utiliza la llamada al sistema `select()` para multiplexar I/O de hasta 64 clientes concurrentes sin necesidad de hilos pesados. Administra la lista de suscripciones por cliente, valida el formato del protocolo (`SUB <tema>` y `PUB <tema> <mensaje>`) y reenvía los mensajes únicamente a los sockets de clientes suscritos.
  * **Por qué es importante:** Demuestra cómo se gestiona la concurrencia en servidores monohilo en Linux y garantiza que ningún mensaje se pierda gracias a la confiabilidad de TCP.

* **`publisher_tcp.c`**
  * **Qué hace:** Cliente TCP que establece una conexión de 3 vías (*3-way handshake*) con el broker hacia un partido determinado. Lee eventos línea por línea y los envía mediante la función auxiliar `send_all()` para garantizar que todos los bytes del buffer sean transferidos a través del socket de flujo.
  * **Por qué es importante:** Modela al publicador deportivo garantizando que las noticias salgan en estricto orden cronológico hacia el broker.

* **`subscriber_tcp.c`**
  * **Qué hace:** Cliente TCP que se conecta al broker y envía comandos `SUB <tema>` para uno o múltiples partidos (hasta 16 simultáneos). Mantiene un ciclo continuo de recepción con `recv()` y procesa el flujo continuo de bytes ensamblando tramas delimitadas por salto de línea (`\n`).
  * **Por qué es importante:** Permite evidenciar el manejo de streams de bytes en TCP (framing de aplicación) y verificar que los hinchas reciben exactamente los partidos que solicitaron en orden.

---

### 2.2. Implementación UDP (Datagramas sin Conexión)

* **`broker_udp.c`**
  * **Qué hace:** Servidor sin conexión que escucha en el puerto `9090` mediante `SOCK_DGRAM`. Mantiene una tabla interna en memoria de tuplas `(IP, Puerto, Tema)` activas. Procesa datagramas `SUB|tema`, `UNSUB|tema` y `PUB|tema|pubid|seq|texto`, reenviando cada publicación a la dirección socket de los clientes inscritos mediante `sendto()`. Responde con confirmaciones `OK|SUB|tema`.
  * **Por qué es importante:** Muestra cómo opera un broker desacoplado sin sobrecarga de conexiones persistentes ni handshakes a nivel de transporte.

* **`publisher_udp.c`**
  * **Qué hace:** Emisor de datagramas UDP que inyecta números de secuencia (`seq`) a cada mensaje para suplir la falta de orden del protocolo. Permite configurar la cantidad de mensajes y el intervalo en milisegundos (`intervalo_ms`), lo que permite realizar transmisiones espaciadas o ráfagas directas.
  * **Por qué es importante:** Facilita las pruebas de estrés del protocolo y permite comprobar cómo UDP envía datagramas independientes hacia el destino.

* **`subscriber_udp.c`**
  * **Qué hace:** Receptor UDP que envía solicitudes de suscripción con mecanismo de confirmación y reintentos (hasta 3 intentos de `SUB` con timeout `SO_RCVTIMEO`). Analiza cada mensaje recibido contra un histórico de secuencias para detectar:
    * Mensajes únicos recibidos.
    * Paquetes perdidos (huecos de secuencia).
    * Mensajes duplicados.
    * Mensajes fuera de orden.
    Al recibir `SIGINT` (Ctrl+C), envía `UNSUB` al broker e imprime un resumen estadístico detallado.
  * **Por qué es importante:** Resuelve a nivel de aplicación las deficiencias inherentes de UDP (falta de confiabilidad y orden), permitiendo obtener métricas cuantitativas para el análisis del laboratorio.

---

### 2.3. Automatización, Pruebas y Construcción

* **`Makefile`**
  * **Qué hace:** Archivo de compilación estandarizado con `gcc` utilizando las banderas estrictas del laboratorio: `-std=c11 -Wall -Wextra -Wpedantic`. Provee metas como `make`, `make tcp`, `make udp` y `make clean`.
  * **Por qué es importante:** Garantiza que el código compile sin advertencias (*warnings*) ni errores en cualquier distribución Linux, evitando binarios corruptos o incompatibles entre sistemas operativos.

* **`test_tcp.sh`**
  * **Qué hace:** Script bash que automatiza la prueba completa requerida por la guía: inicia en segundo plano 1 Broker TCP, 2 Suscriptores y 2 Publicadores, inyecta 10 eventos por publicador, redirige la salida a logs individuales, valida la cantidad de mensajes recibidos y detiene los procesos ordenadamente.
  * **Por qué es importante:** Permite reproducir la prueba obligatoria del laboratorio en 5 segundos sin intervención manual y sin necesidad de abrir 5 terminales simultáneas.

* **`test_udp.sh`**
  * **Qué hace:** Script bash análogo para UDP: ejecuta 1 Broker UDP, 2 Suscriptores y 2 Publicadores enviando 10 mensajes cada uno, finaliza con señal `SIGINT` para activar los resúmenes estadísticos e imprime en consola el reporte de pérdida y orden.
  * **Por qué es importante:** Permite recopilar de inmediato las estadísticas requeridas por la guía para ambos suscriptores y sincronizar fácilmente la captura de tráfico en Wireshark.

* **`eventos_partido_a.txt` y `eventos_partido_b.txt`**
  * **Qué hacen:** Archivos con 10 eventos de fútbol cronológicos cada uno (*Colombia vs Argentina* y *Brasil vs Uruguay*).
  * **Por qué son importantes:** Sirven como datos de entrada limpios y consistentes para las pruebas de publicadores.

* **`.gitignore`**
  * **Qué hace:** Reglas para ignorar ejecutables nativos (`broker_*`, `publisher_*`, `subscriber_*`), archivos objeto (`*.o`), logs (`*.log`) y archivos temporales.
  * **Por qué es importante:** Evita subir archivos binarios compilados en un sistema operativo particular (como macOS) que generen errores de ejecución en las máquinas de otros compañeros o profesores.

* **`tcp_pubsub.pcap` / `.pcapng` y `udp_pubsub.pcap` / `.pcapng`**
  * **Qué hacen:** Archivos de captura de paquetes generados con Wireshark en la interfaz loopback (`lo`).
  * **Por qué son importantes:** Son entregables obligatorios solicitados en la sección 4 y 7 de la guía del laboratorio.

* **`README_UDP.md` y `quic/`**
  * **Qué hacen:** Documentación complementaria del módulo UDP elaborada por el equipo, e implementación experimental opcional del protocolo QUIC sobre UDP.

---

## 3. ¿Qué van a evaluar los Monitores y Profesores?

De acuerdo con el documento oficial **`Laboratorio 3 - Entregable.pdf`**, la evaluación del laboratorio se centra en los siguientes criterios clave:

### 1. Implementación Estricta en C con Sockets Nativos (Sección 5.1 y 6.2)
* **Criterio:** Todo el código debe estar escrito en lenguaje C y compilado con `gcc` en Linux.
* **Qué revisan:**
  * Uso directo de las llamadas al sistema POSIX: `socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`, `sendto()`, `recvfrom()`.
  * **No está permitido el uso de librerías de alto nivel de sockets** a menos que estén 100% documentadas función por función. En nuestro caso, se utilizó únicamente la librería estándar de C y headers POSIX, cumpliendo a cabalidad con la norma.

### 2. Concurrencia y Filtrado del Modelo Pub-Sub (Sección 5.2.1)
* **Criterio:** Ejecutar simultáneamente **al menos 1 Broker, 2 Suscriptores y 2 Publicadores**.
* **Qué revisan:**
  * Que el Broker maneje múltiples clientes sin bloquearse (en TCP mediante `select()` y en UDP por naturaleza de datagramas).
  * Que el Suscriptor 1 reciba únicamente los eventos del partido al que se suscribió (ej. *PartidoA*).
  * Que el Suscriptor 2 reciba los eventos de ambos partidos si se suscribió a ambos (*PartidoA* y *PartidoB*).
  * Que el Broker **no modifique el contenido** de los mensajes recibidos.

### 3. Volumen de Mensajes (Sección 6.3)
* **Criterio:** Cada publicador debe emitir **al menos 10 mensajes**.
* **Qué revisan:** En los logs y capturas de red debe quedar constancia de que se enviaron y recibieron mínimo 20 mensajes en total (10 de cada publicador).

### 4. Archivos de Captura Wireshark (Sección 6.4 y 7.1)
* **Criterio:** Adjuntar o enlazar dos capturas de tráfico válidas: `tcp_pubsub.pcap` y `udp_pubsub.pcap`.
* **Qué revisan:**
  * **En TCP:** Presencia del saludo de 3 vías (*SYN, SYN-ACK, ACK*), banderas de datos (*PSH, ACK*), confirmaciones de recepción (*ACKs* inmediatos o acumulados) y cierre ordenado (*FIN, ACK*).
  * **En UDP:** Ausencia de fase de conexión previa, envío directo de datagramas `SUB` y `PUB`, y tamaño de la cabecera UDP (8 bytes fijos).

### 5. Tabla Comparativa de Desempeño (Sección 6.5)
* **Criterio:** Contrastar cuantitativa y cualitativamente TCP vs UDP bajo cuatro dimensiones:
  1. **Confiabilidad:** Garantía de entrega (ACKs y retransmisiones automáticas en TCP vs *Best-Effort* en UDP).
  2. **Orden de entrega:** Garantizado por números de secuencia en TCP vs sin garantía en UDP (mitigado a nivel de aplicación).
  3. **Pérdida de mensajes:** 0% de pérdida en TCP vs posibilidad de pérdida ante congestión/descarte en UDP.
  4. **Overhead de cabeceras/protocolo:** Cabecera TCP (20 a 32 bytes + paquetes de control SYN/ACK) vs Cabecera UDP (8 bytes fijos sin paquetes de control de transporte).

### 6. Respuestas a las 10 Preguntas de Análisis (Sección 6.6)
* **Criterio:** Responder de forma fundamentada y rigurosa a las 10 preguntas conceptuales:
  1. Escalamiento a 100 partidos simultáneos (desempeño de TCP vs UDP en el broker).
  2. Pérdida del mensaje de un gol en UDP y su impacto en la experiencia real.
  3. Protocolo más adecuado para streaming deportivo en vivo.
  4. Comparación cuantitativa del overhead observado en Wireshark.
  5. Desorden del marcador (recibir 2-1 antes del 1-1) y soluciones a nivel de aplicación.
  6. Aumento de suscriptores por partido (efecto de abanico/fan-out).
  7. Caída inesperada del broker y recuperación de sesión.
  8. Sincronización instantánea de eventos críticos entre suscriptores.
  9. Consumo de CPU y memoria en el broker (descriptores de archivo, tablas y select vs recvfrom).
  10. Diseño arquitectural para millones de usuarios (enfoque híbrido / WebSockets / QUIC / CDN).

### 7. Formato del Entregable y Código Fuente (Sección 7)
* **Criterio:** 
  * Informe en PDF con capturas legibles de terminal y Wireshark con títulos explicativos.
  * Enlace accesible a los archivos `.pcap`.
  * Archivo comprimido `.zip` con el código en C limpio, modular, bien comentado y libre de errores de compilación.

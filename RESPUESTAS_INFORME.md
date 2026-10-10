# Respuestas a las Preguntas de Análisis (Laboratorio #3)
**Sección 6.6 de la Guía Oficial / Criterio 3 de la Rúbrica**

---

### 1. ¿Qué ocurriría si en lugar de dos publicadores (partidos transmitidos) hubiera cien partidos simultáneos? ¿Cómo impactaría esto en el desempeño del broker bajo TCP y bajo UDP?

* **Bajo TCP:**  
  El broker sufriría un impacto severo a nivel de recursos del sistema operativo y latencia de multiplexación:
  1. *Agotamiento de Descriptores de Archivo:* Cada publicador y cada suscriptor requiere un socket TCP dedicado. Con 100 partidos y miles de hinchas conectados, el broker superaría rápidamente el límite por defecto de descriptores de archivo en Linux (`FD_SETSIZE = 1024` en llamadas `select()`). El broker actual implementado con `select()` colapsaría a menos que se migre a `epoll()`.
  2. *Degradación por Bucle O(N):* `select()` debe escanear secuencialmente todos los descriptores en cada iteración para comprobar cuáles tienen datos listos, incrementando drásticamente el consumo de CPU y la latencia de reenvío.
  3. *Consumo de Memoria por Socket:* El kernel reserva buffers dedicados de transmisión (`SO_SNDBUF`) y recepción (`SO_RCVBUF`) por cada conexión activa (de 16 KB a más de 128 KB por socket), demandando gran cantidad de memoria RAM.

* **Bajo UDP:**  
  1. *Un solo descriptor de archivo:* El broker UDP opera sobre un **único socket** para recibir y reenviar todos los datagramas, independientemente del número de partidos o suscriptores. No mantiene conexiones abiertas ni estados de sesión en el kernel.
  2. *Riesgo de Saturación de Cola de Entrada:* El cuello de botella en UDP se traslada al buffer de entrada del socket. Si 100 partidos generan ráfagas concurrentes, el hilo único del broker llamando a `recvfrom()` no podrá drenar los datagramas a la velocidad de llegada, provocando pérdidas masivas de mensajes por desbordamiento del buffer del kernel (`SO_RCVBUF`). Para mitigar esto se requeriría una arquitectura multihilo o múltiples sockets ligados al mismo puerto mediante `SO_REUSEPORT`.

---

### 2. Si un gol se envía como mensaje desde el publicador y un suscriptor no lo recibe en UDP, ¿qué implicaciones tendría para la aplicación real? ¿Por qué TCP maneja mejor este escenario?

* **Implicaciones para la aplicación real:**  
  La pérdida de un evento crítico como un gol destruye por completo la coherencia del estado del partido en la pantalla del usuario. El hincha observaría un marcador congelado o desactualizado (por ejemplo, manteniéndose 0-0 en vez de 1-0). Si posteriormente llega una anotación posterior, el usuario presenciaría un salto inexplicable (ver de repente un 2-0 sin haber recibido nunca el primer gol) o una discrepancia entre el texto narrativo ("Gol de Falcao al min 35") y el contador de goles. Esto arruina la confiabilidad del servicio ante los usuarios.

* **Por qué TCP maneja mejor este escenario:**  
  TCP es un protocolo con **garantía de entrega**. Cada segmento TCP lleva un número de secuencia asociado a los bytes transmitidos. Si el paquete que contiene el gol se pierde en el canal, el receptor no enviará el acuse de recibo (`ACK`) correspondiente. Al vencerse el temporizador de retransmisión (*Retransmission Timeout - RTO*) o al detectarse tres ACKs duplicados (*Fast Retransmit*), la pila TCP del emisor retransmite automáticamente el segmento perdido. El proceso receptor no entrega los datos siguientes a la aplicación hasta que el paquete perdido sea recuperado en el buffer, garantizando que el gol siempre llegue antes que los eventos posteriores.

---

### 3. En un escenario de seguimiento en vivo de partidos, ¿qué protocolo (TCP o UDP) resultaría más adecuado? Justifique con base en los resultados de la práctica.

* **Respuesta:** **TCP (o protocolos de nivel superior basados en TCP como WebSockets / Server-Sent Events)** resulta sustancialmente más adecuado para este escenario.
* **Justificación técnica:**
  1. *Naturaleza del contenido:* En el seguimiento de noticias y estadísticas deportivas, la información consiste en **eventos discretos de texto** que ocurren a intervalos de varios segundos o minutos (no a 60 cuadros por segundo como un videojuego o streaming de video).
  2. *Prioridad de la Confiabilidad sobre la Latencia Extrema:* Un retraso de 20 a 50 milisegundos provocado por el handshake o una eventual retransmisión en TCP es completamente imperceptible para un ser humano leyendo una noticia. En cambio, perder un mensaje de gol o expulsión en UDP es una falla catastrófica e inaceptable para la experiencia del usuario.
  3. *Garantía de Orden:* Como se comprobó en la práctica, TCP garantiza orden estricto de los eventos de forma nativa. Usar UDP obligaría a reinventar en la capa de aplicación mecanismos de confirmación, retransmisión y secuenciamiento, terminando por implementar un TCP incompleto y menos optimizado que el que ya provee el kernel.

*(Nota: UDP solo sería preferible si se transmitiera la señal de audio/video en tiempo real del partido, pero para las alertas y el marcador textual, TCP es la elección correcta).*

---

### 4. Compare el overhead observado en las capturas Wireshark entre TCP y UDP. ¿Cuál protocolo introduce más cabeceras por mensaje? ¿Cómo influye esto en la eficiencia?

* **Comparación de Cabeceras:**  
  * **UDP:** Posee una cabecera fija y mínima de **8 bytes** (Source Port: 2B, Destination Port: 2B, Length: 2B, Checksum: 2B). No añade ningún byte de opciones ni requiere paquetes previos de control.
  * **TCP:** Posee una cabecera base de **20 bytes**, que en la captura real alcanzó los **32 bytes** debido a las opciones negociadas (12 bytes correspondientes a *Timestamps*, *SACK Permitted* y *No-Operation*).

* **Sobrecarga de Paquetes de Control:**  
  Para transmitir los mismos 10 mensajes:
  * TCP necesitó **3 paquetes iniciales para el 3-Way Handshake** (`SYN`, `SYN-ACK`, `ACK`), paquetes dedicados de confirmación `ACK` de 66 bytes (sin datos útiles) y **4 paquetes para el cierre ordenado de la sesión** (`FIN`, `ACK`).
  * UDP envió únicamente los datagramas con datos útiles, sin saludo ni paquetes de control en la capa de transporte.

* **Influencia en la Eficiencia:**  
  En transmisiones con cargas útiles pequeñas (como las noticias deportivas, cuyos mensajes miden entre 25 y 60 bytes), **TCP resulta significativamente menos eficiente en uso de ancho de banda**. En nuestra captura de TCP, más del 50% de los bytes transferidos correspondieron a cabeceras y paquetes de control. En UDP, como se observó en la ventana *Protocol Hierarchy*, los datos útiles representaron el 53.4% del tráfico total y las cabeceras UDP solo 472 bytes en 59 paquetes. Sin embargo, este costo extra de TCP es el precio que se paga para obtener confiabilidad absoluta y control de congestión.

---

### 5. Si el marcador de un partido llega desordenado en UDP (por ejemplo, primero se recibe el 2–1 y luego el 1–1), ¿qué efectos tendría en la experiencia del usuario? ¿Cómo podría solucionarse este problema a nivel de aplicación?

* **Efectos en la experiencia del usuario:**  
  Genera desconcierto, frustración y pérdida total de credibilidad en la plataforma. El usuario observaría en su pantalla que el marcador subió a 2–1 y segundos después "retrocedió" a 1–1, o vería notificaciones de goles en orden cronológico inverso, creyendo que el resultado fue anulado o que la aplicación tiene fallas críticas de sincronización.

* **Solución a nivel de aplicación:**  
  Como UDP no garantiza el orden de llegada, la aplicación debe implementar su propio control de secuencia:
  1. *Identificador Secuencial Monótono:* Incluir un número de secuencia incremental (`seq`) o una marca de tiempo precisa (*timestamp*) en la cabecera de aplicación de cada mensaje (tal como implementamos en `PUB|tema|pubid|seq|texto`).
  2. *Buffer de Reordenamiento (Jitter Buffer):* El cliente suscriptor retiene temporalmente los mensajes entrantes en una cola ordenada por `seq`. Si llega el mensaje #5 (marcador 2–1) y el último procesado fue el #3, el cliente espera un breve periodo (ej. 200 ms) antes de pintarlo en pantalla, permitiendo que el mensaje #4 (marcador 1–1) llegue y se procese primero.
  3. *Descarte de Mensajes Obsoletos:* Si el mensaje retrasado (#4) llega después de que el estado actual (#5) ya fue mostrado en pantalla, el suscriptor simplemente lo descarta al verificar que `seq_recibido <= ultimo_seq_mostrado`.

---

### 6. ¿Cómo cambia el desempeño del sistema cuando aumenta el número de suscriptores interesados en un mismo partido? ¿Qué diferencias se observaron entre TCP y UDP en este aspecto?

* **Comportamiento General (Efecto Fan-Out):**  
  Al aumentar el número de suscriptores $N$ para un mismo partido, el broker debe multiplicar cada publicación recibida: por cada mensaje entrante, el broker debe emitir $N$ envíos individuales.

* **Diferencias TCP vs UDP:**
  * **En TCP:**  
    El broker debe mantener $N$ descriptores de socket abiertos y realizar $N$ llamadas a `send()` por cada evento. Si uno o varios clientes tienen conexiones de red lentas o congestionadas, sus buffers de transmisión TCP (`SO_SNDBUF`) se llenan rápidamente. Si el socket se bloquea o el broker tarda en enviar a los clientes lentos, todo el bucle de despacho se degrada (*Head-of-Line Blocking* a nivel de servidor), afectando a los demás suscriptores.
  * **En UDP:**  
    El broker despacha los $N$ datagramas en un bucle rápido de `sendto()` directo hacia las direcciones IP y puertos registradas en su tabla, sin esperar acuses de recibo ni verificar el estado de los buffers del cliente. Además, UDP ofrece la ventaja teórica de soportar **IP Multicast**: en una red compatible, el broker podría emitir un único datagrama hacia una dirección de grupo multicast y los enrutadores de red se encargarían de clonarlo hacia todos los suscriptores, reduciendo el costo de salida de $O(N)$ a $O(1)$.

---

### 7. ¿Qué sucede si el broker se detiene inesperadamente? ¿Qué diferencias hay entre TCP y UDP en la capacidad de recuperación de la sesión?

* **En TCP:**  
  * *Detección Inmediata:* Si el proceso del broker finaliza de forma abrupta, el sistema operativo del servidor cierra los sockets y envía automáticamente paquetes `FIN` o `RST` a todos los clientes conectados.
  * *Comportamiento del Cliente:* La función `recv()` en los suscriptores y publicadores retorna inmediatamente `0` (cierre ordenado) o `-1` con error `ECONNRESET`. Los clientes se enteran al instante de la caída del broker, lo que les permite cerrar sus sockets e iniciar de inmediato una rutina de reintento de reconexión (*reconnect backoff*).
* **En UDP:**  
  * *Silencio Total:* Como UDP no mantiene estado de conexión ni sesiones en el kernel, la caída del broker no genera ningún paquete de notificación hacia los clientes.
  * *Comportamiento del Cliente:* Los suscriptores continúan bloqueados indefinidamente en su llamada `recvfrom()`, asumiendo simplemente que "no han ocurrido eventos en el partido".
  * *Capacidad de Recuperación:* Para recuperarse, la aplicación en UDP debe implementar obligatoriamente un mecanismo de latido (*heartbeat/keepalive*) o temporizador de inactividad (`SO_RCVTIMEO`). Si pasa cierto tiempo sin recibir tráfico del broker, el suscriptor asume la pérdida de contacto y reenvía su solicitud de suscripción `SUB`.

---

### 8. ¿Cómo garantizar que todos los suscriptores reciban en el mismo instante las actualizaciones críticas (por ejemplo, un gol)? ¿Qué protocolo facilita mejor esta sincronización y por qué?

* **Cómo garantizar la recepción simultánea:**  
  Para que millones de hinchas canten el gol al mismo tiempo y se evite el fenómeno del "spoiler" (escuchar el grito del vecino antes de que aparezca en pantalla), se requiere:
  1. Minimizar el *jitter* (variación en el retardo de paquetes).
  2. Evitar retrasos inducidos por retransmisiones acumulativas.
  3. Utilizar marcas de tiempo sincronizadas por NTP (*Network Time Protocol*) para renderizar el evento en pantalla en un instante predefinido.

* **Protocolo que facilita mejor la sincronización:**  
  **UDP facilita sustancialmente mejor la sincronización temporal simultánea**.
* **Por qué:**  
  TCP sufre del problema de **bloqueo de inicio de línea** (*Head-of-Line Blocking*): si un solo paquete se pierde en el camino hacia un suscriptor con mala cobertura, TCP congela la entrega de todos los paquetes subsiguientes hasta que el paquete perdido sea retransmitido con éxito (lo que añade cientos de milisegundos de retraso). En UDP, en cambio, los datagramas viajan a velocidad de cable sin esperar a nadie; si un suscriptor pierde un paquete, los paquetes siguientes se entregan de inmediato sin retrasar la línea de tiempo global. Asimismo, UDP permite el uso de Multicast a nivel de red para sincronización simultánea masiva.

---

### 9. Analice el uso de CPU y memoria en el broker cuando maneja múltiples conexiones TCP frente al manejo de datagramas UDP. ¿Qué diferencias encontró?

* **Uso de Memoria en el Broker:**  
  * **TCP (Alto Consumo):** Por cada cliente conectado, el kernel debe asignar una estructura de control de transmisión (*Transmission Control Block - TCB*) y reservar dos buffers dedicados: buffer de recepción (`SO_RCVBUF`, mínimo 87 KB por defecto en Linux) y buffer de envío (`SO_SNDBUF`, mínimo 16 KB a 64 KB). Miles de conexiones TCP consumen cientos de megabytes de memoria de kernel exclusivamente en buffers de red.
  * **UDP (Bajo Consumo):** El broker solo requiere un buffer de socket único para su puerto de escucha. La memoria consumida se limita a la tabla de suscripciones que diseñamos en la aplicación (un arreglo estático de estructuras `Suscripcion` de unos pocos kilobytes).

* **Uso de CPU en el Broker:**  
  * **TCP:** La CPU experimenta una carga más alta. El kernel debe procesar la máquina de estados de TCP (handshake, flags, cálculo de RTT, temporizadores de retransmisión, empaquetado de ACKs). Además, la llamada `select()` requiere que la CPU recorra iterativamente un mapa de bits de descriptores de tamaño proporcional a la cantidad de conexiones activas.
  * **UDP:** Carga de CPU sustancialmente menor. El kernel no calcula ventanas ni estados; se limita a calcular el checksum básico, verificar la dirección de destino y pasar el datagrama al espacio de usuario con `recvfrom()`.

---

### 10. Si tuviera que diseñar un sistema real de transmisión de actualizaciones de partidos de fútbol para millones de usuarios, ¿elegiría TCP, UDP o una combinación de ambos? Justifique con base en lo observado en el laboratorio.

* **Respuesta:** Diseñaría un **enfoque HÍBRIDO (o basado en el protocolo moderno QUIC / HTTP/3)**.

* **Justificación de la arquitectura real:**
  En una plataforma moderna a escala masiva (como SofaScore, FlashScore o ESPN), los datos que viajan hacia los aficionados se dividen en dos categorías con requerimientos completamente opuestos:

  1. **Para Eventos Críticos y Marcador (Texto y Datos Estructurados):**  
     Se debe utilizar **TCP**, implementado mediante **WebSockets** o **Server-Sent Events (SSE)** sobre HTTPS, distribuido a través de una red de servidores perimetrales (**CDN** como Cloudflare o Akamai) y colas de mensajería (Apache Kafka / Redis Pub-Sub):  
     * *Razón:* Como observamos en el laboratorio, un gol o una tarjeta roja no puede perderse ni llegar desordenado. El overhead de TCP es despreciable para mensajes textuales de pocos bytes, y la CDN se encarga de absorber la concurrencia de conexiones persistentes cerca del usuario final.

  2. **Para la Transmisión de Audio y Video en Vivo (Streaming Multimedia):**  
     Se debe utilizar **UDP**, mediante tecnologías como **WebRTC**, **SRT** o **RTP/RTCP**:  
     * *Razón:* El video genera millones de paquetes por segundo. Si se pierde un frame milimétrico, es preferible descartarlo y continuar con el siguiente frame en vivo que congelar la imagen esperando una retransmisión TCP.

  3. **La Evolución Moderna: QUIC (HTTP/3):**  
     En la frontera tecnológica actual, la mejor solución unificada es **QUIC**, un protocolo de capa de transporte que corre sobre **UDP**, pero implementa confiabilidad, control de congestión y multiplexación de streams independientes en el espacio de usuario. Esto elimina el *Head-of-Line Blocking* de TCP mientras garantiza que las noticias del partido lleguen completas y en orden.

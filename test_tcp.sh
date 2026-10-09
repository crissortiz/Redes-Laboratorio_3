#!/usr/bin/env bash
# ==============================================================================
# Script de Automatización de Pruebas - Protocolo TCP
# Sistema Publicador-Suscriptor de Noticias Deportivas
# ==============================================================================

set -e
PUERTO=5050
DIR_ACTUAL="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR_ACTUAL"

echo "=================================================="
echo "    INICIANDO PRUEBA DE AUTOMATIZACIÓN (TCP)      "
echo "=================================================="

# 1. Asegurar que los binarios estén compilados
if [ ! -f ./broker_tcp ] || [ ! -f ./subscriber_tcp ] || [ ! -f ./publisher_tcp ]; then
    echo "[!] Compilando programas TCP..."
    gcc -std=c11 -Wall -Wextra broker_tcp.c -o broker_tcp
    gcc -std=c11 -Wall -Wextra publisher_tcp.c -o publisher_tcp
    gcc -std=c11 -Wall -Wextra subscriber_tcp.c -o subscriber_tcp
fi

# Limpiar procesos previos en el puerto si existieran
fuser -k ${PUERTO}/tcp 2>/dev/null || true
sleep 0.5

# 2. Iniciar Broker TCP
echo "[1/4] Iniciando Broker TCP en puerto $PUERTO..."
./broker_tcp $PUERTO > broker_tcp.log 2>&1 &
PID_BROKER=$!
sleep 1

# 3. Iniciar Suscriptores
echo "[2/4] Iniciando Suscriptor 1 (Tema: PartidoA)..."
./subscriber_tcp 127.0.0.1 $PUERTO PartidoA > sub1_tcp.log 2>&1 &
PID_SUB1=$!

echo "[2/4] Iniciando Suscriptor 2 (Temas: PartidoA y PartidoB)..."
./subscriber_tcp 127.0.0.1 $PUERTO PartidoA PartidoB > sub2_tcp.log 2>&1 &
PID_SUB2=$!
sleep 1

# 4. Iniciar Publicadores (enviando 10 eventos cada uno con 0.3s de pausa)
echo "[3/4] Enviando 10 eventos desde Publicador 1 (PartidoA)..."
(
    while IFS= read -r linea; do
        echo "$linea"
        sleep 0.3
    done < eventos_partido_a.txt
) | ./publisher_tcp 127.0.0.1 $PUERTO PartidoA > pub1_tcp.log 2>&1 &
PID_PUB1=$!

echo "[3/4] Enviando 10 eventos desde Publicador 2 (PartidoB)..."
(
    while IFS= read -r linea; do
        echo "$linea"
        sleep 0.3
    done < eventos_partido_b.txt
) | ./publisher_tcp 127.0.0.1 $PUERTO PartidoB > pub2_tcp.log 2>&1 &
PID_PUB2=$!

# Esperar a que terminen los publicadores
wait $PID_PUB1
echo "  -> Publicador 1 finalizó el envío de 10 eventos."
wait $PID_PUB2
echo "  -> Publicador 2 finalizó el envío de 10 eventos."

# Dar un momento para que los suscriptores reciban todo
sleep 1

# 5. Detener procesos
echo "[4/4] Finalizando procesos de prueba..."
kill $PID_SUB1 $PID_SUB2 $PID_BROKER 2>/dev/null || true
wait $PID_SUB1 2>/dev/null || true
wait $PID_SUB2 2>/dev/null || true
wait $PID_BROKER 2>/dev/null || true

echo ""
echo "=================================================="
echo "               RESULTADOS DE LA PRUEBA            "
echo "=================================================="
echo "--- Mensajes recibidos por Suscriptor 1 (Esperados: 10 de PartidoA) ---"
cat sub1_tcp.log | grep -E "^MSG" || cat sub1_tcp.log
echo ""
echo "--- Mensajes recibidos por Suscriptor 2 (Esperados: 20 -> PartidoA y PartidoB) ---"
cat sub2_tcp.log | grep -E "^MSG" || cat sub2_tcp.log
echo ""
echo "Total de mensajes capturados en logs:"
echo "Suscriptor 1: $(grep -c "^MSG" sub1_tcp.log || echo 0) mensajes"
echo "Suscriptor 2: $(grep -c "^MSG" sub2_tcp.log || echo 0) mensajes"
echo "=================================================="
echo "¡Prueba TCP completada con éxito!"

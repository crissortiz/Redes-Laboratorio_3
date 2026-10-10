#!/usr/bin/env bash

set -e
PUERTO=9090
DIR_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR_RAIZ="$(cd "$DIR_SCRIPT/.." && pwd)"
cd "$DIR_RAIZ"

mkdir -p logs

echo "    INICIANDO PRUEBA DE AUTOMATIZACIÓN (UDP)      "

# 1. Asegurar que los binarios estén compilados
if [ ! -f ./udp/broker_udp ] || [ ! -f ./udp/subscriber_udp ] || [ ! -f ./udp/publisher_udp ]; then
    echo "[!] Compilando programas UDP..."
    make -C udp
fi

# Limpiar procesos previos en el puerto si existieran
fuser -k ${PUERTO}/udp 2>/dev/null || true
sleep 0.5

# 2. Iniciar Broker UDP
echo "[1/4] Iniciando Broker UDP en puerto $PUERTO..."
./udp/broker_udp $PUERTO > logs/broker_udp.log 2>&1 &
PID_BROKER=$!
sleep 1

# 3. Iniciar Suscriptores
echo "[2/4] Iniciando Suscriptor 1 (Tema: PartidoA)..."
./udp/subscriber_udp 127.0.0.1 $PUERTO PartidoA > logs/sub1_udp.log 2>&1 &
PID_SUB1=$!

echo "[2/4] Iniciando Suscriptor 2 (Temas: PartidoA y PartidoB)..."
./udp/subscriber_udp 127.0.0.1 $PUERTO PartidoA PartidoB > logs/sub2_udp.log 2>&1 &
PID_SUB2=$!
sleep 1.5

# 4. Iniciar Publicadores (10 mensajes cada uno, intervalo de 300ms)
echo "[3/4] Enviando 10 eventos desde Publicador 1 (PartidoA / P1)..."
./udp/publisher_udp 127.0.0.1 $PUERTO PartidoA P1 10 300 > logs/pub1_udp.log 2>&1 &
PID_PUB1=$!

echo "[3/4] Enviando 10 eventos desde Publicador 2 (PartidoB / P2)..."
./udp/publisher_udp 127.0.0.1 $PUERTO PartidoB P2 10 300 > logs/pub2_udp.log 2>&1 &
PID_PUB2=$!

# Esperar a que terminen los publicadores
wait $PID_PUB1
echo "  -> Publicador 1 finalizó el envío."
wait $PID_PUB2
echo "  -> Publicador 2 finalizó el envío."

# Dar un momento para procesar los datagramas finales
sleep 1

# 5. Detener procesos enviando SIGINT (para que el suscriptor y broker impriman resumen)
echo "[4/4] Enviando señal de cierre y recolectando estadísticas..."
kill -2 $PID_SUB1 $PID_SUB2 $PID_BROKER 2>/dev/null || true
wait $PID_SUB1 2>/dev/null || true
wait $PID_SUB2 2>/dev/null || true
wait $PID_BROKER 2>/dev/null || true

echo ""
echo "           RESUMEN ESTADÍSTICO UDP               "
echo "--- Resumen Suscriptor 1 (Esperados: 10 de PartidoA) ---"
cat logs/sub1_udp.log | grep -A 5 "RESUMEN DEL SUSCRIPTOR" || cat logs/sub1_udp.log
echo ""
echo "--- Resumen Suscriptor 2 (Esperados: 20 -> 10 PartidoA y 10 PartidoB) ---"
cat logs/sub2_udp.log | grep -A 5 "RESUMEN DEL SUSCRIPTOR" || cat logs/sub2_udp.log
echo "¡Prueba UDP completada con éxito! (Logs guardados en logs/)"

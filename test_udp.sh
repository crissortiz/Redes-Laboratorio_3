#!/usr/bin/env bash
# ==============================================================================
# Script de Automatización de Pruebas - Protocolo UDP
# Sistema Publicador-Suscriptor de Noticias Deportivas
# ==============================================================================

set -e
PUERTO=9090
DIR_ACTUAL="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR_ACTUAL"

echo "=================================================="
echo "    INICIANDO PRUEBA DE AUTOMATIZACIÓN (UDP)      "
echo "=================================================="

# 1. Asegurar que los binarios estén compilados
if [ ! -f ./broker_udp ] || [ ! -f ./subscriber_udp ] || [ ! -f ./publisher_udp ]; then
    echo "[!] Compilando programas UDP..."
    gcc -std=c11 -Wall -Wextra broker_udp.c -o broker_udp
    gcc -std=c11 -Wall -Wextra publisher_udp.c -o publisher_udp
    gcc -std=c11 -Wall -Wextra subscriber_udp.c -o subscriber_udp
fi

# Limpiar procesos previos en el puerto si existieran
fuser -k ${PUERTO}/udp 2>/dev/null || true
sleep 0.5

# 2. Iniciar Broker UDP
echo "[1/4] Iniciando Broker UDP en puerto $PUERTO..."
./broker_udp $PUERTO > broker_udp.log 2>&1 &
PID_BROKER=$!
sleep 1

# 3. Iniciar Suscriptores
echo "[2/4] Iniciando Suscriptor 1 (Tema: PartidoA)..."
./subscriber_udp 127.0.0.1 $PUERTO PartidoA > sub1_udp.log 2>&1 &
PID_SUB1=$!

echo "[2/4] Iniciando Suscriptor 2 (Temas: PartidoA y PartidoB)..."
./subscriber_udp 127.0.0.1 $PUERTO PartidoA PartidoB > sub2_udp.log 2>&1 &
PID_SUB2=$!
sleep 1.5

# 4. Iniciar Publicadores (10 mensajes cada uno, intervalo de 300ms)
echo "[3/4] Enviando 10 eventos desde Publicador 1 (PartidoA / P1)..."
./publisher_udp 127.0.0.1 $PUERTO PartidoA P1 10 300 > pub1_udp.log 2>&1 &
PID_PUB1=$!

echo "[3/4] Enviando 10 eventos desde Publicador 2 (PartidoB / P2)..."
./publisher_udp 127.0.0.1 $PUERTO PartidoB P2 10 300 > pub2_udp.log 2>&1 &
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
echo "=================================================="
echo "           RESUMEN ESTADÍSTICO UDP               "
echo "=================================================="
echo "--- Resumen Suscriptor 1 (Esperados: 10 de PartidoA) ---"
cat sub1_udp.log | grep -A 5 "RESUMEN DEL SUSCRIPTOR" || cat sub1_udp.log
echo ""
echo "--- Resumen Suscriptor 2 (Esperados: 20 -> 10 PartidoA y 10 PartidoB) ---"
cat sub2_udp.log | grep -A 5 "RESUMEN DEL SUSCRIPTOR" || cat sub2_udp.log
echo "=================================================="
echo "¡Prueba UDP completada con éxito!"

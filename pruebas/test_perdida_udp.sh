#!/usr/bin/env bash

set -e
PUERTO=9090
DIR_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR_RAIZ="$(cd "$DIR_SCRIPT/.." && pwd)"
cd "$DIR_RAIZ"

mkdir -p logs

echo "    PRUEBA DE ESTRÉS: PÉRDIDA DE PAQUETES EN UDP (RÁFAGA DE 1000 MSGS)   "

# 1. Asegurar compilación
if [ ! -f ./udp/broker_udp ] || [ ! -f ./udp/subscriber_udp ] || [ ! -f ./udp/publisher_udp ]; then
    echo "[!] Compilando programas UDP..."
    make -C udp
fi

# 2. Liberar puerto si estaba ocupado
fuser -k ${PUERTO}/udp 2>/dev/null || true
sleep 0.5

# 3. Iniciar Broker UDP
echo "[1/3] Iniciando Broker UDP en puerto $PUERTO..."
./udp/broker_udp $PUERTO > logs/broker_burst.log 2>&1 &
PID_BROKER=$!
sleep 1

# 4. Iniciar Suscriptor escuchando PartidoA
echo "[2/3] Iniciando Suscriptor para el tema 'PartidoA'..."
./udp/subscriber_udp 127.0.0.1 $PUERTO PartidoA > logs/sub_burst.log 2>&1 &
PID_SUB=$!
sleep 1

# 5. Enviar ráfaga masiva de 1000 mensajes con 0 ms de pausa
echo "[3/3] Enviando ráfaga masiva de 1000 mensajes sin pausa (intervalo = 0 ms)..."
./udp/publisher_udp 127.0.0.1 $PUERTO PartidoA P1 1000 0 > logs/pub_burst.log 2>&1
echo "      -> Ráfaga de 1000 mensajes completada."

sleep 1

# 6. Detener suscriptor con SIGINT para generar el balance estadístico
echo "[!] Finalizando procesos y calculando balance..."
kill -2 $PID_SUB $PID_BROKER 2>/dev/null || true
wait $PID_SUB 2>/dev/null || true
wait $PID_BROKER 2>/dev/null || true

echo ""
echo "           EVIDENCIA EXPERIMENTAL DE PÉRDIDA DE DATAGRAMAS             "
echo "Muestra de saltos en secuencia detectados por el suscriptor [HUECOS]:"
grep "HUECO: posible perdida" logs/sub_burst.log | head -n 6
echo "..."
grep "HUECO: posible perdida" logs/sub_burst.log | tail -n 3
echo ""
echo "RESUMEN ESTADÍSTICO FINAL (GENERADO POR EL SUSCRIPTOR EN C):"
cat logs/sub_burst.log | grep -A 3 "RESUMEN DEL SUSCRIPTOR"
echo ">> TOMA LA CAPTURA DE PANTALLA DE ESTA TERMINAL PARA EL INFORME <<"

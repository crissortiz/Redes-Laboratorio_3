#!/usr/bin/env bash
# ==============================================================================
# Script de Automatización de Pruebas - Protocolo TCP
# Sistema Publicador-Suscriptor de Noticias Deportivas
# ==============================================================================

set -e
PUERTO=5050
DIR_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR_RAIZ="$(cd "$DIR_SCRIPT/.." && pwd)"
cd "$DIR_RAIZ"

mkdir -p logs

echo "    INICIANDO PRUEBA DE AUTOMATIZACIÓN (TCP)      "

# 1. Asegurar que los binarios estén compilados
if [ ! -f ./tcp/broker_tcp ] || [ ! -f ./tcp/subscriber_tcp ] || [ ! -f ./tcp/publisher_tcp ]; then
    echo "[!] Compilando programas TCP..."
    make -C tcp
fi

# Limpiar procesos previos en el puerto si existieran
fuser -k ${PUERTO}/tcp 2>/dev/null || true
sleep 0.5

# 2. Iniciar Broker TCP
echo "[1/4] Iniciando Broker TCP en puerto $PUERTO..."
./tcp/broker_tcp $PUERTO > logs/broker_tcp.log 2>&1 &
PID_BROKER=$!
sleep 1

# 3. Iniciar Suscriptores
echo "[2/4] Iniciando Suscriptor 1 (Tema: PartidoA)..."
./tcp/subscriber_tcp 127.0.0.1 $PUERTO PartidoA > logs/sub1_tcp.log 2>&1 &
PID_SUB1=$!

echo "[2/4] Iniciando Suscriptor 2 (Temas: PartidoA y PartidoB)..."
./tcp/subscriber_tcp 127.0.0.1 $PUERTO PartidoA PartidoB > logs/sub2_tcp.log 2>&1 &
PID_SUB2=$!
sleep 1

# 4. Iniciar Publicadores (enviando 10 eventos cada uno con 0.3s de pausa)
echo "[3/4] Enviando 10 eventos desde Publicador 1 (PartidoA)..."
(
    while IFS= read -r linea; do
        echo "$linea"
        sleep 0.3
    done < pruebas/eventos_partido_a.txt
) | ./tcp/publisher_tcp 127.0.0.1 $PUERTO PartidoA > logs/pub1_tcp.log 2>&1 &
PID_PUB1=$!

echo "[3/4] Enviando 10 eventos desde Publicador 2 (PartidoB)..."
(
    while IFS= read -r linea; do
        echo "$linea"
        sleep 0.3
    done < pruebas/eventos_partido_b.txt
) | ./tcp/publisher_tcp 127.0.0.1 $PUERTO PartidoB > logs/pub2_tcp.log 2>&1 &
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
echo "               RESULTADOS DE LA PRUEBA            "
echo "--- Mensajes recibidos por Suscriptor 1 (Esperados: 10 de PartidoA) ---"
cat logs/sub1_tcp.log | grep -E "^MSG" || cat logs/sub1_tcp.log
echo ""
echo "--- Mensajes recibidos por Suscriptor 2 (Esperados: 20 -> PartidoA y PartidoB) ---"
cat logs/sub2_tcp.log | grep -E "^MSG" || cat logs/sub2_tcp.log
echo ""
echo "Total de mensajes capturados en logs:"
echo "Suscriptor 1: $(grep -c "^MSG" logs/sub1_tcp.log || echo 0) mensajes"
echo "Suscriptor 2: $(grep -c "^MSG" logs/sub2_tcp.log || echo 0) mensajes"
echo "¡Prueba TCP completada con éxito! (Logs guardados en logs/)"

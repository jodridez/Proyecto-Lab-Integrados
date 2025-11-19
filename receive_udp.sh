#!/bin/bash

# Script para recibir el stream UDP del sistema de vigilancia ROI
# Ejecutar en la computadora receptora

PORT=${1:-5000}

echo "Iniciando receptor UDP en puerto $PORT..."
echo "Presiona Ctrl+C para detener"

gst-launch-1.0 -v udpsrc port=$PORT \
    caps="application/x-rtp,media=video,encoding-name=H264,payload=96" ! \
    rtph264depay ! \
    h264parse ! \
    avdec_h264 ! \
    videoconvert ! \
    autovideosink sync=false
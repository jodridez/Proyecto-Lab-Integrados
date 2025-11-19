#!/bin/bash

# Script de pruebas automatizadas para el proyecto ROI
# Ejecuta los casos de prueba requeridos por el proyecto

set -e  # Exit on error

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Video de ejemplo de DeepStream
VIDEO_SAMPLE="/opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4"
APP="./secure_roi"

# Función para imprimir encabezados
print_header() {
    echo ""
    echo -e "${BLUE}=================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=================================================${NC}"
    echo ""
}

# Función para verificar requisitos
check_requirements() {
    print_header "Verificando Requisitos"
    
    # Verificar que la aplicación existe
    if [ ! -f "$APP" ]; then
        echo -e "${RED}✗ Error: No se encuentra $APP${NC}"
        echo "  Ejecuta 'make' primero para compilar"
        exit 1
    fi
    echo -e "${GREEN}✓ Aplicación compilada${NC}"
    
    # Verificar que el video existe
    if [ ! -f "$VIDEO_SAMPLE" ]; then
        echo -e "${RED}✗ Error: No se encuentra el video de ejemplo${NC}"
        echo "  Ruta: $VIDEO_SAMPLE"
        echo "  Verifica la instalación de DeepStream SDK"
        exit 1
    fi
    echo -e "${GREEN}✓ Video de ejemplo encontrado${NC}"
    
    # Verificar DeepStream
    if [ ! -d "/opt/nvidia/deepstream/deepstream" ]; then
        echo -e "${RED}✗ Error: DeepStream no está instalado${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ DeepStream SDK instalado${NC}"
    
    echo ""
}

# Caso 1: Persona dentro del ROI que NO supera el tiempo máximo
test_case_1() {
    print_header "CASO 1: Persona NO excede el tiempo máximo"
    
    echo "Configuración:"
    echo "  - ROI: Grande (80% de la pantalla)"
    echo "  - Tiempo máximo: 20 segundos"
    echo "  - Resultado esperado: Verde → Amarillo → Verde"
    echo ""
    
    $APP --vi-file "$VIDEO_SAMPLE" \
        --left 0.1 --top 0.1 --width 0.8 --height 0.8 \
        --time 20 --file-name report_test1.txt \
        --vo-file output_test1.mp4 --mode video
    
    echo ""
    echo -e "${GREEN}✓ Test 1 completado${NC}"
    echo "  Salida: output_test1.mp4"
    echo "  Reporte: report_test1.txt"
    echo ""
    echo "Contenido del reporte:"
    cat report_test1.txt
    echo ""
}

# Caso 2: Persona dentro del ROI que SUPERA el tiempo máximo
test_case_2() {
    print_header "CASO 2: Persona SUPERA el tiempo máximo"
    
    echo "Configuración:"
    echo "  - ROI: Mediano (60% de la pantalla)"
    echo "  - Tiempo máximo: 2 segundos"
    echo "  - Resultado esperado: Verde → Amarillo → ROJO (con alertas)"
    echo ""
    
    $APP --vi-file "$VIDEO_SAMPLE" \
        --left 0.2 --top 0.2 --width 0.6 --height 0.6 \
        --time 2 --file-name report_test2.txt \
        --vo-file output_test2.mp4 --mode video
    
    echo ""
    echo -e "${GREEN}✓ Test 2 completado${NC}"
    echo "  Salida: output_test2.mp4"
    echo "  Reporte: report_test2.txt"
    echo ""
    echo "Contenido del reporte:"
    cat report_test2.txt
    echo ""
    
    # Verificar que hay alertas
    if grep -q "alert" report_test2.txt; then
        echo -e "${GREEN}✓ Se generaron alertas correctamente${NC}"
    else
        echo -e "${YELLOW}⚠ Advertencia: No se encontraron alertas en el reporte${NC}"
    fi
    echo ""
}

# Caso 3: Múltiples personas (algunas exceden, otras no)
test_case_3() {
    print_header "CASO 3: Dos o más personas (comportamiento mixto)"
    
    echo "Configuración:"
    echo "  - ROI: Mediano (40% de la pantalla, centrado)"
    echo "  - Tiempo máximo: 5 segundos"
    echo "  - Resultado esperado: Algunas personas con alerta, otras sin alerta"
    echo ""
    
    $APP --vi-file "$VIDEO_SAMPLE" \
        --left 0.3 --top 0.3 --width 0.4 --height 0.4 \
        --time 5 --file-name report_test3.txt \
        --vo-file output_test3.mp4 --mode video
    
    echo ""
    echo -e "${GREEN}✓ Test 3 completado${NC}"
    echo "  Salida: output_test3.mp4"
    echo "  Reporte: report_test3.txt"
    echo ""
    echo "Contenido del reporte:"
    cat report_test3.txt
    echo ""
    
    # Análisis del reporte
    TOTAL=$(grep "Detected:" report_test3.txt | awk '{print $2}')
    EXCEEDED=$(grep "Detected:" report_test3.txt | awk '{print $3}' | tr -d '()')
    
    echo "Análisis:"
    echo "  - Personas detectadas: $TOTAL"
    echo "  - Personas que excedieron: $EXCEEDED"
    
    if [ "$EXCEEDED" -gt 0 ] && [ "$EXCEEDED" -lt "$TOTAL" ]; then
        echo -e "${GREEN}✓ Comportamiento mixto detectado correctamente${NC}"
    elif [ "$EXCEEDED" -eq "$TOTAL" ]; then
        echo -e "${YELLOW}⚠ Todas las personas excedieron el tiempo${NC}"
    elif [ "$EXCEEDED" -eq 0 ]; then
        echo -e "${YELLOW}⚠ Ninguna persona excedió el tiempo${NC}"
    fi
    echo ""
}

# Resumen final
print_summary() {
    print_header "RESUMEN DE PRUEBAS"
    
    echo "Archivos generados:"
    echo ""
    ls -lh output_test*.mp4 report_test*.txt 2>/dev/null | while read line; do
        echo "  $line"
    done
    
    echo ""
    echo "Para visualizar los resultados:"
    echo ""
    echo "  Reportes de texto:"
    echo "    cat report_test1.txt"
    echo "    cat report_test2.txt"
    echo "    cat report_test3.txt"
    echo ""
    echo "  Videos (usar VLC, mpv, o cualquier reproductor):"
    echo "    vlc output_test1.mp4"
    echo "    vlc output_test2.mp4"
    echo "    vlc output_test3.mp4"
    echo ""
    echo "  O reproducir con GStreamer:"
    echo "    gst-launch-1.0 filesrc location=output_test1.mp4 ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! autovideosink"
    echo ""
    
    # Comparación de resultados
    echo "Comparación de casos:"
    echo ""
    printf "%-10s %-20s %-20s\n" "Caso" "Detectadas" "Excedieron"
    printf "%-10s %-20s %-20s\n" "----" "-----------" "-----------"
    
    for i in 1 2 3; do
        if [ -f "report_test${i}.txt" ]; then
            DETECTED=$(grep "Detected:" "report_test${i}.txt" | awk '{print $2}')
            EXCEEDED=$(grep "Detected:" "report_test${i}.txt" | awk '{print $3}' | tr -d '()')
            printf "%-10s %-20s %-20s\n" "Test $i" "$DETECTED" "$EXCEEDED"
        fi
    done
    
    echo ""
    echo -e "${GREEN}✓ Todas las pruebas completadas exitosamente${NC}"
    echo ""
}

# Función principal
main() {
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║  Sistema de Vigilancia ROI - Suite de Pruebas Automatizadas  ║"
    echo "║  IE0301 - Proyecto Final                                      ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    
    check_requirements
    
    # Limpiar archivos anteriores
    echo "Limpiando archivos de pruebas anteriores..."
    rm -f output_test*.mp4 report_test*.txt
    echo ""
    
    # Ejecutar casos de prueba
    test_case_1
    test_case_2
    test_case_3
    
    # Mostrar resumen
    print_summary
}

# Ejecutar
main
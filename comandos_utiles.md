# Comandos Útiles - Referencia Rápida

## Compilación

```bash
# Compilar
make

# Limpiar y recompilar
make clean && make

# Mostrar ayuda
make help
```

---

## Tests Rápidos con Videos de DeepStream

### Test Rápido (Desarrollo)
```bash
make quick
```

### Casos del Proyecto
```bash
make test1    # Caso 1: NO excede tiempo
make test2    # Caso 2: EXCEDE tiempo  
make test3    # Caso 3: Múltiples personas
```

### Tests Adicionales
```bash
make test-corner   # ROI pequeño en esquina
make test-center   # ROI centrado
make test-udp      # Solo UDP
make test-both     # Archivo + UDP
```

### Ejecutar Todo
```bash
make test-all      # Todos los casos
./test_all.sh      # Script con análisis detallado
```

---

## Verificaciones

### Verificar Video de Ejemplo
```bash
make check-video
```

### Listar Videos Disponibles
```bash
make list-videos
```

### Ver Información del Video
```bash
gst-discoverer-1.0 /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4
```

---

## Ejecutar Manualmente

### Sintaxis Básica
```bash
./secure_roi --vi-file VIDEO \
             --left X --top Y --width W --height H \
             --time SEGUNDOS \
             --file-name REPORTE.txt \
             --vo-file SALIDA.mp4 \
             --mode video|udp|udp_video
```

### Ejemplo Completo
```bash
./secure_roi \
  --vi-file /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 \
  --left 0.3 --top 0.3 --width 0.4 --height 0.4 \
  --time 5 \
  --file-name mi_reporte.txt \
  --vo-file mi_video.mp4 \
  --mode video
```

---

## Reproducir Videos Generados

### Con VLC (Recomendado)
```bash
vlc output_test1.mp4
vlc output_test2.mp4
vlc output_test3.mp4
```

### Con MPV
```bash
mpv output_test1.mp4
```

### Con GStreamer
```bash
gst-launch-1.0 filesrc location=output_test1.mp4 ! \
  qtdemux ! h264parse ! avdec_h264 ! \
  videoconvert ! autovideosink sync=false
```

---

## Ver Reportes

### Ver Reporte en Terminal
```bash
cat report_test1.txt
cat report_test2.txt
cat report_test3.txt
```

### Ver Reporte con Formato
```bash
cat report_test1.txt | column -t
```

### Buscar Alertas
```bash
grep "alert" report_test*.txt
```

### Contar Personas Detectadas
```bash
grep "Detected:" report_test1.txt
```

### Contar Alertas
```bash
grep -c "alert" report_test2.txt
```

---

## Transmisión UDP

### En la Jetson (Emisor)
```bash
make test-udp
```

### En PC Receptor
```bash
# Opción 1: Script
./receive_udp.sh

# Opción 2: Manual
gst-launch-1.0 -v udpsrc port=5000 \
  caps="application/x-rtp,media=video,encoding-name=H264,payload=96" ! \
  rtph264depay ! h264parse ! avdec_h264 ! \
  videoconvert ! autovideosink sync=false
```

---

## Monitoreo del Sistema

### Uso de Recursos (Tegrastats)
```bash
sudo tegrastats --interval 1000
```

### Uso de Recursos (Guardado en Archivo)
```bash
sudo tegrastats --interval 1000 | tee tegra_log.txt
```

### Ver Procesos de GStreamer
```bash
ps aux | grep gst
```

### Ver Uso de GPU
```bash
sudo jetson_clocks --show
```

---

## Debugging

### Ejecutar con Logs de GStreamer
```bash
GST_DEBUG=3 ./secure_roi --vi-file VIDEO ...
```

Niveles de debug:
- `0`: NONE
- `1`: ERROR
- `2`: WARNING
- `3`: INFO
- `4`: DEBUG
- `5`: LOG

### Verificar Elementos de GStreamer
```bash
gst-inspect-1.0 nvinfer
gst-inspect-1.0 nvtracker
gst-inspect-1.0 nvdsosd
```

### Verificar Librerías de DeepStream
```bash
ls /opt/nvidia/deepstream/deepstream/lib/
ldd ./secure_roi
```

---

## Limpieza

### Limpiar Compilación
```bash
make clean
```

### Limpiar Videos y Reportes Generados
```bash
rm -f output_*.mp4 report*.txt
```

### Limpiar Todo
```bash
make clean
rm -f output_*.mp4 report*.txt *.o
```

---

## Configuración de Red (Para UDP)

### Ver IP de la Jetson
```bash
ifconfig
ip addr show
```

### Verificar Conectividad
```bash
ping 192.168.1.100
```

### Abrir Puerto UDP en Firewall
```bash
sudo ufw allow 5000/udp
sudo ufw status
```

### Test de UDP con Netcat
```bash
# En receptor:
nc -ul 5000

# En emisor:
echo "test" | nc -u 192.168.1.100 5000
```

---

## Casos de Uso Especiales

### ROI Muy Pequeño (Esquina)
```bash
./secure_roi --vi-file VIDEO \
  --left 0.05 --top 0.05 --width 0.15 --height 0.15 \
  --time 3 --file-name report.txt --vo-file output.mp4 --mode video
```

### ROI Muy Grande (Casi Toda la Pantalla)
```bash
./secure_roi --vi-file VIDEO \
  --left 0.05 --top 0.05 --width 0.9 --height 0.9 \
  --time 10 --file-name report.txt --vo-file output.mp4 --mode video
```

### Tiempo Muy Corto (Muchas Alertas)
```bash
./secure_roi --vi-file VIDEO \
  --left 0.3 --top 0.3 --width 0.4 --height 0.4 \
  --time 1 --file-name report.txt --vo-file output.mp4 --mode video
```

### Tiempo Muy Largo (Sin Alertas)
```bash
./secure_roi --vi-file VIDEO \
  --left 0.3 --top 0.3 --width 0.4 --height 0.4 \
  --time 60 --file-name report.txt --vo-file output.mp4 --mode video
```

---

## Análisis de Resultados

### Comparar Múltiples Reportes
```bash
echo "=== Test 1 ===" && cat report_test1.txt && \
echo "=== Test 2 ===" && cat report_test2.txt && \
echo "=== Test 3 ===" && cat report_test3.txt
```

### Extraer Solo las Alertas
```bash
for f in report_test*.txt; do
  echo "=== $f ==="
  grep "alert" "$f"
done
```

### Estadísticas de Detecciones
```bash
for f in report_test*.txt; do
  echo "$f:"
  grep "Detected:" "$f"
done
```

---

## Troubleshooting Común

### Error: "No se encuentra nvdsmeta.h"
```bash
# Verificar headers
ls /opt/nvidia/deepstream/deepstream/sources/includes/
```

### Error: "Failed to create 'nvinfer' element"
```bash
# Verificar config de inferencia
cat /opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt
```

### Error: "Could not load library libnvdsgst_meta.so"
```bash
# Verificar librerías
export LD_LIBRARY_PATH=/opt/nvidia/deepstream/deepstream/lib:$LD_LIBRARY_PATH
```

### Video no se reproduce
```bash
# Verificar que el archivo existe y tiene contenido
ls -lh output_test1.mp4
file output_test1.mp4
```

### Pipeline se detiene inmediatamente
```bash
# Ejecutar con debug para ver el error
GST_DEBUG=3 ./secure_roi ...
```

---

## Atajos Útiles

### Alias para Comandos Frecuentes
```bash
# Agregar a ~/.bashrc
alias roi-quick='cd /ruta/proyecto && make quick'
alias roi-test='cd /ruta/proyecto && ./test_all.sh'
alias roi-clean='cd /ruta/proyecto && make clean && rm -f output_*.mp4 report*.txt'
```

### Variables de Entorno
```bash
# Agregar a ~/.bashrc
export DEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
export VIDEO_SAMPLE=$DEEPSTREAM_DIR/samples/streams/sample_1080p_h264.mp4
```

---

## Scripts One-Liners

### Ver Todas las Detecciones
```bash
find . -name "report*.txt" -exec grep "person" {} +
```

### Contar Total de Alertas
```bash
grep -h "alert" report*.txt | wc -l
```

### Listar Tamaños de Videos
```bash
du -h output_*.mp4 | sort -h
```

### Comparar Duración de Videos
```bash
for f in output_*.mp4; do
  echo -n "$f: "
  gst-discoverer-1.0 "$f" 2>&1 | grep "Duration"
done
```

---

## Información del Sistema

### Versión de DeepStream
```bash
dpkg -l | grep deepstream
```

### Versión de GStreamer
```bash
gst-launch-1.0 --version
```

### Info de Jetson Nano
```bash
cat /etc/nv_tegra_release
```

### Espacio en Disco
```bash
df -h
```

---

## Para la Demostración

### Preparación Rápida
```bash
make clean && make && ./test_all.sh
```

### Verificar que Todo Está Listo
```bash
ls -lh secure_roi output_test*.mp4 report_test*.txt
```

### Abrir Todo para Demo
```bash
# Terminal 1
vlc output_test1.mp4

# Terminal 2
cat report_test1.txt
```
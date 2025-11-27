<<<<<<< HEAD
# Sistema de Vigilancia ROI - Proyecto Final IE0301

Sistema de vigilancia basado en visión por computadora para detección y seguimiento de personas dentro de una Región de Interés (ROI) configurable.

## Requisitos del Sistema

- **Hardware**: NVIDIA Jetson Nano
- **BSP**: JetPack 4.6.4
- **Software**: 
  - GStreamer 1.14.5
  - DeepStream SDK 6.0.1
  - CUDA Toolkit

## Instalación

### 1. Clonar el repositorio

```bash
git clone <url-del-repositorio>
cd proyecto-roi
```

### 2. Verificar DeepStream SDK

```bash
# Verificar instalación de DeepStream
ls /opt/nvidia/deepstream/deepstream-6.0

# Dar permisos si es necesario
sudo chmod -R 777 /opt/nvidia/deepstream/deepstream
```

### 3. Compilar la aplicación

```bash
make clean
make
```

## Uso

### Videos de Ejemplo Disponibles

DeepStream incluye videos de ejemplo en:
```bash
/opt/nvidia/deepstream/deepstream/samples/streams/
```

Videos disponibles:
- `sample_1080p_h264.mp4` - Video 1080p con personas y vehículos (recomendado)
- `sample_720p.h264` - Video 720p alternativo
- Y otros según tu instalación

Para listar todos los videos disponibles:
```bash
make list-videos
```

Para verificar que el video existe:
```bash
make check-video
```

### Sintaxis General

```bash
./secure_roi --vi-file <video_entrada> \
             --left <x> --top <y> --width <w> --height <h> \
             --time <segundos> \
             --file-name <reporte.txt> \
             [--vo-file <video_salida>] \
             --mode <video|udp|udp_video> \
             [--udp-host <ip>] \
             [--udp-port <puerto>]
```

### Parámetros

- `--vi-file`: Archivo de video de entrada (requerido)
- `--left`: Posición X del ROI (0.0-1.0, normalizado)
- `--top`: Posición Y del ROI (0.0-1.0, normalizado)
- `--width`: Ancho del ROI (0.0-1.0, normalizado)
- `--height`: Alto del ROI (0.0-1.0, normalizado)
- `--time`: Tiempo máximo en segundos antes de alerta
- `--file-name`: Nombre del archivo de reporte (requerido)
- `--vo-file`: Archivo de video de salida (requerido si mode=video o udp_video)
- `--mode`: Modo de salida (video, udp, udp_video)
- `--udp-host`: Dirección IP destino para UDP (default: 192.168.0.255)
- `--udp-port`: Puerto UDP (default: 5000)

### Ejemplos de Uso

#### Ejemplo 1: Solo archivo de salida

```bash
./secure_roi --vi-file /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 \
             --left 0.3 --top 0.3 --width 0.4 --height 0.4 \
             --time 5 --file-name report.txt \
             --vo-file output.mp4 --mode video
```

#### Ejemplo 2: Solo transmisión UDP

```bash
./secure_roi --vi-file street-people.mp4 \
             --left 0.5 --top 0.3 --width 0.4 --height 0.4 \
             --time 5 --file-name report.txt \
             --mode udp --udp-host 192.168.1.100 --udp-port 5000
```

#### Ejemplo 3: Archivo y UDP simultáneos

```bash
./secure_roi --vi-file street-people.mp4 \
             --left 0.5 --top 0.3 --width 0.7 --height 0.7 \
             --time 5 --file-name report.txt \
             --vo-file roi-detected.mp4 --mode udp_video \
             --udp-host 192.168.1.100
```

#### Ejemplo 4: Usando video de muestra de DeepStream

```bash
make run
```

### Recibir Stream UDP

En la computadora receptora:

```bash
# Dar permisos de ejecución al script
chmod +x receive_udp.sh

# Ejecutar receptor
./receive_udp.sh 5000
```

O manualmente con GStreamer:

```bash
gst-launch-1.0 udpsrc port=5000 \
    caps="application/x-rtp,media=video,encoding-name=H264,payload=96" ! \
    rtph264depay ! h264parse ! avdec_h264 ! \
    videoconvert ! autovideosink sync=false
```

## Funcionamiento del Sistema

### Estados del ROI (Colores)

- **Verde**: ROI vacío, sin personas detectadas
- **Amarillo**: Persona detectada dentro del tiempo permitido
- **Rojo**: Persona excedió el tiempo máximo en el ROI

### Algoritmo de Detección

1. **Detección**: Usa modelo ResNet10/PeopleNet para detectar personas
2. **Tracking**: Asigna IDs únicos a cada persona mediante nvtracker
3. **Análisis ROI**: Verifica si el centro del bounding box está dentro del ROI
4. **Temporización**: Mide el tiempo de permanencia desde la entrada
5. **Alertas**: Cambia color del ROI cuando se excede el tiempo máximo

### Formato del Reporte

El sistema genera un archivo de texto con el siguiente formato:

```
ROI: left: 576 top: 324 width: 768 height: 432
Max time: 5s
Detected: 12 (3)
0:11 person time 4s
0:18 person time 7s alert
0:22 person time 3s
1:05 person time 6s alert
```

Donde:
- Primera línea: Ubicación y dimensiones del ROI en píxeles
- Segunda línea: Tiempo máximo configurado
- Tercera línea: Total de personas detectadas (personas que excedieron el tiempo)
- Siguientes líneas: Timestamp, tipo de objeto, duración, y "alert" si excedió

## Arquitectura del Sistema

### Pipeline GStreamer

```
filesrc → h264parse → nvv4l2decoder → nvstreammux → nvinfer → 
nvtracker → nvvideoconvert → nvdsosd → tee
                                        ├→ [archivo] nvvideoconvert → nvv4l2h264enc → qtmux → filesink
                                        └→ [udp] nvvideoconvert → nvv4l2h264enc → rtph264pay → udpsink
```

### Componentes Clave

- **nvinfer**: Inferencia del modelo de detección (ResNet10)
- **nvtracker**: Seguimiento de objetos con IDs únicos
- **nvdsosd**: Dibuja ROI y bounding boxes en GPU
- **tee**: Divide el stream para múltiples salidas

### Metadatos de DeepStream

El sistema utiliza los metadatos de DeepStream para:
- Obtener bounding boxes de personas detectadas
- Obtener IDs de tracking únicos
- Agregar gráficos al video (ROI, texto)

## Casos de Prueba

### Caso 1: Persona no excede tiempo

```bash
./secure_roi --vi-file video_test1.mp4 \
             --left 0.4 --top 0.4 --width 0.3 --height 0.3 \
             --time 10 --file-name report1.txt \
             --vo-file output1.mp4 --mode video
```

Resultado esperado: ROI verde → amarillo → verde

### Caso 2: Persona excede tiempo

```bash
./secure_roi --vi-file video_test2.mp4 \
             --left 0.4 --top 0.4 --width 0.3 --height 0.3 \
             --time 3 --file-name report2.txt \
             --vo-file output2.mp4 --mode video
```

Resultado esperado: ROI verde → amarillo → rojo, alerta en reporte

### Caso 3: Múltiples personas

```bash
./secure_roi --vi-file video_test3.mp4 \
             --left 0.3 --top 0.3 --width 0.5 --height 0.5 \
             --time 5 --file-name report3.txt \
             --vo-file output3.mp4 --mode video
```

Resultado esperado: Tracking individual, algunas alertas

## Solución de Problemas

### Error: "Error al crear elementos del pipeline"

Verificar que DeepStream esté correctamente instalado:

```bash
dpkg -l | grep deepstream
```

### Error: "Failed to create 'nvinfer' element"

Verificar el archivo de configuración:

```bash
ls /opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt
```

### Error: Modelo TensorRT no encontrado

Generar el modelo manualmente:

```bash
cd /opt/nvidia/deepstream/deepstream/samples/models/Primary_Detector
# El modelo se generará automáticamente en la primera ejecución
```

### No se recibe video por UDP

1. Verificar conectividad de red:
```bash
ping <ip_destino>
```

2. Verificar firewall:
```bash
sudo ufw allow 5000/udp
```

3. Usar dirección IP específica en lugar de broadcast

### Alto uso de CPU

El sistema está optimizado para usar aceleración por hardware. Verificar con:

```bash
tegrastats
```

Valores esperados:
- CPU: 15-30%
- GR3D: Variable según inferencia
- NVENC/NVDEC: Activos

## Monitoreo del Sistema

### Uso de Recursos

```bash
# Terminal 1: Ejecutar aplicación
./secure_roi ...

# Terminal 2: Monitorear recursos
sudo tegrastats --interval 1000
```

### Logs de GStreamer

Para depuración detallada:

```bash
GST_DEBUG=3 ./secure_roi ...
```

Niveles de debug:
- 0: ninguno
- 1: ERROR
- 2: WARNING
- 3: INFO
- 4: DEBUG
- 5: LOG

## Optimizaciones

### Para mejor rendimiento

1. Usar videos con resolución nativa 1920x1080
2. Ajustar bitrate según ancho de banda disponible
3. Usar ROI pequeños cuando sea posible
4. Considerar reducir fps si no es necesario tiempo real

### Para mejor precisión

1. Ajustar umbral de confianza en el código
2. Usar modelo PeopleNet en lugar de ResNet10 (mejor para personas)
3. Aumentar resolución del tracker

## Estructura del Código

```
proyecto-roi/
├── main.c              # Aplicación principal
├── Makefile            # Configuración de compilación
├── receive_udp.sh      # Script receptor UDP
├── README.md           # Este archivo
└── test_videos/        # Videos de prueba (opcional)
```

## Referencias

- [DeepStream SDK Documentation](https://docs.nvidia.com/metropolis/deepstream/dev-guide/)
- [GStreamer Documentation](https://gstreamer.freedesktop.org/documentation/)
- [NVIDIA Jetson Nano](https://developer.nvidia.com/embedded/jetson-nano)

## Autores

- Juan José Quirós Picado – B96260
- Jonathan Rodríguez Hernández – B76490
- Gonzalo Gutiérrez Mata – B53279

## Licencia

Proyecto académico - Universidad de Costa Rica
IE0301 - Laboratorio de Sistemas Incrustados
=======
# Proyecto-Lab-Integrados
Proyecto Laboratorios de Integrados

## Para compilar el programa se utiliza:

```
make
```

## Para ejecutar el archivo una vez compilado se utiliza el siguiente comando:

```
./deepstream_roi \
  /opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_720p.mp4 \
  300 200 400 250 \
  5 \
  roi_eventos.csv \
  video_roi_final.mp4
```

## Diferentes modos

```
# Solo archivo MP4
./deepstream_roi sample_720p.mp4 300 200 400 250 5 video roi_eventos.csv salida.mp4

# Solo UDP
./deepstream_roi sample_720p.mp4 300 200 400 250 5 udp roi_eventos.csv salida.mp4

# Ambos
./deepstream_roi sample_720p.mp4 300 200 400 250 5 both roi_eventos.csv salida.mp4
```

### Para recibir el stream se utiliza el siguiente comando:

```
gst-launch-1.0 -v \
  udpsrc port=5000 caps="application/x-rtp, media=video, encoding-name=H264, payload=96, clock-rate=90000" ! \
  rtpjitterbuffer ! \
  rtph264depay ! \
  h264parse ! \
  avdec_h264 ! \
  videoconvert ! \
  nveglglessink sync=false
```
>>>>>>> origin/ggutierrez

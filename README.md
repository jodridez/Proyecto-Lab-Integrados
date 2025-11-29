# 🎯 ROI Dwell Time Monitor

Sistema inteligente de monitoreo de permanencia en tiempo real utilizando NVIDIA DeepStream SDK para análisis de video con detección de personas.

[![DeepStream](https://img.shields.io/badge/DeepStream-6.0+-76B900?style=flat&logo=nvidia)](https://developer.nvidia.com/deepstream-sdk)
[![CUDA](https://img.shields.io/badge/CUDA-Required-76B900?style=flat&logo=nvidia)](https://developer.nvidia.com/cuda-toolkit)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## 📋 Tabla de Contenidos

- [Descripción](#-descripción)
- [Características](#-características)
- [Requisitos](#-requisitos)
- [Instalación](#-instalación)
- [Uso](#-uso)
- [Ejemplos Prácticos](#-ejemplos-prácticos)
- [Formato de Salida](#-formato-de-salida)
- [Visualización](#-visualización)
- [Configuración Avanzada](#-configuración-avanzada)

---

## 🔍 Descripción

**ROI Dwell Time Monitor** es una aplicación profesional de visión por computadora que detecta personas dentro de una Región de Interés (ROI) configurable y monitorea su tiempo de permanencia. El sistema genera alertas automáticas cuando el tiempo excede umbrales predefinidos, ideal para aplicaciones de:

- 🏢 Control de acceso y seguridad
- 🏪 Análisis de comportamiento en retail
- 🏭 Monitoreo de áreas restringidas
- 📊 Análisis de flujo de personas
- ⚠️ Detección de situaciones anómalas

## ✨ Características

### Core Features
- ⏱️ **Monitoreo de permanencia** con precisión de milisegundos
- 🚨 **Sistema de alertas** configurable por umbral de tiempo
- 📝 **Reportes CSV** detallados con timestamps y eventos
- 🎨 **Visualización intuitiva** con código de colores

### Modos de Operación
- 💾 **Archivo de video** (MP4/MOV)
- 📡 **Streaming UDP** 
- 🔄 **Modo híbrido** (archivo + streaming simultáneo)

### Optimizaciones
- ⚡ Procesamiento acelerado por GPU (CUDA)
- 🎯 ROI con coordenadas normalizadas (independiente de resolución)
- 🔧 Configuración flexible sin recompilación

---

## 🛠️ Requisitos

### Software
| Componente | Versión Mínima |
|------------|----------------|
| NVIDIA DeepStream SDK | 6.0+ |
| CUDA Toolkit | 11.0+ |
| GStreamer | 1.14+ |
| Ubuntu | 18.04+ |

### Hardware
- GPU NVIDIA compatible con CUDA 

---

## 📦 Instalación

### 1. Clonar el repositorio
```bash
git clone https://github.com/tu-usuario/Proyecto-Lab-Integrados.git
cd Proyecto-Lab-Integrados
```

### 2. Verificar dependencias
```bash
# Verificar DeepStream
dpkg -l | grep deepstream

# Verificar CUDA
nvcc --version

# Verificar GStreamer
gst-inspect-1.0 --version
```

### 3. Compilar
```bash
make
```

---

## 🚀 Uso

### Sintaxis

```bash
./deepstream_roi vi-file <video_entrada> \
  --left <0-1> --top <0-1> --width <0-1> --height <0-1> \
  --time <segundos> \
  --file-name <reporte.csv> \
  vo-file <video_salida> \
  --mode <video|udp|both>
```

### Parámetros

| Parámetro | Tipo | Rango | Descripción |
|-----------|------|-------|-------------|
| `vi-file` | string | - | Ruta al video de entrada (MP4/MOV/AVI) |
| `--left` | float | 0.0-1.0 | Posición X de la ROI (normalizada) |
| `--top` | float | 0.0-1.0 | Posición Y de la ROI (normalizada) |
| `--width` | float | 0.0-1.0 | Ancho de la ROI (normalizado) |
| `--height` | float | 0.0-1.0 | Alto de la ROI (normalizado) |
| `--time` | float | > 0 | Umbral de tiempo en segundos |
| `--file-name` | string | - | Nombre del archivo CSV de salida |
| `vo-file` | string | - | Ruta del video de salida |
| `--mode` | enum | video\|udp\|both | Modo de operación |

> 💡 **Tip:** Las coordenadas normalizadas (0-1) permiten usar la misma configuración para diferentes resoluciones de video.

---

## 📚 Ejemplos Prácticos

### Caso 1: Monitoreo de área central con grabación
**Escenario:** Persona corriendo, ROI centrada, umbral 0.8s

```bash
./deepstream_roi vi-file /opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_run.mov \
  --left 0.25 --top 0.25 --width 0.4 --height 0.4 \
  --time 0.8 --file-name caso1.csv \
  vo-file caso1.mp4 --mode video
```

**Salida:**
- ✅ Video procesado: `caso1.mp4`
- 📊 Reporte: `caso1.csv`

---

### Caso 5: Streaming con ROI amplia
**Escenario:** Persona caminando, ROI lateral grande, umbral 0.3s

```bash
./deepstream_roi vi-file /opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_walk.mov \
  --left 0.05 --top 0.25 --width 0.6 --height 0.6 \
  --time 0.3 --file-name caso5.csv \
  vo-file caso5.mp4 --mode udp
```

**Para visualizar el stream:**
En otra terminal
```bash

gst-launch-1.0 -v \
  udpsrc port=5000 caps="application/x-rtp, media=video, encoding-name=H264, payload=96, clock-rate=90000" ! \
  rtpjitterbuffer ! \
  rtph264depay ! \
  h264parse ! \
  avdec_h264 ! \
  videoconvert ! \
  nveglglessink sync=false
```

**Salida:**
- 📡 Stream UDP en `udp://127.0.0.1:5000`
- 📊 Reporte: `caso5.csv`

---

### Caso 6: Modo híbrido (grabación + streaming)
**Escenario:** Máxima flexibilidad, salida dual

```bash
./deepstream_roi vi-file /opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_run.mov \
  --left 0.25 --top 0.25 --width 0.4 --height 0.4 \
  --time 0.6 --file-name caso6.csv \
  vo-file caso6.mp4 --mode both
```

**Salida:**
- ✅ Video procesado: `caso6.mp4`
- 📡 Stream UDP en `udp://127.0.0.1:5000`
- 📊 Reporte: `caso6.csv`

---

## 📊 Formato de Salida

### Reporte CSV

El sistema genera un archivo CSV estructurado con el siguiente formato:

```csv
ROI: left: 320 top: 180 width: 512 height: 288
Max time: 0.8s
Detected: 12 (3)
event,time,dwell,flag
ENTER,1.234,,
EXIT,2.567,1.333,OVERTIME
ENTER,5.890,,
EXIT,6.234,0.344,OK
OVERTIME,8.123,0.923,OVERTIME
EXIT,8.567,2.677,OVERTIME
```

### Estructura del reporte

#### Header (líneas 1-3)
- **Línea 1:** Coordenadas ROI en píxeles (1280x720)
- **Línea 2:** Umbral de tiempo configurado
- **Línea 3:** Estadísticas `Total (Excedidos)`

#### Eventos (líneas 4+)

| Campo | Descripción | Formato |
|-------|-------------|---------|
| `event` | Tipo de evento | ENTER \| EXIT \| OVERTIME |
| `time` | Timestamp relativo | Segundos desde inicio (float) |
| `dwell` | Tiempo de permanencia | Segundos (float, solo EXIT/OVERTIME) |
| `flag` | Estado | OK \| OVERTIME |

### Tipos de Eventos

- 🟢 **ENTER:** Persona detectada entrando a la ROI
- 🔴 **EXIT:** Persona sale de la ROI
- ⚠️ **OVERTIME:** Umbral excedido (persona aún en ROI)

---

## 🎨 Visualización

### Código de Colores de la ROI

El sistema proporciona retroalimentación visual en tiempo real:

| Estado | Color | Descripción |
|--------|-------|-------------|
| 🟢 **Vacío** | Verde transparente  | No hay personas en la ROI |
| 🟢 **Ocupado OK** | Verde intenso  | Persona dentro, tiempo dentro del límite |
| 🟢 **Alerta** | Verde aún intenso  | ¡Tiempo excedido! |

### Salida por Consola

```
Pipeline ejecutandose...
0:05 ENTER
0:07 person time 2s alert
0:12 ENTER
0:14 person time 2s
0:18 ENTER
0:21 OVERTIME person time 3s (max 0.8s)
0:25 person time 7s alert

=== End of stream ===
Detected: 3 (2)
```

**Formato:** `MM:SS [EVENTO] [detalles]`

---

## ⚙️ Configuración Avanzada

### Archivo de Configuración del Modelo

Ruta por defecto:
```
/home/lab_sistemas/Proyecto-Lab-Integrados/dstest1_pgie_config.txt
```

### Modificar Resolución de Salida

Editar en `deepstream_roi.cpp` (líneas 10-11):
```cpp
#define MUXER_OUTPUT_WIDTH 1920   // Cambiar de 1280
#define MUXER_OUTPUT_HEIGHT 1080  // Cambiar de 720
```

### Configuración UDP

Por defecto el stream se envía a:
- **Host:** `127.0.0.1` (localhost)
- **Puerto:** `5000`
- **Codec:** H.264
- **Payload:** RTP
- **Bitrate:** 4 Mbps

Para cambiar estos valores, editar línea ~260 en el código fuente.

---

## 📄 Licencia

Distribuido bajo la Licencia MIT. Ver `LICENSE` para más información.

---

## 👥 Autores

| Nombre | Carné |
|--------|-------|
| Gonzalo Gutiérrez Mata | B53279 |
| Jonathan Rodríguez Hernández | B76490 |
| Juan José Quirós Picado | B96260 |


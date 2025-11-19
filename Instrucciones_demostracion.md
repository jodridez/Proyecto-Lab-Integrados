# Instrucciones para la Demostración del Proyecto

**Fecha de demostración:** 26 de noviembre de 2025  
**Duración:** 15 minutos  
**Grupo:** Juan José Quirós (B96260), Jonathan Rodríguez (B76490), Gonzalo Gutiérrez (B53279)

---

## Preparación Previa (Antes de la Clase)

### 1. Compilar la Aplicación

```bash
cd /ruta/del/proyecto
make clean
make
```

**Verificar que compila sin errores:**
```bash
# Debe mostrar: "Compilación exitosa: secure_roi"
```

### 2. Verificar Video de Ejemplo

```bash
make check-video
```

**Debe mostrar:**
```
✓ Video encontrado: /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4
```

### 3. Pre-generar los Videos de Prueba

**IMPORTANTE**: Generar todos los videos ANTES de la demostración para ahorrar tiempo.

```bash
# Ejecutar todos los casos de prueba
./test_all.sh

# O individualmente:
make test1
make test2
make test3
```

**Verificar que se generaron:**
```bash
ls -lh output_test*.mp4 report_test*.txt
```

Deberías ver:
- `output_test1.mp4` (Caso 1)
- `output_test2.mp4` (Caso 2)
- `output_test3.mp4` (Caso 3)
- `report_test1.txt` (Caso 1)
- `report_test2.txt` (Caso 2)
- `report_test3.txt` (Caso 3)

### 4. Preparar Terminales

Abrir 2 terminales:

**Terminal 1** (Principal):
```bash
cd /ruta/del/proyecto
```

**Terminal 2** (Monitoreo - Opcional):
```bash
sudo tegrastats --interval 1000
```

---

## Durante la Demostración (15 minutos)

### Parte 1: Descripción del Algoritmo (5 minutos)

#### Diagrama del Sistema

Mostrar el pipeline:
```
filesrc → h264parse → nvv4l2decoder → nvstreammux → nvinfer → 
nvtracker → nvvideoconvert → nvdsosd → tee
                                        ├→ [video] encoder → muxer → filesink
                                        └→ [udp] encoder → rtppay → udpsink
```

#### Componentes Clave

1. **nvinfer**: Detección de personas usando ResNet10
2. **nvtracker**: Asignación de IDs únicos (tracking)
3. **nvdsosd**: Dibujado del ROI con colores dinámicos
4. **Algoritmo ROI**:
   - Verifica si centro del bounding box está en ROI
   - Mide tiempo de permanencia
   - Cambia color según estado
   - Genera alertas cuando excede tiempo

#### Estados del ROI

- 🟢 **Verde**: ROI vacío
- 🟡 **Amarillo**: Persona detectada (dentro del tiempo)
- 🔴 **Rojo**: Persona excedió el tiempo máximo

---

### Parte 2: Demostración de Casos (10 minutos)

#### Caso 1: Objeto NO supera el tiempo máximo (3 min)

**Mostrar el video pre-generado:**
```bash
vlc output_test1.mp4
# O con GStreamer:
gst-launch-1.0 filesrc location=output_test1.mp4 ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! autovideosink
```

**Mientras se reproduce, mostrar el reporte:**
```bash
cat report_test1.txt
```

**Explicar:**
- ROI grande (80% de pantalla)
- Tiempo máximo: 20 segundos
- Personas detectadas pero no exceden
- Color: Verde → Amarillo → Verde
- Sin alertas en reporte

**Mostrar líneas del reporte:**
```
ROI: left: 192 top: 108 width: 1536 height: 864
Max time: 20s
Detected: X (0)
0:15 person time 8s
0:32 person time 12s
```

---

#### Caso 2: Objeto SUPERA el tiempo máximo (3 min)

**Mostrar el video pre-generado:**
```bash
vlc output_test2.mp4
```

**Mostrar el reporte:**
```bash
cat report_test2.txt
```

**Explicar:**
- ROI mediano (60% de pantalla)
- Tiempo máximo: 2 segundos (muy corto)
- Varias personas exceden el tiempo
- Color: Verde → Amarillo → **ROJO**
- Alertas en reporte

**Destacar líneas con "alert":**
```
ROI: left: 384 top: 216 width: 1152 height: 648
Max time: 2s
Detected: 8 (5)
0:02 person time 3s alert  ← EXCEDIÓ
0:05 person time 4s alert  ← EXCEDIÓ
0:08 person time 1s        ← NO EXCEDIÓ
```

---

#### Caso 3: Múltiples objetos (4 min)

**Mostrar el video pre-generado:**
```bash
vlc output_test3.mp4
```

**Mostrar el reporte:**
```bash
cat report_test3.txt
```

**Explicar:**
- ROI centrado (40% de pantalla)
- Tiempo máximo: 5 segundos
- Comportamiento mixto: algunas exceden, otras no
- Tracking individual de cada persona

**Analizar el reporte:**
```bash
# Contar total detectados
grep "Detected:" report_test3.txt

# Contar alertas
grep -c "alert" report_test3.txt
```

**Ejemplo de análisis:**
```
Total detectadas: 12 personas
Con alerta: 3 personas
Sin alerta: 9 personas
```

---

### Caso Opcional: Falsos Positivos/Negativos (Si hay tiempo)

**Explicar mitigación:**
- Umbral de confianza: Solo objetos > 40% confianza
- Persistencia: 2 segundos sin detectar → eliminar
- Tracking robusto con nvtracker

**Opcional - Mostrar test adicional:**
```bash
vlc output_corner.mp4
cat report_corner.txt
```

---

## Tips para la Demostración

### ✅ DO's

1. **Tener todo pre-generado** - No ejecutar en vivo (toma tiempo)
2. **Usar VLC o reproductor rápido** - No gst-launch en vivo
3. **Tener reportes abiertos en editor** - Para mostrar rápido
4. **Practicar el flow** - Caso 1 → Caso 2 → Caso 3
5. **Preparar respuestas a preguntas comunes**:
   - ¿Por qué ese tiempo máximo?
   - ¿Cómo se calcula el centro del bounding box?
   - ¿Qué pasa si hay oclusión?

### ❌ DON'Ts

1. **NO compilar durante la demo** - Ya debe estar compilado
2. **NO ejecutar inference en tiempo real** - Usar videos pre-generados
3. **NO abrir muchos archivos** - Tener solo los necesarios
4. **NO improvisar** - Seguir el orden de casos

---

## Comandos de Respaldo (Por si acaso)

### Si necesitas re-generar un video rápidamente:

```bash
# Caso 1 (20 seg aprox)
make test1

# Caso 2 (20 seg aprox)
make test2

# Caso 3 (20 seg aprox)
make test3
```

### Si VLC no funciona:

```bash
# Alternativa 1: mpv
mpv output_test1.mp4

# Alternativa 2: GStreamer
gst-launch-1.0 filesrc location=output_test1.mp4 ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! autovideosink sync=false
```

### Si necesitas mostrar código:

```bash
# Abrir main.c en la sección relevante
nano +250 main.c  # Función de análisis ROI
```

---

## Checklist Final

Antes de la demostración, verificar:

- [ ] Aplicación compilada (`make`)
- [ ] Todos los videos generados (`test1`, `test2`, `test3`)
- [ ] Todos los reportes generados
- [ ] VLC o reproductor instalado y funcionando
- [ ] Terminales preparadas y en el directorio correcto
- [ ] Presentación del algoritmo lista (slides opcionales)
- [ ] Monitor conectado y configurado
- [ ] Audio apagado (opcional)

---

## Estructura de los 15 Minutos

| Tiempo | Actividad |
|--------|-----------|
| 0-5 min | Descripción del algoritmo y arquitectura |
| 5-8 min | Caso 1: No excede tiempo |
| 8-11 min | Caso 2: Excede tiempo |
| 11-14 min | Caso 3: Múltiples personas |
| 14-15 min | Conclusiones y preguntas |

---

## Preguntas Frecuentes

### P: ¿Por qué usan ese tiempo máximo?
**R:** Para demostrar los tres escenarios requeridos. En producción sería configurable según necesidad (ej: 30 seg para zona de espera, 5 seg para área restringida).

### P: ¿Cómo manejan la oclusión?
**R:** El tracker de NVIDIA (nvtracker) mantiene IDs incluso con oclusión temporal. Si desaparece >2 segundos, se considera que salió del ROI.

### P: ¿Qué pasa con múltiples personas muy juntas?
**R:** El modelo detecta cada persona independientemente y nvtracker asigna IDs únicos. Se rastrea cada una por separado.

### P: ¿Por qué el centro del bounding box?
**R:** Es más robusto que usar esquinas o área completa. Una persona "está en ROI" si su centro está dentro, evitando falsos positivos de bordes.

### P: ¿Cómo afecta el tamaño del ROI?
**R:** ROI grande = más detecciones, ROI pequeño = solo objetos que pasen exactamente ahí. Es configurable según la zona a vigilar.

---

## Recursos de Emergencia

Si algo falla durante la demo:

1. **Video no se reproduce**: Mostrar screenshots del video
2. **Reporte no se encuentra**: Recrear manualmente el formato
3. **Aplicación crashea**: Mostrar el código fuente y explicar
4. **Tiempo se acaba**: Priorizar Casos 1 y 2 (son los más importantes)

---

## Después de la Demostración

Preparar para entrega del 30 de noviembre:
- Diagrama final de la aplicación
- Descripción detallada del algoritmo ROI
- Screenshots de los resultados
- Análisis de CPU/GPU con tegrastats
- Repositorio con código limpio y documentado

¡Buena suerte! 🚀
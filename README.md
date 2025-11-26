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

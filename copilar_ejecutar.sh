# Compilar
make clean && make

# Ejemplo básico (video de salida)
./secure_roi --vi-file video.mp4 --left 0.3 --top 0.3 \
  --width 0.4 --height 0.4 --time 5 \
  --file-name report.txt --vo-file output.mp4 --mode video

# Ejemplo UDP + Video
./secure_roi --vi-file video.mp4 --left 0.5 --top 0.3 \
  --width 0.4 --height 0.4 --time 5 \
  --file-name report.txt --vo-file output.mp4 \
  --mode udp_video --udp-host 192.168.1.100
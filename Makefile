<<<<<<< HEAD
CC = gcc
TARGET = secure_roi

PKGS = gstreamer-1.0

CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))

LIBS = $(shell pkg-config --libs $(PKGS))

DEEPSTREAM_DIR = /opt/nvidia/deepstream/deepstream
DEEPSTREAM_INCLUDES = -I$(DEEPSTREAM_DIR)/sources/includes
DEEPSTREAM_LIBS = -L$(DEEPSTREAM_DIR)/lib -lnvdsgst_meta -lnvds_meta -lnvdsgst_helper -lnvbufsurface

CUDA_INCLUDES = -I/usr/local/cuda/include
CUDA_LIBS = -L/usr/local/cuda/lib64 -lcudart

ALL_CFLAGS = $(CFLAGS) $(DEEPSTREAM_INCLUDES) $(CUDA_INCLUDES)
ALL_LIBS = $(LIBS) $(DEEPSTREAM_LIBS) $(CUDA_LIBS) -Wl,-rpath,$(DEEPSTREAM_DIR)/lib

SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

# Video de ejemplo de DeepStream
VIDEO_SAMPLE = /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(ALL_LIBS)
	@echo "Compilación exitosa: $(TARGET)"

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	rm -f output_*.mp4 report*.txt
	@echo "Limpieza completada"

# Caso 1: Persona dentro del ROI que NO supera el tiempo máximo
test1: $(TARGET)
	@echo "=== CASO 1: Persona no excede tiempo (ROI grande, tiempo largo) ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.1 --top 0.1 --width 0.8 --height 0.8 \
		--time 20 --file-name report_test1.txt \
		--vo-file output_test1.mp4 --mode video
	@echo ""
	@echo "Resultado esperado: ROI verde → amarillo → verde"
	@echo "Ver: output_test1.mp4"
	@echo "Reporte: report_test1.txt"
	@cat report_test1.txt

# Caso 2: Persona dentro del ROI que SUPERA el tiempo máximo
test2: $(TARGET)
	@echo "=== CASO 2: Persona excede tiempo (ROI grande, tiempo corto) ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.2 --top 0.2 --width 0.6 --height 0.6 \
		--time 2 --file-name report_test2.txt \
		--vo-file output_test2.mp4 --mode video
	@echo ""
	@echo "Resultado esperado: ROI verde → amarillo → ROJO + alerta"
	@echo "Ver: output_test2.mp4"
	@echo "Reporte: report_test2.txt"
	@cat report_test2.txt

# Caso 3: Múltiples personas (ROI mediano, tiempo medio)
test3: $(TARGET)
	@echo "=== CASO 3: Múltiples personas (algunas exceden, otras no) ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report_test3.txt \
		--vo-file output_test3.mp4 --mode video
	@echo ""
	@echo "Resultado esperado: Algunas personas con alerta, otras sin alerta"
	@echo "Ver: output_test3.mp4"
	@echo "Reporte: report_test3.txt"
	@cat report_test3.txt

# Test con transmisión UDP
test-udp: $(TARGET)
	@echo "=== TEST UDP: Transmisión por red ==="
	@echo "Ejecuta en otra terminal: ./receive_udp.sh"
	@echo ""
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report_udp.txt \
		--mode udp --udp-host 192.168.0.255 --udp-port 5000

# Test con ambas salidas (archivo + UDP)
test-both: $(TARGET)
	@echo "=== TEST DUAL: Archivo + UDP simultáneos ==="
	@echo "Ejecuta en otra terminal: ./receive_udp.sh"
	@echo ""
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report_both.txt \
		--vo-file output_both.mp4 --mode udp_video \
		--udp-host 192.168.0.255 --udp-port 5000

# Test con ROI pequeño en esquina (detecta solo objetos que pasen por ahí)
test-corner: $(TARGET)
	@echo "=== TEST ESQUINA: ROI pequeño en esquina superior izquierda ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.05 --top 0.05 --width 0.2 --height 0.2 \
		--time 3 --file-name report_corner.txt \
		--vo-file output_corner.mp4 --mode video
	@echo ""
	@echo "Resultado esperado: Solo detecta personas que crucen la esquina"
	@echo "Ver: output_corner.mp4"
	@echo "Reporte: report_corner.txt"
	@cat report_corner.txt

# Test con ROI centrado (zona de mayor tráfico)
test-center: $(TARGET)
	@echo "=== TEST CENTRO: ROI centrado (zona de mayor tráfico) ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.35 --top 0.35 --width 0.3 --height 0.3 \
		--time 4 --file-name report_center.txt \
		--vo-file output_center.mp4 --mode video
	@echo ""
	@echo "Resultado esperado: Múltiples detecciones en zona central"
	@echo "Ver: output_center.mp4"
	@echo "Reporte: report_center.txt"
	@cat report_center.txt

# Ejecutar todos los casos de prueba en secuencia
test-all: test1 test2 test3 test-corner test-center
	@echo ""
	@echo "==================================================="
	@echo "TODOS LOS TESTS COMPLETADOS"
	@echo "==================================================="
	@echo ""
	@echo "Archivos generados:"
	@ls -lh output_*.mp4 report*.txt
	@echo ""
	@echo "Para ver los reportes:"
	@echo "  cat report_test1.txt"
	@echo "  cat report_test2.txt"
	@echo "  cat report_test3.txt"
	@echo ""
	@echo "Para reproducir videos:"
	@echo "  vlc output_test1.mp4"
	@echo "  vlc output_test2.mp4"
	@echo "  vlc output_test3.mp4"

# Verificar que el video de ejemplo existe
check-video:
	@echo "Verificando video de ejemplo..."
	@if [ -f $(VIDEO_SAMPLE) ]; then \
		echo "✓ Video encontrado: $(VIDEO_SAMPLE)"; \
		gst-discoverer-1.0 $(VIDEO_SAMPLE); \
	else \
		echo "✗ Video no encontrado: $(VIDEO_SAMPLE)"; \
		echo "  Verifica la instalación de DeepStream SDK"; \
		exit 1; \
	fi

# Listar todos los videos de ejemplo disponibles
list-videos:
	@echo "Videos de ejemplo disponibles en DeepStream:"
	@ls -lh /opt/nvidia/deepstream/deepstream/samples/streams/*.mp4 2>/dev/null || \
		echo "No se encontraron videos en /opt/nvidia/deepstream/deepstream/samples/streams/"

# Quick test (más rápido para desarrollo)
quick: $(TARGET)
	@echo "=== TEST RÁPIDO: Configuración básica ==="
	./$(TARGET) --vi-file $(VIDEO_SAMPLE) \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report.txt \
		--vo-file output.mp4 --mode video
	@echo ""
	@cat report.txt

# Ayuda
help:
	@echo "Targets disponibles:"
	@echo "  make              - Compilar la aplicación"
	@echo "  make clean        - Limpiar archivos compilados y generados"
	@echo "  make check-video  - Verificar que el video de ejemplo existe"
	@echo "  make list-videos  - Listar todos los videos de ejemplo"
	@echo ""
	@echo "Tests individuales (casos del proyecto):"
	@echo "  make test1        - Persona NO excede tiempo"
	@echo "  make test2        - Persona EXCEDE tiempo"
	@echo "  make test3        - Múltiples personas"
	@echo ""
	@echo "Tests adicionales:"
	@echo "  make test-corner  - ROI pequeño en esquina"
	@echo "  make test-center  - ROI centrado"
	@echo "  make test-udp     - Transmisión UDP"
	@echo "  make test-both    - Archivo + UDP"
	@echo ""
	@echo "  make test-all     - Ejecutar todos los tests"
	@echo "  make quick        - Test rápido para desarrollo"

.PHONY: all clean check-video list-videos test1 test2 test3 test-udp test-both \
        test-corner test-center test-all quick help
=======
# Versión de CUDA en JetPack 4.6.x
CUDA_VER:=10.2

APP:=deepstream_roi

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

# Ruta de instalación de DeepStream en Jetson
NVDS_PATH?=/opt/nvidia/deepstream/deepstream-6.0
LIB_INSTALL_DIR?=$(NVDS_PATH)/lib
APP_INSTALL_DIR?=$(NVDS_PATH)/bin

ifeq ($(TARGET_DEVICE),aarch64)
  CFLAGS:= -DPLATFORM_TEGRA
endif

CXX:=g++
SRCS:= $(wildcard *.cpp)
INCS:= $(wildcard *.h)

PKGS:= gstreamer-1.0 gstreamer-video-1.0

OBJS:= $(SRCS:.cpp=.o)

CFLAGS+= -I$(NVDS_PATH)/sources/includes \
         -I$(NVDS_PATH)/includes \
         -I/usr/local/cuda-$(CUDA_VER)/include

CFLAGS+= $(shell pkg-config --cflags $(PKGS))

LIBS:= $(shell pkg-config --libs $(PKGS))

LIBS+= -L/usr/local/cuda-$(CUDA_VER)/lib64 -lcudart \
       -L$(LIB_INSTALL_DIR) \
       -lnvdsgst_meta -lnvds_meta \
       -lnvbufsurface -lnvbufsurftransform \
       -Wl,-rpath,$(LIB_INSTALL_DIR)

all: $(APP)

%.o: %.cpp $(INCS) Makefile
	$(CXX) -c -o $@ $(CFLAGS) $<

$(APP): $(OBJS) Makefile
	$(CXX) -o $(APP) $(OBJS) $(LIBS)

install: $(APP)
	cp -v $(APP) $(APP_INSTALL_DIR)

clean:
	rm -f $(OBJS) $(APP)
>>>>>>> origin/ggutierrez

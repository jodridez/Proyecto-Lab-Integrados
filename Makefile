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

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(ALL_LIBS)
	@echo "Compilación exitosa: $(TARGET)"

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Limpieza completada"

run: $(TARGET)
	./$(TARGET) --vi-file /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report.txt \
		--vo-file output_roi.mp4 --mode video

test-udp: $(TARGET)
	./$(TARGET) --vi-file /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report.txt \
		--mode udp --udp-host 192.168.0.255 --udp-port 5000

test-both: $(TARGET)
	./$(TARGET) --vi-file /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 \
		--left 0.3 --top 0.3 --width 0.4 --height 0.4 \
		--time 5 --file-name report.txt \
		--vo-file output_roi.mp4 --mode udp_video \
		--udp-host 192.168.0.255 --udp-port 5000

.PHONY: all clean run test-udp test-both
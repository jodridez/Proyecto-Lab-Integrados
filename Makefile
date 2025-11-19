# Makefile

# Ajusta esta ruta según la versión de DeepStream de la Jetson
DS_PATH ?= /opt/nvidia/deepstream/deepstream-6.3

CC      := gcc

CFLAGS  := -Wall -Wextra -O2 \
           -I$(DS_PATH)/sources/includes \
           -I$(DS_PATH)/includes \
           `pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0`

LDFLAGS := `pkg-config --libs gstreamer-1.0 gstreamer-video-1.0` \
           -L$(DS_PATH)/lib \
           -lnvdsgst_meta -lnvds_meta -lnvds_infer -lnvds_infer_server -lnvds_utils

all: roi_app

roi_app: src/main.o
	$(CC) -o $@ $^ $(LDFLAGS)

src/main.o: src/main.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f roi_app src/*.o

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

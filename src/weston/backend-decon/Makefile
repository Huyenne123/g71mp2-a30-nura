# Makefile for Samsung Exynos DECON Weston Backend
# Target: Weston 14.0.2 / libweston-14

CC ?= clang
CFLAGS ?= -O2 -g -Wall -Wextra -fPIC -fvisibility=hidden
CPPFLAGS ?= -I. -I../../compositor -I/usr/include/drm
LDFLAGS ?= -shared

PKG_CONFIG ?= pkg-config
DEPS = libweston-14 pixman-1 wayland-server

# Detect host vs cross/sysroot flags
HAVE_PKG := $(shell $(PKG_CONFIG) --exists $(DEPS) 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAVE_PKG),1)
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(DEPS))
LIBS += $(shell $(PKG_CONFIG) --libs $(DEPS))
else
SYSROOT ?= ../../../scratch/sysroot
CPPFLAGS += -I$(SYSROOT)/usr/include/libweston-14 \
            -I$(SYSROOT)/usr/include/pixman-1 \
            -I$(SYSROOT)/usr/include \
            -I../../../scratch/weston \
            -I../../../scratch/weston/libweston \
            -include "../../../scratch/weston/config.h"
LDFLAGS += -L$(SYSROOT)/usr/lib/x86_64-linux-gnu
LIBS += -lweston-14 -lpixman-1 -lwayland-server
endif

SRCS = decon.c ../../compositor/decon_backend.c
OBJS = $(SRCS:.c=.o)
TARGET = decon-backend.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f $(TARGET) $(OBJS)

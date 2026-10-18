# Makefile for Weston Backend Unit Tests
# Milestone 6B: Weston Direct-DECON Backend Unit Test Suite

CC ?= clang
CFLAGS ?= -O2 -g -Wall -Wextra
CPPFLAGS ?= -I../src/weston/backend-decon \
            -I../src/compositor \
            -I/usr/include/drm \
            -I../scratch/sysroot/usr/include/libweston-14 \
            -I../scratch/sysroot/usr/include/pixman-1 \
            -I../scratch/sysroot/usr/include \
            -I../scratch/weston \
            -I../scratch/weston/libweston \
            -include "../scratch/weston/config.h"

LDFLAGS ?= -L../scratch/sysroot/usr/lib/x86_64-linux-gnu \
           -Wl,-rpath,/home/neyuh/mali-a30/scratch/sysroot/usr/lib/x86_64-linux-gnu

LIBS ?= -lweston-14 -lpixman-1 -lwayland-server

TARGET = weston_backend_test
SRCS = weston_backend_test.c \
       ../src/weston/backend-decon/decon.c \
       ../src/compositor/decon_backend.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS) $(LIBS) -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

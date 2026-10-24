#!/bin/sh
set -e

SRC_DIR="/home/user/decon-build/weston-src"
OUT_DIR="/home/user/build-xwayland"

echo "[*] Generating protocol files..."
wayland-scanner server-header /usr/share/wayland-protocols/staging/xwayland-shell/xwayland-shell-v1.xml "${OUT_DIR}/xwayland-shell-v1-server-protocol.h"
wayland-scanner private-code /usr/share/wayland-protocols/staging/xwayland-shell/xwayland-shell-v1.xml "${OUT_DIR}/xwayland-shell-v1-protocol.c"

PKG_CFLAGS="$(pkg-config --cflags xcb xcb-composite xcb-shape xcb-xfixes xcursor cairo-xcb wayland-server pixman-1 xkbcommon)"
PKG_LIBS="$(pkg-config --libs xcb xcb-composite xcb-shape xcb-xfixes xcursor cairo-xcb wayland-server pixman-1 xkbcommon)"

CFLAGS="-fPIC -O2 -DHAVE_XWAYLAND_LISTENFD=1 \
  -I${OUT_DIR} \
  -I${SRC_DIR} \
  -I${SRC_DIR}/include \
  -I${SRC_DIR}/xwayland \
  -I/home/user/build-decon/include \
  -I/home/user/build-decon/include/weston-inc \
  ${PKG_CFLAGS}"

echo "[*] Compiling sources..."
gcc ${CFLAGS} -c "${SRC_DIR}/xwayland/launcher.c" -o "${OUT_DIR}/launcher.o"
gcc ${CFLAGS} -c "${SRC_DIR}/xwayland/window-manager.c" -o "${OUT_DIR}/window-manager.o"
gcc ${CFLAGS} -c "${SRC_DIR}/xwayland/selection.c" -o "${OUT_DIR}/selection.o"
gcc ${CFLAGS} -c "${SRC_DIR}/xwayland/dnd.c" -o "${OUT_DIR}/dnd.o"
gcc ${CFLAGS} -c "${SRC_DIR}/shared/xcb-xwayland.c" -o "${OUT_DIR}/xcb-xwayland.o"
gcc ${CFLAGS} -c "${OUT_DIR}/xwayland-shell-v1-protocol.c" -o "${OUT_DIR}/xwayland-shell-v1-protocol.o"

echo "[*] Linking xwayland.so..."
gcc -shared -fPIC -o "${OUT_DIR}/xwayland.so" \
  "${OUT_DIR}/launcher.o" \
  "${OUT_DIR}/window-manager.o" \
  "${OUT_DIR}/selection.o" \
  "${OUT_DIR}/dnd.o" \
  "${OUT_DIR}/xcb-xwayland.o" \
  "${OUT_DIR}/xwayland-shell-v1-protocol.o" \
  ${PKG_LIBS} -L/opt/weston-decon/usr/lib -lweston-14

echo "[+] Successfully built xwayland.so:"
ls -lh "${OUT_DIR}/xwayland.so"

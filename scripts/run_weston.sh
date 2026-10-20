#!/bin/sh
set -e

BUNDLE_DIR="/opt/weston-decon"

echo "[*] Setting up Samsung vendor/system compatibility symlinks..."
[ -e /vendor ] || ln -sf "${BUNDLE_DIR}/vendor" /vendor
[ -e /system ] || ln -sf "${BUNDLE_DIR}/system" /system
[ -e /usr/lib/libweston-14 ] || ln -sf "${BUNDLE_DIR}/usr/lib/libweston-14" /usr/lib/libweston-14
[ -e /usr/lib/weston ] || ln -sf "${BUNDLE_DIR}/usr/lib/weston" /usr/lib/weston
[ -e /usr/lib/libhybris ] || ln -sf "${BUNDLE_DIR}/usr/lib/libhybris" /usr/lib/libhybris
[ -e /usr/share/weston ] || ln -sf "${BUNDLE_DIR}/usr/share/weston" /usr/share/weston

echo "[*] Setting permissions on graphics and synchronization devices..."
chmod 666 /dev/fb0 2>/dev/null || true
chmod 666 /dev/mali0 2>/dev/null || true
chmod 666 /dev/ion 2>/dev/null || true
chmod 666 /dev/sw_sync 2>/dev/null || true
chmod 666 /dev/input/event* 2>/dev/null || true

echo "[*] Releasing Sxmo input grab (stopping lisgd) and pausing Xorg..."
pkill -TERM -f lisgd 2>/dev/null || true
pkill -STOP -f Xorg 2>/dev/null || true
pkill -STOP -f sxmo 2>/dev/null || true

mkdir -p /tmp/weston-runtime
chmod 777 /tmp/weston-runtime
rm -f /tmp/weston-runtime/wayland-* 2>/dev/null || true

export XDG_RUNTIME_DIR="/tmp/weston-runtime"
export WAYLAND_DISPLAY="wayland-0"
export LD_LIBRARY_PATH="${BUNDLE_DIR}/usr/lib:${BUNDLE_DIR}/usr/lib/weston:/usr/lib"
export HYBRIS_LD_LIBRARY_PATH="/vendor/lib64:/vendor/lib64/hw:/vendor/lib64/egl:/system/lib64"
export XDG_DATA_DIRS="${BUNDLE_DIR}/usr/share:/usr/share"
export EGL_PLATFORM="hwcomposer"
export HYBRIS_EGLPLATFORM="hwcomposer"
export LIBEGL="/vendor/lib64/egl/libGLES_mali.so"
export LIBGLESV2="/vendor/lib64/egl/libGLES_mali.so"

echo "[*] Starting Weston 14 Direct-DECON GL compositor on physical AMOLED display..."
exec "${BUNDLE_DIR}/usr/bin/weston" \
    --backend=decon \
    --renderer=gl \
    --idle-time=0 \
    --socket=wayland-0 \
    --config="${BUNDLE_DIR}/etc/weston.ini" \
    --log=/tmp/weston.log "$@"

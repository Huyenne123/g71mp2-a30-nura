#!/bin/sh

BUNDLE_DIR="/opt/weston-decon"

cleanup() {
    trap - EXIT INT TERM
    echo ""
    echo "[*] Restoring Sxmo desktop service (tinydm)..."
    killall -9 weston-simple-egl weston-simple-touch weston-simple-shm weston-keyboard weston-desktop-shell weston phoc phosh 2>/dev/null || true
    killall -9 Xorg dwm lisgd st 2>/dev/null || true
    rm -f /tmp/.X*-lock /tmp/.X11-unix/X* 2>/dev/null || true
    rc-service tinydm restart >/dev/null 2>&1 || true
    echo "[+] Sxmo restored successfully."
}

trap cleanup EXIT INT TERM

echo "[*] Setting up Samsung vendor/system compatibility symlinks..."
[ -e /vendor ] || ln -sf "${BUNDLE_DIR}/vendor" /vendor
[ -e /system ] || ln -sf "${BUNDLE_DIR}/system" /system
[ -e /usr/lib/libweston-14 ] || ln -sf "${BUNDLE_DIR}/usr/lib/libweston-14" /usr/lib/libweston-14
[ -e /usr/lib/weston ] || ln -sf "${BUNDLE_DIR}/usr/lib/weston" /usr/lib/weston
[ -e /usr/lib/libhybris ] || ln -sf "${BUNDLE_DIR}/usr/lib/libhybris" /usr/lib/libhybris
[ -e /usr/share/weston ] || ln -sf "${BUNDLE_DIR}/usr/share/weston" /usr/share/weston

# Create Android graphics device node compatibility
mkdir -p /dev/graphics
[ -e /dev/graphics/fb0 ] || ln -sf /dev/fb0 /dev/graphics/fb0

echo "[*] Setting permissions on graphics and synchronization devices..."
chmod 666 /dev/fb0 2>/dev/null || true
chmod 666 /dev/graphics/fb0 2>/dev/null || true
chmod 666 /dev/mali0 2>/dev/null || true
chmod 666 /dev/ion 2>/dev/null || true
chmod 666 /dev/sw_sync 2>/dev/null || true
chmod 666 /dev/input/event* 2>/dev/null || true

echo "[*] Stopping Xorg / Sxmo desktop service (tinydm)..."
rc-service tinydm stop
killall -9 Xorg dwm lisgd st 2>/dev/null || true
rm -f /tmp/.X*-lock /tmp/.X11-unix/X* 2>/dev/null || true
sleep 0.5

rm -rf /tmp/weston-runtime
mkdir -p /tmp/weston-runtime
chmod 700 /tmp/weston-runtime

export XDG_RUNTIME_DIR="/tmp/weston-runtime"
export WAYLAND_DISPLAY="wayland-0"
export LD_LIBRARY_PATH="${BUNDLE_DIR}/usr/lib:${BUNDLE_DIR}/usr/lib/weston:/usr/lib"
export HYBRIS_LD_LIBRARY_PATH="/vendor/lib64:/vendor/lib64/hw:/vendor/lib64/egl:/system/lib64"
export XDG_DATA_DIRS="${BUNDLE_DIR}/usr/share:/usr/share"
export EGL_PLATFORM="hwcomposer"
export HYBRIS_EGLPLATFORM="hwcomposer"
export LIBEGL="/vendor/lib64/egl/libGLES_mali.so"
export LIBGLESV2="/vendor/lib64/egl/libGLES_mali.so"

# Optional client auto-launch: pass CLIENT=<cmd> or start desktop shell by default
if [ -n "$AUTO_CLIENT" ]; then
(
    while [ ! -S /tmp/weston-runtime/wayland-0 ]; do sleep 0.2; done
    sleep 0.5
    echo "[*] Auto-launching client: $AUTO_CLIENT"
    export XDG_RUNTIME_DIR="/tmp/weston-runtime"
    export WAYLAND_DISPLAY="wayland-0"
    export LD_LIBRARY_PATH="${BUNDLE_DIR}/usr/lib:${BUNDLE_DIR}/usr/lib/weston:/usr/lib"
    $AUTO_CLIENT >/tmp/client.log 2>&1 &
) &
fi

echo "[*] Starting Weston 14 Direct-DECON GL compositor on physical AMOLED display..."
echo "[*] Press Ctrl+C at any time in this terminal to exit and return to Sxmo."

"${BUNDLE_DIR}/usr/bin/weston" \
    --backend=decon \
    --renderer=gl \
    --idle-time=0 \
    --socket=wayland-0 \
    --config="${BUNDLE_DIR}/etc/weston.ini" \
    --log=/tmp/weston.log "$@"

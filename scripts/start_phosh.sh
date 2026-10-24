#!/bin/sh

BUNDLE_DIR="/opt/weston-decon"

cleanup() {
    trap - EXIT INT TERM
    echo ""
    echo "[*] Exiting Phosh and restoring Sxmo desktop..."
    kill -TERM $PHOC_PID 2>/dev/null || true
    kill -INT $WESTON_PID 2>/dev/null || true
    sleep 1
    killall -9 phoc phosh gnome-session weston 2>/dev/null || true
    rc-service tinydm restart >/dev/null 2>&1 || true
    echo "[+] Sxmo restored successfully."
}
trap cleanup EXIT INT TERM

echo "[*] Setting up system symlinks..."
[ -e /vendor ] || ln -sf "${BUNDLE_DIR}/vendor" /vendor
[ -e /system ] || ln -sf "${BUNDLE_DIR}/system" /system
[ -e /usr/lib/libweston-14 ] || ln -sf "${BUNDLE_DIR}/usr/lib/libweston-14" /usr/lib/libweston-14
[ -e /usr/lib/weston ] || ln -sf "${BUNDLE_DIR}/usr/lib/weston" /usr/lib/weston
[ -e /usr/lib/libhybris ] || ln -sf "${BUNDLE_DIR}/usr/lib/libhybris" /usr/lib/libhybris
[ -e /usr/share/weston ] || ln -sf "${BUNDLE_DIR}/usr/share/weston" /usr/share/weston

mkdir -p /dev/graphics
[ -e /dev/graphics/fb0 ] || ln -sf /dev/fb0 /dev/graphics/fb0
chmod 666 /dev/fb0 /dev/graphics/fb0 /dev/mali0 /dev/ion /dev/sw_sync /dev/input/event* 2>/dev/null || true

echo "[*] Stopping tinydm (Xorg/Sxmo)..."
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

echo "[*] Starting hardware-accelerated Weston master compositor..."
"${BUNDLE_DIR}/usr/bin/weston" \
    --backend=decon \
    --renderer=gl \
    --idle-time=0 \
    --socket=wayland-0 \
    --shell=kiosk-shell.so \
    --log=/tmp/weston.log &
WESTON_PID=$!

COUNT=0
while [ ! -S /tmp/weston-runtime/wayland-0 ]; do
    sleep 0.1
    COUNT=$((COUNT + 1))
    if [ $COUNT -gt 50 ]; then
        echo "[-] Timeout waiting for wayland-0"
        exit 1
    fi
done
echo "[+] Weston active on physical AMOLED display."
sleep 0.5

unset EGL_PLATFORM HYBRIS_EGLPLATFORM LIBEGL LIBGLESV2
export WAYLAND_DISPLAY="wayland-0"
export WLR_BACKENDS="wayland"
export WLR_RENDERER="pixman"
export XDG_CURRENT_DESKTOP="GNOME:Phosh"
export DESKTOP_SESSION="phosh"
export GSK_RENDERER="cairo"
export GDK_DEBUG="gl-disable"
export GLYCIN_DISABLE_SANDBOX=1
export LD_PRELOAD="/usr/local/lib/libfaccessat_fix.so"

echo "[*] Launching Phoc + Phosh mobile desktop..."
echo "[*] Press Ctrl+C at any time to exit Phosh and return to Sxmo."

dbus-run-session /usr/sbin/phoc -C /home/user/phoc-decon.ini -E "/usr/sbin/gnome-session --session=phosh" -v > /tmp/phosh.log 2>&1 &
PHOC_PID=$!

wait $PHOC_PID

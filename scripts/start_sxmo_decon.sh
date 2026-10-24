#!/bin/sh

BUNDLE_DIR="/opt/weston-decon"

cleanup() {
    trap - EXIT INT TERM
    echo ""
    echo "[*] Exiting Hardware-Accelerated Sxmo and restoring native display..."
    kill -TERM $SXMO_PID 2>/dev/null || true
    kill -TERM $XWAYLAND_PID 2>/dev/null || true
    kill -INT $WESTON_PID 2>/dev/null || true
    sleep 1
    killall -9 dwm sxmobar lisgd st Xwayland weston 2>/dev/null || true
    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /run/user/10000/wayland-0 2>/dev/null || true
    rc-service tinydm restart >/dev/null 2>&1 || true
    echo "[+] Native Sxmo restored successfully."
}
trap cleanup EXIT INT TERM

echo "[*] Setting up hardware display links..."
[ -e /vendor ] || ln -sf "${BUNDLE_DIR}/vendor" /vendor
[ -e /system ] || ln -sf "${BUNDLE_DIR}/system" /system
[ -e /usr/lib/libweston-14 ] || ln -sf "${BUNDLE_DIR}/usr/lib/libweston-14" /usr/lib/libweston-14
[ -e /usr/lib/weston ] || ln -sf "${BUNDLE_DIR}/usr/lib/weston" /usr/lib/weston
[ -e /usr/lib/libhybris ] || ln -sf "${BUNDLE_DIR}/usr/lib/libhybris" /usr/lib/libhybris
[ -e /usr/share/weston ] || ln -sf "${BUNDLE_DIR}/usr/share/weston" /usr/share/weston

mkdir -p /dev/graphics
[ -e /dev/graphics/fb0 ] || ln -sf /dev/fb0 /dev/graphics/fb0
chmod 666 /dev/fb0 /dev/graphics/fb0 /dev/mali0 /dev/ion /dev/sw_sync /dev/input/event* 2>/dev/null || true

# Maximize CPU & GPU clocks for best responsiveness
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true
echo 676000 > /sys/class/misc/mali0/device/dvfs_min_lock 2>/dev/null || true

echo "[*] Stopping native unaccelerated Xorg/tinydm..."
rc-service tinydm stop
killall -9 Xorg dwm lisgd st 2>/dev/null || true
rm -f /tmp/.X*-lock /tmp/.X11-unix/X* 2>/dev/null || true
mkdir -p /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix
sleep 0.5

rm -rf /tmp/weston-runtime
mkdir -p /tmp/weston-runtime
chmod 777 /tmp/weston-runtime

export XDG_RUNTIME_DIR="/tmp/weston-runtime"
export WAYLAND_DISPLAY="wayland-0"
export LD_LIBRARY_PATH="${BUNDLE_DIR}/usr/lib:${BUNDLE_DIR}/usr/lib/weston:/usr/lib"
export HYBRIS_LD_LIBRARY_PATH="/vendor/lib64:/vendor/lib64/hw:/vendor/lib64/egl:/system/lib64"
export XDG_DATA_DIRS="${BUNDLE_DIR}/usr/share:/usr/share"
export EGL_PLATFORM="hwcomposer"
export HYBRIS_EGLPLATFORM="hwcomposer"
export LIBEGL="/vendor/lib64/egl/libGLES_mali.so"
export LIBGLESV2="/vendor/lib64/egl/libGLES_mali.so"

rm -f /tmp/weston.log
echo "[*] Starting hardware-accelerated Weston master compositor (Direct-DECON GL)..."
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
chmod 666 /tmp/weston-runtime/wayland-0
chmod 777 /tmp/weston-runtime

mkdir -p /run/user/10000
chown user:user /run/user/10000
chmod 700 /run/user/10000
rm -f /run/user/10000/wayland-*
ln -sf /tmp/weston-runtime/wayland-0 /run/user/10000/wayland-0
chown -h user:user /run/user/10000/wayland-0

echo "[+] Weston active on physical AMOLED display (1080x2340 GL)."
sleep 0.5

echo "[*] Starting rootful Xwayland on :0..."
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /tmp/xwayland.log

runuser -u user -- sh -c '
    export WAYLAND_DISPLAY="wayland-0"
    export XDG_RUNTIME_DIR="/run/user/10000"
    exec /usr/bin/Xwayland :0 -geometry 1080x2340 -fullscreen -ac -retro > /tmp/xwayland.log 2>&1
' &
XWAYLAND_PID=$!

COUNT=0
while [ ! -S /tmp/.X11-unix/X0 ]; do
    sleep 0.1
    COUNT=$((COUNT + 1))
    if [ $COUNT -gt 50 ]; then
        echo "[-] Timeout waiting for /tmp/.X11-unix/X0"
        cat /tmp/xwayland.log
        exit 1
    fi
done
echo "[+] X11 display :0 active via Xwayland (Hardware GPU presentation)."

echo "[*] Launching Sxmo (dwm) session on :0..."
echo "[*] Press Ctrl+C at any time to exit and return to native Sxmo."

runuser -u user -- sh -c '
    export DISPLAY=":0"
    export WAYLAND_DISPLAY="wayland-0"
    export XDG_RUNTIME_DIR="/run/user/10000"
    export HOME="/home/user"
    export USER="user"
    export LOGNAME="user"
    cd /home/user
    exec dbus-run-session /usr/sbin/sxmo_dwminit.sh
' &
SXMO_PID=$!

wait $SXMO_PID

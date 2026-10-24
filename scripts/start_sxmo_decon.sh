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
    killall -9 dwm sxmobar lisgd st Xwayland weston superd 2>/dev/null || true
    for i in $(seq 1 30); do
        if ! netstat -xl 2>/dev/null | grep -q '/tmp/\.X11-unix/X0'; then
            break
        fi
        sleep 0.1
    done
    rm -rf /tmp/.X*-lock /tmp/.X11-unix/X* /run/user/10000/wayland-* /run/user/10000/superd.sock /run/user/10000/dbus.bus 2>/dev/null || true
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
rc-service tinydm stop >/dev/null 2>&1 || true
killall -9 Xwayland Xorg xinit startx dwm sxmobar lisgd st superd weston tinydm-run-session firefox-esr 2>/dev/null || true

for i in $(seq 1 30); do
    if ! netstat -xl 2>/dev/null | grep -q '/tmp/\.X11-unix/X0'; then
        break
    fi
    sleep 0.1
done

rm -rf /tmp/.X*-lock /tmp/.X11-unix/X* /run/user/10000/superd.sock /run/user/10000/wayland-* /run/user/10000/dbus.bus /home/user/.Xauthority 2>/dev/null || true
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
rm -f /run/user/10000/wayland-* /run/user/10000/superd.sock
ln -sf /tmp/weston-runtime/wayland-0 /run/user/10000/wayland-0
chown -h user:user /run/user/10000/wayland-0

echo "[+] Weston active on physical AMOLED display (1080x2340 GL)."
sleep 0.5

echo "[*] Starting rootful Xwayland on :0 (-dpi 288 phone scaling)..."
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /tmp/xwayland.log

runuser -u user -- sh -c '
    export WAYLAND_DISPLAY="wayland-0"
    export XDG_RUNTIME_DIR="/run/user/10000"
    exec /usr/bin/Xwayland :0 -geometry 1080x2340 -fullscreen -dpi 288 -fakescreenfps 60 +bs -ac -retro > /tmp/xwayland.log 2>&1
' &
XWAYLAND_PID=$!

COUNT=0
while ! DISPLAY=:0 xset q >/dev/null 2>&1; do
    sleep 0.1
    COUNT=$((COUNT + 1))
    if [ $COUNT -gt 60 ]; then
        echo "[-] Timeout waiting for Xwayland :0 ready"
        cat /tmp/xwayland.log
        exit 1
    fi
done
echo "[+] X11 display :0 active via Xwayland (Hardware GPU presentation)."

# Pre-seed xrdb resources immediately before any X11 client connects
runuser -u user -- sh -c '
    export DISPLAY=":0"
    if [ -f /home/user/.Xresources ]; then
        xrdb -merge /home/user/.Xresources
    fi
    echo "Xft.dpi: 288" | xrdb -merge
    if [ -f /home/user/.config/sxmo/conky.conf ]; then
        sed -i "s/out_to_wayland = true/out_to_wayland = false/g" /home/user/.config/sxmo/conky.conf
    fi
'

echo "[*] Launching Sxmo (dwm) session on :0..."
echo "[*] Press Ctrl+C at any time to exit and return to native Sxmo."

runuser -u user -- sh -c '
    export PATH="/home/user/.config/sxmo/hooks:/usr/share/sxmo/default_hooks:/home/user/.local/bin:/usr/local/bin:/usr/bin:/bin:$PATH"
    . /etc/profile
    [ -f /etc/profile.d/sxmo_init.sh ] && . /etc/profile.d/sxmo_init.sh
    export PATH="/home/user/.config/sxmo/hooks:/usr/share/sxmo/default_hooks:/usr/local/bin:$PATH"
    export DISPLAY=":0"
    unset WAYLAND_DISPLAY
    export XDG_RUNTIME_DIR="/run/user/10000"
    export HOME="/home/user"
    export USER="user"
    export LOGNAME="user"
    export SXMO_WM=dwm
    export XDG_CURRENT_DESKTOP=dwm
    export SXMO_TERMINAL="st"
    export SXMO_MENU="dmenu"
    export SXMO_MONITOR="XWAYLAND0"
    export MOZ_USE_XINPUT2=1
    export GDK_BACKEND=x11
    export QT_QPA_PLATFORM=xcb
    cd /home/user
    exec dbus-run-session /usr/sbin/sxmo_dwminit.sh
' &
SXMO_PID=$!

wait $SXMO_PID

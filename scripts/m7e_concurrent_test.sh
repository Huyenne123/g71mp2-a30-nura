#!/system/bin/sh
set -e

LOG="/data/local/tmp/concurrent_test.log"
WESTON_LOG="/data/alpine/tmp/weston-concurrent.log"
rm -f "$LOG" "$WESTON_LOG"

log() {
    echo "$@" | tee -a "$LOG"
}

get_decon_irq() {
    cat /proc/interrupts | grep -i "249:" | awk '{s=0; for(i=2;i<=NF-3;i++) s+=$i; print s}'
}

get_mali_irq() {
    cat /proc/interrupts | grep -i "11500000.mali" | awk '{for(i=2;i<=NF-3;i++) s+=$i} END {print s}'
}

ORIG_TIMEOUT=$(settings get system screen_off_timeout 2>/dev/null || echo 60000)

restore_services() {
    log "[*] Restoring Android display services..."
    settings put system screen_off_timeout "$ORIG_TIMEOUT" 2>/dev/null || true
    svc power stayon false 2>/dev/null || true
    echo "weston_soak" > /sys/power/wake_unlock 2>/dev/null || true
    start vendor.hwcomposer-2-1 2>/dev/null || true
    start surfaceflinger 2>/dev/null || true
    for i in $(seq 1 20); do
        if service list 2>/dev/null | grep -q "input:"; then break; fi
        sleep 0.5
    done
    input keyevent 82 2>/dev/null || true
    log "[*] Android display services successfully restored."
}

trap restore_services EXIT INT TERM

log "[*] Mounting Alpine..."
/data/local/tmp/mount_alpine.sh

log "[*] Acquiring wake lock..."
svc power stayon true
settings put system screen_off_timeout 2147483647
echo "weston_soak" > /sys/power/wake_lock 2>/dev/null || true

log "[*] Stopping Android SurfaceFlinger and HWC2..."
stop surfaceflinger
stop vendor.hwcomposer-2-1
sleep 1

mkdir -p /data/alpine/tmp/weston-xdg-runtime
chmod 0700 /data/alpine/tmp/weston-xdg-runtime
rm -f /data/alpine/tmp/weston-xdg-runtime/wayland-*

DECON_START=$(get_decon_irq)
MALI_START=$(get_mali_irq)

log "[*] Launching Weston GL renderer..."
chroot /data/alpine /bin/sh -c "
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export XDG_RUNTIME_DIR=/tmp/weston-xdg-runtime
export LD_LIBRARY_PATH=/usr/lib
exec /usr/bin/weston --backend=decon --renderer=gl --idle-time=0 --socket=wayland-0 --log=/tmp/weston-concurrent.log
" &
WESTON_PID=$!

for i in $(seq 1 50); do
    if [ -S /data/alpine/tmp/weston-xdg-runtime/wayland-0 ]; then
        break
    fi
    sleep 0.1
done

WESTON_REAL_PID=$(pidof weston || true)
log "[*] Weston ready (PID: $WESTON_REAL_PID)."

log "[*] Launching weston-simple-egl (GPU client)..."
chroot /data/alpine /bin/sh -c "
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export XDG_RUNTIME_DIR=/tmp/weston-xdg-runtime
export WAYLAND_DISPLAY=wayland-0
export LD_LIBRARY_PATH=/usr/lib
export EGL_PLATFORM=wayland
exec /usr/bin/weston-simple-egl > /tmp/concurrent-egl.log 2>&1
" &
EGL_CLIENT_PID=$!

log "[*] Launching weston-simple-shm (CPU client)..."
chroot /data/alpine /bin/sh -c "
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export XDG_RUNTIME_DIR=/tmp/weston-xdg-runtime
export WAYLAND_DISPLAY=wayland-0
exec /usr/bin/weston-simple-shm > /tmp/concurrent-shm.log 2>&1
" &
SHM_CLIENT_PID=$!

log "[*] Both clients running! Sampling for 12 seconds..."
sleep 12

RSS_KB=$(grep "VmRSS" /proc/$WESTON_REAL_PID/status 2>/dev/null | awk '{print $2}' || echo "0")
FD_COUNT=$(ls /proc/$WESTON_REAL_PID/fd 2>/dev/null | wc -l || echo "0")

log "[*] Stopping clients..."
kill -INT $EGL_CLIENT_PID 2>/dev/null || true
kill -INT $SHM_CLIENT_PID 2>/dev/null || true
sleep 1

log "[*] Stopping Weston..."
kill -INT $WESTON_PID 2>/dev/null || true
wait $WESTON_PID 2>/dev/null || true

DECON_END=$(get_decon_irq)
MALI_END=$(get_mali_irq)
DECON_DELTA=$((DECON_END - DECON_START))
MALI_DELTA=$((MALI_END - MALI_START))
FINAL_FRAMES=$(grep -o "Hardware frame #[0-9]*" "$WESTON_LOG" 2>/dev/null | tail -n 1 | awk '{print $3}' | tr -d '#' || echo 0)

log "================================================================="
log "   Concurrent GPU EGL + CPU SHM Presentation Results             "
log "================================================================="
log "Composited Hardware Frames    : $FINAL_FRAMES"
log "Mali GPU Interrupts Delta     : +$MALI_DELTA"
log "DECON Panel VSYNC IRQ Delta   : +$DECON_DELTA"
log "Compositor Final RSS          : ${RSS_KB} KB"
log "Compositor Open FDs           : $FD_COUNT"
log "================================================================="
cat /data/alpine/tmp/concurrent-egl.log 2>/dev/null | head -n 10 | tee -a "$LOG" || true

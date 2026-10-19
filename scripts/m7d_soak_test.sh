#!/system/bin/sh
set -e

OUT_DIR="/data/local/tmp/m7d_soak"
mkdir -p "$OUT_DIR"
LOG="$OUT_DIR/soak.log"
WESTON_LOG="/data/alpine/tmp/weston-soak.log"
TELEMETRY="$OUT_DIR/telemetry.csv"
rm -f "$LOG" "$TELEMETRY" "$WESTON_LOG"

log() {
    echo "$@" | tee -a "$LOG"
}

get_decon_irq() {
    cat /proc/interrupts | grep -i "249:" | awk '{s=0; for(i=2;i<=NF-3;i++) s+=$i; print s}'
}

get_mali_irq() {
    cat /proc/interrupts | grep -i "11500000.mali" | awk '{for(i=2;i<=NF-3;i++) s+=$i} END {print s}'
}

# Preserve original screen timeout
ORIG_TIMEOUT=$(settings get system screen_off_timeout 2>/dev/null || echo 60000)

restore_services() {
    log "[*] Restoring Android power settings and display services..."
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

log "================================================================="
log "   Milestone 7D: Weston 14 Direct-DECON GL Renderer Soak Test    "
log "================================================================="
log "Target: Samsung Galaxy A30 (SM-A305F) / Exynos 7904 / Mali-G71 MP2"
log "Display: EA8076 Super AMOLED (1080x2340 @ 60 Hz)"
log "Target Duration: 600 seconds (10 minutes)"
log "Start Date: $(date)"

# Prevent Android display sleep during benchmark (FIND-050)
log "[*] Acquiring wake lock and disabling screen timeout..."
svc power stayon true
settings put system screen_off_timeout 2147483647
echo "weston_soak" > /sys/power/wake_lock 2>/dev/null || true

# Ensure alpine mounts
mkdir -p /data/alpine/proc /data/alpine/sys /data/alpine/dev /data/alpine/dev/pts /data/alpine/system /data/alpine/vendor /data/alpine/apex/com.android.runtime /data/alpine/apex/com.android.vndk.v30
mount | grep -q "/data/alpine/proc" || mount -t proc proc /data/alpine/proc
mount | grep -q "/data/alpine/sys" || mount -t sysfs sys /data/alpine/sys
mount | grep -q "/data/alpine/dev " || mount --bind /dev /data/alpine/dev
mount | grep -q "/data/alpine/dev/pts" || mount --bind /dev/pts /data/alpine/dev/pts
mount | grep -q "/data/alpine/system" || mount --bind /system /data/alpine/system
mount | grep -q "/data/alpine/vendor" || mount --bind /vendor /data/alpine/vendor
mount | grep -q "/data/alpine/apex/com.android.runtime" || mount --bind /apex/com.android.runtime /data/alpine/apex/com.android.runtime
mount | grep -q "/data/alpine/apex/com.android.vndk.v30" || mount --bind /apex/com.android.vndk.v30 /data/alpine/apex/com.android.vndk.v30

# Initial IRQ baseline
DECON_START=$(get_decon_irq)
MALI_START=$(get_mali_irq)
log "[*] Initial DECON VSYNC IRQ (249): $DECON_START"
log "[*] Initial Mali GPU IRQ (GIC 190/191): $MALI_START"

# Stop Android services
log "[*] Stopping Android SurfaceFlinger and HWC2..."
stop surfaceflinger
stop vendor.hwcomposer-2-1
sleep 1

# Setup runtime dir
mkdir -p /data/alpine/tmp/weston-xdg-runtime
chmod 0700 /data/alpine/tmp/weston-xdg-runtime
rm -f /data/alpine/tmp/weston-xdg-runtime/wayland-*

# Launch Weston GL renderer in background
log "[*] Launching Weston with GL renderer..."
chroot /data/alpine /bin/sh -c "
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export XDG_RUNTIME_DIR=/tmp/weston-xdg-runtime
export LD_LIBRARY_PATH=/usr/lib
exec /usr/bin/weston --backend=decon --renderer=gl --idle-time=0 --socket=wayland-0 --log=/tmp/weston-soak.log
" &
WESTON_WRAPPER_PID=$!

# Wait for wayland socket
for i in $(seq 1 50); do
    if [ -S /data/alpine/tmp/weston-xdg-runtime/wayland-0 ]; then
        break
    fi
    sleep 0.1
done

WESTON_PID=$(pidof weston || true)
log "[*] Detected Weston PID: $WESTON_PID"

# Launch client
log "[*] Launching animated client (weston-simple-shm)..."
chroot /data/alpine /bin/sh -c "
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export XDG_RUNTIME_DIR=/tmp/weston-xdg-runtime
export WAYLAND_DISPLAY=wayland-0
exec /usr/bin/weston-simple-shm
" &
CLIENT_WRAPPER_PID=$!

sleep 2
CLIENT_PID=$(pidof weston-simple-shm || true)
log "[*] Detected Client PID: $CLIENT_PID"

echo "timestamp,elapsed_sec,frames,fps,decon_irq_delta,mali_irq_delta,rss_kb,open_fds" > "$TELEMETRY"

T_START=$(date +%s)
LAST_FRAMES=0
LAST_T=$T_START

log ""
log "| Elapsed Time | Frame Count | Instantaneous FPS | GPU IRQ Delta | Panel VSYNC Delta | Compositor RSS | Open FDs |"
log "| :---: | :---: | :---: | :---: | :---: | :---: | :---: |"

# Run 10-minute sampling loop (10 intervals of 60 seconds)
for INTERVAL in $(seq 1 10); do
    sleep 60
    T_NOW=$(date +%s)
    ELAPSED=$((T_NOW - T_START))
    INTERVAL_SEC=$((T_NOW - LAST_T))
    if [ "$INTERVAL_SEC" -le 0 ]; then INTERVAL_SEC=1; fi

    # Extract latest frame count from log
    FRAMES=$(grep -o "Hardware frame #[0-9]*" "$WESTON_LOG" 2>/dev/null | tail -n 1 | awk '{print $3}' | tr -d '#' || echo 0)
    if [ -z "$FRAMES" ]; then FRAMES=0; fi

    FRAME_DELTA=$((FRAMES - LAST_FRAMES))
    FPS=$(awk -v fd="$FRAME_DELTA" -v is="$INTERVAL_SEC" 'BEGIN {printf "%.2f", fd/is}')

    DECON_CUR=$(get_decon_irq)
    MALI_CUR=$(get_mali_irq)
    DECON_DELTA=$((DECON_CUR - DECON_START))
    MALI_DELTA=$((MALI_CUR - MALI_START))

    RSS_KB=$(grep "VmRSS" /proc/$WESTON_PID/status 2>/dev/null | awk '{print $2}' || echo "0")
    FD_COUNT=$(ls /proc/$WESTON_PID/fd 2>/dev/null | wc -l || echo "0")

    log "| ${ELAPSED}s | $FRAMES | $FPS FPS | +$MALI_DELTA | +$DECON_DELTA | ${RSS_KB} KB | $FD_COUNT |"
    echo "$(date +%s),$ELAPSED,$FRAMES,$FPS,$DECON_DELTA,$MALI_DELTA,$RSS_KB,$FD_COUNT" >> "$TELEMETRY"

    LAST_FRAMES=$FRAMES
    LAST_T=$T_NOW
done

log ""
log "[*] 600s Soak test completed! Stopping client and compositor cleanly..."
if [ -n "$CLIENT_PID" ]; then kill -INT $CLIENT_PID 2>/dev/null || true; fi
sleep 1
if [ -n "$WESTON_PID" ]; then kill -INT $WESTON_PID 2>/dev/null || true; fi
wait $WESTON_WRAPPER_PID 2>/dev/null || true

FINAL_DECON=$(get_decon_irq)
FINAL_MALI=$(get_mali_irq)
TOTAL_DECON=$((FINAL_DECON - DECON_START))
TOTAL_MALI=$((FINAL_MALI - MALI_START))
FINAL_FRAMES=$(grep -o "Hardware frame #[0-9]*" "$WESTON_LOG" 2>/dev/null | tail -n 1 | awk '{print $3}' | tr -d '#' || echo 0)
AVG_FPS=$(awk -v tf="$FINAL_FRAMES" 'BEGIN {printf "%.2f", tf/600.0}')

log "================================================================="
log "   Milestone 7D Soak Test Final Telemetry Summary               "
log "================================================================="
log "Total Elapsed Time            : 600.0 seconds"
log "Total Hardware Frames Composed: $FINAL_FRAMES"
log "Average Presentation Cadence  : $AVG_FPS FPS"
log "Total DECON Panel VSYNC IRQs  : +$TOTAL_DECON"
log "Total Mali GPU Interrupts     : +$TOTAL_MALI"
log "End Date                      : $(date)"
log "================================================================="

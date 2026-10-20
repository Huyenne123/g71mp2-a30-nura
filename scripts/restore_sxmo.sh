#!/bin/sh
echo "[*] Resuming Xorg / Sxmo desktop..."
pkill -KILL -x weston-simple-egl 2>/dev/null || true
pkill -KILL -x weston-simple-touch 2>/dev/null || true
pkill -KILL -x weston-keyboard 2>/dev/null || true
pkill -KILL -x weston-desktop-shell 2>/dev/null || true
pkill -KILL -x weston 2>/dev/null || true

rm -f /usr/lib/libweston-14 /usr/lib/weston /usr/lib/libhybris /usr/share/weston 2>/dev/null || true

# Cycle fb0 blank to force DECON to re-enable Window 0
echo 1 > /sys/class/graphics/fb0/blank 2>/dev/null || true
sleep 0.1
echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null || true

# Resume Xorg, dwm, and sxmo
pkill -CONT -x Xorg 2>/dev/null || true
pkill -CONT -x dwm 2>/dev/null || true
pkill -CONT -f sxmo 2>/dev/null || true

sleep 0.5
pkill -KILL -x lisgd 2>/dev/null || true
DISPLAY=:0 XAUTHORITY=/home/user/.Xauthority doas -u user /usr/share/sxmo/default_hooks/sxmo_hook_lisgdstart.sh >/dev/null 2>&1 &
echo "[+] Sxmo resumed successfully with touch gestures restored."

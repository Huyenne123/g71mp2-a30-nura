#!/bin/sh
echo "[*] Resuming Xorg / Sxmo desktop..."
pkill -KILL -f weston-simple 2>/dev/null || true
pkill -KILL -f weston 2>/dev/null || true
rm -f /usr/lib/libweston-14 /usr/lib/weston /usr/lib/libhybris /usr/share/weston 2>/dev/null || true
pkill -CONT -f Xorg 2>/dev/null || true
pkill -CONT -f dwm 2>/dev/null || true
pkill -CONT -f sxmo 2>/dev/null || true
sleep 0.5
pkill -KILL -f lisgd 2>/dev/null || true
DISPLAY=:0 XAUTHORITY=/home/user/.Xauthority doas -u user /usr/share/sxmo/default_hooks/sxmo_hook_lisgdstart.sh >/dev/null 2>&1 &
echo "[+] Sxmo resumed successfully with touch gestures restored."

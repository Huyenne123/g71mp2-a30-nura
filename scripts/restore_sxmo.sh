#!/bin/sh
echo "[*] Restoring Sxmo desktop service (tinydm)..."
killall -9 weston-simple-egl weston-simple-touch weston-keyboard weston-desktop-shell weston 2>/dev/null || true
rm -f /usr/lib/libweston-14 /usr/lib/weston /usr/lib/libhybris /usr/share/weston 2>/dev/null || true
killall -9 Xorg dwm lisgd st 2>/dev/null || true
rm -f /tmp/.X*-lock /tmp/.X11-unix/X* 2>/dev/null || true

rc-service tinydm restart >/dev/null 2>&1 || true
echo "[+] Sxmo service restarted successfully."

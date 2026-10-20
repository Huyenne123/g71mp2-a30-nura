#!/bin/sh
echo "[*] Resuming Xorg / Sxmo desktop..."
pkill -KILL -f weston 2>/dev/null || true
rm -f /usr/lib/libweston-14 /usr/lib/weston /usr/lib/libhybris /usr/share/weston 2>/dev/null || true
pkill -CONT -f Xorg 2>/dev/null || true
pkill -CONT -f sxmo 2>/dev/null || true
sleep 1
if ! pgrep -f lisgd >/dev/null; then
    echo "[*] Restarting Sxmo gesture daemon (lisgd)..."
    doas -u user /usr/share/sxmo/default_hooks/sxmo_hook_lisgdstart.sh >/dev/null 2>&1 &
fi
echo "[+] Sxmo resumed successfully with touch gestures restored."

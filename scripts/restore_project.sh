#!/bin/sh
# Mali-G71 MP2 Exynos 7904 Environment Restore & Deployment Script
set -e
echo "[*] Restoring Mali-A30 hardware graphics environment..."
BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -d "$BASE_DIR/opt/weston-decon" ]; then
    echo "[+] Restoring /opt/weston-decon from backup bundle..."
    doas cp -a "$BASE_DIR/opt/weston-decon" /opt/
elif [ -d "/opt/weston-decon" ]; then
    echo "[+] Existing /opt/weston-decon detected."
else
    echo "[-] Warning: /opt/weston-decon not found. Please compile or unpack the weston-decon bundle first."
fi

if [ -d "$BASE_DIR/usr_local_bin" ]; then
    echo "[+] Restoring /usr/local/bin binaries from backup bundle..."
    doas cp -a "$BASE_DIR/usr_local_bin/"* /usr/local/bin/
elif [ -f "$BASE_DIR/start_sxmo_decon.sh" ]; then
    echo "[+] Deploying scripts from repository to /usr/local/bin..."
    doas cp -a "$BASE_DIR/start_sxmo_decon.sh" /usr/local/bin/start-sxmo-decon
    doas cp -a "$BASE_DIR/restore_sxmo.sh" /usr/local/bin/restore-sxmo
    [ -f "$BASE_DIR/../configs/sxmo/sxmo_dmenu.sh" ] && doas cp -a "$BASE_DIR/../configs/sxmo/sxmo_dmenu.sh" /usr/local/bin/
fi
doas chmod +x /usr/local/bin/* 2>/dev/null || true

echo "[+] Restoring Sxmo configuration..."
mkdir -p "$HOME/.config/sxmo/hooks"
if [ -d "$BASE_DIR/sxmo_config/sxmo" ]; then
    cp -a "$BASE_DIR/sxmo_config/sxmo/"* "$HOME/.config/sxmo/"
elif [ -d "$BASE_DIR/../configs/sxmo" ]; then
    cp -a "$BASE_DIR/../configs/sxmo/"* "$HOME/.config/sxmo/"
fi

echo "[+] Restoring Firefox touch & rendering preferences..."
for p in "$HOME/.mozilla/firefox/"*default*; do
    if [ -d "$p" ]; then
        if [ -f "$BASE_DIR/firefox_prefs/user.js" ]; then
            cp "$BASE_DIR/firefox_prefs/user.js" "$p/user.js"
        elif [ -f "$BASE_DIR/../configs/firefox/user.js" ]; then
            cp "$BASE_DIR/../configs/firefox/user.js" "$p/user.js"
        fi
    fi
done

echo "[+] Setting up hardware device node permissions..."
doas chmod 666 /dev/fb0 /dev/graphics/fb0 /dev/mali0 /dev/ion /dev/sw_sync /dev/input/event* 2>/dev/null || true
echo "[+] Done! You can now run: doas start-sxmo-decon"

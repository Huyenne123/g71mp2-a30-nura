#!/usr/bin/env bash
# ==============================================================================
# flash_adb_twrp.sh - Automated Deployment Script via ADB / TWRP Recovery
# Target: Samsung Galaxy A30 (SM-A305F)
# Project: postmarketOS Direct-DECON Hardware Graphics
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOT_IMG="${SCRIPT_DIR}/boot.img"
ROOTFS_TAR="${SCRIPT_DIR}/rootfs.tar.gz"

echo "================================================================="
echo "   postmarketOS Direct-DECON Deployment (ADB / TWRP Mode)        "
echo "================================================================="

if [ ! -f "${BOOT_IMG}" ]; then
    echo "[-] Error: ${BOOT_IMG} not found!"
    exit 1
fi

echo "[*] Checking ADB device connection..."
if ! adb get-state 1>/dev/null 2>&1; then
    echo "[-] No ADB device detected. Please connect phone and enable USB debugging or enter TWRP."
    exit 1
fi

DEVICE_MODEL=$(adb shell getprop ro.product.model 2>/dev/null | tr -d '\r' || echo "Unknown")
echo "[+] Connected device: ${DEVICE_MODEL}"

echo "[*] Verifying root privileges..."
IS_ROOT=$(adb shell "id -u" 2>/dev/null | tr -d '\r' || echo "unknown")
SU_CMD=""
if [ "$IS_ROOT" != "0" ]; then
    if adb shell "su -c id -u" 2>/dev/null | grep -q "0"; then
        SU_CMD="su -c"
    else
        echo "[-] Error: Root access is required via ADB or TWRP recovery."
        exit 1
    fi
fi

# Step 1: Backup current boot partition
BACKUP_PATH="/data/local/tmp/stock_boot_backup_$(date +%s).img"
echo "[*] Creating safety backup of current boot partition to ${BACKUP_PATH}..."
adb shell "${SU_CMD} dd if=/dev/block/by-name/boot of=${BACKUP_PATH} bs=1M"
echo "[+] Safety backup created successfully on device."

# Step 2: Push and Flash boot.img
echo "[*] Uploading boot.img to device..."
adb push "${BOOT_IMG}" /data/local/tmp/pmos_boot.img

echo "[*] Flashing boot.img to /dev/block/by-name/boot..."
adb shell "${SU_CMD} dd if=/data/local/tmp/pmos_boot.img of=/dev/block/by-name/boot bs=1M"
adb shell "${SU_CMD} sync"
adb shell "rm -f /data/local/tmp/pmos_boot.img"
echo "[+] boot.img successfully flashed!"

# Step 3: Check rootfs deployment
echo "[*] Checking rootfs installation at /data/alpine..."
ROOTFS_EXISTS=$(adb shell "${SU_CMD} test -d /data/alpine/bin && echo 'yes' || echo 'no'" 2>/dev/null | tr -d '\r')

if [ "$ROOTFS_EXISTS" = "yes" ]; then
    echo "[+] Verified: postmarketOS rootfs is already installed at /data/alpine."
elif [ -f "${ROOTFS_TAR}" ]; then
    echo "[*] Deploying rootfs.tar.gz to /data/alpine..."
    adb push "${ROOTFS_TAR}" /data/local/tmp/rootfs.tar.gz
    adb shell "${SU_CMD} mkdir -p /data/alpine && cd /data/alpine && tar -xzf /data/local/tmp/rootfs.tar.gz"
    adb shell "rm -f /data/local/tmp/rootfs.tar.gz"
    echo "[+] Rootfs deployed successfully!"
else
    echo "[!] Warning: /data/alpine does not exist and rootfs.tar.gz was not found in release directory."
fi

echo "================================================================="
echo "   Deployment Complete!                                          "
echo "================================================================="
echo "You can now reboot into postmarketOS by running:"
echo "    adb reboot"
echo ""
echo "Emergency rollback command (if needed):"
echo "    adb shell dd if=${BACKUP_PATH} of=/dev/block/by-name/boot"
echo "================================================================="

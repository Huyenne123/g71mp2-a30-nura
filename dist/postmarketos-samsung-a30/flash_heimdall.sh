#!/usr/bin/env bash
# ==============================================================================
# flash_heimdall.sh - Automated Flashing via Heimdall (Samsung Download Mode)
# Target: Samsung Galaxy A30 (SM-A305F)
# Project: postmarketOS Direct-DECON Hardware Graphics
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOT_IMG="${SCRIPT_DIR}/boot.img"

echo "================================================================="
echo "   postmarketOS Direct-DECON Flashing (Heimdall Download Mode)   "
echo "================================================================="

if ! command -v heimdall &> /dev/null; then
    echo "[-] Error: heimdall is not installed on this host."
    echo "    Install with: sudo apt install heimdall-flash"
    exit 1
fi

if [ ! -f "${BOOT_IMG}" ]; then
    echo "[-] Error: ${BOOT_IMG} not found!"
    exit 1
fi

echo "[*] Detecting Samsung Download Mode device..."
echo "[!] Make sure phone is in Download Mode (Power Off -> Hold Vol-Up + Vol-Down -> Connect USB)"
heimdall detect

echo "[*] Flashing postmarketOS boot.img to BOOT partition..."
heimdall flash --BOOT "${BOOT_IMG}" --no-reboot

echo "================================================================="
echo "   Flashing Succeeded!                                           "
echo "================================================================="
echo "Unplug USB and hold Power + Volume Down to reboot."

#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build"
ZIP_DIR="${BUILD_DIR}/twrp_zip"
DIST_DIR="${ROOT_DIR}/dist"
OUTPUT_ZIP="${DIST_DIR}/pmos-samsung-a30.zip"

BOOT_IMG="${DIST_DIR}/postmarketos-samsung-a30/boot.img"
ROOTFS_TAR="${DIST_DIR}/postmarketos-samsung-a30/rootfs.tar.gz"

echo "================================================================="
echo "   Building TWRP Flashable ZIP for Samsung Galaxy A30            "
echo "================================================================="

if [ ! -f "${BOOT_IMG}" ]; then
    echo "[-] Error: ${BOOT_IMG} does not exist! Run scripts/build_direct_boot.sh first."
    exit 1
fi

if [ ! -f "${ROOTFS_TAR}" ]; then
    echo "[-] Error: ${ROOTFS_TAR} does not exist!"
    exit 1
fi

rm -rf "${ZIP_DIR}"
mkdir -p "${ZIP_DIR}/META-INF/com/google/android"

echo "[*] Creating update-binary for TWRP..."
cat << 'EOF' > "${ZIP_DIR}/META-INF/com/google/android/update-binary"
#!/sbin/sh
# ==============================================================================
# TWRP Recovery Installer for Samsung Galaxy A30 (SM-A305F / samsung-a30)
# postmarketOS Direct-DECON Hardware-Accelerated Edition
# ==============================================================================

OUTFD=$2
ZIP=$3

ui_print() {
    echo "ui_print $1" > /proc/self/fd/$OUTFD
    echo "ui_print" > /proc/self/fd/$OUTFD
}

set_progress() {
    echo "set_progress $1" > /proc/self/fd/$OUTFD
}

ui_print " "
ui_print "========================================="
ui_print "       postmarketOS (Direct-DECON)       "
ui_print "      Samsung Galaxy A30 (SM-A305F)      "
ui_print "========================================="
ui_print " "

set_progress 0.1

# 1. Flash kernel & direct-boot image to boot partition
ui_print "[*] Extracting and flashing boot.img to /dev/block/by-name/boot..."
unzip -p "$ZIP" boot.img > /tmp/pmos_boot.img

if [ ! -s /tmp/pmos_boot.img ]; then
    ui_print "[-] ERROR: Failed to extract boot.img from zip!"
    exit 1
fi

BOOT_PART=""
if [ -b /dev/block/by-name/boot ]; then
    BOOT_PART="/dev/block/by-name/boot"
elif [ -b /dev/block/mmcblk0p15 ]; then
    BOOT_PART="/dev/block/mmcblk0p15"
fi

if [ -z "$BOOT_PART" ]; then
    ui_print "[-] ERROR: Could not find boot partition block device!"
    exit 1
fi

dd if=/tmp/pmos_boot.img of="$BOOT_PART" bs=1M
sync
rm -f /tmp/pmos_boot.img
ui_print "[+] boot.img successfully flashed!"

set_progress 0.3

# 2. Locate, format and mount target rootfs partition (userdata / mmcblk0p32)
ui_print "[*] Locating target installation partition (userdata)..."
DATA_PART=""
if [ -b /dev/block/by-name/userdata ]; then
    DATA_PART="/dev/block/by-name/userdata"
elif [ -b /dev/block/mmcblk0p32 ]; then
    DATA_PART="/dev/block/mmcblk0p32"
fi

if [ -z "$DATA_PART" ]; then
    ui_print "[-] ERROR: Could not find userdata partition!"
    exit 1
fi

ui_print "[*] Target partition: $DATA_PART"
ui_print "[*] Formatting userdata as ext4 (label: pmOS_root)..."
umount /pmos_target 2>/dev/null || true
umount "$DATA_PART" 2>/dev/null || true

# Format userdata cleanly with pmOS_root label
if ! mke2fs -F -t ext4 -L "pmOS_root" -O "^metadata_csum,^huge_file" "$DATA_PART" 2>/dev/null; then
    if ! mkfs.ext4 -F -L "pmOS_root" -O "^metadata_csum,^huge_file" "$DATA_PART" 2>/dev/null; then
        mkfs.ext4 -F -L "pmOS_root" "$DATA_PART"
    fi
fi

mkdir -p /pmos_target
if ! mount -t ext4 -o rw,noatime "$DATA_PART" /pmos_target; then
    ui_print "[-] ERROR: Failed to mount formatted userdata partition!"
    exit 1
fi

set_progress 0.4

# 3. Deploy rootfs into /pmos_target
ui_print "[*] Extracting postmarketOS Direct-DECON rootfs into userdata..."
ui_print "    (This takes ~1-2 minutes, please wait...)"

unzip -p "$ZIP" rootfs.tar.gz | tar -xz -C /pmos_target || {
    ui_print "[-] ERROR: Failed to extract rootfs.tar.gz!"
    umount /pmos_target 2>/dev/null || true
    exit 1
}

set_progress 0.8

# 4. Create compatibility layout and virtual filesystem mountpoints
ui_print "[*] Setting up system directories and compatibility links..."
mkdir -p /pmos_target/boot \
         /pmos_target/proc \
         /pmos_target/sys \
         /pmos_target/dev \
         /pmos_target/tmp \
         /pmos_target/run \
         /pmos_target/sysroot \
         /pmos_target/android/vendor \
         /pmos_target/android/system \
         /pmos_target/android/apex

# Save boot image copy in /boot/boot.img
unzip -p "$ZIP" boot.img > /pmos_target/boot/boot.img 2>/dev/null || true

# Symlink /data/alpine -> . for dual-path bootloader compatibility
ln -sf . /pmos_target/alpine 2>/dev/null || true

# 5. Flush and clean up
set_progress 0.95
ui_print "[*] Syncing filesystem buffers..."
sync
umount /pmos_target

# Workaround to disable 'No OS installed' prompt in TWRP
(
    sleep 4
    echo "set tw_backup_system_size 999" > /sbin/orsin 2>/dev/null || true
    sleep 2
    echo "set tw_app_prompt 0" > /sbin/orsin 2>/dev/null || true
) &

set_progress 1.0
ui_print " "
ui_print "========================================="
ui_print "   postmarketOS Installation Complete!   "
ui_print "========================================="
ui_print "Reboot to System to start postmarketOS."
ui_print " "
exit 0
EOF

chmod 755 "${ZIP_DIR}/META-INF/com/google/android/update-binary"

echo "# Dummy updater-script" > "${ZIP_DIR}/META-INF/com/google/android/updater-script"

echo "[*] Creating postmarketOS recovery installer metadata (install_options)..."
cat << 'EOF' > "${ZIP_DIR}/install_options"
DEVICE='samsung-a30'
FLASH_KERNEL='true'
FLAVOR=''
ISOREC='false'
KERNEL_PARTLABEL='boot'
INITFS_PARTLABEL='boot'
SYSTEM_PARTLABEL='system'
INSTALL_PARTITION='data'
CIPHER='aes-xts-plain64'
FDE='false'
EOF

echo "[*] Linking boot.img and rootfs.tar.gz..."
ln -s "${BOOT_IMG}" "${ZIP_DIR}/boot.img"
ln -s "${ROOTFS_TAR}" "${ZIP_DIR}/rootfs.tar.gz"

echo "[*] Creating flashable ZIP package: ${OUTPUT_ZIP}..."
rm -f "${OUTPUT_ZIP}"
(cd "${ZIP_DIR}" && zip -0 -r "${OUTPUT_ZIP}" META-INF/ install_options boot.img rootfs.tar.gz)

SHA256=$(sha256sum "${OUTPUT_ZIP}" | awk '{print $1}')
SIZE=$(stat -c%s "${OUTPUT_ZIP}")

echo "================================================================="
echo "   TWRP Flashable ZIP Ready!                                     "
echo "================================================================="
echo "Output File : ${OUTPUT_ZIP}"
echo "Size        : ${SIZE} bytes ($(du -h "${OUTPUT_ZIP}" | awk '{print $1}'))"
echo "SHA-256     : ${SHA256}"
echo "================================================================="

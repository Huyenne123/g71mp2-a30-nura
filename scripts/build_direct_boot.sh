#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build"
INITFS_DIR="${BUILD_DIR}/initfs"
TOOLS_DIR="${ROOT_DIR}/tools"
KERNEL_BIN="${ROOT_DIR}/artifacts/extracted/stock_boot_unpacked/kernel"
OUTPUT_BOOT="${BUILD_DIR}/boot-direct.img"

echo "[*] Initializing Standalone Direct Boot Image Generator..."
mkdir -p "${BUILD_DIR}"
rm -rf "${INITFS_DIR}"
mkdir -p "${INITFS_DIR}"/{bin,dev,etc,mnt/userdata,proc,sys,sysroot}

echo "[*] Populating static busybox binary..."
cp "${TOOLS_DIR}/busybox-arm64-static" "${INITFS_DIR}/bin/busybox"
chmod 755 "${INITFS_DIR}/bin/busybox"
ln -sf busybox "${INITFS_DIR}/bin/sh"

echo "[*] Writing standalone initramfs init script..."
cat << 'EOF' > "${INITFS_DIR}/init"
#!/bin/busybox sh
# ==============================================================================
# Standalone Direct Boot Initramfs for Samsung Galaxy A30 (SM-A305F)
# Project: Mali-G71 Open Graphics / postmarketOS Direct-DECON
# ==============================================================================

# Install busybox applets
/bin/busybox --install -s /bin

# Mount early kernel filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# Setup kmsg logging and console
exec 0</dev/console
exec 1>/dev/kmsg
exec 2>/dev/kmsg

echo "================================================================="
echo "   postmarketOS / Direct-DECON Standalone Direct Boot Init       "
echo "   Hardware: Samsung Galaxy A30 (Exynos 7904 / Mali-G71 MP2)     "
echo "================================================================="

# Wait for eMMC block devices to enumerate
echo "[*] Waiting for eMMC storage to enumerate..."
for i in $(seq 1 30); do
    if [ -b /dev/block/by-name/userdata ] || [ -b /dev/block/mmcblk0p32 ]; then
        break
    fi
    sleep 0.1
done

# Resolve userdata partition
USERDATA_DEV=""
if [ -b /dev/block/by-name/userdata ]; then
    USERDATA_DEV="/dev/block/by-name/userdata"
elif [ -b /dev/block/mmcblk0p32 ]; then
    USERDATA_DEV="/dev/block/mmcblk0p32"
fi

# Fallback: check for external SD card (mmcblk1p2 or mmcblk1p1)
SD_DEV=""
if [ -b /dev/mmcblk1p2 ]; then
    SD_DEV="/dev/mmcblk1p2"
elif [ -b /dev/mmcblk1p1 ]; then
    SD_DEV="/dev/mmcblk1p1"
fi

mkdir -p /mnt/userdata /sysroot

ROOT_MOUNTED=0

# Priority 1: Boot from dedicated rootfs on external microSD if present
if [ -n "$SD_DEV" ]; then
    echo "[*] Probing external microSD rootfs on $SD_DEV..."
    if mount -t ext4 -o ro "$SD_DEV" /sysroot 2>/dev/null; then
        echo "[+] Successfully mounted rootfs from external microSD ($SD_DEV)."
        ROOT_MOUNTED=1
    fi
fi

# Priority 2: Boot from /data/alpine on internal eMMC userdata
if [ $ROOT_MOUNTED -eq 0 ] && [ -n "$USERDATA_DEV" ]; then
    echo "[*] Mounting eMMC userdata partition ($USERDATA_DEV)..."
    if mount -t ext4 -o noatime "$USERDATA_DEV" /mnt/userdata; then
        if [ -d /mnt/userdata/alpine/bin ]; then
            echo "[+] Found Alpine Linux rootfs at /data/alpine."
            mount --bind /mnt/userdata/alpine /sysroot
            ROOT_MOUNTED=1
        elif [ -f /mnt/userdata/rootfs.img ]; then
            echo "[+] Found loopback rootfs image at /data/rootfs.img."
            mount -t ext4 -o loop,noatime /mnt/userdata/rootfs.img /sysroot
            ROOT_MOUNTED=1
        fi
    fi
fi

if [ $ROOT_MOUNTED -eq 0 ]; then
    echo "[-] FATAL: Could not locate a valid Linux rootfs!"
    echo "[-] Entering emergency rescue shell. Kernel output on /dev/kmsg."
    exec /bin/busybox sh
fi

# Mount Android vendor and system partitions for Libhybris compatibility
echo "[*] Mounting Android vendor and system partitions for Libhybris..."
mkdir -p /sysroot/android/vendor /sysroot/android/system /sysroot/android/apex

VENDOR_DEV=""
if [ -b /dev/block/by-name/vendor ]; then
    VENDOR_DEV="/dev/block/by-name/vendor"
elif [ -b /dev/block/mmcblk0p26 ]; then
    VENDOR_DEV="/dev/block/mmcblk0p26"
fi

SYSTEM_DEV=""
if [ -b /dev/block/by-name/system ]; then
    SYSTEM_DEV="/dev/block/by-name/system"
elif [ -b /dev/block/mmcblk0p25 ]; then
    SYSTEM_DEV="/dev/block/mmcblk0p25"
fi

if [ -n "$VENDOR_DEV" ]; then
    mount -t ext4 -o ro "$VENDOR_DEV" /sysroot/android/vendor 2>/dev/null || true
    echo "[+] Mounted /android/vendor"
fi

if [ -n "$SYSTEM_DEV" ]; then
    mount -t ext4 -o ro "$SYSTEM_DEV" /sysroot/android/system 2>/dev/null || true
    echo "[+] Mounted /android/system"
fi

# Ensure compatibility symlinks
[ -e /sysroot/vendor ] || ln -s /android/vendor /sysroot/vendor 2>/dev/null || true
[ -e /sysroot/system ] || ln -s /android/system /sysroot/system 2>/dev/null || true
[ -e /sysroot/apex ] || ln -s /android/apex /sysroot/apex 2>/dev/null || true

# Move dev, proc, sys into sysroot before switchroot
mount --move /sys /sysroot/sys
mount --move /proc /sysroot/proc
mount --move /dev /sysroot/dev

echo "[*] Switching root to postmarketOS / Alpine userspace..."
exec switch_root /sysroot /sbin/init
EOF

chmod 755 "${INITFS_DIR}/init"

echo "[*] Packaging initramfs CPIO archive..."
RAMDISK_CPIO="${BUILD_DIR}/initramfs-directboot.cpio.gz"
(cd "${INITFS_DIR}" && find . | cpio -o -H newc | gzip -9 > "${RAMDISK_CPIO}")

echo "[+] Ramdisk archive built: ${RAMDISK_CPIO} ($(du -h "${RAMDISK_CPIO}" | awk '{print $1}'))"

if [ ! -f "${KERNEL_BIN}" ]; then
    echo "[-] Error: Kernel binary not found at ${KERNEL_BIN}"
    exit 1
fi

echo "[*] Generating Android boot image via mkbootimg.py..."
python3 "${TOOLS_DIR}/mkbootimg.py" \
    --kernel "${KERNEL_BIN}" \
    --ramdisk "${RAMDISK_CPIO}" \
    --base "0x10000000" \
    --kernel_offset "0x00008000" \
    --ramdisk_offset "0x01000000" \
    --tags_offset "0x00000100" \
    --pagesize 2048 \
    --header_version 0 \
    --cmdline "buildvariant=userdebug console=null androidboot.hardware=universal7904 loop.max_part=7" \
    -o "${OUTPUT_BOOT}"

echo "[+] Verifying generated boot image with unpackbootimg.py..."
VERIFY_DIR="${BUILD_DIR}/verify_boot"
rm -rf "${VERIFY_DIR}"
python3 "${TOOLS_DIR}/unpackbootimg.py" -i "${OUTPUT_BOOT}" -o "${VERIFY_DIR}"

SHA256=$(sha256sum "${OUTPUT_BOOT}" | awk '{print $1}')
echo "================================================================="
echo "   Standalone Direct Boot Image Ready                            "
echo "================================================================="
echo "Output Path : ${OUTPUT_BOOT}"
echo "Size        : $(stat -c%s "${OUTPUT_BOOT}") bytes ($(du -h "${OUTPUT_BOOT}" | awk '{print $1}'))"
echo "SHA-256     : ${SHA256}"
echo "================================================================="

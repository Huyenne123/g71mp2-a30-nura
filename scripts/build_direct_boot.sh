#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build"
INITFS_DIR="${BUILD_DIR}/initfs"
TOOLS_DIR="${ROOT_DIR}/tools"
KERNEL_BIN="${ROOT_DIR}/artifacts/extracted/stock_boot_unpacked/kernel-patched.bin"
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

# Setup kmsg logging and stdin safety (console=null protection)
exec 0</dev/zero
exec 1>/dev/kmsg
exec 2>/dev/kmsg

echo "================================================================="
echo "   postmarketOS / Direct-DECON Standalone Direct Boot Init       "
echo "   Hardware: Samsung Galaxy A30 (Exynos 7904 / Mali-G71 MP2)     "
echo "================================================================="

# Wait up to 15 seconds for eMMC block devices to enumerate
echo "[*] Waiting for storage devices to enumerate..."
for i in $(seq 1 150); do
    if [ -b /dev/mmcblk0p32 ] || [ -b /dev/block/mmcblk0p32 ] || [ -b /dev/block/by-name/userdata ]; then
        echo "[+] Storage device enumerated after $((i * 100)) ms."
        break
    fi
    sleep 0.1
done

# Ensure loop control and loop block devices exist
[ -e /dev/loop-control ] || mknod /dev/loop-control c 10 237 2>/dev/null || true
for i in 0 1 2 3 4 5 6 7; do
    [ -e /dev/loop$i ] || mknod /dev/loop$i b 7 $i 2>/dev/null || true
done
mkdir -p /dev/block

# Locate userdata partition
USERDATA_DEV=""
if [ -b /dev/mmcblk0p32 ]; then
    USERDATA_DEV="/dev/mmcblk0p32"
elif [ -b /dev/block/mmcblk0p32 ]; then
    USERDATA_DEV="/dev/block/mmcblk0p32"
elif [ -b /dev/block/by-name/userdata ]; then
    USERDATA_DEV="/dev/block/by-name/userdata"
fi

# Attach loop device with partition scan (-P) on userdata if present
if [ -n "$USERDATA_DEV" ]; then
    echo "[*] Checking userdata partition ($USERDATA_DEV) for subpartitions..."
    losetup -P /dev/loop0 "$USERDATA_DEV" 2>&1 || true
    sleep 0.5
fi

mkdir -p /mnt/userdata /mnt/boot /sysroot

ROOT_MOUNTED=0

# Priority 1: Label pmOS_root (standard postmarketOS)
echo "[*] Scanning for partition with label pmOS_root..."
PMOS_ROOT_DEV=$(findfs LABEL=pmOS_root 2>/dev/null || true)
if [ -z "$PMOS_ROOT_DEV" ]; then
    for candidate in /dev/loop0p2 /dev/block/loop0p2 /dev/loop1p2 /dev/block/loop1p2; do
        if [ -b "$candidate" ]; then
            PMOS_ROOT_DEV="$candidate"
            break
        fi
    done
fi

if [ -n "$PMOS_ROOT_DEV" ] && [ -b "$PMOS_ROOT_DEV" ]; then
    echo "[*] Found pmOS_root partition at $PMOS_ROOT_DEV, mounting..."
    if mount -t ext4 -o noatime "$PMOS_ROOT_DEV" /sysroot 2>/dev/null; then
        echo "[+] Successfully mounted pmOS_root partition."
        ROOT_MOUNTED=1
    fi
fi

# Priority 2: Direct userdata partition
if [ $ROOT_MOUNTED -eq 0 ] && [ -n "$USERDATA_DEV" ]; then
    echo "[*] Checking userdata partition directly ($USERDATA_DEV)..."
    if mount -t ext4 -o noatime "$USERDATA_DEV" /mnt/userdata 2>/dev/null; then
        if [ -x /mnt/userdata/sbin/init ] || [ -x /mnt/userdata/bin/sh ]; then
            echo "[+] Found rootfs directly on userdata partition."
            mount --bind /mnt/userdata /sysroot
            ROOT_MOUNTED=1
        elif [ -d /mnt/userdata/alpine/bin ]; then
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

# Priority 3: External MicroSD
if [ $ROOT_MOUNTED -eq 0 ]; then
    for SD_DEV in /dev/mmcblk1p2 /dev/mmcblk1p1; do
        if [ -b "$SD_DEV" ]; then
            echo "[*] Probing external microSD on $SD_DEV..."
            if mount -t ext4 -o ro "$SD_DEV" /sysroot 2>/dev/null; then
                if [ -x /sysroot/sbin/init ] || [ -x /sysroot/bin/sh ]; then
                    echo "[+] Successfully mounted rootfs from external microSD ($SD_DEV)."
                    ROOT_MOUNTED=1
                    break
                else
                    umount /sysroot 2>/dev/null || true
                fi
            fi
        fi
    done
fi

if [ $ROOT_MOUNTED -eq 0 ]; then
    echo "[-] FATAL: Could not locate a valid Linux rootfs!"
    echo "[-] Rebooting into TWRP recovery in 5 seconds..."
    sleep 5
    reboot recovery || reboot -f || true
fi

# Check for pmOS_boot partition
PMOS_BOOT_DEV=$(findfs LABEL=pmOS_boot 2>/dev/null || true)
if [ -z "$PMOS_BOOT_DEV" ]; then
    for candidate in /dev/loop0p1 /dev/block/loop0p1 /dev/loop1p1 /dev/block/loop1p1; do
        if [ -b "$candidate" ]; then
            PMOS_BOOT_DEV="$candidate"
            break
        fi
    done
fi
if [ -n "$PMOS_BOOT_DEV" ] && [ -b "$PMOS_BOOT_DEV" ]; then
    mkdir -p /sysroot/boot
    mount -t ext2 -o ro "$PMOS_BOOT_DEV" /sysroot/boot 2>/dev/null || \
    mount -t ext4 -o ro "$PMOS_BOOT_DEV" /sysroot/boot 2>/dev/null || true
    echo "[+] Mounted pmOS_boot ($PMOS_BOOT_DEV) on /boot"
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

#!/bin/sh
# initfs-hook.sh - postmarketOS early boot hook for Samsung Galaxy A30 (SM-A305F)

mount_android_partitions() {
	echo "[*] postmarketOS initfs-hook: Mounting Android partitions for libhybris..."

	# Ensure target mountpoints exist
	mkdir -p /sysroot/android/vendor
	mkdir -p /sysroot/android/system
	mkdir -p /sysroot/android/apex

	# Mount vendor partition (contains libGLES_mali.so and gralloc.exynos7904.so)
	if [ -e /dev/block/by-name/vendor ]; then
		mount -o ro /dev/block/by-name/vendor /sysroot/android/vendor
		echo "[+] Mounted /android/vendor"
	elif [ -e /dev/block/mmcblk0p26 ]; then
		mount -o ro /dev/block/mmcblk0p26 /sysroot/android/vendor
		echo "[+] Mounted /android/vendor (by node)"
	fi

	# Mount system partition (contains Android runtime libraries and ld-android.so)
	if [ -e /dev/block/by-name/system ]; then
		mount -o ro /dev/block/by-name/system /sysroot/android/system
		echo "[+] Mounted /android/system"
	elif [ -e /dev/block/mmcblk0p25 ]; then
		mount -o ro /dev/block/mmcblk0p25 /sysroot/android/system
		echo "[+] Mounted /android/system (by node)"
	fi

	# Create compatibility symlinks for libhybris linker (/system, /vendor)
	[ -e /sysroot/vendor ] || ln -s /android/vendor /sysroot/vendor
	[ -e /sysroot/system ] || ln -s /android/system /sysroot/system
	[ -e /sysroot/apex ] || ln -s /android/apex /sysroot/apex

	# Ensure mandatory Exynos character device nodes exist with proper permissions
	[ -e /dev/graphics/fb0 ] || (mkdir -p /dev/graphics && mknod -m 666 /dev/graphics/fb0 c 29 0)
	[ -e /dev/ion ] || mknod -m 666 /dev/ion c 10 61
	[ -e /dev/mali0 ] || mknod -m 666 /dev/mali0 c 242 0
	[ -e /dev/sync ] || mknod -m 666 /dev/sync c 10 60
}

mount_android_partitions

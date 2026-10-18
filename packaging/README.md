# postmarketOS / Alpine Packaging: Weston Direct-DECON Backend

This directory contains the packaging definitions and build instructions for `weston-backend-decon`, providing native display presentation on the Samsung Galaxy A30 (SM-A305F) running postmarketOS.

---

## 1. Overview

The `weston-backend-decon` package builds the `decon-backend.so` shared module for Weston 14 (`libweston-14`). It interfaces Weston's compositor core directly with the Samsung Exynos 7904 DECON display controller via:
- ION DMA-BUF allocation (`/dev/ion`, Exynos contig/system heaps).
- DECON Window 3 presentation (`/dev/graphics/fb0`, `S3CFB_WIN_CONFIG_IOCTL` `0x441046d1`).
- Hardware retire fence synchronization via Wayland's main event loop (`wl_event_loop_add_fd`).
- Pixman CPU rasterization or EGLImage GL compositing.

---

## 2. Dependencies

### Runtime Dependencies:
- `weston >= 14.0.0`
- `libweston-14`
- `pixman`
- `wayland`
- Downstream Samsung Exynos 4.4 kernel with `/dev/graphics/fb0` and `/dev/ion`.

### Build Dependencies:
- `meson >= 0.60.0`
- `ninja`
- `gcc` or `clang`
- `musl-dev`
- `weston-dev >= 14.0.0`
- `pixman-dev`
- `wayland-dev`
- `linux-headers`
- `libdrm-dev`

---

## 3. Building with `pmbootstrap` (postmarketOS)

To build this package within a postmarketOS chroot or aports tree:

```bash
# 1. Place APKBUILD into your local aports overlay
mkdir -p ~/aports/device/testing/weston-backend-decon
cp packaging/APKBUILD.weston-backend-decon ~/aports/device/testing/weston-backend-decon/APKBUILD

# 2. Generate package checksums
pmbootstrap checksum weston-backend-decon

# 3. Build package for aarch64
pmbootstrap build --arch=aarch64 weston-backend-decon

# 4. Install into target chroot or device
pmbootstrap install
```

---

## 4. Cross-Compilation via Alpine Sysroot

If cross-compiling outside of `pmbootstrap` using an Alpine aarch64 sysroot:

```bash
# 1. Prepare cross-compilation meson cross file (aarch64-linux-musl.txt):
cat << 'EOF' > aarch64-cross.txt
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkgconfig = 'pkg-config'

[properties]
sys_root = '/path/to/alpine-aarch64-sysroot'
pkg_config_libdir = '/path/to/alpine-aarch64-sysroot/usr/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a73'
endian = 'little'
EOF

# 2. Configure and build
meson setup --cross-file aarch64-cross.txt build/
ninja -C build/
```

---

## 5. Launching Weston with DECON Backend

In postmarketOS, Weston can load the backend via configuration or CLI:

```bash
# CLI launch:
weston --backend=decon-backend.so

# Or in /etc/xdg/weston/weston.ini:
# [core]
# backend=decon-backend.so
```

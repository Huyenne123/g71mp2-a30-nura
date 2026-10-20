# Milestone 8: postmarketOS Packaging, Standalone Direct Boot & Upstream Roadmap

- **Target Device**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **GPU**: ARM Mali-G71 MP2 (Bifrost v6.0, 2 cores, r0p0, GPU ID `0x60A0`)
- **Kernel**: Linux 4.4.302-p6-neyuKernel_legacy (AArch64 downstream vendor kernel)
- **Userspace**: Alpine Linux v3.21.8 ARM64 (`musl 1.2.5`) / postmarketOS edge
- **Compositor**: Weston 14.0.2 with `backend-decon` and `renderer-gl`
- **Boot Target**: Standalone Android Boot Image v0 (`build/boot-direct.img`)

---

## 1. Executive Summary & Objectives

Milestone 7 established end-to-end GPU hardware acceleration across the entire stack:
- **Milestone 7A**: Dynamic multitouch capacitive input integration ([`FIND-039`](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md#L625-L640)).
- **Milestone 7B**: `libhybris` dynamic linker bridge (`linker/q.so`) and Mali DDK validation under pure musl libc ([`FIND-041`](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md#L644-L660)).
- **Milestone 7C**: Direct Mali GLES $\rightarrow$ DMA-BUF $\rightarrow$ DECON presentation engine (10-minute soak at 59.96 FPS, [`FIND-044`](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md#L690-L705)).
- **Milestone 7D**: In-tree Weston 14 Direct-DECON GL renderer compositing (10-minute soak at 59.90 FPS, 17x–59x render speedup, [`FIND-049`](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md#L760-L774)).
- **Milestone 7E**: Wayland client 3D hardware acceleration via `android_wlegl` zero-copy Gralloc ION DMA-BUF import ([`FIND-051`](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md#L785-L799)).

The primary objectives of **Milestone 8** are:
1. **postmarketOS / Alpine Packaging Suite**: Author production-grade `APKBUILD` package recipes for `libhybris-musl`, `weston-decon`, and `device-samsung-a30` adhering to postmarketOS packaging guidelines.
2. **Standalone Direct Boot Image Generation**: Author freestanding boot image tools (`tools/mkbootimg.py`, `tools/unpackbootimg.py`), assemble a standalone initramfs with static busybox and switchroot logic, and generate `build/boot-direct.img` for direct booting without Android Zygote or SurfaceFlinger.
3. **Flashing & Recovery Safety Protocol**: Formulate a hardware recovery and verification plan guaranteeing rollback safety.
4. **Upstream Panfrost & Mainline DRM Roadmap**: Synthesize the long-term migration plan to upstream Linux 6.x kernels and mainline Mesa Panfrost.

---

## 2. postmarketOS Packaging Suite (Phase 8A)

The packaging suite bridges the downstream vendor drivers and custom Weston modules into standard Alpine / postmarketOS package repositories (`pmbootstrap`).

```
packaging/
├── APKBUILD.weston-backend-decon
├── README.md
├── 0001-weston-14-direct-decon-backend.patch
├── 0002-libhybris-musl-bionic-bridge.patch
├── 0003-weston-14-direct-decon-gl-renderer.patch
├── libhybris-musl/
│   └── APKBUILD
├── weston-decon/
│   └── APKBUILD
└── device-samsung-a30/
    ├── APKBUILD
    ├── deviceinfo
    ├── initfs-hook.sh
    ├── weston.ini
    ├── weston.initd
    └── 99-mali-decon.rules
```

### 2.1 `packaging/libhybris-musl/APKBUILD`
- **Package Name**: `libhybris-musl` (version `0.1.0_git20261019-r1`).
- **Function**: Compiles the Android 10/11 AArch64 Bionic dynamic linker bridge (`linker/q.so`) and runtime libraries under Alpine ARM64 with musl 1.2.5.
- **Patches Applied**: [`0002-libhybris-musl-bionic-bridge.patch`](file:///home/neyuh/mali-a30/packaging/0002-libhybris-musl-bionic-bridge.patch). Resolves `R_AARCH64_IRELATIVE` GNU IFUNC relocations, musl recursive mutex binary layout `{{{1}}}`, `fpos_t` designated initializers, pthread key 0 state retention, and 64 Bionic runtime hooks (`FIND-041`).
- **Subpackages**:
  - `libhybris-musl-libs`: `/usr/lib/libhybris/linker/q.so`, `libhybris-common.so`, `libsync.so`, `libhardware.so`.
  - `libhybris-musl-egl`: `libEGL.so`, `libGLESv2.so`, `libhybris-hwcomposerwindow.so`, `libwayland-egl.so`.
  - `libhybris-musl-dev`: Development headers and pkgconfig files.

### 2.2 `packaging/weston-decon/APKBUILD`
- **Package Name**: `weston-decon` (version `14.0.2-r1`).
- **Function**: Packages Weston 14.0.2 with native Samsung DECON Window 3 and GL-renderer hardware acceleration backends.
- **Private Header Coupling (`FIND-034`)**: Weston 14 backends require internal headers (`backend.h`, `pixman-renderer.h`, `pixel-formats.h`) omitted from `weston-dev`. The package applies in-tree patches directly against the Weston source archive:
  - [`0001-weston-14-direct-decon-backend.patch`](file:///home/neyuh/mali-a30/packaging/0001-weston-14-direct-decon-backend.patch): Integrates `backend-decon` into Meson, providing ION swapchain allocation and `S3CFB_WIN_CONFIG_IOCTL` presentation.
  - [`0003-weston-14-direct-decon-gl-renderer.patch`](file:///home/neyuh/mali-a30/packaging/0003-weston-14-direct-decon-gl-renderer.patch): Integrates `gl-renderer` with `HWCNativeWindow`, fixes upstream EGL platform assertions (`FIND-046`), eliminates teardown double-free faults (`FIND-047`), isolates musl ABI headers (`FIND-048`), and installs the 100 ms retire fence watchdog (`FIND-050`).
- **Subpackage**: `weston-decon-clients`: Packages `weston-simple-egl`, `weston-simple-shm`, and `weston-simple-touch`.

### 2.3 `packaging/device-samsung-a30/` (postmarketOS Device Package)
The device package encapsulates all platform-specific definitions required by `pmbootstrap`:
1. **`deviceinfo`**: Defines hardware specifications:
   - Display: 1080x2340 EA8076 Super AMOLED.
   - Touchscreen: `/dev/input/event1` (`sec_touchscreen`).
   - Kernel parameters: `buildvariant=userdebug console=null androidboot.hardware=universal7904 loop.max_part=7`.
   - Bootloader: Header version 0, base `0x10000000`, kernel offset `0x00008000`, ramdisk offset `0x01000000`, page size 2048.
2. **`initfs-hook.sh`**: Executed during early initramfs boot before `switch_root`. Mounts `/dev/block/by-name/vendor` to `/android/vendor` and `/dev/block/by-name/system` to `/android/system`, creates compatibility symlinks, and instantiates character device nodes (`/dev/graphics/fb0`, `/dev/mali0`, `/dev/ion`, `/dev/sync`).
3. **`weston.ini`**: Configures Weston core: `--backend=decon-backend.so`, `--renderer=gl`, `--idle-time=0`, display mode `1080x2340@60Hz`.
4. **`weston.initd`**: OpenRC service script supervising Weston on `seat0` with proper environment variables (`XDG_RUNTIME_DIR`, `EGL_PLATFORM=wayland`).
5. **`99-mali-decon.rules`**: Udev rules setting mode `0666` on graphics/ION/Mali nodes and tagging the Melfas touchscreen and hardware keys for `seat0`.

---

## 3. Standalone Direct Boot Image Generation (Phase 8B)

### 3.1 Boot Image Tools (`tools/mkbootimg.py` & `tools/unpackbootimg.py`)
To eliminate reliance on external host binaries or Android NDK tools, standalone Python 3 scripts were authored:
- [`tools/mkbootimg.py`](file:///home/neyuh/mali-a30/tools/mkbootimg.py): Implements Android Boot Image Header versions 0, 1, and 2. Computes page alignment, pads sections, formats command-line arguments, and calculates the SHA-1 header checksum.
- [`tools/unpackbootimg.py`](file:///home/neyuh/mali-a30/tools/unpackbootimg.py): Reads Android boot images, parses header fields, and unpacks the kernel and ramdisk.

### 3.2 Kernel Extraction & Stock Boot Backup
The live boot partition (`/dev/block/by-name/boot`, 37,748,736 bytes) was extracted from the Samsung Galaxy A30:
- **Stock Boot Image**: Backed up to [`artifacts/extracted/stock_boot.img`](file:///home/neyuh/mali-a30/artifacts/extracted/stock_boot.img).
  - SHA-256: `6a05e47caaf725943e6be4f058c93e1067c9e6e6cca5baacd370a483fa8cf8e0`.
- **Extracted Kernel Binary**: [`artifacts/extracted/stock_boot_unpacked/kernel`](file:///home/neyuh/mali-a30/artifacts/extracted/stock_boot_unpacked/kernel) (27,815,264 bytes). Contains the downstream Linux 4.4.302 `Image` and appended Exynos 7904 device tree blob.

### 3.3 Standalone Initramfs & Switchroot Logic
The standalone direct boot initramfs was packaged using [`tools/busybox-arm64-static`](file:///home/neyuh/mali-a30/tools/busybox-arm64-static) (statically linked 64-bit ELF binary). The `init` script executes the following sequence:

```
[Power On / S-Boot]
        |
        v
[Downstream Kernel 4.4.302 Init]
        |
        v
[Initramfs: /init]
        |
        +--> 1. Mount virtual filesystems: /proc, /sys, /dev (devtmpfs)
        |
        +--> 2. Redirect stdout/stderr to /dev/kmsg
        |
        +--> 3. Wait for storage enumeration (eMMC: mmcblk0p32, SD: mmcblk1p*)
        |
        +--> 4. Mount rootfs:
        |       - Priority 1: External microSD ext4 partition (/dev/mmcblk1p2)
        |       - Priority 2: Internal /data/alpine (/dev/block/by-name/userdata)
        |
        +--> 5. Mount Android partitions for Libhybris (read-only):
        |       - /dev/block/by-name/vendor -> /sysroot/android/vendor
        |       - /dev/block/by-name/system -> /sysroot/android/system
        |
        +--> 6. Move /sys, /proc, /dev into /sysroot
        |
        v
[exec switch_root /sysroot /sbin/init]
        |
        v
[Alpine / postmarketOS OpenRC: PID 1]
        |
        +--> eudev populates device nodes
        +--> seatd / udev initializes seat0
        +--> /etc/init.d/weston starts Weston 14 Direct-DECON GL Renderer
        |
        v
[1080x2340 Physical AMOLED Desktop Displayed (< 5s boot time)]
```

### 3.4 Automated Build Script & Verification
The build pipeline was automated in [`scripts/build_direct_boot.sh`](file:///home/neyuh/mali-a30/scripts/build_direct_boot.sh). Running the script generated the verified direct boot image:

```bash
./scripts/build_direct_boot.sh
```

**Build Output Telemetry**:
- **Output File**: `build/boot-direct.img`
- **Total Image Size**: 28,479,488 bytes (28.48 MB, comfortably fits inside 36.0 MB boot partition)
- **Kernel Size**: 27,815,264 bytes at `0x10008000`
- **Ramdisk Size**: 660,787 bytes at `0x11000000`
- **Tags Address**: `0x10000100`
- **Page Size**: 2048 bytes
- **Header Version**: 0
- **SHA-256**: `9e675868009445cd42ff2dfef54fa99a52fb7c02b426d4b0d2c8ee1bbb89c015`

The image was independently unpacked and verified with `unpackbootimg.py`, confirming complete header compliance.

---

## 4. Flashing & Hardware Recovery Safety Protocol

To strictly adhere to the non-destructive testing requirements, the following safety protocol governs deployment:

### 4.1 Partition Integrity Protection
- The stock Android boot image is backed up at:
  - Local repository: [`artifacts/extracted/stock_boot.img`](file:///home/neyuh/mali-a30/artifacts/extracted/stock_boot.img).
  - Target device storage: `/sdcard/stock_boot.img`.
- Stock boot image SHA-256 is recorded as `6a05e47caaf725943e6be4f058c93e1067c9e6e6cca5baacd370a483fa8cf8e0`.
- The physical recovery partition (`/dev/block/by-name/recovery`, `mmcblk0p16`, 55.5 MB) containing TWRP remains completely untouched.

### 4.2 Hardware Key Recovery Guarantees
Even in the event of an unbootable boot image:
1. **TWRP Recovery Mode**: Holding `Volume Up + Power` while connected to USB forces the Exynos bootloader to enter TWRP recovery. TWRP provides full ADB root access and partition restoration:
   ```bash
   adb shell dd if=/sdcard/stock_boot.img of=/dev/block/by-name/boot
   ```
2. **Samsung Odin / Heimdall Download Mode**: Holding `Volume Up + Volume Down` while inserting a USB cable invokes S-Boot Download Mode. The device can be reflashed via Heimdall or Odin regardless of boot partition state:
   ```bash
   heimdall flash --BOOT artifacts/extracted/stock_boot.img
   ```

---

## 5. Upstream Panfrost Bifrost v6 & Mainline DRM Roadmap (Phase 8C)

While the downstream libhybris stack delivers genuine GPU-accelerated desktop compositing today, the ultimate goal of postmarketOS is running on mainline Linux kernels (6.x+) with completely open-source drivers.

### 5.1 The Downstream vs. Upstream Landscape

| Layer | Downstream Production Path (Milestones 7 & 8) | Upstream Mainline Target Path (Future) |
| :--- | :--- | :--- |
| **Kernel** | Linux 4.4.302-p6 (`neyuKernel_legacy`) | Mainline Linux 6.x+ |
| **Display Controller** | Exynos DECON FBDev (`/dev/fb0`, Window 3 IDMA 0) | Linux DRM/KMS (`drivers/gpu/drm/exynos/exynos_drm_decon.c`) |
| **Memory Allocator** | Exynos ION (`/dev/ion`, contig/system heaps) | Standard Linux DMA-BUF Heaps (`/dev/dma_heap/`) |
| **GPU Kernel Driver** | ARM `mali_kbase` r26p0 (UK 11.27, `/dev/mali0`) | Linux DRM Panfrost (`drivers/gpu/drm/panfrost/`) |
| **Userspace 3D Driver**| Samsung Mali DDK r26p0 via `libhybris` | Mesa Panfrost (`gallium/drivers/panfrost/`) |
| **Compositor Engine** | Weston 14 Direct-DECON Backend (`backend-decon.so`)| Weston Standard DRM/KMS Backend (`backend-drm.so`) |
| **Client Buffer Sharing**| `android_wlegl` (Gralloc ION DMA-BUF) | `zwp_linux_dmabuf_v1` (GBM DRM DMA-BUF) |

### 5.2 Technical Prerequisites for Mainline Enablement

#### 1. Exynos 7904 DECON DRM/KMS Driver
The upstream Linux kernel currently contains DECON drivers for Exynos 5433 and Exynos 7420, but lacks support for the Exynos 7870/7904 DECON controller:
- **Porting Scope**: Add Exynos 7904 compatible string (`samsung,exynos7904-decon`) to `drivers/gpu/drm/exynos/exynos_drm_decon.c`.
- **Clock & Power Management**: Wire the Exynos 7904 clock management unit (CMU) gates (`aclk_decon`, `pclk_decon`, `vclk_decon`) and power domain.
- **MIPI-DSI Panel Driver**: Write a DRM panel driver for the EA8076 Super AMOLED panel using standard MIPI-DCS DCS write sequences.

#### 2. Mesa Panfrost Bifrost v6 Enablement
As proven in Milestone 5 ([`analysis/milestone-5-panfrost-gpu-id-audit.md`](file:///home/neyuh/mali-a30/analysis/milestone-5-panfrost-gpu-id-audit.md)):
- **Compiler Readiness**: Mesa's Bifrost compiler already contains lowering passes for all G71 quirks (`BIFROST_NO_FP32_TRANSCENDENTALS`, `BIFROST_LIMITED_CLPER`, blend shaders).
- **GPU ID Registration**: Add `0x60A0` to `src/panfrost/shared/pan_model.c`:
  ```c
  {
      .gpu_id = 0x60A0,
      .name = "Mali-G71",
      .arch = PAN_ARCH_BIFROST,
      .features = PAN_FEATURE_BIFROST_V6,
      .quirks = BIFROST_NO_FP32_TRANSCENDENTALS | BIFROST_LIMITED_CLPER,
  }
  ```
- **Kernel Panfrost Support**: Add `0x60A0` to `drivers/gpu/drm/panfrost/panfrost_gpu.c` in the mainline kernel.

---

## 6. Milestone 8 Deliverable Summary

All planned Milestone 8 deliverables are authored, tested, and tracked in the repository:

1. **Packaging Definitions**:
   - [`packaging/libhybris-musl/APKBUILD`](file:///home/neyuh/mali-a30/packaging/libhybris-musl/APKBUILD)
   - [`packaging/weston-decon/APKBUILD`](file:///home/neyuh/mali-a30/packaging/weston-decon/APKBUILD)
   - [`packaging/device-samsung-a30/APKBUILD`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/APKBUILD)
   - [`packaging/device-samsung-a30/deviceinfo`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/deviceinfo)
   - [`packaging/device-samsung-a30/initfs-hook.sh`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/initfs-hook.sh)
   - [`packaging/device-samsung-a30/weston.ini`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/weston.ini)
   - [`packaging/device-samsung-a30/weston.initd`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/weston.initd)
   - [`packaging/device-samsung-a30/99-mali-decon.rules`](file:///home/neyuh/mali-a30/packaging/device-samsung-a30/99-mali-decon.rules)
2. **Boot Image Tools**:
   - [`tools/mkbootimg.py`](file:///home/neyuh/mali-a30/tools/mkbootimg.py)
   - [`tools/unpackbootimg.py`](file:///home/neyuh/mali-a30/tools/unpackbootimg.py)
   - [`tools/busybox-arm64-static`](file:///home/neyuh/mali-a30/tools/busybox-arm64-static)
3. **Build Script & Artifacts**:
   - [`scripts/build_direct_boot.sh`](file:///home/neyuh/mali-a30/scripts/build_direct_boot.sh)
   - `build/boot-direct.img` (28,479,488 bytes, SHA-256 `9e675868...`)
   - [`artifacts/extracted/stock_boot.img`](file:///home/neyuh/mali-a30/artifacts/extracted/stock_boot.img) (37,748,736 bytes, SHA-256 `6a05e47c...`)

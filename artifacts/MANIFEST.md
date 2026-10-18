# Artifact Manifest & Provenance

This manifest documents all proprietary vendor binaries, firmware blobs, and hardware definitions extracted from the target hardware or provided for analysis. In accordance with project policy, proprietary binaries are excluded from Git version control; their cryptographic hashes, file headers, and verified provenance are cataloged below.

---

## 1. Proprietary Userspace Graphics Binaries

All proprietary binaries were pulled from a physical Samsung Galaxy A30 (SM-A305F) running Android 11 (LineageOS 18.1, build `RQ3A.211001.001`) with root permissions via ADB.

| Artifact | Original Device Path | Architecture | Size (Bytes) | SHA-256 Hash | Stripped / Debug | SONAME | Build ID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `libGLES_mali-64.so` | `/vendor/lib64/egl/libGLES_mali.so` | ARM AArch64 | 51,466,696 | `70b4fd349187346c82648337d2138da3a0b4c36afe14c079e7cfce98f1be9613` | **Not stripped** (`.symtab`, `.debug_info`) | `libGLES_mali.so` | `3b7c95da48bd7a0312d91b53b5350eb3` |
| `libGLES_mali-32.so` | `/vendor/lib/egl/libGLES_mali.so` | ARM 32-bit (EABI5) | 39,837,680 | `7bc07feb3c8dde0901e1f9b2149ee25d52c929613d1572603ca95268f528332f` | **Not stripped** (`.symtab`, `.debug_info`) | `libGLES_mali.so` | `338c4e353707ff27614617113ad255d2` |
| `gralloc-64.so` | `/vendor/lib64/hw/gralloc.exynos7904.so` | ARM AArch64 | 80,480 | `7934c96c03509d7d589100d6c8ba2f174a6ffb48e0aef92a9c86a51891a59a28` | Stripped | `gralloc.exynos7904.so` | `b9edf86163bfe051e152b024b7aa46e0` |
| `gralloc-32.so` | `/vendor/lib/hw/gralloc.exynos7904.so` | ARM 32-bit (EABI5) | 63,332 | `e40ebdc3fd229eb686fed6deb89bf0ad790b1a7cb3aab42f24fe189fe6c08ac6` | Stripped | `gralloc.exynos7904.so` | `2004fbccfa5175be46511263facc110c` |
| `hwcomposer-64.so` | `/vendor/lib64/hw/hwcomposer.exynos7904.so` | ARM AArch64 | 64,312 | `4fa06e6fcbbebc83aef6fb3c1fe2f6cade7cf9ac68c71717a5322ad6e9c186a3` | Stripped | `hwcomposer.exynos7904.so` | `ba6c23fc71a00f2924e778c3a80e218c` |
| `hwcomposer-32.so` | `/vendor/lib/hw/hwcomposer.exynos7904.so` | ARM 32-bit (EABI5) | 52,980 | `7f9e636212df439e7d7f68487520e0998eab56e1d59dbef28ca13d751e7004a4` | Stripped | `hwcomposer.exynos7904.so` | `b4fb246e29a7736324ddf27ad7581ebc` |
| `libion_exynos.so` | `/vendor/lib64/libion_exynos.so` | ARM AArch64 | 19,752 | `739817dcc2cc3e8caba5fe666f13c8f1516e4c1f56ec8e2180cda2db5a4c08e1` | Stripped | `libion_exynos.so` | `0a8b27866f6c0fda08c277ed35213a6c` |
| `libexynosdisplay.so` | `/vendor/lib64/libexynosdisplay.so` | ARM AArch64 | 559,088 | `5fdcf9f6b8de018e3e0c5363b99c54873924028d86b6f57d78d27226dd13136c` | Stripped | `libexynosdisplay.so` | `38d7bf8a972e38c7154563a6a928ba56` |
| `libacryl.so` | `/vendor/lib64/libacryl.so` | ARM AArch64 | 103,256 | `080181c1be271efbfb98f651a108acc65139f020cce1b33c284477fb6f878ac9` | Stripped | `libacryl.so` | `b1e2e92c6999908cf67414df688921be` |



### Detailed Binary Characteristics

#### 1. `libGLES_mali-64.so`
- **Driver Build String**: `v1.r26p0-01eac0.###other-sha0123456789ABCDEF0###`
- **DDK Tree Source Path**: `vendor/arm/MaliG71_r26p0_LS_R/`
- **Mali Architecture**: Bifrost (v6), Family `FeatureFamily_bifrost`
- **Exported APIs**: EGL 1.4, OpenGL ES 1.1, OpenGL ES 2.0, OpenGL ES 3.2, OpenCL 2.0 Full Profile, Android Vulkan HAL
- **Symbol Count**: 191,392 symbols present in `.symtab`
- **Dynamic Dependencies (`DT_NEEDED`)**:
  - `liblog.so`
  - `libz.so`
  - `libnativewindow.so`
  - `libm.so`
  - `libc.so`
  - `libdl.so`
  - `libion_exynos.so`
  - `libc++.so`

#### 2. `libGLES_mali-32.so`
- **Driver Build String**: `v1.r26p0-01eac0`
- **Symbol Count**: 233,955 symbols present in `.symtab`
- **Dynamic Dependencies (`DT_NEEDED`)**: Identical library set to 64-bit counterpart.

#### 3. `gralloc-64.so` / `gralloc-32.so`
- **HAL Module Identifier (`HMI`)**: `mali_gralloc_module_methods`
- **Buffer Allocation Backend**: Samsung Exynos ION (`exynos_ion_open`, `exynos_ion_alloc`, `exynos_ion_import_handle`)
- **AFBC (ARM Framebuffer Compression)**: Supported (`init_afbc`)
- **Dynamic Dependencies (`DT_NEEDED`)**:
  - `libhardware.so`
  - `liblog.so`
  - `libcutils.so`
  - `libGLESv1_CM.so`
  - `libion_exynos.so`
  - `libsync.so`
  - `libutils.so`
  - `libnativewindow.so`
  - `libc++.so`, `libc.so`, `libm.so`, `libdl.so`

#### 4. `hwcomposer-64.so` / `hwcomposer-32.so`
- **HAL Interface**: HWC2 (`exynos_hwc2_device_t`, `HWCWatchdogThread`)
- **Display Controller Interface**: Samsung DECON (`libexynosdisplay.so`, `libacryl.so`, `libExynosHWCService.so`)
- **Dynamic Dependencies (`DT_NEEDED`)**:
  - `liblog.so`, `libcutils.so`, `libutils.so`, `libexynosdisplay.so`, `libacryl.so`, `libui.so`, `libion.so`, `libExynosHWCService.so`, `libc++.so`, `libc.so`, `libm.so`, `libdl.so`

---

## 2. Kernel and Hardware Configuration Artifacts

Extracted directly from device sysfs and debugfs via rooted ADB session.

| Artifact | Extracted Source | Size (Bytes) | SHA-256 Hash | Description |
| :--- | :--- | :--- | :--- | :--- |
| `kernel_config.gz` | `/proc/config.gz` | 31,753 | `06a5f2aa7393ee4b687d7e4d4c11e8b7637eb88c3d50c15f298c2ae301b4212e` | Active Linux kernel build configuration (`4.4.302-p6-neyuKernel_legacy`) |
| `kernel_config.txt` | Decompressed config | 134,814 | `49f99f36e4f3a4b64816be58461db7ba5139fbfa7f60741da10b25e1df1efcb3` | Plaintext kernel options |
| `device_tree.dtb` | `/sys/firmware/fdt` | 160,704 | `9b9b2052389f4595effcdc2670e064da674f474be84370f1d81fb734cb40d08d` | Flattened Device Tree binary from live boot |
| `boot.img` | `/dev/block/by-name/boot` | 33,554,432 | `d13242c3b3989d2f9a552848a7c5cbf6f99831067229ef057f87afd24ae929f9` | Raw boot partition image dumped from device |
| `kernel.img` | Extracted from `boot.img` | 27,815,936 | `96556a9681076ff529b58208b1d89cefcd3459f66646c0955160a8cb114bcff1` | Raw ARM64 Linux kernel Image executable |

---

## 3. Missing Artifacts & External Dependencies

The following external artifacts are not stored locally in this repo and represent required targets for investigation or acquisition:

1. **Downstream Kernel Source**: Full source tree matching `4.4.302-p6-neyuKernel_legacy` (or upstream open-source Eureka kernel `Chatur27/Eureka-Kernel-Exynos7885-Q-R` / `eurekadevelopment/Eureka-Kernel`).
2. **Proprietary Helper Libraries (for libhybris pathway)**:
   - `/vendor/lib64/libion_exynos.so` (**Acquired & cataloged above**)
   - `/vendor/lib64/libexynosdisplay.so` (**Acquired & cataloged above**)
   - `/vendor/lib64/libacryl.so` (**Acquired & cataloged above**)
3. **Upstream Mesa Source**: Upstream Mesa Git checkout containing `src/panfrost` (tested revision: main / 24.x+).
4. **Upstream Linux DRM Panfrost Tree**: Linux kernel 6.x+ with `drivers/gpu/drm/panfrost/` and mainline Exynos DTS for 7885.



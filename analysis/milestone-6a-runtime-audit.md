# Milestone 6A Analysis: Runtime Environment & Compatibility Shim Audit

**Document ID**: `analysis/milestone-6a-runtime-audit.md`  
**Date**: October 18, 2026  
**Status**: **Rigorous Engineering Audit**  
**Audited Commit**: `11d771f`  
**Target Hardware**: Samsung Galaxy A30 (`SM-A305F`), Exynos 7904, Mali-G71 MP2  
**Active Device OS**: Android 11 (LineageOS 18.1 userdebug, `Linux 4.4.302-p6-neyuKernel_legacy`)  

---

## 1. Executive Summary

Milestone 6 delivered two critical technical achievements:
1. Proof that Exynos 7904 DECON Window 3 can be driven directly via `S3CFB_WIN_CONFIG` (`0x441046d1`) from a standalone userspace process at locked 60 Hz VSYNC without running Android SurfaceFlinger or HWC2 (`tests/decon_compositor_test`).
2. An isolated C implementation of six common Bionic libc compatibility APIs (`tests/bionic_shim_test`).

However, an engineering audit of commit `11d771f` reveals a fundamental architectural distinction that must be formally documented:

> [!IMPORTANT]
> **Audit Finding 1**: All test binaries and shared libraries in Milestone 6 were compiled against the Android Bionic sysroot with ELF interpreter `/system/bin/linker64` and executed exclusively under Android 11 (LineageOS 18.1). **No test has yet executed under postmarketOS or Alpine Linux's musl dynamic linker (`/lib/ld-musl-aarch64.so.1`).**

> [!IMPORTANT]
> **Audit Finding 2**: In `tests/decon_compositor_test`, `libGLES_mali.so` was loaded directly by Android's `/system/bin/linker64`, which resolved its `DT_NEEDED` dependencies against Android system libraries (`/system/lib64/libc.so`, `libc++.so`, `liblog.so`), **not against `libbionic_musl_shim.so`**. The compatibility shim was only unit-tested in isolation by `tests/bionic_shim_test`.

> [!IMPORTANT]
> **Audit Finding 3**: A simple C shim is **insufficient** on its own to load `/vendor/lib64/egl/libGLES_mali.so` under native musl libc. The vendor library has hard dynamic dependencies on Android's C++ runtime (`libc++.so`), Android symbol versions (`LIBC_N`, `LIBC_P`, `LIBC_R`), and Android-specific thread/linker structures that require either `libhybris` or an isolated Bionic runtime container.

---

## 2. ELF Metadata & Dynamic Linker Audit

Every binary and shared library built in Milestone 6 was audited using `readelf` on the host x86_64 system and target hardware:

### 2.1 Complete ELF Inspection Matrix

| Artifact | ELF Class / Arch | ELF Type | Program Interpreter (`PT_INTERP`) | `DT_NEEDED` Dependencies | Execution Environment |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `tests/decon_compositor_test` | ELF64 / AArch64 | DYN (PIE) | `/system/bin/linker64` | `libc.so`, `libdl.so` | Android LineageOS 18.1 (Bionic) |
| `tests/bionic_shim_test` | ELF64 / AArch64 | DYN (PIE) | `/system/bin/linker64` | `libc.so`, `libdl.so` | Android LineageOS 18.1 (Bionic) |
| `src/bridge/libbionic_musl_shim.so` | ELF64 / AArch64 | DYN (Shared) | None | `libc.so` | Android LineageOS 18.1 (Bionic) |
| `src/compositor/libdecon_backend.so` | ELF64 / AArch64 | DYN (Shared) | None | None | Android LineageOS 18.1 (Bionic) |
| `libGLES_mali-64.so` (Vendor DDK) | ELF64 / AArch64 | DYN (Shared) | None | `liblog.so`, `libz.so`, `libnativewindow.so`, `libm.so`, `libc.so`, `libdl.so`, `libion_exynos.so`, `libc++.so` | Android LineageOS 18.1 (Bionic) |

### 2.2 Relocation and Symbol Analysis

#### `tests/decon_compositor_test`
- **Dynamic relocations**: 15 PLT relocations (`R_AARCH64_JUMP_SLOT`): `__libc_init`, `printf`, `memset`, `dlopen`, `dlerror`, `dlsym`, `dlclose`, `clock_gettime`, `open`, `close`, `ioctl`, `poll`.
- **Linker invocation**: Compiled with `-Wl,-dynamic-linker,/system/bin/linker64 -Lartifacts/extracted/sysroot -lc -ldl`.
- **Runtime reality**: Requires Android's `/system/bin/linker64` and `/system/lib64/libc.so`. Attempting to execute this binary in Alpine Linux fails immediately with:
  ```text
  -sh: ./decon_compositor_test: not found
  ```
  because `/system/bin/linker64` does not exist in standard postmarketOS.

#### `src/bridge/libbionic_musl_shim.so`
- **Exported Symbols (39 total)**: `__errno`, `_ctype_`, `__progname`, `__android_log_print`, `property_get`, `property_set`, FORTIFY checks (`__memcpy_chk`, etc.), `ion_*`.
- **Imported (Undefined) Symbols**: `vsnprintf`, `write`, `strlen`, `abort`, `memset`, `read`, `open`, `close`, `ioctl`, and `__errno_location` (WEAK).
- **Critical observation**: While the source code of `bionic_musl_shim.c` was authored to be freestanding, the compiled shared library links against Android Bionic `libc.so` (`DT_NEEDED: [libc.so]`).

---

## 3. Compatibility Shim vs. Vendor DDK ABI Audit

### 3.1 Did the Vendor Driver Load the Shim?
In `tests/decon_compositor_test.c`:
```c
/* Step 1: Load proprietary graphics driver */
void *mali_lib = dlopen("/vendor/lib64/egl/libGLES_mali.so", RTLD_NOW);
```
When `dlopen()` is called inside `decon_compositor_test`:
1. The call enters Android's `/system/bin/linker64`.
2. `linker64` parses `libGLES_mali.so`'s dynamic section:
   - `DT_NEEDED: [liblog.so]` -> loaded from `/system/lib64/liblog.so`
   - `DT_NEEDED: [libc.so]` -> loaded from `/system/lib64/libc.so`
   - `DT_NEEDED: [libc++.so]` -> loaded from `/system/lib64/libc++.so`
   - `DT_NEEDED: [libnativewindow.so]` -> loaded from `/system/lib64/libnativewindow.so`
   - `DT_NEEDED: [libion_exynos.so]` -> loaded from `/vendor/lib64/libion_exynos.so`
3. `libbionic_musl_shim.so` was **not on the library search path and was not preloaded**.
4. Therefore, `libGLES_mali.so` ran against **real Android Bionic system libraries**, completely bypassing `libbionic_musl_shim.so`.

### 3.2 What Happens If Musl Tries to Load `libGLES_mali.so`?
If Alpine Linux's musl dynamic linker (`ld-musl-aarch64.so.1`) attempts to load `/vendor/lib64/egl/libGLES_mali.so`, it encounters three insurmountable barriers:

#### Barrier 1: Missing DT_NEEDED Shared Libraries
`libGLES_mali.so` cannot be loaded in isolation; it requires 8 shared libraries. In Alpine musl:
- `liblog.so`: Missing (Android logging daemon HAL).
- `libnativewindow.so`: Missing (Android ANativeWindow HAL).
- `libion_exynos.so`: Missing (Samsung vendor memory HAL).
- `libc++.so`: Incompatible. Alpine supplies `libc++.so` or `libstdc++.so` built against musl. Android's `libc++.so` is built against Bionic, with incompatible C++ exception handling (`libunwind`), typeinfo structures, and STL thread primitives.

#### Barrier 2: Android Symbol Versioning (`.gnu.version_r`)
`readelf -V libGLES_mali-64.so` proves that the vendor binary strictly requires specific Bionic symbol versions:
- `libc.so` versions: `LIBC`, `LIBC_N` (Nougat), `LIBC_P` (Pie), `LIBC_R` (Android 11).
Musl libc does **not** implement symbol versioning. Any dynamic linker attempting to enforce these versions will fail; any linker ignoring them risks symbol mismatches.

#### Barrier 3: Thread-Local Storage & Pthread Structures
The vendor driver extensively uses Bionic thread-local storage (`pthread_key_create`, TLS slot lookups) and expects Bionic's internal `pthread_internal_t` structure layout. In musl libc:
- `pthread_t` is a pointer to musl's `struct pthread`.
- Bionic's `pthread_t` is a pointer to Bionic's `pthread_internal_t`.
- Passing a musl `pthread_t` or musl `pthread_mutex_t` to a Bionic-compiled function (or vice versa) results in immediate memory corruption.

---

## 4. Comparison: Freestanding Shim vs. Libhybris

| Feature / Subsystem | `libbionic_musl_shim.so` (Milestone 6) | `libhybris` Architecture |
| :--- | :--- | :--- |
| **Dynamic Linker** | Relies on host linker (`ld-musl` or `/system/bin/linker64`) | Embeds custom `hybris-linker` that emulates Android's `linker64` inside the host process |
| **Linker Namespaces** | Single flat namespace (vulnerable to symbol collisions) | Isolated dual namespaces: host (musl/glibc) vs Android (Bionic) |
| **`libc++.so` Support** | None (only C standard library stubs) | Full (loads vendor `libc++.so` inside the Bionic namespace) |
| **Pthread / TLS Translation** | None (stubs only `__errno`) | Comprehensive hooks translating `pthread_mutex`, `pthread_cond`, TLS keys, and `pthread_create` |
| **Android HAL Stubs** | Stubs `property_get/set` and `log_print` in C | Provides modular wrapper libraries: `libandroid-properties`, `libsync`, `libhardware` |
| **EGL Window System** | None (requires DMA-BUF FBO presentation) | Translates `EGLNativeWindowType` (`ANativeWindow`) to Wayland `wl_egl_window` |
| **Target Verdict** | **Sufficient for isolated DECON presentation testing on Android; insufficient for loading vendor DDK under pure musl** | **Mandatory requirement for running proprietary Bionic GPU DDKs under native Linux distributions** |

---

## 5. Minimal Reproducible Test Matrix

To prevent conflation of Android test results with postmarketOS results, the test matrix is formally partitioned into two distinct execution environments:

### Environment A: Android 11 (LineageOS 18.1 userdebug) — Physical Target Active

| Test Target | Source File | Linker Used | Libc Used | Result | Hardware Evidence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Kernel ABI Probe** | `tests/kbase_probe.c` | None (Static) | None (Raw syscalls) | **PASS** | MTP `0x3000`, Context ID 31, Alloc/Free OK |
| **EGL Headless Offscreen** | `tests/egl_offscreen_test.c` | `/system/bin/linker64` | Android Bionic `libc.so` | **PASS** | Pbuffer 64x64, Mali Job IRQ +2, glReadPixels match |
| **ION DMA-BUF Import** | `tests/ion_mali_import_test.c`| `/system/bin/linker64` | Android Bionic `libc.so` | **PASS** | `0xc0204900` alloc, FBO complete, CPU mmap match |
| **Physical DECON Scanout** | `tests/decon_present_test.c` | `/system/bin/linker64` | Android Bionic `libc.so` | **PASS** | Fullscreen 1080x2340 calibration bar on AMOLED |
| **Continuous 60 FPS Flip** | `tests/decon_flip_test.c` | `/system/bin/linker64` | Android Bionic `libc.so` | **PASS** | 60 frames, 59.89 FPS, 16.6 ms steady pacing |
| **Bionic Shim Unit Test** | `tests/bionic_shim_test.c` | `/system/bin/linker64` | Android Bionic `libc.so` | **PASS** | All 6 shim unit functions verified in isolation |
| **Compositor Engine Loop** | `tests/decon_compositor_test.c`| `/system/bin/linker64`| Android Bionic `libc.so` | **PASS** | 120 frames, 59.34 FPS, AMOLED scanout OK |

### Environment B: postmarketOS (Alpine Linux / musl libc) — Target Unbooted

| Test Target | Intended Tool | Expected Linker | Expected Libc | Current Status | Blocker / Requirement |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct Kernel Probe** | `tests/kbase_probe` | None (Static) | None | **Ready to Run** | Self-contained static binary; will run immediately once postmarketOS kernel boots. |
| **DECON Framebuffer Probe** | `tests/fb_probe` | None (Static) | None | **Ready to Run** | Self-contained static binary; will probe `/dev/graphics/fb0` on postmarketOS. |
| **Native Weston DECON Backend** | `weston-backend-decon.so` | `/lib/ld-musl-aarch64.so.1` | musl `libc.musl-aarch64.so.1` | **Not Built** | Requires authoring Weston output plugin C source. |
| **Proprietary DDK via Hybris** | `libhybris` + `libGLES_mali` | `hybris-linker` | Dual: musl + Bionic | **Not Integrated** | Requires packaging `libhybris` APK on postmarketOS. |
| **Open-Source Mesa Panfrost** | Mesa `pan_kmod_kbase` | `/lib/ld-musl-aarch64.so.1` | Pure musl libc | **Architecture Mapped** | Requires writing `pan_kmod_kbase.c` kernel translation layer. |

---

## 6. Audit Conclusion

1. **Test Execution Truth**: All benchmarks and presentation tests in Milestones 3A through 6 ran on Android with Bionic libraries. No test has run inside a postmarketOS userspace.
2. **Shim Scope**: `libbionic_musl_shim.so` proves that Bionic C primitives can be written cleanly in freestanding C, but it does not enable `libGLES_mali.so` to run on musl due to `libc++.so`, symbol versioning, and Android HAL dependencies.
3. **Actionable Path Forward**: Real postmarketOS hardware acceleration requires either:
   - **Path A (Libhybris + Direct-DECON)**: Running `libGLES_mali.so` inside a proper `libhybris` wrapper linked to a Weston DECON backend.
   - **Path B (Upstream Mesa Panfrost)**: Running native, pure-musl Mesa Panfrost via a `pan_kmod_kbase` translation layer on downstream kernel 4.4.

# Research Log

This log chronicles engineering steps, binary analysis sessions, hardware interrogations, and decisions made during the project.

---

## Session: 2026-10-17 — Session 1: Environment Discovery & Baseline Verification

### Objectives
1. Inspect available tools, workspace, and connected target hardware.
2. Verify hardware model, GPU architecture, kernel version, and userspace artifacts.
3. Establish Git repository structure, evidence ledger, and artifact manifest.
4. Reverse-engineer the kernel ABI interface of the proprietary driver.

---

### Chronological Log & Observations

#### 1. Initial Workspace and Environment Audit
- Workspace inspected at `/home/neyuh/mali-a30`.
- Existing subdirectories observed: `analysis/` and `artifacts/proprietary/`.
- Local binary analysis tools verified:
  - `file`, `readelf`, `nm`, `strings`, `objdump`, `python3`, `git`.
  - `llvm-objdump`, `llvm-dwarfdump`, `dwarfdump`, `eu-readelf` installed.
  - Reverse Engineer Anything (REA) CLI located at `~/.npm/_npx/9b9809298fe40d2c/node_modules/.bin/rea` (v6.5.0) with Hopper Disassembler provider available at `/opt/hopper/bin/Hopper`.

#### 2. Physical Target Discovery
- Connected USB devices checked via `adb devices -l`:
  - Device detected: `R58M41M451D device usb:1-1 product:a30dd model:SM_A305F device:a30 transport_id:3`.
  - Target system: Samsung Galaxy A30 (`SM-A305F`).
  - Active OS: LineageOS 18.1 (Android 11, API level 30, build `RQ3A.211001.001`).
  - Active Kernel: `Linux localhost 4.4.302-p6-neyuKernel_legacy #1 SMP PREEMPT Wed Jul 29 10:13:09 UTC 2026 aarch64`.
  - SoC: Samsung Exynos 7904 (`universal7904` / Exynos 7885 architecture family).

#### 3. Live Hardware & Sysfs Interrogation
- Non-destructive read-only queries executed against device sysfs:
  - `/sys/devices/platform/11500000.mali/gpuinfo` -> `Mali-G71 2 cores r0p0 0x60A0`.
  - Device nodes: `/dev/mali0` (major 10, minor 112, `crw-rw-rw-`), `/dev/ion` (major 10, minor 127).
  - Devicetree node at `mali@11500000`:
    - Base address: `0x11500000` (length `0x00050000`).
    - Interrupts: JOB = 159 (`0x9f`), MMU = 160 (`0xa0`), GPU = 158 (`0x9e`).
    - Compatible string: `arm,mali`.
  - Active kernel config extracted from `/proc/config.gz` -> saved to `artifacts/extracted/kernel_config.txt`.
    - Key config items: `CONFIG_MALI_TMIX=y`, `CONFIG_MALI_TMIX_R26P0=y`, `CONFIG_MALI_MIDGARD=y`, `CONFIG_MALI_DMA_FENCE=y`.
    - Display config: `CONFIG_EXYNOS_DECON_FB=y`, `# CONFIG_DRM is not set`.
  - Active flattened device tree extracted from `/sys/firmware/fdt` -> saved to `artifacts/extracted/device_tree.dtb`.

#### 4. Proprietary Binary Audit & Symbol Analysis
- Extracted 6 vendor libraries in `artifacts/proprietary/`:
  - `libGLES_mali-64.so` (51.4 MB) — SHA-256: `70b4fd349187346c82648337d2138da3a0b4c36afe14c079e7cfce98f1be9613`
  - `libGLES_mali-32.so` (39.8 MB) — SHA-256: `7bc07feb3c8dde0901e1f9b2149ee25d52c929613d1572603ca95268f528332f`
  - `gralloc-64.so` (80.4 KB) — SHA-256: `7934c96c03509d7d589100d6c8ba2f174a6ffb48e0aef92a9c86a51891a59a28`
  - `gralloc-32.so` (63.3 KB) — SHA-256: `e40ebdc3fd229eb686fed6deb89bf0ad790b1a7cb3aab42f24fe189fe6c08ac6`
  - `hwcomposer-64.so` (64.3 KB) — SHA-256: `4fa06e6fcbbebc83aef6fb3c1fe2f6cade7cf9ac68c71717a5322ad6e9c186a3`
  - `hwcomposer-32.so` (52.9 KB) — SHA-256: `7f9e636212df439e7d7f68487520e0998eab56e1d59dbef28ca13d751e7004a4`
- Remarkable finding: Both `libGLES_mali-64.so` and `libGLES_mali-32.so` are **not stripped**.
  - `libGLES_mali-64.so` contains 191,392 symbols in `.symtab`.
  - `libGLES_mali-32.so` contains 233,955 symbols in `.symtab`.
  - Driver build string: `v1.r26p0-01eac0`. DDK source tree: `vendor/arm/MaliG71_r26p0_LS_R/`.

#### 5. Systematic Reverse Engineering of Kernel IOCTL ABI
- Disassembled functions `basep_ioctl_*` and `base_mem_*` in `libGLES_mali-64.so` using `llvm-objdump`.
- Mapped and verified 22 distinct ioctl numbers and structures under magic `0x80`:
  - `0xc0048000`: Version check handshake (Major = 11, Minor = 27).
  - `0x40048001`: Set context flags.
  - `0x40108002`: Job submit (array of atoms).
  - `0x40108003`: Get GPU properties.
  - `0xc0208005`: Memory allocate (va_pages, commit_pages, extent, flags).
  - `0xc0108006`: Memory flags query.
  - `0x40088007`: Memory free.
  - `0x40108014`: Memory commit pages.
  - `0xc0188016`: Memory import DMA-BUF.
- Verified that device initialization formats path `/dev/mali0` using `cutils_cstr_snprintf`, opens with `0x80802`, and verifies character device mode (`S_ISCHR`).

#### 6. Architecture Strategy Decision
- Authored `ADR-001-graphics-stack-feasibility.md`.
- Concluded that Option B (downstream kernel + userspace compatibility) provides the immediate, viable path to accelerated graphics on postmarketOS, while Option A (upstream Mesa Panfrost) represents the long-term target requiring gap closure for Mali-G71 Bifrost v6.

---

### Summary of Completed Files (Session 1)
- `.gitignore`: Excludes proprietary `.so` and binary dumps from git.
- `artifacts/MANIFEST.md`: Cryptographic provenance ledger.
- `HARDWARE.md`: Verified hardware specification.
- `reverse-engineering/evidence-ledger.md`: Evidence ledger with findings FIND-001 - FIND-006.
- `reverse-engineering/ioctl-analysis/kbase-ioctls.md`: Complete ioctl interface specification.
- `architecture/ADR-001-graphics-stack-feasibility.md`: Architecture decision record.
- `ROADMAP.md`: Phased project roadmap.
- `PROJECT_STATUS.md`: Active project status.

---

## Session: 2026-10-17 — Session 2: Milestone 3A - Proprietary EGL Offscreen Validation

### Objectives
1. Inspect existing `tests/kbase_probe.c` and verify kernel handshake and memory management against `/dev/mali0`.
2. Analyze complete dynamic ELF dependencies and undefined symbols of `libGLES_mali-64.so`.
3. Reverse-engineer internal EGL initialization paths, platform dispatch, and Pbuffer creation in the proprietary DDK.
4. Build freestanding AArch64 Android test compilation toolchain using host `clang` and target Bionic sysroot.
5. Create and execute `tests/egl_offscreen_test` on physical hardware (`SM-A305F`) without a display server.
6. Verify shader compilation, dual-zone pixel rendering, and independent hardware interrupt generation (Mali Job IRQ).
7. Author Milestone 3A documentation and update evidence ledger.

---

### Chronological Log & Observations

#### 1. Low-Level Kernel Verification Probe (`kbase_probe`)
- Built `tests/kbase_probe` as a freestanding static ARM64 binary using raw system calls (`svc #0`).
- Discovered and verified FIND-007: `mali_kbase` strictly mandates mapping the Memory Tracking Page (`0x3000`, `PROT_NONE`, `MAP_SHARED`) prior to `KBASE_IOCTL_MEM_ALLOC` (`0xc0208005`).
- Discovered and verified FIND-008: SVM memory unmapping occurs via userspace `munmap()`, while non-SVM buffers use ioctl `0x40088007`.
- Executed `tests/kbase_probe` on physical device:
  - Version check: Major = 11, Minor = 27 (SUCCESS).
  - Context ID allocated: 31 (SUCCESS).
  - Allocation and deallocation: 1 page committed, freed cleanly.

#### 2. ELF Dependency & Symbol Analysis
- Extracted all 621 relocation entries resolving to 391 unique undefined symbols in `libGLES_mali-64.so`.
- Categorized all 8 `DT_NEEDED` shared libraries (`libc.so`, `libm.so`, `libdl.so`, `libc++.so`, `liblog.so`, `libz.so`, `libnativewindow.so`, `libion_exynos.so`).
- Analyzed vendor dependencies:
  - `libion_exynos.so`: Only 3 functions imported (`exynos_ion_open`, `exynos_ion_alloc`, `exynos_ion_close`). Never reached during Pbuffer offscreen rendering.
  - `libnativewindow.so`: Only 5 `AHardwareBuffer_*` functions imported for Vulkan/EGLImage.
- Contrasted Bionic libc vs Alpine musl libc ABI: `__errno` vs `__errno_location`, Bionic FORTIFY checking (`__open_2`, `__*_chk`), `pthread_t` layout differences, TLS model.
- Authored `analysis/milestone-3a-elf-dependencies.md`.

#### 3. EGL Initialization Path Reverse Engineering
- Disassembled `eglGetDisplay`, `egl_winsys_get_implementation`, and `_ZL11new_displayPvS_PKi` (`0x774ca0`):
  - Proved that passing non-zero `native_display` returns `EGL_NO_DISPLAY` with `"mali_winsys: Unsupported display"`.
  - Passing `EGL_DEFAULT_DISPLAY` (`0`) allocates 16-byte winsys display structure and succeeds without any display server.
- Disassembled `eglCreatePbufferSurface` (`0x78021c`):
  - Sets `native_window = NULL` (`xzr`) and surface type `EGL_PBUFFER_BIT` (`1`), branching directly to `eglp_create_surface` (`0x78183c`).
  - No `ANativeWindow` or SurfaceFlinger IPC is attempted.
- Established finding FIND-010.

#### 4. End-to-End Headless EGL Test Execution (`egl_offscreen_test`)
- Authored `tests/egl_offscreen_test.c` featuring:
  - Freestanding Bionic `_start` entry point.
  - Dynamic loading of `/vendor/lib64/egl/libGLES_mali.so`.
  - Platform probing (`EGL_PLATFORM_SURFACELESS_MESA` rejected with `0x300C`; `EGL_PLATFORM_ANDROID_KHR` accepted).
  - EGL 1.4 initialization (`ARM`, `1.4 Bifrost-"r26p0-01eac0"`).
  - Pbuffer configuration enumeration (25 configs; selected RGBA8888).
  - 64x64 offscreen Pbuffer surface allocation.
  - OpenGL ES 2.0/3.2 context creation and binding.
  - Hardware query: confirmed `GL_RENDERER = Mali-G71`, `GL_VENDOR = ARM`, `GL_VERSION = OpenGL ES 3.2 v1.r26p0-01eac0`.
  - Hardware Job Interrupt tracking: queried `/proc/interrupts` line 57 (`11500000.mali` GIC 191 Edge). Baseline count: `11554933`.
  - Dual-zone rendering:
    - Background Clear: Cornflower Blue `RGBA(100, 149, 237, 255)`.
    - Centered Geometry: Red Triangle `RGBA(255, 0, 0, 255)` via compiled vertex/fragment shader.
  - `glFinish()`: post-render interrupt count `11554935` (`delta = +2`), confirming physical hardware job execution.
  - `glReadPixels`:
    - Background `(0, 0)` returned `RGBA(100, 149, 237, 255)`.
    - Center `(32, 32)` returned `RGBA(255, 0, 0, 255)`.
  - Clean teardown executed without error.
- Established finding FIND-009.

#### 5. Documentation & Deliverables
- Authored `analysis/milestone-3a-elf-dependencies.md`.
- Authored `analysis/milestone-3a-test-plan.md`.
- Authored `tests/README.md`.
- Updated `reverse-engineering/evidence-ledger.md` with FIND-009 and FIND-010.
- Updated `PROJECT_STATUS.md`.

---

## Session: 2026-10-17 — Session 3: Milestone 3B - Display Presentation & Buffer-Sharing Investigation

### Objectives
1. Inspect downstream kernel source, display drivers, and active kernel configuration.
2. Interrogate DECON framebuffer device node (`/dev/graphics/fb0`) to determine geometry, formats, stride, and mmap behavior.
3. Reverse-engineer Samsung HWC2, Acryl, and Gralloc implementations to determine display presentation and buffer sharing.
4. Investigate GPU buffer export/import support in `mali_kbase` and identify universal allocator (ION).
5. Build and execute a non-destructive presentation prototype (`tests/fb_present_test.c`).
6. Define the postmarketOS compatibility boundary for display integration.
7. Author Milestone 3B documentation and update evidence ledger.

---

### Chronological Log & Observations

#### 1. Framebuffer Driver & Hardware Interrogation (`fb_probe`)
- Authored and executed `tests/fb_probe.c` on physical device `SM-A305F`:
  - `FBIOGET_VSCREENINFO`: returned 1080x2340 resolution, 32 bpp, Red offset 16 len 8, Green offset 8 len 8, Blue offset 0 len 8, Alpha offset 24 len 8 -> native `BGRA_8888` (`0xAARRGGBB`) (`FIND-011`).
  - `FBIOGET_FSCREENINFO`: returned line_length = 4320 bytes (tightly packed: `1080 * 4`), `smem_len = 20,217,600` bytes (2 frames double-buffered), `smem_start = 0x10000000`.
  - Discovered that conventional `mmap()` on `/dev/graphics/fb0` fails with `-EINVAL` (-22) (`FIND-012`).
  - Cross-referenced `/proc/iomem`: physical RAM begins at `0x80000000`; `0x10000000` is outside RAM. The driver allocates no static scanout buffer, rendering legacy fbdev writes impossible.

#### 2. Samsung HWC2 & Acryl Reverse Engineering
- Pulled and analyzed `/vendor/lib64/libexynosdisplay.so` and `/vendor/lib64/libacryl.so`:
  - Disassembled `ExynosDisplayFbInterface::deliverWinConfigData` (`0x70194` - `0x7040c`).
  - Mapped the DECON multi-window presentation ioctl: `S3CFB_WIN_CONFIG` (`0x441046d1`, `_IOW('F', 209, struct decon_win_config_data)` size 1040 bytes) (`FIND-013`).
  - Proved that each hardware overlay window in DECON takes DMA-BUF file descriptors (`fd_idma[0]`) and sync fences (`fence_fd`).
  - Verified that `android.hardware.graphics.composer@2.1-service` (PID 3935) exclusively controls `/dev/graphics/fb0`.
  - Disassembled `libacryl.so`: verified Samsung 2D hardware blitter (`AcrylicCompositorM2M1SHOT2_G2D` / `fimg2d`) handling format conversion, scaling, and rotation.

#### 3. GPU Buffer Export vs Import Architecture
- Audited all 22 `mali_kbase` ioctls: verified that `mali_kbase` has **zero DMA-BUF export ioctls** (`FIND-014`).
- Disassembled `base_mem_import_padded` (`0xacc7dc`): confirmed that `mali_kbase` imports external DMA-BUFs via `0xc0188016` (`KBASE_IOCTL_MEM_IMPORT`, `BASE_MEM_IMPORT_TYPE_UMM` = 1).
- Disassembled `gralloc.exynos7904.so` (`0xc7a0`): confirmed that Samsung Exynos ION (`/dev/ion`, `libion_exynos.so`) is the universal memory exporter (`FIND-015`).
- Traced the complete buffer sharing lifecycle: ION allocation -> DMA-BUF fd -> GPU import & render -> Sync fence -> HWC2 -> DECON `S3CFB_WIN_CONFIG` scanout.

#### 4. Presentation Prototype Execution (`fb_present_test`)
- Authored and compiled `tests/fb_present_test.c`:
  - Rendered an offscreen amber test pattern via Mali-G71 GPU.
  - Read back pixels via `glReadPixels`.
  - Inspected `/dev/graphics/fb0` hardware geometry (1080x2340) and stride (4320).
  - Adapted pixel channels from RGBA to native target BGRA8888.
  - Performed scanout safety assessment, verifying that direct `mmap` is rejected by the kernel, safely protecting the active screen scanout.
  - Saved the scanout-ready frame to `/data/local/tmp/fb_frame_test.raw`.

#### 5. Documentation & Deliverables
- Authored `analysis/milestone-3b-display-path.md`.
- Authored `analysis/milestone-3b-buffer-sharing.md`.
- Authored `analysis/milestone-3b-postmarketos-compatibility.md`.
- Updated `reverse-engineering/evidence-ledger.md` with FIND-011 through FIND-015.
- Updated `artifacts/MANIFEST.md` with hashes for `libexynosdisplay.so` and `libacryl.so`.
- Updated `tests/README.md`, `PROJECT_STATUS.md`, and `ROADMAP.md`.



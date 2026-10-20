# Research & Implementation Roadmap: Mali-G71 Open Graphics for postmarketOS

This roadmap outlines the milestones, concrete deliverables, and verification procedures required to achieve functional GPU hardware acceleration on the Samsung Galaxy A30 running postmarketOS.

---

## Milestone 0: Environment & Hardware Verification (Completed)
- [x] Inspect local tools, ADB connectivity, REA MCP capabilities.
- [x] Query live target device to verify exact SoC, GPU model, core count, silicon revision, and GPU ID (`Exynos 7904`, `Mali-G71 MP2`, `r0p0`, `0x60A0`).
- [x] Determine kernel configuration, memory subsystem, and display controller (`4.4.302-p6-neyuKernel_legacy`, `mali_kbase r26p0`, `Exynos DECON FB`, `# CONFIG_DRM is not set`).
- [x] Audit proprietary binaries and extract symbol tables (`libGLES_mali` 64-bit and 32-bit with complete unstripped `.symtab` and debug info).

---

## Milestone 1: Research Infrastructure & Evidence Ledger (Current)
- [x] Establish Git repository structure adhering to project specifications.
- [x] Create comprehensive artifact manifest with SHA-256 hashes, ELF headers, and provenance (`artifacts/MANIFEST.md`).
- [x] Establish evidence ledger with stable finding IDs (`reverse-engineering/evidence-ledger.md`).
- [x] Extract live devicetree binary (`artifacts/extracted/device_tree.dtb`) and active kernel config (`artifacts/extracted/kernel_config.txt`).
- [ ] Build automated test harness for offline parsing and validation of Mali data structures.

---

## Milestone 2: Kernel ABI & Memory Management Disassembly (Completed)
- [x] Disassemble and decode downstream `mali_kbase` ioctl table (`0x80`, commands `0x00` through `0x26`).
- [x] Reverse engineer `base_mem_alloc`, `base_mem_free`, `base_mem_commit`, `base_mem_import_padded`, and `base_mem_alias`.
- [x] Discover and verify mandatory Memory Tracking Page (`0x3000`) prerequisite for memory allocation (`FIND-007`).
- [x] Disassemble SVM dual-path deallocation (`munmap` vs `0x40088007`, `FIND-008`).
- [x] Write and physically execute standalone C probe (`tests/kbase_probe`) validating UK 11.27 handshake, context creation, and memory management.

---

## Milestone 3A: Proprietary EGL Offscreen Validation (Completed)
- [x] Catalog all 621 relocation entries and 391 undefined symbols in `libGLES_mali-64.so`.
- [x] Document Android Bionic vs Alpine musl libc ABI divergence (`analysis/milestone-3a-elf-dependencies.md`).
- [x] Reverse-engineer EGL winsys initialization and Pbuffer dispatch path (`_ZL11new_displayPvS_PKi`, `eglCreatePbufferSurface`, `FIND-010`).
- [x] Build freestanding AArch64 Android test compilation toolchain using host `clang` and target Bionic sysroot.
- [x] Author and physically execute `tests/egl_offscreen_test` on SM-A305F:
  - Probed platforms: rejected Surfaceless Mesa, accepted `EGL_PLATFORM_ANDROID_KHR` and Default Display.
  - Enumerated 25 configs, created 64x64 RGBA8888 Pbuffer, initialized GLES 3.2 context.
  - Rendered dual-zone scene: Cornflower Blue clear + Red Triangle.
  - Verified physical GPU execution via Mali Job IRQ counter increment (`+2 interrupts`, `FIND-009`).
  - Read back pixels via `glReadPixels`, confirming 0 LSB mathematical match.

---

## Milestone 3B: Display Presentation & Buffer-Sharing Investigation (Completed)
- [x] Interrogate `/dev/graphics/fb0` on physical target hardware via `tests/fb_probe`:
  - Verified visible and virtual resolution: `1080 x 2340`.
  - Verified scanout line stride: `4320` bytes/row (tightly packed).
  - Verified channel bitfields: Red offset 16 len 8, Green offset 8 len 8, Blue offset 0 len 8, Alpha offset 24 len 8 -> native `BGRA_8888` (`FIND-011`).
- [x] Verified that conventional FBDev `mmap()` is rejected with `-EINVAL` (-22) because no static RAM is allocated at boot (`smem_start = 0x10000000`, `FIND-012`).
- [x] Disassembled and mapped the DECON multi-window presentation ioctl `S3CFB_WIN_CONFIG` (`0x441046d1`, 1040 bytes) in `libexynosdisplay.so` (`FIND-013`).
- [x] Verified that `mali_kbase` is an import-only driver (`0xc0188016`) without DMA-BUF export capability (`FIND-014`).
- [x] Verified that Exynos ION (`/dev/ion`, `libion_exynos.so`) acts as the universal DMA-BUF exporter (`FIND-015`).
- [x] Built and physically executed `tests/fb_present_test.c`:
  - Rendered offscreen EGL pattern on Mali-G71.
  - Adapted RGBA pixel buffer to target BGRA8888 format with stride math.
  - Performed scanout safety assessment, verifying that active screen scanout cannot be corrupted via legacy fbdev writes.
  - Saved scanout-ready test frame to `/data/local/tmp/fb_frame_test.raw`.
- [x] Documented postmarketOS compatibility boundary (`analysis/milestone-3b-postmarketos-compatibility.md`).

---

## Milestone 4A: End-to-End DMA-BUF Render-Target Prototype (Completed)
- [x] Reverse-engineer and validate Exynos ION allocation protocol (`ION_IOC_ALLOC`, `ION_IOC_SHARE`, `FIND-016`).
- [x] Author and physically verify low-level kernel DMA-BUF import in `mali_kbase` (`KBASE_IOCTL_MEM_IMPORT`, GPU VA `0x41000`, `FIND-017`).
- [x] Create `EGLImageKHR` via `EGL_EXT_image_dma_buf_import` and bind to FBO color attachment (`GL_FRAMEBUFFER_COMPLETE`).
- [x] Execute 3D GPU clear rendering to Lime Green and verify via direct CPU `mmap()` readback (`0x00, 0xff, 0x40, 0xff`).
- [x] Reverse-engineer DECON `S3CFB_WIN_CONFIG` (`0x441046d1`) structure layout and analyze HWC2 arbitration safety (`FIND-018`).
- [x] Author analysis reports:
  - `analysis/milestone-4a-ion-allocation.md`
  - `analysis/milestone-4a-shared-render-target.md`
  - `analysis/milestone-4a-decon-presentation.md`
- [x] Deliver standalone reproducible test harness: `tests/ion_mali_import_test.c`.

---

## Milestone 4B: Standalone DECON Presentation Prototype (Completed)
- [x] Authoritative audit of `KBASE_IOCTL_MEM_IMPORT` ABI proving `BASE_MEM_IMPORT_TYPE_UMM = 2` (`FIND-019`).
- [x] Reverse-engineer complete 1040-byte `S3CFB_WIN_CONFIG` ABI (8-byte header, 5 windows of 200 bytes, `FIND-020`).
- [x] Disassemble kernel `decon_ioctl` and solve IDMA routing: IDMA channel 0 is dedicated to Window 3 (`FIND-021`).
- [x] Implement standalone DECON scanout harness (`tests/decon_present_test.c`) driving `S3CFB_WIN_CONFIG`.
- [x] Validate full-screen (1080x2340, 10.24 MB) ION DMA-BUF allocation, Mali-G71 GPU rendering, and direct CPU memory readback.
- [x] Execute safe standalone presentation under documented recovery sandbox (`stop surfaceflinger; stop vendor.hwcomposer-2-1`).
- [x] Verify hardware retire fence synchronization via `poll()` (`revents = 0x1`, `POLLIN`).
- [x] Verify physical presentation: multi-color calibration bars displayed on the EA8076 Super AMOLED screen for 5 seconds (`FIND-022`).
- [x] Author analysis reports: `analysis/milestone-4b-abi-audit.md`, `analysis/milestone-4b-safety-plan.md`, `analysis/milestone-4b-results.md`.

---

## Milestone 4C: postmarketOS Integration & Libhybris Architecture (Completed)
- [x] Analyze Bionic vs musl libc ABI divergence (`analysis/milestone-4c-bionic-musl-bridge.md`).
- [x] Audit `libhybris` dynamic linker mechanics, symbol translation hooks (`__errno`, FORTIFY, TLS), and platform modules.
- [x] Formulate postmarketOS compositor models: direct-DECON Wayland backend vs Halium HWC2 (`analysis/milestone-4c-compositor-architecture.md`).
- [x] Build and physically verify double-buffered continuous presentation engine (`tests/decon_flip_test.c`).
- [x] Benchmark hardware refresh: achieved locked **59.89 FPS** at 16.6 ms per frame on EA8076 Super AMOLED screen (`FIND-023`).
- [x] Confirm zero-tear, zero-leak, zero-panic continuous scanout stability (`analysis/milestone-4c-results.md`, `FIND-024`).

---

## Milestone 5: Mesa Panfrost Gap Analysis for Mali-G71 (Completed)
- [x] Audit Mesa `src/panfrost/` for all occurrences of `PAN_ARCH == 6` and `0x6000` / `0x60A0`.
- [x] Audit GPU ID parsing: proved `mali_kbase`, `drm/panfrost`, and Mesa normalize `0x60A00000` to `PAN_PROD_ID(6, 0, 0)` via identical `0xF00F` bitmask (`FIND-025`).
- [x] Compare Mali-G71 instruction set encoding (Bifrost v6.0) with Mali-G72 (v6.2) and Mali-G52 (v7.2) (`FIND-026`).
- [x] Audit compiler quirks: verified lowering passes for `BIFROST_NO_FP32_TRANSCENDENTALS`, `BIFROST_LIMITED_CLPER`, attr `0x688`, and blend shaders in upstream Mesa (`FIND-026`).
- [x] Document kernel interface gap: upstream DRM Panfrost (`pan_kmod`) vs downstream ARM Kbase UK 11.27 (`FIND-027`).
- [x] Author comprehensive reports: `analysis/milestone-5-panfrost-gpu-id-audit.md`, `analysis/milestone-5-bifrost-isa-comparison.md`, `analysis/milestone-5-upstream-enablement-roadmap.md`.

---

## Milestone 6: postmarketOS Wayland DECON Backend & Bionic Bridge (Completed)
- [x] Implement freestanding Bionic-musl runtime shim (`src/bridge/bionic_musl_shim.c` -> `libbionic_musl_shim.so`) resolving the 6 core incompatibility classes (`__errno`, `_ctype_`, `__android_log_print`, `property_get/set`, Bionic FORTIFY checks, `ion_*` wrappers, `FIND-028`).
- [x] Implement standalone Direct-DECON presentation backend (`src/compositor/decon_backend.c` -> `libdecon_backend.so`) driving Window 3 IDMA 0 via 1040-byte `S3CFB_WIN_CONFIG` (`0x441046d1`) with double-buffered ION swapchain and compile-time static assertions (`FIND-029`).
- [x] Author and physically verify unit test suite `tests/bionic_shim_test` on Samsung Galaxy A30 hardware (all 6 verification tests passing).
- [x] Author and physically execute 120-frame simulated presentation loop (`tests/decon_compositor_test`) on physical EA8076 Super AMOLED screen, achieving locked **59.34 FPS** refresh rate, **16.61 ms** min frame time, **16.77 ms** max frame time (jitter < 0.16 ms), and clean system recovery (`FIND-029`).
- [x] Deliver Milestone 6 analysis reports:
  - `analysis/milestone-6-bionic-musl-bridge-implementation.md`
  - `analysis/milestone-6-decon-wayland-backend.md`
  - `analysis/milestone-6-results.md`

---

## Milestone 6A: Verify Native postmarketOS Integration & Runtime Audit (Completed)
- [x] Audit commit `11d771f` ELF metadata: verify that all binaries and libraries execute under Android's `/system/bin/linker64` with Bionic `libc.so` (`FIND-030`).
- [x] Document that zero tests have executed under native Alpine Linux / musl libc (`ld-musl-aarch64.so.1`).
- [x] Audit compatibility shim scope: verify that vendor `libGLES_mali.so` was loaded against Android system libraries, not against `libbionic_musl_shim.so` (`FIND-028`).
- [x] Analyze vendor DDK dynamic dependency barrier under pure musl (`libc++.so`, symbol versioning `LIBC_N/P/R`, `FIND-031`).
- [x] Define compositor integration boundary: document that `tests/decon_compositor_test` is a synthetic presentation benchmark, not an integrated Wayland compositor.
- [x] Author complete specification for native Weston output backend plugin (`weston-backend-decon`).
- [x] Partition test matrix into Android/Bionic (active physical target) vs Alpine/musl (unbooted target).
- [x] Deliver Milestone 6A analysis reports:
  - `analysis/milestone-6a-runtime-audit.md`
  - `analysis/milestone-6a-wayland-integration-gap.md`

---

## Milestone 6B: Real Weston Direct-DECON Backend Implementation & Validation (Completed)
- [x] Pin target compositor API to Weston 14.0.2 (`libweston-14`) matching Alpine Linux / postmarketOS edge/v3.20+.
- [x] Refactor `src/compositor/decon_backend.c` into modular functional blocks: separate ION allocation, CPU `mmap()`, optional EGLImage binding, and add injectable `ioctl_fn` function pointer for deterministic mock verification.
- [x] Implement complete Weston 14 backend module (`src/weston/backend-decon/decon-backend.so`) exporting `weston_backend_init`, `decon_output_create`, head/monitor topology registration with Samsung EA8076 Super AMOLED metadata (1080x2340@60Hz, 68x147mm), Pixman renderbuffer creation via `create_image_from_ptr`, and DECON Window 3 presentation dispatch (`FIND-032`).
- [x] Integrate DECON hardware retire fences with Wayland's main event loop (`wl_event_loop_add_fd`), replacing synchronous polling with asynchronous VSYNC-aligned presentation feedback (`FIND-033`).
- [x] Implement comprehensive 6-stage unit test harness (`tests/weston_backend_test.c`) validating configuration validation, head registration, output enable/disable lifecycle, presentation repaint, multi-frame loop, and error recovery (100% pass rate).
- [x] Re-verify underlying presentation engine on physical hardware (`R58M41M451D`), confirming steady-state 60 Hz cadence with sub-0.2 ms jitter and safe SurfaceFlinger recovery.
- [x] Create Alpine Linux / postmarketOS package recipe (`packaging/APKBUILD.weston-backend-decon`) and packaging documentation.
- [x] Deliver Milestone 6B technical report: `analysis/milestone-6b-weston-backend.md`.

---

## Milestone 6C: Weston Direct-DECON Integration Audit & Hardening (Completed)
- [x] Conduct deep audit of Weston 14.0.2 header boundaries: document private in-tree coupling of `backend.h`, `pixman-renderer.h`, and `pixel-formats.h`, and establish in-tree packaging requirement for postmarketOS (`FIND-034`).
- [x] Audit `decon-backend.so` ELF dynamic dependencies (`libweston-14.so.0`, `libpixman-1.so.0`, `libwayland-server.so.0`, `libc.so.6`) and verify single exported entry point `weston_backend_init`.
- [x] Author comprehensive 5-track test matrix (`analysis/milestone-6c-test-matrix.md`) strictly separating Source Verification, Compilation, Mock Testing, Real Weston Runtime, and Physical Display Scanout.
- [x] Expand unit test suite (`tests/weston_backend_test.c`) from 6 to 10 tests with active failure injection:
  - Test 7: Retire fence error/hangup (`WL_EVENT_ERROR`/`WL_EVENT_HANGUP`) sets `WP_PRESENTATION_FEEDBACK_INVALID`, closes descriptors, and continues presentation cleanly.
  - Test 8: Output destruction with pending in-flight fence safely removes event sources and closes descriptors with zero use-after-free.
  - Test 9: Stale fence protection guards against re-entrant repaints, verified via peer socket EOF detection.
  - Test 10: Swapchain allocation exhaustion during output initialization cleanly reclaims all handles and DMA-BUF descriptors via `decon_backend_teardown()`.
- [x] Achieve 100% pass rate (10/10 tests) on unit test suite.
- [x] Harden `src/compositor/decon_backend.c` against handle double-free faults and descriptor leaks during allocation failures.
- [x] Deliver Milestone 6C technical reports: `analysis/milestone-6c-integration-audit.md` and `analysis/milestone-6c-test-matrix.md`.

---

## Milestone 6D: In-Tree Weston Integration & Real Wayland-Client Presentation (Completed)
- [x] Pin exact Weston 14.0.2 source revision (`015b3b4d4c05da44a22349ea6e651d1a8f678c59`).
- [x] Integrate `backend-decon` in-tree into Weston 14.0.2 source tree (`scratch/weston/libweston/backend-decon/`).
- [x] Add `backend-decon` to Meson build targets, options, and frontend dispatcher (`frontend/main.c`, `frontend/config-helpers.c`).
- [x] Generate unified in-tree patch: `packaging/0001-weston-14-direct-decon-backend.patch` (59 KB, 14 files modified, 1597 insertions).
- [x] Deploy target userspace: set up Alpine Linux v3.21.8 ARM64 (`aarch64`) musl environment directly on physical Galaxy A30 storage.
- [x] Build complete Weston compositor and `decon-backend.so` natively for ARM64 using GCC 14.2.0 under `musl 1.2.5` (106 targets compiled and linked, 0 undefined symbols, linked strictly to `/lib/ld-musl-aarch64.so.1`).
- [x] Resolve output transform and scale matrix initialization timing bug in `libweston` (`decon_backend_output_configure`).
- [x] Validate mock event-loop execution under Alpine chroot (`weston --backend=decon --mock`).
- [x] Verify underlying hardware DECON engine baseline via standalone harness (`tests/decon_present_test`).
- [x] Execute live Wayland compositor session on hardware DECON Window 3 alongside real Wayland client `/usr/bin/weston-simple-shm`.
- [x] Verify continuous physical presentation on Samsung EA8076 Super AMOLED screen across 480+ consecutive hardware frames at 60.0 Hz VSYNC.
- [x] Verify non-blocking retire fence signaling (`win_data.fence`, fd 24, flags `0x1`) via Wayland event loop (`wl_event_loop_add_fd`).
- [x] Verify clean, glitch-free recovery of Android LineageOS 18.1 SurfaceFlinger and HWC2 services.
- [x] Deliver Milestone 6D technical reports:
  - `analysis/milestone-6d-build-matrix.md`
  - `analysis/milestone-6d-weston-runtime.md`
- [x] Record FIND-036 in `reverse-engineering/evidence-ledger.md`.

---

## Milestone 6E: Practical Wayland Desktop Validation & Input Integration (Completed)
- [x] Profile initial Wayland baseline and uncover per-frame file descriptor leak (~60 FDs/sec) on physical target hardware.
- [x] Reverse-engineer DECON `S3CFB_WIN_CONFIG_IOCTL` sync fence creation mechanics: identify that driver generates an in-flight Release Fence FD (`rel_fence`) at byte offset `0x18` of `struct decon_win_config` for Window 3 (`FIND-037`).
- [x] Update `src/compositor/decon_backend.h`, `src/compositor/decon_backend.c`, and `src/weston/backend-decon/decon.c` to track and immediately close `rel_fence >= 0`, eliminating kernel descriptor leakage (open FDs locked at 24–26 across thousands of frames).
- [x] Conduct comprehensive evdev input device discovery:
  - Discovered `/dev/input/event1` (`sec_touchscreen`, Melfas `mms_ts`, 10-point multitouch `ABS_MT_SLOT` 0..9, range 0..4095, `INPUT_PROP_DIRECT`).
  - Discovered `/dev/input/event14` (`gpio_keys`, Vol+/Vol-/Power).
  - Validated input events with `libinput record` inside Alpine Linux ARM64 chroot.
  - Documented `backend-decon` seat initialization architecture for libinput integration.
- [x] Validate real Wayland application suite on physical hardware:
  - Test 3A: `weston-simple-damage --use-damage-buffer` verified damage buffer tracking and hardware VSYNC pacing.
  - Test 3B: Multi-surface concurrency: `weston-simple-shm` and `weston-simple-damage` simultaneously composited by Pixman into double-buffered ION DMA-BUFs on DECON Window 3.
  - Test 3C: Dynamic surface lifecycle robustness: 3 sequential client spawn-and-destroy cycles while compositor remained running continuously (0 memory leaks, flat VmRSS 17.2 MB).
- [x] Execute continuous presentation soak benchmark:
  - Presented **over 1,920 consecutive hardware frames** during a 33.3-second continuous soak session on physical EA8076 Super AMOLED screen.
  - Confirmed zero memory leaks (VmRSS flat at 17,228 kB -> 17,232 kB, +4 kB single-page rounding).
  - Confirmed zero FD leaks (open FDs flat at 24 -> 26).
- [x] Independent hardware verification: recorded **1,995 physical panel scanout interrupts** on DECON GPIO IRQ 249 (`14860000.decon_f`), measuring an exact **59.91 Hz** hardware cadence.
- [x] High-resolution frame-by-frame performance profiling instrumented via `CLOCK_MONOTONIC`:
  - CPU Pixman compositing latency: **0.66 ms – 0.74 ms per frame** (<4.5% of 16.67 ms frame budget at 1080x2340 resolution).
  - DECON submission ioctl latency: **0.31 ms – 0.37 ms**.
  - Hardware retire fence interval: **16.67 ms ± 0.03 ms** locked to 60.0 Hz VSYNC.
  - Frame idle headroom: **15.60 ms (93.6%)**.
- [x] GPU acceleration feasibility research: audited `libhybris` vs Mesa Panfrost `pan_kmod_kbase`. Confirmed CPU Pixman at 0.72 ms provides immediate production viability.
- [x] Deliver Milestone 6E technical reports:
  - `analysis/milestone-6e-input-validation.md`
  - `analysis/milestone-6e-desktop-tests.md`
  - `analysis/milestone-6e-performance-baseline.md`
- [x] Record FIND-037 and FIND-038 in `reverse-engineering/evidence-ledger.md`.

---

## Milestone 7A: Standalone Linux Userspace Deployment, Input Integration, and Recovery Validation (Completed)
- [x] Capture reproducible baseline: kernel `4.4.302-p6-neyuKernel_legacy`, Alpine v3.21.8 ARM64 (`musl 1.2.5`), Weston 14.0.2, input device inventory (16 devices), mount layout, and recovery path.
- [x] Rootfs Deployment Strategy: Audited and compared Option A (Android chroot), Option B (Standalone userspace on 4.4 kernel), and Option C (Mainline postmarketOS). Selected Option A as safe reproducible development rootfs without flashing partition hazards (`analysis/milestone-7a-deployment.md`).
- [x] Dynamic Touchscreen Discovery (`FIND-039`): Identified `sec_touchscreen` via `INPUT_PROP_DIRECT` (`0x2`) and multitouch absolute axes `ABS_MT_POSITION_X`/`Y`/`SLOT` (`0x4263800000000000`), distinguishing it from `sec_touchpad` (`INPUT_PROP_POINTER`) and sensor hubs.
- [x] Embedded Seat Integration: Extended `backend-decon` with `udev_input_init` on `seat0` and direct device openers (`weston_launcher_open`/`close`) for standalone/chroot environments without `systemd-logind` or `seatd`.
- [x] Live Client Touch Interaction: Fullscreen `weston-simple-touch` verified live touch DOWN, MOTION, UP, and FRAME event delivery on physical EA8076 Super AMOLED screen at coordinates `(496.0, 68.6)` through `(564.5, 1290.5)`.
- [x] Hardware Key Policy: Audited `gpio_keys` (`/dev/input/event14`) and documented routing to platform services.
- [x] Compositor Lifecycle Persistence: Verified clean device detachment and re-initialization across compositor restarts.
- [x] Udev Autosuspend Disconnection Resolution: Disabled `/usr/lib/udev/rules.d/60-autosuspend.rules` and restricted udev triggers to input subsystem, preventing USB reset.
- [x] Stability Soak and Recovery Validation: Verified clean SIGTERM shutdown, zero memory leaks (flat at 17.3 MB), zero FD leaks (flat at 24–35), and guaranteed HWC2/SurfaceFlinger restoration.
- [x] GPU Acceleration Feasibility Audit (`FIND-040`): Audited `pan_kmod_kbase` vs `libhybris`. Confirmed `pan_kmod_kbase` is UNVERIFIED / CONCEPTUAL ONLY for Bifrost v6 (G71). Marked `libhybris` as feasible path for vendor DDK (`analysis/milestone-7-gpu-path-feasibility.md`).
- [x] Deliver Milestone 7A technical reports:
  - `analysis/milestone-7a-deployment.md`
  - `analysis/milestone-7a-input.md`
  - `analysis/milestone-7-gpu-path-feasibility.md`
- [x] Record FIND-039 and FIND-040 in `reverse-engineering/evidence-ledger.md`.

---

## Milestone 7B: Libhybris Bridge Implementation for Vendor Mali DDK (Completed)
- [x] Port and compile `libhybris` dynamic linker (`linker/q.so`) on Alpine ARM64 musl 1.2.5 (`FIND-041`).
- [x] Resolve musl ABI gaps: `R_AARCH64_IRELATIVE` relocation, largefile 64-bit offsets, recursive mutex layout `{{{1}}}`, `fpos_t` designated initializers, and modern 64-bit Bionic GOT relocations.
- [x] Hook 64 missing Bionic runtime symbols in `hooks.c` (`__cxa_thread_atexit`, FORTIFY bounds checking, ctype/locales, abort handling) to prevent execution of uninitialized Bionic `libc.so`.
- [x] Fix critical TLS pthread key drop bug (`if (!key) return NULL;`) in `_hybris_hook_pthread_getspecific()`, restoring key index 0 lookups for Mali DDK thread state.
- [x] Execute `test_dlopen /vendor/lib64/egl/libGLES_mali.so` across all 35+ dependent Android 11 system libraries (returned handle `0x6058c667`).
- [x] Enumerate 24 hardware EGL configurations exposing RGBA8888, RGB565, depth, stencil, and MSAA via `test_egl_configs`.
- [x] Validate headless offscreen EGL/GLES context creation under pure musl userspace via `tests/test_offscreen_mali.c`:
  - Verified `GL_VENDOR: ARM`, `GL_RENDERER: Mali-G71`, `GL_VERSION: OpenGL ES 3.2 v1.r26p0-01eac0`.
  - Executed hardware color clear; verified pixel readback (`glReadPixels`) with 0 LSB delta (`FIND-042`).
  - Recorded physical SoC hardware interrupts on GIC 190 (+134) and GIC 191 (+13) (`11500000.mali`).
- [x] Validate Exynos 7904 hardware Gralloc buffer allocation via `tests/test_gralloc.c`:
  - Allocated real 1080x2340 `HAL_PIXEL_FORMAT_RGBA_8888` ION DMA-BUF with stride 1088 (numFds=2).
  - Verified user-space buffer lock, memory pattern write, unlock, and release.
- [x] Bridge `libhybris-hwcomposerwindow.so` to presentation pipeline via `tests/test_hwc_window.c` (`FIND-043`):
  - Created `ANativeWindow` backed by `HWCNativeWindowCreate`.
  - Created `EGLSurface` with `eglCreateWindowSurface()`.
  - Rendered frame with Mali-G71 GPU and executed `eglSwapBuffers()`.
  - Present callback successfully delivered shared ION DMA-BUF FD (`fd[0]=12`) and physical GPU acquire fence FD (`fence=17`).
- [x] Deliver Milestone 7B technical report `analysis/milestone-7b-libhybris-bridge.md` and export patch `packaging/0002-libhybris-musl-bionic-bridge.patch`.
- [x] Record FIND-041, FIND-042, and FIND-043 in `reverse-engineering/evidence-ledger.md`.

---

## Milestone 7C: Mali GLES -> DMA-BUF -> DECON End-to-End Validation (Completed)
- [x] Conduct deep technical audit of `libhybris` musl/Bionic bridge: dynamic linker, relocation handling, symbol resolution, TLS/pthread data, recursive mutex assumptions, and library loading order (`analysis/milestone-7c-bridge-audit.md`).
- [x] Verify `R_AARCH64_IRELATIVE` (1032) GNU IFUNC resolution against AArch64 ELF ABI specification (`hwcap` + `__ifunc_arg_t`).
- [x] Verify `stdin`, `stdout`, `stderr` double-pointer indirection against modern Bionic 64-bit GOT relocations and translation via `_get_actual_fp()`.
- [x] Audit all added runtime hooks for ABI correctness, argument types, return values, and ownership.
- [x] Diagnose and resolve upstream EGL display mapping lifecycle defect (`FIND-044`): implemented `_removeMapping()` in `hybris/egl/egl.c` to prevent use-after-free and double-free during repeated `eglTerminate()` calls.
- [x] Author and pass 5/5 unit tests in `tests/test_bridge_audit.c` on target hardware: pthread key 0 storage, thread termination key destructors, 4 concurrent multithreaded rendering contexts, and 10 full cycles of repeated EGL initialization, rendering, and teardown.
- [x] Build and execute Mali-G71 rendering stability test suite (`tests/test_mali_stability.c`):
  - Validated clear colors across RGB/Alpha channels.
  - Validated colored vertex triangle interpolation.
  - Validated 2D texture generation, upload, and shader sampling across 4 quadrants.
  - Validated hardware alpha blending (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) against exact mathematical formula.
  - Validated Framebuffer Object (FBO) offscreen rendering and texture readback.
  - Rendered 120 sequential animated frames with zero GL errors.
  - Mathematically verified pixel readbacks with 0 LSB delta.
  - Measured physical GPU hardware interrupts on GIC 190 (+44) and GIC 191 (+25).
- [x] Formally document complete buffer and fence ownership matrix and synchronization lifecycle (`analysis/milestone-7c-buffer-fence-lifecycle.md`).
- [x] Diagnose and eliminate acquire sync fence file descriptor leak (`FIND-045`): adding `close(acquire_fence)` immediately following DECON ioctl eliminated a 60 FD/sec leak, keeping open descriptors rock-solid at 20 across thousands of frames.
- [x] Author and execute physical GPU-to-DECON presentation benchmark (`tests/mali_decon_e2e_test.c`):
  - Rendered animated GLES geometry directly into Gralloc ION DMA-BUFs (1080x2340 stride 1088).
  - Submitted frames directly to DECON Window 3 / IDMA channel 0 via `S3CFB_WIN_CONFIG_IOCTL` without any CPU blitting.
  - Consumed GPU acquire fence before scanout; fed composite retire fence back to Gralloc to guard buffer reuse.
  - Executed continuous 10-minute presentation soak test at an exact **59.96 FPS** average cadence (35,978 frames over 600.00s).
  - Recorded 1:1 correlation with physical panel VSYNC interrupts on IRQ 249 (`14860000.decon_f`).
  - Recorded continuous physical Mali GPU interrupts on GIC 190/191.
  - Verified flat memory RSS (24.2 MB) and flat open file descriptors (20) with zero resource leaks.
  - Confirmed safe Android display service stopping and restoration on demand.
- [x] Author Weston integration design for `gl-renderer` and `backend-decon` (`analysis/milestone-7c-weston-integration-design.md`).
- [x] Record FIND-044 and FIND-045 in `reverse-engineering/evidence-ledger.md`.
- [x] Deliver Milestone 7C analysis reports:
  - `analysis/milestone-7c-bridge-audit.md`
  - `analysis/milestone-7c-buffer-fence-lifecycle.md`
  - `analysis/milestone-7c-gpu-decon-test.md`
  - `analysis/milestone-7c-weston-integration-design.md`

---

## Milestone 7D: Weston Direct-DECON GL-Renderer Hardware Acceleration (Completed)
- [x] Connect `libhybris-hwcomposerwindow` to Weston 14.0.2 `gl-renderer` via `gl_renderer_interface->output_window_create()`.
- [x] Implement zero-copy presentation callback in `backend-decon` submitting GL-rendered ION DMA-BUFs directly to DECON Window 3 via `S3CFB_WIN_CONFIG_IOCTL`.
- [x] Implement dual-fence synchronization: GPU acquire fences synchronize DECON scanout; DECON composite retire fences pace Wayland event loop (`wl_event_loop_add_fd`) at 60 Hz.
- [x] Resolve upstream Weston EGL platform assertion abort on Android/surfaceless displays in `libweston/renderer-gl/egl-glue.c` (`FIND-046`).
- [x] Eliminate `HWCNativeWindow` shutdown double-free `SIGSEGV` by respecting libhybris reference counting (`FIND-047`).
- [x] Isolate Bionic struct definitions (`native_handle_t`, `ANativeWindowBuffer`) in `decon_internal.h` for clean musl compilation (`FIND-048`).
- [x] Implement 100 ms retire fence watchdog timer in `backend-decon` and establish Android power lock + `--idle-time=0` protection (`FIND-050`).
- [x] Validate multi-surface desktop composition with concurrent Wayland clients (`weston-desktop-shell`, `weston-simple-shm`, `weston-simple-damage`).
- [x] Execute continuous 10-minute presentation soak test on physical hardware at 59.90 FPS (35,940 frames) with zero memory/FD leaks.
- [x] Deliver Milestone 7D technical reports and patch updates:
  - `analysis/milestone-7d-weston-api-audit.md`
  - `analysis/milestone-7d-architecture-decision.md`
  - `analysis/milestone-7d-gl-renderer-implementation.md`
  - `analysis/milestone-7d-hardware-validation.md`
  - `packaging/0001-weston-14-direct-decon-backend.patch`
  - `packaging/0003-weston-14-direct-decon-gl-renderer.patch`

---

## Milestone 7E: Wayland Client GPU Buffer Import & Desktop Integration (Completed)
- [x] Investigate client-side buffer allocation architectures: proved downstream Exynos kernel 4.4 has no `/dev/dri/renderD*` nodes, making DRM/GBM `zwp_linux_dmabuf_v1` inoperable without Panfrost.
- [x] Integrate `libwayland-egl` via `libhybris` (`EGL_PLATFORM=wayland`) implementing the `android_wlegl` protocol extension (`FIND-051`).
- [x] Verify Weston 14 `gl-renderer` binding to `android_wlegl` via `eglBindWaylandDisplayWL()` and zero-copy `EGLImage` import (`EGL_WAYLAND_BUFFER_WL` -> `EGL_ANDROID_image_native_buffer`) backed by Gralloc ION DMA-BUFs without intermediate CPU copies.
- [x] Validate live hardware-accelerated GLES Wayland client: `weston-simple-egl` running on Mali-G71 GPU at **59.80 FPS** steady state.
- [x] Execute concurrent multi-surface desktop compositing benchmark: simultaneously composited GPU GLES client (`weston-simple-egl`), CPU SHM client (`weston-simple-shm`), and desktop shell across **660 hardware frames** at a locked **60.00 FPS cadence** (1.65–4.86 ms repaint latency, +6,911 GPU interrupts, +784 DECON VSYNC IRQs, flat 90.1 MB RSS, flat 55 open FDs).
- [x] Author comprehensive technical evaluation of Standalone Direct Boot (Option B): specified initramfs switchroot sequence, mounting `/dev/block/by-name/userdata` or microSD, binding Android vendor/system partitions for libhybris, reducing idle RAM from ~1,850 MB to ~120 MB, achieving <5s boot times, and eliminating Android power manager blanking conflicts with guaranteed TWRP / Odin recovery safety (`analysis/milestone-7e-client-gpu-acceleration.md`).
- [x] Deliver Milestone 7E technical report: `analysis/milestone-7e-client-gpu-acceleration.md`.
- [x] Record FIND-051 in `reverse-engineering/evidence-ledger.md`.

---

## Milestone 8: postmarketOS Packaging & Standalone Direct Boot Deployment (Next)
- [ ] Author APKBUILD recipes for `libhybris-musl` and `weston-backend-decon` for Alpine / postmarketOS.
- [ ] Build custom `boot.img` with postmarketOS initramfs switchroot script to boot directly into Alpine userspace.
- [ ] Validate standalone cold-boot to Weston desktop on hardware without Android framework running.
- [ ] Evaluate long-term upstream path: Exynos 7904 DECON DRM driver and Panfrost Bifrost v6 support on Linux 6.x.

---

## Verification Gates & Quality Rules

1. **Compilation != Functional**: A driver or test utility that compiles is not considered working until GPU registers change state, interrupts fire, and rendered output matches ground truth.
2. **No Blind Submissions**: Do not submit experimental job descriptors to a live GPU without verifying ringbuffer bounds and atom layout.
3. **Reproducibility**: Every finding must be reproducible with documented shell commands or standalone scripts.


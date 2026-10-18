# Project Status: Mali-G71 Open GPU Driver for postmarketOS

- **Current Date**: 2026-10-18
- **Active Phase**: Phase 2, 3 & 4 — **Milestones 3A, 3B, 4A, 4B, and 4C VERIFIED**. Moving to Phase 4 (Mesa Panfrost G71 Gap Analysis & Upstream Enablement Roadmap).
- **Device Target**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **GPU Target**: ARM Mali-G71 MP2 (2 cores, r0p0, GPU ID `0x60A0`, Bifrost v6)
- **Kernel Target**: Linux 4.4.302-p6 (`neyuKernel_legacy`) downstream with `mali_kbase r26p0`

---

## 1. Executive Summary

Research infrastructure, target verification, kernel ABI mapping, offscreen 3D hardware acceleration, display presentation architecture, shared DMA-BUF render-target pipelines, **direct physical scanout on the EA8076 Super AMOLED panel**, and **continuous 60 FPS double-buffered presentation** are fully established and physically verified on the Samsung Galaxy A30 (`SM-A305F`).

**Milestone 3A (Proprietary EGL Offscreen Validation) — 100% physically verified**:
1. Standalone `kbase_probe` verified low-level kernel handshake (UK 11.27), MTP page mapping (`0x3000`), context allocation, and memory management.
2. `egl_offscreen_test` verified headless EGL 1.4 initialization, Pbuffer allocation, OpenGL ES 2.0/3.2 context creation, physical Mali-G71 GPU identification, 3D shader pipeline rendering, independent kernel interrupt generation (Mali Job IRQ delta = +2), and dual-zone pixel readback math verification.
3. Complete ELF dependency mapping of all 621 relocations / 391 unique symbols across Bionic libc vs musl libc documented.

**Milestone 3B (Display Presentation & Buffer-Sharing Investigation) — 100% physically verified**:
1. Physical interrogation of `/dev/graphics/fb0` (`tests/fb_probe`) revealed exact hardware parameters: 1080x2340 resolution, 4320 bytes stride, and native BGRA8888 channel ordering (`FIND-011`).
2. Proved that conventional FBDev `mmap()` is unsupported (`-EINVAL` / -22) because DECON does not allocate static scanout RAM (`FIND-012`).
3. Disassembled and mapped the DECON multi-window presentation ioctl `S3CFB_WIN_CONFIG` (`0x441046d1`, size 1040) used by `libexynosdisplay.so` and HWC2 (`FIND-013`).
4. Proved that `mali_kbase` is strictly an import-only driver (`0xc0188016`) without DMA-BUF export capability (`FIND-014`). Universal buffer sharing is mediated by Exynos ION (`/dev/ion`, `libion_exynos.so`) (`FIND-015`).
5. Built and physically executed `tests/fb_present_test`, rendering offscreen EGL pattern on Mali-G71, adapting RGBA to native BGRA8888, verifying scanout safety, and saving scanout-ready test frame.

**Milestone 4A (End-to-End DMA-BUF Presentation Prototype) — 100% physically verified**:
1. Directly allocated physical backing memory from `ion_noncontig_heap` (Heap Mask 1, flags 0) and exported Linux DMA-BUF file descriptor (`dma_buf_fd = 4`) via `ION_IOC_SHARE` (`FIND-016`).
2. Verified direct low-level kernel import into `mali_kbase` via `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`), successfully assigning GPU Virtual Address `0x41000` with flags `0x500f`.
3. Created `EGLImageKHR` from the DMA-BUF descriptor via `EGL_EXT_image_dma_buf_import` (`EGL_LINUX_DMA_BUF_EXT`, `DRM_FORMAT_ABGR8888`), bound to `GL_TEXTURE_2D`, and attached to FBO (`GL_FRAMEBUFFER_COMPLETE` = `0x8CD5`) (`FIND-017`).
4. Executed 3D hardware GPU clear rendering to Lime Green `RGBA(0, 255, 64, 255)` and flushed with `glFinish()`.
5. Mapped the DMA-BUF descriptor directly into CPU memory via `mmap()` (independent of OpenGL) and verified raw pixel bytes (`[0]=0x00, [1]=0xff, [2]=0x40, [3]=0xff`), proving zero-copy 3D hardware rendering into shared memory.
6. Analyzed DECON multi-window presentation arbitration (`S3CFB_WIN_CONFIG`), documented the HWC2 race condition boundary, and established the standalone postmarketOS presentation architecture (`FIND-018`).

**Milestone 4B (Standalone DECON Presentation Prototype) — 100% physically verified**:
1. Proved conclusively via disassembly of `libGLES_mali-64.so` and kernel headers that `BASE_MEM_IMPORT_TYPE_UMM` is `2` (`FIND-019`).
2. Reverse-engineered the complete 1040-byte `S3CFB_WIN_CONFIG` ABI: 8-byte header, 5 windows of 200 bytes each, mapping exact coordinate and control offsets down to single bytes (`FIND-020`).
3. Discovered that on Exynos 7904 DECON-0, IDMA channel 0 is exclusively dedicated to Window 3; Window 0 cannot bind to IDMA 0 (`FIND-021`).
4. Built `tests/decon_present_test` executing under a verified recovery sandbox with SurfaceFlinger stopped. Successfully rendered full-screen 1080x2340 calibration pattern on Mali-G71 GPU, presented to DECON Window 3, signaled hardware retire fence (`revents = 0x1`), physically displayed image on the EA8076 Super AMOLED screen for 5 seconds, and restored Android UI cleanly (`FIND-022`).

**Milestone 4C (postmarketOS Integration & Double-Buffered Swapchain) — 100% physically verified**:
1. Conducted exhaustive Bionic vs musl libc ABI divergence and symbol hook analysis (`analysis/milestone-4c-bionic-musl-bridge.md`).
2. Evaluated four presentation models for postmarketOS, establishing the native direct-DECON Wayland compositor backend as the optimal low-overhead, containerless architecture (`analysis/milestone-4c-compositor-architecture.md`).
3. Built `tests/decon_flip_test` implementing a ping-pong double-buffered swapchain across 2 full-screen ION DMA-BUFs with dual EGLImage FBOs.
4. Executed live continuous presentation benchmark: rendered 60 consecutive dynamic animated frames on Mali-G71 GPU, presented to DECON Window 3, synchronized via retire fences, achieving locked **59.89 FPS** with steady-state frame times of **16.62 to 16.69 ms** matching the 60.0 Hz VSYNC cycle (`FIND-023`, `FIND-024`).

---

## 2. Checkpoint Summary

| Milestone | Status | Key Output / Evidence |
| :--- | :--- | :--- |
| **Hardware & Environment Inventory** | **VERIFIED** | `HARDWARE.md`, sysfs `gpuinfo` confirms Mali-G71 MP2 r0p0 `0x60A0`. |
| **Artifact Ledger & Hashes** | **VERIFIED** | `artifacts/MANIFEST.md`, SHA-256 hashes recorded for all binaries + FDT + kernel config. |
| **Evidence Ledger** | **VERIFIED** | `reverse-engineering/evidence-ledger.md`, Findings FIND-001 through FIND-024 verified. |
| **Kbase IOCTL Interface Mapping** | **VERIFIED** | `reverse-engineering/ioctl-analysis/kbase-ioctls.md`, 22 ioctls mapped from raw ARM64 machine code. |
| **Architecture Strategy Decision** | **COMPLETED** | `architecture/ADR-001-graphics-stack-feasibility.md`. |
| **Direct Kernel Probe (`kbase_probe`)** | **VERIFIED** | `tests/kbase_probe`, UK ABI 11.27 handshake, MTP mapping, Alloc, Free (FIND-007, FIND-008). |
| **ELF Dependency Analysis** | **COMPLETED** | `analysis/milestone-3a-elf-dependencies.md`, cataloged 391 symbols, 8 DT_NEEDED, Bionic vs Musl. |
| **EGL Offscreen Validation (Milestone 3A)** | **VERIFIED** | `tests/egl_offscreen_test`, EGL 1.4, Pbuffer 64x64, GLES 3.2, IRQ delta +2 (FIND-009, FIND-010). |
| **Display Path Analysis (Milestone 3B)** | **VERIFIED** | `analysis/milestone-3b-display-path.md`, `tests/fb_probe` (FIND-011, FIND-012, FIND-013). |
| **Buffer-Sharing Architecture (Milestone 3B)** | **VERIFIED** | `analysis/milestone-3b-buffer-sharing.md`, ION DMA-BUF & Kbase import (FIND-014, FIND-015). |
| **postmarketOS Compatibility Boundary** | **COMPLETED** | `analysis/milestone-3b-postmarketos-compatibility.md`. |
| **Presentation Prototype (`fb_present_test`)**| **VERIFIED** | `tests/fb_present_test`, offscreen render, RGBA->BGRA8888 conversion, scanout safety verified. |
| **ION DMA-BUF Allocation (Milestone 4A)** | **VERIFIED** | `analysis/milestone-4a-ion-allocation.md`, `tests/ion_mali_import_test` (FIND-016). |
| **Shared Render-Target (Milestone 4A)** | **VERIFIED** | `analysis/milestone-4a-shared-render-target.md`, FBO Complete, CPU mmap readback (FIND-017). |
| **DECON Presentation Architecture (4A)** | **COMPLETED** | `analysis/milestone-4a-decon-presentation.md`, `S3CFB_WIN_CONFIG` mapping & HWC2 safety (FIND-018). |
| **ABI Audit (Milestone 4B)** | **VERIFIED** | `analysis/milestone-4b-abi-audit.md`, UMM type 2 proof, 200B window layout (FIND-019, FIND-020). |
| **Safety Plan (Milestone 4B)** | **VERIFIED** | `analysis/milestone-4b-safety-plan.md`, HWC teardown/recovery, ADB persistence. |
| **Physical Presentation (Milestone 4B)** | **VERIFIED** | `analysis/milestone-4b-results.md`, `tests/decon_present_test`, retire fence, AMOLED output (FIND-021, FIND-022). |
| **Bionic-Musl Bridge Analysis (4C)** | **COMPLETED** | `analysis/milestone-4c-bionic-musl-bridge.md`, hooks and dynamic linking mechanics. |
| **Compositor Architecture (Milestone 4C)**| **COMPLETED** | `analysis/milestone-4c-compositor-architecture.md`, standalone DECON Wayland backend model. |
| **Double-Buffered Flip Benchmark (4C)** | **VERIFIED** | `analysis/milestone-4c-results.md`, `tests/decon_flip_test`, 59.89 FPS locked VSYNC (FIND-023, FIND-024). |

---

## 3. Current Blockers & Challenges

1. **Absence of Mainline DRM in Downstream Kernel**:
   - The active kernel config disables DRM (`# CONFIG_DRM is not set`) in favor of Samsung's legacy DECON FB (`/dev/fb0`).
   - Mitigation: Standalone DECON presentation is now verified functional at locked 60 FPS; Wayland compositors under postmarketOS can drive DECON via a direct `decon-backend.so` without needing DRM/KMS.
2. **Alpine libc Compatibility (musl vs Bionic)**:
   - Proprietary libraries are compiled against Android Bionic libc.
   - Mitigation: Solved by either compiling the compositor against Bionic or using `libhybris` symbol translation.
3. **Mali-G71 Support Gap in Mesa Panfrost**:
   - Mesa documentation explicitly notes G71 is unsupported in Panfrost despite G72/G52 having support.

---

## 4. Next Concrete Tasks

1. **TASK-001 (Phase 4 / Milestone 5)**: Audit Mesa Panfrost Bifrost v6 (G71, GPU ID `0x60A0`) compiler & driver gaps to create the upstream enablement patch roadmap.
2. **TASK-002 (Phase 4 / Milestone 5)**: Compare Mali-G71 ISA encoding (v6.0) with Mali-G72 (v6.2) and document missing quirks, blend shaders, and register banking differences.



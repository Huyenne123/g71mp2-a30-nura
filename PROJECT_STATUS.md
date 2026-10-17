# Project Status: Mali-G71 Open GPU Driver for postmarketOS

- **Current Date**: 2026-10-17
- **Active Phase**: Phase 2 & 3 — **Milestone 3A & 3B VERIFIED**. Moving to Milestone 4A (postmarketOS Integration / Libhybris Architecture) & Phase 4 (Mesa Panfrost Gap Analysis).
- **Device Target**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **GPU Target**: ARM Mali-G71 MP2 (2 cores, r0p0, GPU ID `0x60A0`, Bifrost v6)
- **Kernel Target**: Linux 4.4.302-p6 (`neyuKernel_legacy`) downstream with `mali_kbase r26p0`

---

## 1. Executive Summary

Research infrastructure, target verification, kernel ABI mapping, offscreen 3D hardware acceleration, and display presentation architecture are fully established and physically verified on the Samsung Galaxy A30 (`SM-A305F`).

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

---

## 2. Checkpoint Summary

| Milestone | Status | Key Output / Evidence |
| :--- | :--- | :--- |
| **Hardware & Environment Inventory** | **VERIFIED** | `HARDWARE.md`, sysfs `gpuinfo` confirms Mali-G71 MP2 r0p0 `0x60A0`. |
| **Artifact Ledger & Hashes** | **VERIFIED** | `artifacts/MANIFEST.md`, SHA-256 hashes recorded for all binaries + FDT + kernel config. |
| **Evidence Ledger** | **VERIFIED** | `reverse-engineering/evidence-ledger.md`, Findings FIND-001 through FIND-015 verified. |
| **Kbase IOCTL Interface Mapping** | **VERIFIED** | `reverse-engineering/ioctl-analysis/kbase-ioctls.md`, 22 ioctls mapped from raw ARM64 machine code. |
| **Architecture Strategy Decision** | **COMPLETED** | `architecture/ADR-001-graphics-stack-feasibility.md`. |
| **Direct Kernel Probe (`kbase_probe`)** | **VERIFIED** | `tests/kbase_probe`, UK ABI 11.27 handshake, MTP mapping, Alloc, Free (FIND-007, FIND-008). |
| **ELF Dependency Analysis** | **COMPLETED** | `analysis/milestone-3a-elf-dependencies.md`, cataloged 391 symbols, 8 DT_NEEDED, Bionic vs Musl. |
| **EGL Offscreen Validation (Milestone 3A)** | **VERIFIED** | `tests/egl_offscreen_test`, EGL 1.4, Pbuffer 64x64, GLES 3.2, IRQ delta +2 (FIND-009, FIND-010). |
| **Display Path Analysis (Milestone 3B)** | **VERIFIED** | `analysis/milestone-3b-display-path.md`, `tests/fb_probe` (FIND-011, FIND-012, FIND-013). |
| **Buffer-Sharing Architecture (Milestone 3B)** | **VERIFIED** | `analysis/milestone-3b-buffer-sharing.md`, ION DMA-BUF & Kbase import (FIND-014, FIND-015). |
| **postmarketOS Compatibility Boundary** | **COMPLETED** | `analysis/milestone-3b-postmarketos-compatibility.md`. |
| **Presentation Prototype (`fb_present_test`)**| **VERIFIED** | `tests/fb_present_test`, offscreen render, RGBA->BGRA8888 conversion, scanout safety verified. |
| **Test Documentation** | **COMPLETED** | `tests/README.md`. |

---

## 3. Current Blockers & Challenges

1. **Absence of Mainline DRM in Downstream Kernel**:
   - The active kernel config disables DRM (`# CONFIG_DRM is not set`) in favor of Samsung's legacy DECON FB (`/dev/fb0`).
   - Impact: Standard Wayland compositors requiring `/dev/dri/card0` cannot run without a framebuffer/HWC backend or kernel DRM port.
2. **Alpine libc Compatibility (musl vs Bionic)**:
   - Proprietary libraries are compiled against Android Bionic libc.
   - Mitigation: Requires `libhybris` or standalone Bionic loader wrapper to run in postmarketOS.
3. **Mali-G71 Support Gap in Mesa Panfrost**:
   - Mesa documentation explicitly notes G71 is unsupported in Panfrost despite G72/G52 having support.

---

## 4. Next Concrete Tasks

1. **TASK-001 (Milestone 4A)**: Design minimal `libhybris` compatibility bridge or Bionic micro-container for Alpine Linux / postmarketOS.
2. **TASK-002 (Milestone 4B)**: Implement DECON presentation bridge binding ION DMA-BUFs with `S3CFB_WIN_CONFIG` for standalone scanout without SurfaceFlinger.
3. **TASK-003 (Phase 4)**: Audit Mesa Panfrost Bifrost v6 (G71) compiler & driver gaps to create the upstream enablement patch roadmap.



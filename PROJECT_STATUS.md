# Project Status: Mali-G71 Open GPU Driver for postmarketOS

- **Current Date**: 2026-10-17
- **Active Phase**: Phase 2 (Systematic Binary Analysis) & Phase 3 (Kernel ABI & Memory Management) — **Milestone 3A (Proprietary EGL Offscreen Validation) VERIFIED**. Moving to Milestone 3B (Display Integration & Buffer Sharing).
- **Device Target**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **GPU Target**: ARM Mali-G71 MP2 (2 cores, r0p0, GPU ID `0x60A0`, Bifrost v6)
- **Kernel Target**: Linux 4.4.302-p6 (`neyuKernel_legacy`) downstream with `mali_kbase r26p0`

---

## 1. Executive Summary

Research infrastructure, target verification, kernel ABI mapping, and offscreen 3D hardware acceleration are fully established and physically verified on the Samsung Galaxy A30 (`SM-A305F`).

All primary proprietary binaries (`libGLES_mali` 64-bit and 32-bit, `gralloc`, `hwcomposer`) are extracted and cataloged. Critical finding: **`libGLES_mali.so` is unstripped, containing over 191,000 intact symbols and DWARF debug info**, dramatically reducing reverse-engineering friction.

The complete kernel ioctl table for the downstream `mali_kbase` (r26p0) driver has been disassembled, mapped, and verified from machine code. 

**Milestone 3A (Proprietary EGL Offscreen Validation) has been 100% physically verified**:
1. Standalone `kbase_probe` verified low-level kernel handshake (UK 11.27), MTP page mapping (`0x3000`), context allocation, and memory management.
2. `egl_offscreen_test` verified headless EGL 1.4 initialization, Pbuffer allocation, OpenGL ES 2.0/3.2 context creation, physical Mali-G71 GPU identification, 3D shader pipeline rendering (clear + red triangle), independent kernel interrupt generation (Mali Job IRQ delta = +2), and dual-zone pixel readback math verification.
3. Complete ELF dependency mapping of all 621 relocations / 391 unique symbols across Bionic libc vs musl libc documented.

An Architecture Decision Record (`ADR-001`) has been produced, recommending a dual-track strategy: immediate hardware acceleration on postmarketOS via the verified downstream `mali_kbase` interface (Option B), while simultaneously conducting gap analysis on upstream Mesa Panfrost (Option A).

---

## 2. Checkpoint Summary

| Milestone | Status | Key Output / Evidence |
| :--- | :--- | :--- |
| **Hardware & Environment Inventory** | **VERIFIED** | `HARDWARE.md`, sysfs `gpuinfo` confirms Mali-G71 MP2 r0p0 `0x60A0`. |
| **Artifact Ledger & Hashes** | **VERIFIED** | `artifacts/MANIFEST.md`, SHA-256 hashes recorded for all binaries + FDT + kernel config. |
| **Evidence Ledger** | **VERIFIED** | `reverse-engineering/evidence-ledger.md`, Findings FIND-001 through FIND-010 verified. |
| **Kbase IOCTL Interface Mapping** | **VERIFIED** | `reverse-engineering/ioctl-analysis/kbase-ioctls.md`, 22 ioctls mapped from raw ARM64 machine code. |
| **Architecture Strategy Decision** | **COMPLETED** | `architecture/ADR-001-graphics-stack-feasibility.md`. |
| **Direct Kernel Probe (`kbase_probe`)** | **VERIFIED** | `tests/kbase_probe`, UK ABI 11.27 handshake, MTP mapping, Alloc, Free (FIND-007, FIND-008). |
| **ELF Dependency Analysis** | **COMPLETED** | `analysis/milestone-3a-elf-dependencies.md`, cataloged 391 symbols, 8 DT_NEEDED, Bionic vs Musl. |
| **EGL Test Plan & Methodology** | **COMPLETED** | `analysis/milestone-3a-test-plan.md`, dual-zone verification, IRQ monitoring. |
| **EGL Offscreen Validation (Milestone 3A)** | **VERIFIED** | `tests/egl_offscreen_test`, EGL 1.4, Pbuffer 64x64, GLES 3.2, IRQ delta +2, readback matched (FIND-009, FIND-010). |
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

1. **TASK-001 (Milestone 3B)**: Disassemble and document buffer sharing interfaces: Exynos ION (`/dev/ion`), Android Gralloc (`gralloc-64.so`), and DMA-BUF export (`KBASE_IOCTL_MEM_IMPORT`).
2. **TASK-002 (Milestone 3B)**: Evaluate display presentation paths on downstream kernel: DECON FBDev direct blit (`/dev/fb0`) vs Android Hardware Composer (`hwcomposer-64.so`).
3. **TASK-003**: Investigate minimal `libhybris` or lightweight Bionic loader build for Alpine Linux / postmarketOS to execute the verified EGL stack outside Android.
4. **TASK-004**: Audit Mesa's `src/panfrost/compiler/` and `src/panfrost/lib/` to compile the specific feature matrix of implemented vs missing G71 capabilities.


# Project Status: Mali-G71 Open GPU Driver for postmarketOS

- **Current Date**: 2026-10-17
- **Active Phase**: Phase 0 (Hardware & Environment Identification) & Phase 1 (Research Infrastructure) — **Completed**. Moving into Phase 2 & 3.
- **Device Target**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **GPU Target**: ARM Mali-G71 MP2 (2 cores, r0p0, GPU ID `0x60A0`, Bifrost v6)
- **Kernel Target**: Linux 4.4.302-p6 (`neyuKernel_legacy`) downstream with `mali_kbase r26p0`

---

## 1. Executive Summary

Research infrastructure and target verification are established. Direct physical access to the Galaxy A30 was verified via a rooted ADB session. 

All primary proprietary binaries (`libGLES_mali` 64-bit and 32-bit, `gralloc`, `hwcomposer`) are extracted and cataloged. Critical finding: **`libGLES_mali.so` is unstripped, containing over 191,000 intact symbols and DWARF debug info**, dramatically reducing reverse-engineering friction.

The complete kernel ioctl table for the downstream `mali_kbase` (r26p0) driver has been disassembled, mapped, and verified from machine code. 

An Architecture Decision Record (`ADR-001`) has been produced, recommending a dual-track strategy: immediate hardware acceleration on postmarketOS via the verified downstream `mali_kbase` interface (Option B), while simultaneously conducting gap analysis on upstream Mesa Panfrost (Option A).

---

## 2. Checkpoint Summary

| Milestone | Status | Key Output / Evidence |
| :--- | :--- | :--- |
| **Hardware & Environment Inventory** | **VERIFIED** | `HARDWARE.md`, sysfs `gpuinfo` confirms Mali-G71 MP2 r0p0 `0x60A0`. |
| **Artifact Ledger & Hashes** | **VERIFIED** | `artifacts/MANIFEST.md`, SHA-256 hashes recorded for all 6 binaries + FDT + kernel config. |
| **Evidence Ledger** | **VERIFIED** | `reverse-engineering/evidence-ledger.md`, Findings FIND-001 through FIND-006 verified. |
| **Kbase IOCTL Interface Mapping** | **VERIFIED** | `reverse-engineering/ioctl-analysis/kbase-ioctls.md`, 22 ioctls mapped from raw ARM64 machine code. |
| **Architecture Strategy Decision** | **COMPLETED** | `architecture/ADR-001-graphics-stack-feasibility.md`. |
| **Standalone Userspace Memory Test** | **IN PROGRESS** | Developing standalone test utility to open `/dev/mali0`, negotiate protocol 11.27, and allocate GPU memory. |

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

1. **TASK-001**: Build and execute an offline/sandboxed C test client to verify the `kbase` protocol handshake (`0xc0048000`) and memory allocation (`0xc0208005`) against `/dev/mali0` on the target device.
2. **TASK-002**: Disassemble `base_jd_atom_v2` and atom submission routines to document job chain formatting and GPU ringbuffer submission.
3. **TASK-003**: Pull supporting vendor libraries (`libion_exynos.so`, `libacryl.so`, `libexynosdisplay.so`) from target hardware to complete the display and buffer-sharing picture.
4. **TASK-004**: Audit Mesa's `src/panfrost/compiler/` and `src/panfrost/lib/` to compile the specific feature matrix of implemented vs missing G71 capabilities.

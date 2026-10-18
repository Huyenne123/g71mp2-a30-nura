# Milestone 5: Upstream Enablement Roadmap & Architectural Strategy

**Project**: Hardware-Accelerated Graphics for Samsung Galaxy A30 on postmarketOS  
**Target Hardware**: Samsung Galaxy A30 (`SM-A305F`, Exynos 7904, Mali-G71 MP2)  
**Evaluated Paths**: Downstream DDK Presentation, Mesa `pan_kmod_kbase`, Mainline DRM/KMS  

---

## 1. Executive Summary

Milestone 5 completed an end-to-end architectural gap analysis across:
1. **The physical hardware**: Mali-G71 MP2 `r0p0` (`0x60A00000`) on Exynos 7904.
2. **The downstream vendor stack**: `mali_kbase` r26p0 (UK 11.27), `libGLES_mali-64.so`, and Samsung DECON FBDev (`/dev/fb0`, Window 3 IDMA 0).
3. **The upstream open-source stack**: Mesa Panfrost (Bifrost v6 compiler, `v6.xml` command stream, `pan_model.c`) and Linux DRM Panfrost (`drivers/gpu/drm/panfrost/panfrost_gpu.c`).

### The Foundational Insight
**Upstream Mesa Panfrost does not have a shader compiler or hardware descriptor gap for Mali-G71.**
Mesa's Bifrost compiler already contains complete lowering passes for all G71 silicon quirks (`BIFROST_NO_FP32_TRANSCENDENTALS`, `BIFROST_LIMITED_CLPER`, position attribute identity `0x688`, and blend shaders).

The **sole technical divergence** lies in the kernel driver boundary:
- Upstream Mesa Panfrost expects the **Linux DRM subsystem** (`/dev/dri/renderD128` via `pan_kmod_panfrost`).
- The downstream kernel provides only the **ARM Kbase subsystem** (`/dev/mali0` via UK 11.27 ioctls) with `CONFIG_DRM` disabled.

---

## 2. Strategic Pathways: Comparative Decision Matrix

```
                      +------------------------------------------+
                      | Hardware Acceleration Pathways for A30   |
                      +------------------------------------------+
                                           |
         +---------------------------------+---------------------------------+
         |                                 |                                 |
         v                                 v                                 v
+------------------+             +-------------------+             +-------------------+
|  PATH A: DDK     |             |  PATH B: Mesa     |             |  PATH C: Mainline |
|  Direct-DECON    |             |  pan_kmod_kbase   |             |  DRM/KMS + Panfrost
+------------------+             +-------------------+             +-------------------+
| - Proprietary DDK|             | - Open-source Mesa|             | - Open-source Mesa|
| - Downstream 4.4 |             | - Downstream 4.4  |             | - Mainline 6.x    |
| - DECON Window 3 |             | - Kbase ioctl shim|             | - Exynos DRM      |
| - 59.89 FPS VSYNC|             | - Direct DECON    |             | - Standard KMS    |
| - Ready NOW      |             | - 2-3 Weeks Eng.  |             | - 6-12 Months Eng.|
+------------------+             +-------------------+             +-------------------+
```

### Detailed Pathway Comparison

| Evaluation Metric | Path A: Direct-DECON DDK Bridge | Path B: Mesa `pan_kmod_kbase` | Path C: Mainline DRM/KMS |
|:---|:---|:---|:---|
| **Userspace Driver** | Proprietary `libGLES_mali-64.so` | Open-Source Mesa Panfrost | Open-Source Mesa Panfrost |
| **Kernel Driver** | Downstream `mali_kbase` r26p0 | Downstream `mali_kbase` r26p0 | Upstream `drivers/gpu/drm/panfrost` |
| **Display Interface** | DECON Window 3 (`/dev/fb0`) | DECON Window 3 (`/dev/fb0`) | Standard DRM/KMS (`/dev/dri/card0`) |
| **Linux Distro Compatibility** | postmarketOS via Bionic Bridge | postmarketOS Native musl | postmarketOS Native musl |
| **Measured Performance** | **59.89 FPS** (Hardware locked) | Estimated 55-60 FPS | Estimated 50-60 FPS |
| **Engineering Effort** | **1-2 weeks** (Bridge wrapping) | **3-4 weeks** (Kmod backend) | **6-12 months** (SoC bring-up) |
| **Stability Risk** | Low (Proven in Milestone 4C) | Medium (New Kmod translation) | High (Board bring-up blockers) |
| **Upstream Alignment** | Pragmatic device port | Upstreamable Mesa backend | Pure mainline Linux |

---

## 3. Deep Dive: The Three Pathways

### 3.1 Path A: Direct-DECON Proprietary DDK Bridge (Recommended Immediate Path)

#### Architectural Description
Path A builds directly upon the breakthroughs established in Milestones 3A through 4C:
1. `libGLES_mali-64.so` is loaded into a postmarketOS userspace process using a lightweight Bionic-musl dynamic linking shim (`analysis/milestone-4c-bionic-musl-bridge.md`).
2. The Wayland compositor (Weston or a custom TinyWL backend) initializes headless EGL 1.4 on the Mali GPU.
3. Frames are rendered into ping-pong double-buffered ION DMA-BUFs (`tests/decon_flip_test.c`).
4. Buffers are submitted directly to DECON Window 3 via `S3CFB_WIN_CONFIG` (`0x441046d1`) and synchronized using physical hardware retire fences.

#### Outstanding Implementation Tasks
1. Package `libbionic_musl_shim.so` intercepting missing Bionic exports (`__aeabi_*`, `__sF`, TLS structure offsets).
2. Author `backend-decon.so` plugin for Weston or `wlroots` that encapsulates the 200-byte `S3CFB_WIN_CONFIG` Window 3 scanout engine.
3. Create postmarketOS APK package `device-samsung-a30-gpu`.

---

### 3.2 Path B: Mesa Panfrost Kbase Backend (`pan_kmod_kbase`) (Mid-Term Open Path)

#### Architectural Description
In Mesa, all kernel interactions are isolated behind `struct pan_kmod_ops` (`src/panfrost/lib/kmod/pan_kmod.h`).
Path B implements a new backend: `src/panfrost/lib/kmod/kbase_kmod.c`.

#### Interface Translation Mapping

| Panfrost Kmod Op (`struct pan_kmod_ops`) | Downstream Kbase Equivalent (`/dev/mali0`) | ABI Details |
|:---|:---|:---|
| `dev_create(fd)` | Handshake + MTP Setup | `KBASE_IOCTL_VERSION_CHECK` (`11.27`) + `mmap(0x3000)` |
| `dev_query_user_va_range()` | Query MMU VA bits | `KBASE_IOCTL_GET_GPUPROPS` -> 48-bit address space |
| `bo_alloc(size, flags)` | GPU memory allocation | `KBASE_IOCTL_MEM_ALLOC` (`0xc0208005`) |
| `bo_free(bo)` | GPU memory release | `KBASE_IOCTL_MEM_FREE` (`0x40088007`) |
| `bo_import(fd)` | DMA-BUF import | `KBASE_IOCTL_MEM_IMPORT` (`BASE_MEM_IMPORT_TYPE_UMM = 2`) |
| `bo_mmap(bo)` | Host virtual mapping | `mmap(mali_fd, ..., bo->gpu_va)` |
| `submit_job(submit)` | GPU job queue submission | Wrap `submit.jc` in `struct base_jd_atom` -> `KBASE_IOCTL_JOB_SUBMIT` |
| `bo_wait(bo)` | Fence completion check | `poll()` on event pipe or MTP status word |

#### Technical Feasibility Assessment
- **Pros**: Replaces the closed-source Mali userspace blob with standard upstream Mesa Gallium3D. Applications link against native Alpine Linux `libGLESv2.so.2` without any Bionic libc shims.
- **Challenges**:
  - Requires writing ~1,200 lines of C implementing `pan_kmod_ops`.
  - Display scanout still requires an out-of-band mechanism (ION DMA-BUF export to DECON Window 3) because DRM KMS is absent.

---

### 3.3 Path C: Mainline Linux Kernel Bring-Up (Long-Term Ideal)

#### Architectural Description
Booting a modern mainline kernel (Linux 6.x) with upstream `panfrost.ko` and an Exynos DRM display controller driver.

#### Prerequisites and Blockers
1. **Clock Management Unit (CMU)**: Exynos 7904 G3D clock tree and PLL drivers must be written for the mainline common clock framework (`clk-exynos`).
2. **Power Domains**: Generic power domain (`genpd`) support for `g3d_genpd` to power on/off the Mali GPU island.
3. **Display Subsystem**: Exynos DRM driver must be extended to support Exynos 7885/7904 DECON and MIPI-DSI controller (MIPI DSI 4-lane link to EA8076 Super AMOLED panel).
4. **Device Tree**: Complete Device Tree Source (`exynos7904-a30.dts`) defining Mali GPU node with `arm,mali-bifrost` compatible string and interrupt bindings.

**Estimated Timeline**: 6 to 12 months of specialized SoC bring-up effort.

---

## 4. Phased Implementation Roadmap

```
2026 Q4                      2026 Q4 / 2027 Q1            2027+
+--------------------------+  +--------------------------+  +--------------------------+
| PHASE 1: Path A          |  | PHASE 2: Path B          |  | PHASE 3: Path C          |
| Direct-DECON DDK Bridge  |  | Mesa pan_kmod_kbase      |  | Mainline DRM / Panfrost  |
+--------------------------+  +--------------------------+  +--------------------------+
| - Complete Bionic bridge |  | - Implement pan_kmod_ops |  | - Mainline DT bringup    |
| - Weston DECON backend   |  | - Map Kbase ioctls       |  | - Exynos 7904 CMU/PM     |
| - postmarketOS UI boot   |  | - Open Gallium on 4.4    |  | - DECON DRM KMS driver   |
| - Target: Working UI NOW |  | - Target: Pure open user |  | - Target: Upstream Linux |
+--------------------------+  +--------------------------+  +--------------------------+
```

### Recommendation
**Proceed immediately with Phase 1 (Path A: Direct-DECON DDK Bridge).**
Milestones 4A, 4B, and 4C have already solved the hardest problems:
- ION DMA-BUF allocation and GPU binding (`FIND-016`, `FIND-017`).
- 1040-byte `S3CFB_WIN_CONFIG` ioctl structure reverse-engineering (`FIND-020`).
- DECON Window 3 IDMA channel 0 routing (`FIND-021`).
- Double-buffered swapchain achieving **59.89 FPS** hardware-locked presentation (`FIND-023`).

Phase 1 delivers a fully accelerated, responsive postmarketOS environment on the Galaxy A30 in weeks rather than months.
Phase 2 (`pan_kmod_kbase`) can then be developed as an in-tree Mesa research prototype.

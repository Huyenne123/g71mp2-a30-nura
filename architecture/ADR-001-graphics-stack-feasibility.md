# Architecture Decision Record (ADR-001)

## Title: Technical Strategy for GPU Acceleration on postmarketOS (Samsung Galaxy A30 / Exynos 7904)

- **Date**: 2026-10-17
- **Status**: Proposed / Under Evaluation
- **Target**: Samsung Galaxy A30 (SM-A305F), Exynos 7904, Mali-G71 MP2, postmarketOS

---

## 1. Context and Problem Statement

postmarketOS on the Samsung Galaxy A30 currently operates without hardware-accelerated 3D graphics (classified as archived in pmaports with legacy downstream 4.4 kernel). The display is driven by the Samsung DECON framebuffer (`/dev/fb0`), and rendering is performed entirely by software (llvmpipe / pixman), causing high CPU load, thermal throttling, and poor responsiveness under Wayland compositors.

To achieve functional GPU acceleration, three primary pathways must be evaluated against actual empirical evidence from the target device and source repositories:

1. **Option A (Mainline Path)**: Upstream Mesa Panfrost + Linux Mainline `drm/panfrost` + `drm/exynos`.
2. **Option B (Downstream Compatibility Path)**: Downstream `mali_kbase` + `libhybris` + Android vendor blobs (`libGLES_mali.so`, `gralloc.exynos7904.so`, `hwcomposer.exynos7904.so`).
3. **Option C (Hybrid Open Path)**: Mesa Panfrost userspace adapted to talk directly to the downstream `mali_kbase` kernel driver (`/dev/mali0`), or an open GBM/EGL wrapper around `libGLES_mali`.

---

## 2. Comparative Analysis Matrix

| Evaluation Criteria | Option A: Upstream Panfrost + Mainline DRM | Option B: Downstream Kbase + libhybris | Option C: Hybrid Panfrost on Kbase / Open Shim |
| :--- | :--- | :--- | :--- |
| **Engineering Effort** | **Very High** (Requires mainline kernel port for Exynos 7885/7904, DECON DRM driver, DSI panel driver, and Panfrost G71 support). | **Medium** (Kernel driver is already working on device; requires Bionic-musl linking and Wayland-EGL integration). | **High** (Requires porting Panfrost's kernel abstraction to `kbase` r26p0 ioctls or writing a clean-room EGL state tracker). |
| **Available Evidence** | High for GPU ID `0x6000` in Linux DRM, but low for Mali-G71 in Mesa (`PAN_ARCH == 6` lacks G71 testing; docs state G71 is unsupported). | **Complete & Verified** (Proprietary binaries for 64-bit and 32-bit are extracted, unstripped with `.symtab` and debug info; device running with root ADB). | Verified IOCTL table (`0x80`, r26p0) and memory layouts reverse engineered in FIND-004. |
| **Kernel Compatibility** | Incompatible with active downstream 4.4 kernel (`# CONFIG_DRM is not set`). Requires mainline Linux 6.x. | **100% Compatible** with active downstream kernel (`mali_kbase` r26p0 is already compiled and running). | Compatible with downstream 4.4 kernel. |
| **Display Integration** | Native Wayland via standard DRM/KMS (`/dev/dri/card0`). | Requires `hwcomposer` backend or `libhybris` FBDev display glue. | Requires custom FBDev swapchain or dumb buffer emulation. |
| **Upstreamability** | High (Direct contribution to postmarketOS mainline, Mesa, and Linux kernel). | Low (Device-specific downstream workaround; not accepted into Alpine main). | Moderate (Panfrost formerly had experimental kbase support early in its history). |
| **Redistribution & Licensing** | Clean-room open source (MIT/GPL). | Distributes proprietary blobs (`libGLES_mali.so`, restricted Samsung license). | Mixed (Open userspace communicating with proprietary kernel or open shim). |

---

## 3. Evaluation of Options

### Option A: Upstream Mesa Panfrost + Mainline DRM
- **Strengths**: True open-source stack, long-term maintainability, standard Wayland/X11 compatibility without binary shims.
- **Blockers Identified**:
  1. Downstream 4.4 kernel has no DRM (`# CONFIG_DRM is not set`). Running Option A on the current kernel is physically impossible without backporting DRM/KMS + Exynos DECON DRM + Panfrost DRM driver.
  2. Mainline Linux support for Exynos 7885/7904 SoC display controller (DECON) and DSI panel (EA8076) is not yet upstreamed.
  3. Upstream Mesa Panfrost explicitly flags Mali-G71 as not yet supported due to 1st-generation Bifrost differences.
- **Verdict**: Ultimate long-term goal, but not viable for immediate acceleration on the current kernel.

### Option B: Downstream Kbase + libhybris
- **Strengths**:
  1. The kernel driver `mali_kbase` is already running on the device (`/dev/mali0` is active and responsive).
  2. We have the exact matched proprietary userspace libraries (`libGLES_mali-64.so`, `gralloc-64.so`, `hwcomposer-64.so`) from the live device.
  3. Crucially, `libGLES_mali-64.so` is **not stripped** and contains 191,392 symbols, making debugging, symbols hooking, and API wrapping exceptionally straightforward.
  4. Provides immediate full OpenGL ES 3.2, EGL 1.4, Vulkan, and OpenCL acceleration.
- **Blockers Identified**:
  1. Linking against Alpine Linux `musl` libc requires `libhybris` to bridge Bionic libc calls.
  2. Display server in postmarketOS must route frames through `hwcomposer` or `fbdev` since DRM is disabled.
- **Verdict**: Fastest and most practical path to immediate working hardware graphics acceleration on the device.

### Option C: Hybrid Panfrost on Kbase (or Mini-GBM Shim)
- **Strengths**: Keeps Mesa's open OpenGL implementation while leveraging the existing downstream kernel.
- **Verdict**: High initial research value; serves as a stepping stone between Option B and Option A.

---

## 4. Architectural Decision & Staged Strategy

We adopt a **two-phase phased engineering strategy**:

### Phase I: Immediate Acceleration via Downstream Stack (Option B)
- Stand up an isolated `libhybris` test environment on the Galaxy A30 running postmarketOS.
- Wrap `libGLES_mali.so` to provide EGL and GLESv2 to postmarketOS compositors.
- Validate acceleration using offscreen EGL render tests, `es2gears`, and Wayland windowing.

### Phase II: Upstream Enabling & Panfrost Gap Analysis (Option A)
- Use our reverse-engineered findings (ISA differences, quirk registers, job structures) to implement the missing G71 bits in Mesa Panfrost.
- Prepare device tree and kernel patches for mainline DRM Panfrost.

---

## 5. Consequences and Risks

- **Option B Trade-offs**: Relies on proprietary blobs; cannot be submitted to upstream Alpine Linux main repositories (placed in postmarketOS non-free / testing).
- **Mitigation**: All reverse-engineering documentation, symbol callgraphs, and ioctl analyses generated during Option B directly serve to close the gaps in Option A (Panfrost).

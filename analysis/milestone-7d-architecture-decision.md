# Milestone 7D Phase B: Weston Direct-DECON GL Renderer Architectural Decision

## 1. Executive Summary

Milestone 7D aims to achieve genuine hardware-accelerated OpenGL ES desktop compositing in Weston 14.0.2 on the Samsung Galaxy A30 (`SM-A305F`, Exynos 7904, ARM Mali-G71 MP2) with direct presentation through DECON Window 3 / IDMA channel 0.

This document formally evaluates three architectural alternatives against Weston invariants, GPU drivers, synchronization contracts, and recovery requirements, and records the architectural decision.

---

## 2. Evaluation of Architectural Options

### Option 1: Offscreen FBO Compositing into Backend-Allocated DMA-BUFs
- **Mechanism**:
  - The backend allocates Exynos ION DMA-BUFs directly.
  - For each buffer, the backend imports it into EGL as an `EGLImageKHR`, attaches it to a GL renderbuffer (`glEGLImageTargetRenderbufferStorageOES`), and constructs an FBO.
  - Weston `gl-renderer` renders into the FBO via `output_fbo_create()`.
  - The backend extracts the DMA-BUF and presents it to DECON.
- **Assessment**:
  - *API Compatibility*: Weston 14.0.2 provides `output_fbo_create()` and `create_fbo()`. However, `create_renderbuffer_dmabuf()` in Weston assumes Linux DRM/GBM buffer import semantics (`EGL_EXT_image_dma_buf_import`), which are not exposed by the proprietary Android Mali DDK (which exposes `EGL_ANDROID_image_native_buffer` instead).
  - *Compositing Path*: Genuine GLES composition into offscreen texture/renderbuffer.
  - *Buffer Lifecycle*: Requires custom downstream plumbing inside `gl-renderer` to bridge Android ION handles into `EGLImage` without DRM modifier tables.
  - *Synchronization*: Requires manual EGL sync fence injection (`eglCreateSyncKHR`) and extraction to obtain an acquire fence for DECON.
  - *Implementation Complexity*: **HIGH**. Requires modifying internal `gl-renderer` allocator abstractions.

---

### Option 2: Native Window Integration via `HWCNativeWindowCreate` (Recommended)
- **Mechanism**:
  - The backend creates an Android `ANativeWindow` backed by `libhybris-hwcomposerwindow.so` using `HWCNativeWindowCreate(1080, 2340, HAL_PIXEL_FORMAT_RGBA_8888, decon_weston_present_cb, output)`.
  - The backend passes this `ANativeWindow` to standard Weston `gl_renderer_interface->output_window_create()`.
  - Weston `gl-renderer` creates a native on-screen `EGLSurface` backed by the `ANativeWindow`.
  - During repaint, Weston composites the scene into default framebuffer `fbo = 0` via Mali fragment shaders.
  - Weston `gl_renderer_repaint_output()` executes `eglSwapBuffers()`.
  - `libhybris` queues the buffer and synchronously invokes `decon_weston_present_cb`, delivering:
    1. The shared Exynos ION DMA-BUF file descriptor (`buf->handle->data[0]`).
    2. The GPU acquire sync fence (`HWCNativeBufferGetFence(buf)`).
  - The callback submits the buffer directly to DECON Window 3 via `S3CFB_WIN_CONFIG_IOCTL`.
  - On retire fence signaling from the AMOLED panel VSYNC (GPIO IRQ 249), the Wayland event loop calls `weston_output_finish_frame()`.
- **Assessment**:
  - *API Compatibility*: **100% compliant** with Weston 14.0.2's `output_window_create()` interface contract.
  - *Compositing Path*: **Genuine GLES composition**. All views, shadows, borders, and damage regions are rasterized by the Mali-G71 GPU directly into the Gralloc ION backing store.
  - *Zero CPU Copy*: Gralloc buffers are shared directly with DECON IDMA channel 0. Zero `memcpy`, zero `glReadPixels`.
  - *Proven Reliability*: The exact same `HWCNativeWindowCreate` -> `eglSwapBuffers` -> `S3CFB_WIN_CONFIG_IOCTL` pipeline was empirically validated in Milestone 7C over a 600.00-second physical soak test running at 59.96 FPS with 0 memory and 0 FD leaks.
  - *Damage & Repaint*: Fully preserves Weston damage tracking. `eglSwapBuffersWithDamage` and `eglSetDamageRegionKHR` are supported by the driver.
  - *Fallback*: Completely preserves the CPU Pixman path. If `--use-pixman` is specified or EGL initialization fails, `backend-decon` activates its Pixman renderbuffers.
  - *Implementation Complexity*: **LOW to MODERATE**. Requires small downstream patch to `backend-decon/decon.c` and `egl-glue.c`.

---

### Option 3: Independent Custom Downstream Renderer
- **Mechanism**:
  - Write an entirely new `decon-gl-renderer.so` outside of Weston's upstream `gl-renderer.so`.
- **Assessment**:
  - *API Compatibility*: Violates design principles; duplicates thousands of lines of Weston matrix math, shader compilation, clipping, and color management code.
  - *Constraint Check*: Explicitly prohibited by project instructions: *"Do not build a parallel EGL compositor beside Weston; Do not rewrite the entire renderer."*
  - *Implementation Complexity*: **EXTREME**. Unmaintainable and unnecessary.

---

## 3. Comparison Matrix

| Evaluation Criteria | Option 1: FBO DMA-BUF Import | Option 2: `HWCNativeWindow` Integration (Selected) | Option 3: Custom Renderer Rewrite |
| :--- | :--- | :--- | :--- |
| **Weston 14.0.2 Invariant Preservation** | Partial (requires allocator bypass) | **Full (uses standard `output_window_create`)** | None (rewrites renderer) |
| **GLES Hardware Compositing** | Yes | **Yes (Mali-G71 Bifrost v6)** | Yes |
| **Zero CPU Copy to DECON** | Yes | **Yes (Direct ION DMA-BUF scanout)** | Yes |
| **Prior Empirical Proof on Target** | Unverified | **100% Verified (Milestone 7C 600s Soak)** | None |
| **Acquire / Release Fence ABI** | Manual EGLSync mapping | **Hardware-proven (`FIND-045` / `FIND-037`)** | Manual |
| **Pixman Fallback Preservation** | Complex | **Simple dual-path branch** | Difficult |
| **Code Footprint** | ~600 lines | **~120 lines** | >3000 lines |

---

## 4. Architectural Decision & Selection

**Decision: Adopt Option 2 (Native Window Integration backed by `HWCNativeWindowCreate`)**.

### Key Architecture Components:
1. **Weston Core GL Hook**:
   Add `EGL_PLATFORM_ANDROID_KHR` recognition to `libweston/renderer-gl/egl-glue.c` to prevent platform assertion failures during client extension probing.
2. **Backend Display Creation**:
   Configure `struct gl_renderer_display_options` in `decon_backend_create()`:
   ```c
   struct gl_renderer_display_options gl_options = {
       .base = { .option_type = WESTON_RENDERER_OPTIONS_GL },
       .egl_platform = EGL_PLATFORM_ANDROID_KHR,
       .egl_native_display = EGL_DEFAULT_DISPLAY,
       .egl_surface_type = EGL_WINDOW_BIT,
       .formats = b->formats,
       .formats_count = b->formats_count,
   };
   ret = weston_compositor_init_renderer(compositor, WESTON_RENDERER_GL, &gl_options.base);
   ```
3. **Output Native Window Binding**:
   In `decon_output_enable()`, if `ec->renderer->type == WESTON_RENDERER_GL`:
   - Instantiate `HWCNativeWindowCreate(1080, 2340, HAL_PIXEL_FORMAT_RGBA_8888, decon_weston_present_cb, output)`.
   - Pass window handle to `gl_renderer->output_window_create(&output->base, &gl_out_options)`.
4. **Zero-Copy Presentation Callback**:
   In `decon_weston_present_cb()`:
   - Extract DMA-BUF FD and GPU acquire fence.
   - Submit Window 3 config to DECON `/dev/graphics/fb0`.
   - Reclaim acquire fence FD (`FIND-045`) and release fences (`FIND-037`).
   - Feed composite retire fence back to Gralloc buffer via `HWCNativeBufferSetFence(buf, dup(retire_fence))` to protect against buffer reuse until scanout completes.
   - Register retire fence with `wl_event_loop_add_fd()` for asynchronous frame completion notification (`weston_output_finish_frame`).
5. **Clean Fallback**:
   If GL renderer initialization fails, `backend-decon` logs an explicit diagnostic and falls back automatically to `WESTON_RENDERER_PIXMAN`.

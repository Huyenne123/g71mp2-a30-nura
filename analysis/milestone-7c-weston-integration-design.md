# Milestone 7C Phase E: Weston Direct-DECON GPU Integration Design

## 1. Executive Summary

Milestones 6D, 6E, and 7A established a fully operational CPU Pixman presentation pipeline for Weston 14.0.2 on the Samsung Galaxy A30 (`SM-A305F`, Exynos 7904, Mali-G71 MP2), rendering over double-buffered ION DMA-BUFs directly into Samsung DECON Window 3 / IDMA 0.

Milestones 7B and 7C prove that:
1. The proprietary Samsung/ARM Mali DDK (`libGLES_mali.so` v1.r26p0-01eac0) executes natively under Alpine Linux ARM64 (`musl libc 1.2.5`) via the `libhybris` bridge.
2. Hardware OpenGL ES 3.2 rendering and offscreen FBO composition operate with zero delta against reference mathematics.
3. An `ANativeWindow` created via `HWCNativeWindowCreate` delivers zero-copy ION DMA-BUFs with physical GPU acquire fences directly to DECON Window 3 at a rock-solid 60.00 FPS cadence with 0 memory and 0 FD leakage.

This document specifies the architectural transition from CPU Pixman rendering to hardware-accelerated OpenGL ES composition and client rendering in Weston 14.0.2.

---

## 2. Weston 14.0.2 Renderer Architecture Overview

Weston decouples the display backend (`libweston/backend-*`) from the composition engine (`libweston/renderer-*`):
- **Compositor Renderers**:
  - `pixman-renderer`: CPU software rasterization into shared memory or DMA-BUF backing store.
  - `gl-renderer`: OpenGL ES 2.0/3.0 GPU rasterization using vertex/fragment shaders (`vertex.glsl`, `fragment.glsl`).
  - `noop-renderer`: Headless testing without drawing operations.

### 2.1. Weston `gl-renderer` Interface Contract
In Weston 14.0.2 (`libweston/renderer-gl/gl-renderer.h`), the GL renderer exposes:
```c
struct gl_renderer_interface {
    int (*display_create)(struct weston_compositor *ec,
                          const struct gl_renderer_display_options *options);
    int (*output_window_create)(struct weston_output *output,
                                const struct gl_renderer_output_options *options);
    int (*output_fbo_create)(struct weston_output *output,
                             const struct gl_renderer_fbo_options *options);
    void (*output_destroy)(struct weston_output *output);
    ...
};
```

---

## 3. Compositor-Side vs. Client-Side GPU Acceleration

A critical distinction must be drawn between **Compositor Acceleration** and **Client Acceleration**:

```
+-----------------------------------------------------------------------------+
|                            WAYLAND ARCHITECTURE                             |
|                                                                             |
|  [ Client App (e.g. Simple-SHM) ]         [ Client App (e.g. GLES Client) ] |
|  - CPU rendering into wl_shm              - Hardware GPU render via DMA-BUF |
|              |                                           |                  |
|              v wl_shm_buffer                             v zwp_linux_dmabuf |
|  +-----------------------------------------------------------------------+  |
|  | WESTON COMPOSITOR (backend-decon)                                     |  |
|  |                                                                       |  |
|  |   [ Scenario A: CPU Pixman ]         [ Scenario B: Mali GL Renderer ] |  |
|  |   - Composites onto CPU buffer       - Composites onto GPU EGLSurface |  |
|  |   - memcpy / CPU blend               - Mali G71 fragment shaders      |  |
|  |               |                                      |                |  |
|  |               +------------------+-------------------+                |  |
|  |                                  v eglSwapBuffers()                   |  |
|  |                   [ HWComposer Native Window Present ]                |  |
|  |                   - ION DMA-BUF FD + GPU Acquire Fence                |  |
|  +----------------------------------|------------------------------------+  |
|                                     v S3CFB_WIN_CONFIG_IOCTL                 |
|  +-----------------------------------------------------------------------+  |
|  | Samsung DECON Controller (Window 3 / IDMA 0) -> EA8076 AMOLED Panel   |  |
|  +-----------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------+
```

### 3.1. Comparison Matrix

| Property | Path 1: Current Baseline (Milestone 6E/7A) | Path 2: Compositor GPU Acceleration (Milestone 7D) | Path 3: Direct Client Hardware Overlays |
| :--- | :--- | :--- | :--- |
| **Compositor Renderer** | `pixman-renderer` (CPU) | `gl-renderer` (Mali-G71 GPU) | `noop` / Hardware planes bypass |
| **Compositor Backing Store** | Double-buffered ION DMA-BUF (`mmap`) | `HWCNativeWindow` EGLSurface (3x ION DMA-BUF) | Direct client DMA-BUF passed to DECON |
| **Client Shm Surfaces** | Blitted by CPU Pixman | Texturized and composited via Mali shaders | N/A (requires full composite) |
| **Client GL Surfaces** | CPU converted or unsupported | Zero-copy import via `EGLImage` / DMA-BUF | Scanned out directly on DECON Window 1/2 |
| **CPU Usage (Idle/Anim)** | 12% - 35% Cortex-A53 | < 2% CPU (all blitting offloaded to Mali) | < 1% CPU |
| **Hardware Requirements** | Only ION allocator | `libhybris` + `libGLES_mali.so` + `libEGL` | Multi-window DECON routing (Windows 1-3) |

---

## 4. Implementation Design: `backend-decon` with `gl-renderer`

### 4.1. Step 1: EGL Display Initialization in Backend
When Weston starts with `--renderer=gl`:
```c
struct gl_renderer_display_options gl_options = {
    .base = {
        .option_type = WESTON_RENDERER_OPTIONS_GL,
    },
    .egl_platform = EGL_PLATFORM_ANDROID_KHR, // or EGL_DEFAULT_DISPLAY
    .egl_native_display = EGL_DEFAULT_DISPLAY,
    .egl_surface_type = EGL_WINDOW_BIT,
    .formats = b->formats,
    .formats_count = b->formats_count,
};

ret = gl_renderer_interface->display_create(compositor, &gl_options);
```

### 4.2. Step 2: Output Window Surface Creation
During `decon_output_enable()`:
```c
/* Create ANativeWindow backed by libhybris-hwcomposerwindow */
output->native_window = HWCNativeWindowCreate(
    output->base.current_mode->width,
    output->base.current_mode->height,
    HAL_PIXEL_FORMAT_RGBA_8888,
    decon_weston_present_cb,
    output
);

struct gl_renderer_output_options gl_out_options = {
    .window_for_legacy = (EGLNativeWindowType)output->native_window,
    .fb_size = {
        .width = output->base.current_mode->width,
        .height = output->base.current_mode->height,
    },
    .area = {
        .x = 0, .y = 0,
        .width = output->base.current_mode->width,
        .height = output->base.current_mode->height,
    },
    .formats = output->backend->formats,
    .formats_count = output->backend->formats_count,
};

ret = gl_renderer_interface->output_window_create(&output->base, &gl_out_options);
```

### 4.3. Step 3: Repaint & Zero-Copy DECON Presentation
In `decon_output_repaint()`:
```c
/* 1. Delegate composition of all visible surfaces to Weston gl-renderer */
ec->renderer->repaint_output(&output->base, &damage, NULL);

/* Note: gl-renderer's output_repaint automatically executes eglSwapBuffers(),
 * which invokes decon_weston_present_cb() */
```

### 4.4. Step 4: Callback Synchronization & VSYNC Signaling
In `decon_weston_present_cb()`:
```c
static void decon_weston_present_cb(void *user_data, struct ANativeWindow *win, struct ANativeWindowBuffer *buf)
{
    struct decon_output *output = user_data;
    int dma_buf_fd = buf->handle->data[0];
    int acquire_fence = HWCNativeBufferGetFence(buf);

    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    win_data.fence = -1;

    for (int i = 0; i < 5; i++) {
        win_data.config[i].state = 0;
        win_data.config[i].fd_idma[0] = -1;
        win_data.config[i].fd_idma[1] = -1;
        win_data.config[i].fd_idma[2] = -1;
        win_data.config[i].fence_fd = -1;
        win_data.config[i].rel_fence = -1;
    }

    win_data.config[3].state = 2; /* DECON_WIN_STATE_BUFFER */
    win_data.config[3].fd_idma[0] = dma_buf_fd;
    win_data.config[3].fence_fd = acquire_fence;
    win_data.config[3].plane_alpha = 255;
    win_data.config[3].blending = 0;
    win_data.config[3].idma_type = 0;
    win_data.config[3].format = 2; /* RGBA_8888 */
    win_data.config[3].src_x = 0; win_data.config[3].src_y = 0;
    win_data.config[3].src_w = 1080; win_data.config[3].src_h = 2340;
    win_data.config[3].dst_x = 0; win_data.config[3].dst_y = 0;
    win_data.config[3].dst_w = 1080; win_data.config[3].dst_h = 2340;

    ioctl(output->backend->fb_fd, S3CFB_WIN_CONFIG_IOCTL, &win_data);

    /* Close acquire_fence (FIND-045) */
    if (acquire_fence >= 0) close(acquire_fence);

    /* Close release fences (FIND-037) */
    for (int i = 0; i < 5; i++) {
        if (win_data.config[i].rel_fence >= 0) {
            close(win_data.config[i].rel_fence);
            win_data.config[i].rel_fence = -1;
        }
    }

    /* Hand retire fence back to buffer for reuse protection */
    if (win_data.fence >= 0) {
        HWCNativeBufferSetFence(buf, dup(win_data.fence));

        /* Register retire fence in Weston event loop to complete frame asynchronously */
        output->retire_source = wl_event_loop_add_fd(
            wl_display_get_event_loop(output->base.compositor->wl_display),
            win_data.fence,
            WL_EVENT_READABLE,
            decon_retire_fence_handler,
            output
        );
    }
}
```

---

## 5. Risk Assessment & Safe Fallback Strategy

1. **Dual Renderer Architecture**:
   Preserve `WESTON_RENDERER_PIXMAN` as the default fallback renderer. The `--renderer=gl` flag enables hardware GLES composition only when explicitly invoked.
2. **Crash Resilience**:
   If `libhybris` or vendor DDK encounters an unrecoverable driver fault, `backend-decon` can fall back dynamically to CPU Pixman rendering.
3. **Android Coexistence**:
   The compositor respects Android recovery paths, cleanly tearing down display hardware allocations when stopping.

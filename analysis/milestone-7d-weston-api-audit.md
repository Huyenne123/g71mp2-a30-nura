# Milestone 7D Phase A: Weston 14.0.2 Renderer and Backend API Audit

## 1. Executive Summary

Milestone 7D transitions the Samsung Galaxy A30 Direct-DECON presentation pipeline from CPU Pixman rasterization to native GPU-accelerated OpenGL ES composition using Weston 14.0.2 (`015b3b4d4c05da44a22349ea6e651d1a8f678c59`).

This audit inspects the exact source files, interface contracts, symbol resolutions, buffer lifecycles, and synchronization invariants between:
1. **Weston Compositor Core**: `libweston/compositor.c`, `include/libweston/libweston.h`
2. **Weston GL Renderer**: `libweston/renderer-gl/gl-renderer.c`, `gl-renderer.h`, `egl-glue.c`
3. **Direct-DECON Backend**: `libweston/backend-decon/decon.c`, `decon_internal.h`
4. **Libhybris Hardware Window Platform**: `hybris/egl/platforms/hwcomposer/hwcomposer.h`, `libhybris-hwcomposerwindow.so`
5. **Samsung Exynos DECON Kernel Driver**: `/dev/graphics/fb0`, `S3CFB_WIN_CONFIG_IOCTL` (`0x441046d1`)

---

## 2. Weston `gl_renderer_interface` Contract Inspection

In Weston 14.0.2 ([`libweston/renderer-gl/gl-renderer.h`](file:///home/neyuh/mali-a30/scratch/weston/libweston/renderer-gl/gl-renderer.h)), the OpenGL ES rendering engine is decoupled from display backends via `struct gl_renderer_interface`:

```c
struct gl_renderer_interface {
    int (*display_create)(struct weston_compositor *ec,
                          const struct gl_renderer_display_options *options);

    int (*output_window_create)(struct weston_output *output,
                                const struct gl_renderer_output_options *options);

    int (*output_fbo_create)(struct weston_output *output,
                             const struct gl_renderer_fbo_options *options);

    void (*output_destroy)(struct weston_output *output);

    void (*output_set_border)(struct weston_output *output,
                              enum gl_renderer_border_side side,
                              int32_t width, int32_t height,
                              int32_t tex_width, unsigned char *data);

    int (*create_fence_fd)(struct weston_output *output);

    struct weston_renderbuffer *(*create_fbo)(struct weston_output *output,
                                              const struct pixel_format_info *format,
                                              int width, int height,
                                              uint32_t *pixels);
};
```

### 2.1. `display_create()` Contract
- **Purpose**: Initializes EGL display connection, verifies required EGL extensions, chooses EGL configuration, creates shared EGLContext (targeting OpenGL ES 3.0+ with fallback to 2.0), compiles shader programs, and configures Wayland display bindings.
- **Input Options** (`struct gl_renderer_display_options`):
  - `base.option_type = WESTON_RENDERER_OPTIONS_GL`
  - `egl_platform`: The EGL platform enum.
  - `egl_native_display`: The native display handle passed to `eglGetPlatformDisplay` or `eglGetDisplay`.
  - `egl_surface_type`: Required surface bits (`EGL_WINDOW_BIT` or `EGL_PBUFFER_BIT`).
  - `formats`: Accepted pixel formats for context configuration.
  - `formats_count`: Number of formats.
- **Upstream Weston Bug Discovered in Audit**:
  In [`libweston/renderer-gl/egl-glue.c:527`](file:///home/neyuh/mali-a30/scratch/weston/libweston/renderer-gl/egl-glue.c#L527), function `platform_to_extension(EGLenum platform)` contains a strict `switch (platform)` that only recognizes GBM, Wayland, X11, and Surfaceless platforms. Any other enum (such as `EGL_PLATFORM_ANDROID_KHR = 0x3141` or `0`) triggers `assert(0 && "bad EGL platform enum")`. Adding `case EGL_PLATFORM_ANDROID_KHR: return "android";` and `case 0: return NULL;` allows clean client extension matching against `EGL_KHR_platform_android`.

### 2.2. `output_window_create()` Contract
- **Purpose**: Attaches the GL renderer to a physical output using a native window handle. Creates the on-screen `EGLSurface` and allocates the default framebuffer (`fbo = 0`) wrapper renderbuffer.
- **Input Options** (`struct gl_renderer_output_options`):
  - `window_for_legacy`: `(EGLNativeWindowType)` handle for `eglCreateWindowSurface()`.
  - `window_for_platform`: Pointer for `eglCreatePlatformWindowSurfaceEXT()`.
  - `fb_size`: Resolution of output (width: 1080, height: 2340).
  - `area`: Compositing viewport rect (x: 0, y: 0, width: 1080, height: 2340).
  - `formats`: Pixel format array (e.g. `DRM_FORMAT_RGBA8888`, `DRM_FORMAT_RGBX8888`).
  - `formats_count`: Format count.

---

## 3. Renderer Lifecycle & Execution Trace

### 3.1. Initialization Sequence
```
backend_create()
   |
   v
weston_compositor_init_renderer(ec, WESTON_RENDERER_GL, &gl_options.base)
   |
   +--> weston_load_module("gl-renderer.so", "gl_renderer_interface")
   |
   +--> gl_renderer->display_create(ec, &gl_options)
           |
           +--> gl_renderer_setup_egl_client_extensions(gr)
           |       Query EGL_NO_DISPLAY extensions
           |       Confirm EGL_KHR_platform_android
           |
           +--> gl_renderer_setup_egl_display(gr, native_display)
           |       eglGetDisplay(EGL_DEFAULT_DISPLAY) -> 0x6058c667
           |       eglInitialize() -> EGL 1.4
           |
           +--> gl_renderer_setup_egl_extensions(ec)
           |       Confirm EGL_KHR_surfaceless_context: TRUE
           |       Confirm EGL_KHR_no_config_context: TRUE
           |       Confirm EGL_KHR_swap_buffers_with_damage: TRUE
           |       Confirm EGL_KHR_partial_update: TRUE
           |       Confirm EGL_KHR_wait_sync: TRUE
           |       Confirm EGL_WL_bind_wayland_display: TRUE
           |
           +--> gl_renderer_setup(ec)
                   eglCreateContext(dpy, EGL_NO_CONFIG, NULL, [GLES=3]) -> Context
                   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)
                   glGetString(GL_VERSION) -> "OpenGL ES 3.2 v1.r26p0-01eac0"
                   Check GL_EXT_texture_format_BGRA8888 -> PRESENT
                   Compile vertex & fragment shaders (vertex.glsl, fragment.glsl)
                   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)
```

### 3.2. Output Creation & Binding Sequence
```
decon_output_enable(struct weston_output *base)
   |
   +--> HWCNativeWindowCreate(1080, 2340, HAL_PIXEL_FORMAT_RGBA_8888,
   |                          decon_weston_present_cb, output)
   |       Allocates Android ANativeWindow with libhybris buffer queue
   |
   +--> gl_renderer->output_window_create(base, &gl_output_options)
           |
           +--> eglCreateWindowSurface(dpy, config, ANativeWindow, NULL) -> EGLSurface
           |
           +--> gl_renderer_output_create()
                   Allocates gl_output_state
                   Allocates dummy_renderbuffer (fbo = 0)
```

### 3.3. Repaint & Zero-Copy Presentation Trace
```
decon_output_repaint(struct weston_output *base)
   |
   +--> weston_output_flush_damage_for_primary_plane(base, &damage)
   |
   +--> ec->renderer->repaint_output(base, &damage, NULL)  <-- renderbuffer=NULL
           |
           +--> eglMakeCurrent(dpy, go->egl_surface, go->egl_surface, ctx)
           |
           +--> glBindFramebuffer(GL_FRAMEBUFFER, 0)
           |
           +--> glViewport(0, 0, 1080, 2340)
           |
           +--> repaint_views(output, damage)
           |       Iterates paint nodes in z-order
           |       Executes Mali-G71 fragment shaders compositing client surfaces
           |
           +--> eglSwapBuffers(dpy, go->egl_surface)
                   |
                   v (libhybris queueBuffer)
                decon_weston_present_cb(output, win, buf)
                   |
                   +--> dma_buf_fd = buf->handle->data[0]
                   +--> acquire_fence = HWCNativeBufferGetFence(buf)
                   +--> Populate struct decon_win_config_data (Window 3)
                   |
                   +--> ioctl(fb_fd, S3CFB_WIN_CONFIG_IOCTL, &win_data)
                   |
                   +--> close(acquire_fence)                    [FIND-045 fix]
                   +--> close(win_data.config[i].rel_fence)     [FIND-037 fix]
                   +--> HWCNativeBufferSetFence(buf, dup(win_data.fence))
                   |
                   +--> wl_event_loop_add_fd(loop, win_data.fence, WL_EVENT_READABLE,
                                            decon_fence_handler, output)
```

### 3.4. Frame Completion Sequence
```
Physical EA8076 Panel VSYNC Pulse (GPIO IRQ 249)
   |
   v
DECON Scanout Completes -> win_data.fence Signals Readable
   |
   v
Wayland Event Loop Wakes Up -> decon_fence_handler(fd, mask, output)
   |
   +--> wl_event_source_remove(output->fence_source)
   +--> close(output->current_retire_fence)
   +--> weston_compositor_read_presentation_clock(compositor, &ts)
   +--> weston_output_finish_frame(base, &ts, WP_PRESENTATION_FEEDBACK_KIND_VSYNC)
   |
   v
Compositor dispatches wl_surface.frame callbacks to Wayland clients
```

---

## 4. Architectural Analysis: HWCNativeWindow vs. Weston Invariants

### 4.1. Can `HWCNativeWindowCreate` be used as the Native Window?
**YES**.
1. **ABI Compatibility**: `HWCNativeWindowCreate` returns a pointer to `struct ANativeWindow`. In AArch64 Android/libhybris EGL, `EGLNativeWindowType` is `intptr_t` (a pointer cast).
2. **Buffer Lifecycle**: The native window manages a triple-buffered pool of `ANativeWindowBuffer` objects allocated via `gralloc.exynos7904.so`. Each buffer is backed by an Exynos ION DMA-BUF with 64-byte row alignment (stride 1088).
3. **Pacing Invariant**: When `eglSwapBuffers` executes, `queueBuffer` delivers the buffer synchronously to `decon_weston_present_cb`. Handing the DECON retire fence back to `HWCNativeBufferSetFence` prevents the GPU from dequeuing and overwriting the buffer until DECON scanout finishes.
4. **No Double Swapchain**: Unlike the Pixman path which allocates its own ION buffers and maps them to CPU addresses, the GL path relies entirely on Gralloc's ION buffers. The backend does not need its own swapchain allocation when GL mode is active.

### 4.2. Output Destruction Invariants
When an output is disabled or destroyed (`decon_output_disable` / `decon_output_destroy`):
1. **Fence Source Cleanup**: If `output->fence_source` is registered in the event loop, it must be removed via `wl_event_source_remove()` before closing `output->current_retire_fence`.
2. **GL Renderer Output State**: Calling `ec->renderer->gl->output_destroy(base)` destroys the EGLSurface, unbinds FBOs, and cleans up `gl_output_state`.
3. **Native Window Destruction**: Calling `HWCNativeWindowDestroy(output->native_window)` frees the window buffer queue and Gralloc allocations.

---

## 5. Potential Pitfalls and Mitigation Strategies

1. **Null Options Pointer Crash**:
   Passing `NULL` to `weston_compositor_init_renderer(ec, WESTON_RENDERER_GL, NULL)` causes `container_of(NULL, struct gl_renderer_display_options, base)` to compute an invalid pointer, crashing in `display_create`.
   *Mitigation*: Always pass a fully populated `struct gl_renderer_display_options` with `.egl_platform = EGL_PLATFORM_ANDROID_KHR`, `.egl_native_display = EGL_DEFAULT_DISPLAY`, `.egl_surface_type = EGL_WINDOW_BIT`.

2. **Platform Enum Assertion**:
   Upstream Weston `platform_to_extension()` asserts on non-standard platforms.
   *Mitigation*: Patch `egl-glue.c` to accept `EGL_PLATFORM_ANDROID_KHR` and `0`.

3. **Fence File Descriptor Leaks**:
   Failure to close the acquire fence (`FIND-045`) or release fence (`FIND-037`) causes kernel file descriptor exhaustion within seconds at 60 FPS.
   *Mitigation*: Enforce mandatory descriptor closure immediately following `ioctl(fb_fd, S3CFB_WIN_CONFIG_IOCTL)`.

4. **Pixman Path Preservation**:
   The working CPU Pixman path must remain fully functional as a fallback if EGL fails to initialize or if the user requests `--use-pixman`.
   *Mitigation*: Maintain dual code branches in `decon_output_enable()` and `decon_output_repaint()`, selecting Pixman or GL based on `ec->renderer->type`.

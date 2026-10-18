# Milestone 6A Analysis: Wayland Compositor Integration Gap

**Document ID**: `analysis/milestone-6a-wayland-integration-gap.md`  
**Date**: October 18, 2026  
**Status**: **Architecture & Gap Specification**  
**Target Compositor**: Weston (or wlroots/TinyWL) on postmarketOS (Alpine Linux)  
**Hardware Display**: Samsung Galaxy A30 (`SM-A305F`), EA8076 Super AMOLED (1080x2340)  

---

## 1. Explicit Architectural Declaration

> [!WARNING]
> **Definitive Statement on Current Codebase State**:  
> **No integrated Wayland compositor currently exists in this repository.**  
> - `src/compositor/decon_backend.c` is a **reusable display presentation abstraction library** that manages an EGL context, double-buffered ION DMA-BUFs, and `S3CFB_WIN_CONFIG` ioctls.  
> - `tests/decon_compositor_test.c` is a **standalone synthetic test harness** that renders hardcoded geometric shapes (`glScissor` boxes) to simulate desktop activity. It does not speak the Wayland protocol, does not accept client connections, does not handle input events, and does not interface with Weston, wlroots, or TinyWL.

---

## 2. Current Implementation Breakdown

### 2.1 What Was Built (`src/compositor/decon_backend.c`)
The current presentation backend implements low-level display primitives:

```c
struct decon_backend *decon_backend_init(const struct decon_backend_config *cfg);
int decon_backend_setup_egl(struct decon_backend *b, void *dl_handle);
int decon_backend_setup_swapchain(struct decon_backend *b);
int decon_backend_begin_frame(struct decon_backend *b, uint32_t *active_fbo);
int decon_backend_present(struct decon_backend *b, int *out_retire_fence);
int decon_backend_wait_vsync(struct decon_backend *b, int timeout_ms);
void decon_backend_teardown(struct decon_backend *b);
```

These functions successfully solve:
1. Opening and arbitrating `/dev/graphics/fb0` and `/dev/ion`.
2. Initializing EGL 1.4 offscreen with a 64x64 Pbuffer.
3. Allocating two full-screen (10.24 MB) ION DMA-BUFs from `ion_noncontig_heap`.
4. Binding DMA-BUFs to `GL_TEXTURE_2D` via `eglCreateImageKHR` and attaching to `gl_fbo`.
5. Constructing the 1040-byte `S3CFB_WIN_CONFIG` payload with static assertions.
6. Submitting Window 3 IDMA 0 page-flips and synchronizing with hardware retire fences.

### 2.2 What Is Missing for a Functional Wayland Compositor
To transition from a synthetic display test to a functional Linux desktop, the following five major subsystems must be implemented:

```
+-----------------------------------------------------------------------------------+
|                        Client Applications (Wayland Clients)                      |
|                     Phosh / Terminal / Browser (wl_surface)                       |
+-----------------------------------------------------------------------------------+
                                      |
                           Wayland Protocol (shm / dmabuf)
                                      v
+-----------------------------------------------------------------------------------+
|                         Wayland Compositor Core (Weston)                          |
|   +---------------------------------------------------------------------------+   |
|   | Scene Graph, Surface Commit, Damage Tracking, View Transformations         |   |
|   +---------------------------------------------------------------------------+   |
|   | Compositor Renderer (GL-Renderer: Renders client surfaces into back buffer)|   |
|   +---------------------------------------------------------------------------+   |
|   | Input Management (libinput: Touchscreen events -> wl_touch/wl_pointer)    |   |
|   +---------------------------------------------------------------------------+   |
|   | Output Backend (WESTON-BACKEND-DECON) <--- [MISSING INTEGRATION GAP]      |   |
+---+---------------------------------------------------------------------------+---+
                                      |
                      Calls Presentation API Hooks
                                      v
+-----------------------------------------------------------------------------------+
|               Direct-DECON Presentation Engine (libdecon_backend.so)              |
|        decon_backend_begin_frame()  --> Bind back-buffer FBO                      |
|        decon_backend_present()      --> S3CFB_WIN_CONFIG ioctl                    |
|        decon_backend_wait_vsync()   --> Hardware retire fence                     |
+-----------------------------------------------------------------------------------+
```

1. **Wayland Wire Protocol Server**:
   - IPC socket (`/run/user/10000/wayland-0`).
   - Global interfaces: `wl_compositor`, `wl_subcompositor`, `wl_shm`, `linux_dmabuf_v1`, `xdg_wm_base`.
2. **Client Buffer Import (`linux_dmabuf_v1`)**:
   - Accepting DMA-BUFs passed from Wayland clients and importing them into EGL as textures.
3. **Compositor Repaint Loop & Damage Tracking**:
   - Accumulating damage rectangles from client surfaces.
   - Drawing only damaged regions into the FBO using Weston's `gl-renderer`.
4. **Input Handling Subsystem**:
   - Opening `/dev/input/event*` (Samsung touchscreen / touchkeys / volume buttons).
   - Dispatching multi-touch coordinates to Wayland clients via `wl_touch`.
5. **Weston Backend Plugin Architecture**:
   - Implementing Weston's dynamic module interface (`weston_backend`).

---

## 3. Minimal Weston Backend Specification (`backend-decon`)

In the reference Weston architecture, backends are shared modules loaded dynamically at startup via `-B<backend>.so`. Below is the exact technical specification required to implement `backend-decon.so`.

### 3.1 Plugin Entry Point & Registration
Weston expects backends to define `weston_backend_init`:

```c
#include <libweston/libweston.h>
#include <libweston/backend.h>
#include <libweston/weston-log.h>
#include "decon_backend.h"

struct decon_output {
    struct weston_output base;
    struct decon_backend *engine;
    struct wl_event_source *fence_source;
};

struct decon_compositor {
    struct weston_compositor *compositor;
    struct decon_backend_config config;
};

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
                    void *config_base)
{
    struct decon_compositor *b = calloc(1, sizeof(*b));
    if (!b) return -1;

    b->compositor = compositor;
    /* Register Weston backend interface */
    compositor->backend = (struct weston_backend *)b;

    /* Initialize display output */
    return decon_output_create(b);
}
```

### 3.2 Output Lifecycle Hooks

The backend must bind four fundamental lifecycle callbacks to `struct weston_output`:

#### Hook 1: `output->enable()`
- Invokes `decon_backend_init(&cfg)`.
- Invokes `decon_backend_setup_egl(engine, dl_handle)`.
- Invokes `decon_backend_setup_swapchain(engine)`.
- Sets `output->width = 1080`, `output->height = 2340`, `output->scale = 2`.
- Initializes Weston's `gl-renderer` using the EGLDisplay and EGLContext provided by `decon_backend`.

#### Hook 2: `output->start_repaint_loop()`
- Called by Weston when an output becomes active to anchor the VSYNC timeline.
- Queries `clock_gettime(CLOCK_MONOTONIC)` and immediately invokes `weston_output_finish_frame(output, &now, WP_PRESENTATION_FEEDBACK_KIND_VSYNC)`.

#### Hook 3: `output->repaint()`
- **Step 1**: Retrieves active back-buffer FBO via `decon_backend_begin_frame(engine, &active_fbo)`.
- **Step 2**: Calls `weston_renderer->repaint_output(output, output_damage)` to composite Wayland client surfaces into the active FBO.
- **Step 3**: Dispatches scanout via `decon_backend_present(engine, &retire_fence)`.
- **Step 4**: Watches `retire_fence` using `wl_event_loop_add_fd()` on Weston's event loop. When the fence signals, calls `weston_output_finish_frame()` and closes the fence.

#### Hook 4: `output->destroy()`
- Detaches from event loop.
- Calls `decon_backend_teardown(engine)`.
- Frees `struct decon_output`.

---

## 4. Required postmarketOS Packaging Path

To deploy this backend on postmarketOS:

1. **Alpine APKBUILD Packaging**:
   - Package `src/compositor/` into `apkbuild` producing `/usr/lib/libdecon_backend.so`.
   - Package `src/bridge/` into `apkbuild` producing `/usr/lib/libbionic_musl_shim.so`.
2. **Weston Fork or Out-of-Tree Plugin**:
   - Compile `weston-backend-decon.c` against `weston-devel` (Alpine package `weston-dev`).
   - Install to `/usr/lib/libweston-10/backend-decon.so`.
3. **Launch Command**:
   - `weston --backend=backend-decon.so --shell=kiosk-shell`
   - With touchscreen input provided via standard `libinput` backend.

---

## 5. Summary Gap Matrix

| Component | Current State in Repository | Required State for Working Desktop | Engineering Effort |
| :--- | :--- | :--- | :--- |
| **DECON IOCTL Presentation** | **Complete & Verified** (`decon_backend.c`) | Complete & Verified | 0 hours |
| **ION DMA-BUF Swapchain** | **Complete & Verified** (2x 10.24 MB) | Complete & Verified | 0 hours |
| **Weston Output Plugin** | **Not Implemented** (Mock harness only) | C plugin implementing `weston_output` hooks | ~16-24 hours |
| **Bionic DDK on Musl** | **Unit Stubs Only** (`libbionic_musl_shim.so`) | `libhybris` or native `pan_kmod_kbase` | ~32-48 hours |
| **Wayland Touch Input** | **Not Implemented** | `libinput` integrated with `weston_seat` | ~8 hours |
| **postmarketOS Boot Image** | **Android 11 Active** | Booting postmarketOS rootfs with downstream kernel | ~12 hours |

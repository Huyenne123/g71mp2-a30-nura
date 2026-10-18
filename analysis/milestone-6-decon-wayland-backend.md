# Milestone 6 Analysis: Direct-DECON Wayland Compositor Backend

**Document ID**: `analysis/milestone-6-decon-wayland-backend.md`  
**Date**: October 18, 2026  
**Status**: **Verified on Physical Hardware**  
**Target Hardware**: Samsung Galaxy A30 (`SM-A305F`), Exynos 7904, Mali-G71 MP2  
**Display Hardware**: EA8076 Super AMOLED (1080x2340 FHD+, 60 Hz VSYNC)  

---

## 1. Executive Summary

In Milestone 4C and Milestone 5, we determined that downstream Samsung Exynos DECON does not support conventional Linux DRM KMS scanout (`/dev/dri/card0`) or legacy framebuffer mmap (`/dev/graphics/fb0` rejects `mmap()` with `-EINVAL`). Display scanout is exclusively controlled via the proprietary Samsung Display Controller ioctl `S3CFB_WIN_CONFIG` (`0x441046d1`).

To enable native Wayland desktop environments on postmarketOS (such as Phosh, Plasma Mobile, or Weston) without requiring an Android HWC2 daemon, we authored the **Direct-DECON Presentation Engine** (`src/compositor/decon_backend.c` and `src/compositor/libdecon_backend.so`).

This document details the modular backend architecture, the exact hardware structure layout, the swapchain lifecycle, and the Wayland compositor integration interface.

---

## 2. Architecture & Data Flow

The Direct-DECON engine serves as the presentation backend between a standard Wayland desktop compositor and the Exynos DECON display hardware:

```
+-----------------------------------------------------------------------------------+
|                        Wayland Compositor (Weston / TinyWL)                       |
+-----------------------------------------------------------------------------------+
       |                                                            |
       | Composits Desktop Layers                                   | Requests Swap
       v                                                            v
+-----------------------------------------------------------------------------------+
|              Direct-DECON Backend API (src/compositor/decon_backend.h)             |
|   decon_backend_init()           decon_backend_begin_frame()                      |
|   decon_backend_setup_egl()      decon_backend_present()                          |
|   decon_backend_setup_swapchain() decon_backend_wait_vsync()                      |
+-----------------------------------------------------------------------------------+
       |                                                            |
       | GPU Render to FBO                                          | Page Flip ioctl
       v                                                            v
+----------------------------------+         +--------------------------------------+
|    Mali-G71 MP2 (Bifrost r26p0)  |         |     Samsung DECON (/dev/graphics/fb0)|
|    Ping-Pong ION DMA-BUFs        |         |     S3CFB_WIN_CONFIG (0x441046d1)    |
|    Buffer 0: FBO 1 (10.24 MB)    |         |     Window 3 (IDMA Channel 0)        |
|    Buffer 1: FBO 2 (10.24 MB)    |         |     Hardware Retire Fence Output     |
+----------------------------------+         +--------------------------------------+
                                                            |
                                                            v
                                             +--------------------------------------+
                                             |  EA8076 Super AMOLED (1080x2340)     |
                                             |  Physical 60.0 Hz VSYNC Scanout      |
                                             +--------------------------------------+
```

---

## 3. Verified Hardware Structure Layout (1040 Bytes)

The Exynos 7904 DECON display controller requires an exact 1040-byte window configuration payload. Any deviation in structure alignment or field sizing corrupts stack memory or causes the kernel to reject presentation with `-EINVAL`.

### 3.1 `struct decon_win_config` (200 Bytes per Window)

```c
struct decon_win_config {
    int32_t  state;             /* Offset 0x00 (0)  : DECON_WIN_STATE_BUFFER = 2, DISABLED = 0 */
    int32_t  reserved0;         /* Offset 0x04 (4)  : 0 */
    int32_t  fd_idma[3];        /* Offset 0x08 (8)  : [0]=DMA-BUF fd, [1]=-1, [2]=-1 */
    int32_t  fence_fd;          /* Offset 0x14 (20) : -1 if pre-synced */
    int32_t  fence_reg;         /* Offset 0x18 (24) : 0 */
    uint32_t plane_alpha;       /* Offset 0x1C (28) : 0-255 (255 = fully opaque) */
    uint32_t blending;          /* Offset 0x20 (32) : DECON_BLENDING_NONE = 0 */
    uint32_t idma_type;         /* Offset 0x24 (36) : 0 (Primary IDMA channel) */
    uint32_t format;            /* Offset 0x28 (40) : DECON_PIXEL_FORMAT_RGBA_8888 = 2 */
    uint8_t  padding0[100];     /* Offset 0x2C (44) .. 0x8F (143): 100 bytes reserved */
    int32_t  src_x;             /* Offset 0x90 (144): source crop X */
    int32_t  src_y;             /* Offset 0x94 (148): source crop Y */
    uint8_t  padding_src[8];    /* Offset 0x98 (152) .. 0x9F (159): 8 bytes padding */
    uint32_t src_w;             /* Offset 0xA0 (160): source crop width (1080) */
    uint32_t src_h;             /* Offset 0xA4 (164): source crop height (2340) */
    int32_t  dst_x;             /* Offset 0xA8 (168): destination display X */
    int32_t  dst_y;             /* Offset 0xAC (172): destination display Y */
    uint32_t dst_w;             /* Offset 0xB0 (176): destination display width (1080) */
    uint32_t dst_h;             /* Offset 0xB4 (180): destination display height (2340) */
    uint32_t extra0;            /* Offset 0xB8 (184): 0 */
    uint32_t extra1;            /* Offset 0xBC (188): 0 */
    uint8_t  padding1[8];       /* Offset 0xC0 (192) .. 0xC7 (199): 8 bytes padding */
};

_Static_assert(sizeof(struct decon_win_config) == 200, "decon_win_config must be 200 bytes");
```

### 3.2 `struct decon_win_config_data` (1040 Bytes Total)

```c
struct decon_win_config_data {
    int32_t                 fence;        /* Offset 0x000 (0)   : [OUT] Hardware retire fence */
    int32_t                 reserved;     /* Offset 0x004 (4)   : 0 */
    struct decon_win_config config[5];    /* Offset 0x008 (8)   : 5 windows * 200 bytes = 1000 bytes */
    uint8_t                 reserved2[32];/* Offset 0x3F0 (1008): 32 bytes tail padding */
};

_Static_assert(sizeof(struct decon_win_config_data) == 1040, "decon_win_config_data must be 1040 bytes");
```

Compile-time static assertions guarantee binary layout integrity across target architectures.

---

## 4. Swapchain Presentation Lifecycle

The backend manages a zero-copy, double-buffered presentation loop:

### Step 1: Initialization (`decon_backend_init`)
- Opens `/dev/graphics/fb0` for DECON hardware control.
- Opens `/dev/ion` for physical memory management.

### Step 2: EGL/GLES Setup (`decon_backend_setup_egl`)
- Resolves EGL 1.4 and OpenGL ES 3.0 functions from `libGLES_mali.so`.
- Initializes `EGL_DEFAULT_DISPLAY`.
- Creates a 64x64 Pbuffer dummy surface and binds a GLES3 context.

### Step 3: Swapchain Allocation (`decon_backend_setup_swapchain`)
- Allocates two full-screen buffers (10,108,800 bytes each) via `ION_IOC_ALLOC` (`0xc0204900`) from heap mask 1 (`ion_noncontig_heap`).
- Exports kernel DMA-BUF file descriptors via `ION_IOC_SHARE` (`0xc0084904`) and closes userspace handles.
- Imports DMA-BUFs into EGL via `eglCreateImageKHR` (`EGL_LINUX_DMA_BUF_EXT`).
- Binds EGLImages to `GL_TEXTURE_2D` and creates OpenGL ES Framebuffer Objects (`gl_fbo`).

### Step 4: Frame Rendering & Presentation (`decon_backend_present`)
- The compositor binds the current back buffer FBO and renders the desktop scene.
- GPU commands are flushed and synchronized via `glFinish()`.
- Windows 0, 1, 2, and 4 are configured with `state = 0` (disabled).
- Window 3 is configured with `state = 2`, `format = 2`, `idma_type = 0`, and `fd_idma[0] = dma_buf_fd`.
- Dispatches `ioctl(fb_fd, 0x441046d1, &win_data)`, which initiates hardware scanout and returns a Linux sync file descriptor (`win_data.fence`).

### Step 5: Hardware VSYNC Synchronization (`decon_backend_wait_vsync`)
- Calls `poll()` with `POLLIN` on the hardware retire fence returned by DECON.
- Upon fence signaling (triggered by the EA8076 panel VSYNC IRQ), the fence FD is closed.
- Flips the active ping-pong index (`active_idx ^= 1`).

### Step 6: Teardown (`decon_backend_teardown`)
- Destroys FBOs, GL textures, EGLImages, contexts, and closes all DMA-BUF and device file descriptors.

---

## 5. Wayland Compositor Integration Guide

To integrate this backend into Weston, wlroots, or TinyWL on postmarketOS:

1. **Weston Output Setup**:
   - In `weston_output`, implement `output->repaint` using `decon_backend_begin_frame()` and `decon_backend_present()`.
   - Forward the retire fence from `decon_backend_present()` to `weston_output_finish_frame()`.
2. **Dynamic Linking**:
   - Link the compositor against `libdecon_backend.so` and `libbionic_musl_shim.so`.
3. **No DRM KMS Daemon Required**:
   - The compositor does not require `/dev/dri/card0` or DRM master privileges, operating entirely through standard `/dev/graphics/fb0` and `/dev/ion` node permissions.

# DECON DMA-BUF Presentation Architecture & HWC2 Arbitration (Milestone 4A)

- **Target Device**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **Display Controller**: Exynos DECON (`14860000.decon_f`, `samsung,exynos7885-decon`)
- **Display Panel**: EA8076 Super AMOLED (1080x2340 FHD+, DSI command mode)
- **Device Node**: `/dev/graphics/fb0` (Major 29, Minor 0)
- **Active HWC Process**: `android.hardware.graphics.composer@2.1-service` (PID 3935)
- **Presentation IOCTL**: `S3CFB_WIN_CONFIG` (`0x441046d1`)

---

## 1. Executive Summary

Having successfully proven in Milestones 4A.1 and 4A.2 that an ION-allocated DMA-BUF can be imported into the Mali-G71 GPU and rendered into via EGL/GLES, this report analyzes the final stage of the display pipeline: presenting the rendered DMA-BUF to the physical display panel via Samsung's DECON display controller (`/dev/graphics/fb0`).

This analysis establishes:
1. The exact C structure definition and field semantics for `S3CFB_WIN_CONFIG` (`0x441046d1`).
2. The role of Samsung HWC2 (`hwcomposer.exynos7904.so`) and Acrylic (`libacryl.so`) in arbitrating display scanout.
3. The display safety boundary: why concurrent userspace presentation while Android's HWC2 is running is strictly unsafe.
4. The standalone presentation architecture for postmarketOS, where Android services are absent and the display controller can be driven directly and exclusively.

```mermaid
flowchart TD
    subgraph Android Environment (Collision Risk)
        SF["SurfaceFlinger"] --> HWC["composer@2.1-service\n(PID 3935)"]
        HWC -->|Continuous VSync (60Hz)\nS3CFB_WIN_CONFIG (0x441046d1)| FB["/dev/graphics/fb0\n(DECON Driver)"]
        TEST["Independent Test Utility"] -.->|RACE CONDITION / COLLISION\nTearing / HWC Watchdog Crash| FB
    end

    subgraph postmarketOS Environment (Safe Direct Scanout)
        WAYLAND["Wayland Compositor (Weston / wlroots)"] --> COMP["Standalone DECON Presentation Engine"]
        COMP -->|1. Allocate Buffer| ION["/dev/ion"]
        ION -->|2. Export DMA-BUF fd| COMP
        COMP -->|3. Render via EGLImage| MALI["Mali-G71 GPU (/dev/mali0)"]
        COMP -->|4. Exclusive Presentation\nS3CFB_WIN_CONFIG (0x441046d1)| FB_PMOS["/dev/graphics/fb0\n(DECON Driver)"]
        FB_PMOS --> DPU["DECON Hardware IDMA Engine"]
        DPU --> PANEL["EA8076 Super AMOLED (1080x2340)"]
    end
```

---

## 2. The Presentation IOCTL: `S3CFB_WIN_CONFIG` (`0x441046d1`)

Disassembly of `ExynosDisplayFbInterface::deliverWinConfigData` (`0x70194`) in `libexynosdisplay.so` and cross-referencing with downstream Exynos kernel headers reveals the complete data structures required to configure DECON overlay windows:

### 2.1 Top-Level IOCTL Definition
- **Magic**: `'F'` (0x46)
- **Sequence Number**: `209` (0xD1)
- **Direction**: `_IOW` (Write from userspace to kernel)
- **Size**: 1040 bytes (`0x410`)
- **IOCTL Value**: `0x441046d1`

### 2.2 Data Structures
```c
#define DECON_WIN_UPDATE_IDX    0
#define S3CFB_WIN_CONFIG        0x441046d1

/* Window States */
enum {
    DECON_WIN_STATE_DISABLED = 0,
    DECON_WIN_STATE_COLOR    = 1,
    DECON_WIN_STATE_BUFFER   = 2,
    DECON_WIN_STATE_UPDATE   = 3
};

/* Blending Modes */
enum {
    DECON_BLENDING_NONE     = 0,
    DECON_BLENDING_PREMULT  = 1,
    DECON_BLENDING_COVERAGE = 2
};

/* Pixel Formats */
enum {
    DECON_PIXEL_FORMAT_ARGB_8888 = 0,
    DECON_PIXEL_FORMAT_ABGR_8888 = 1,
    DECON_PIXEL_FORMAT_RGBA_8888 = 2,
    DECON_PIXEL_FORMAT_BGRA_8888 = 3,
    DECON_PIXEL_FORMAT_RGB_565   = 4,
    DECON_PIXEL_FORMAT_NV12      = 5,
    DECON_PIXEL_FORMAT_NV21      = 6
};

/* Individual Hardware Overlay Window (144 bytes) */
struct decon_win_config {
    int32_t  state;             /* Offset 0x00: Window state (DECON_WIN_STATE_BUFFER = 2) */
    int32_t  fd_idma[3];        /* Offset 0x04: DMA-BUF fds: [0] = RGB/Y plane, [1] = U, [2] = V (-1 if unused) */
    int32_t  fence_fd;          /* Offset 0x10: Acquire sync fence fd (-1 if already signaled) */
    
    /* Destination Rectangle (Screen coordinates) */
    int32_t  dst_x;             /* Offset 0x14: Destination X on panel (0) */
    int32_t  dst_y;             /* Offset 0x18: Destination Y on panel (0) */
    uint32_t dst_w;             /* Offset 0x1C: Destination width on panel (1080) */
    uint32_t dst_h;             /* Offset 0x20: Destination height on panel (2340) */

    /* Source Crop Rectangle (Buffer coordinates) */
    int32_t  src_x;             /* Offset 0x24: Source crop X in buffer (0) */
    int32_t  src_y;             /* Offset 0x28: Source crop Y in buffer (0) */
    uint32_t src_w;             /* Offset 0x2C: Source crop width in buffer (1080) */
    uint32_t src_h;             /* Offset 0x30: Source crop height in buffer (2340) */

    uint32_t format;            /* Offset 0x34: Pixel format (e.g., DECON_PIXEL_FORMAT_BGRA_8888 = 3) */
    uint32_t blending;          /* Offset 0x38: Alpha blending mode (DECON_BLENDING_NONE = 0) */
    uint32_t fence_reg;         /* Offset 0x3C: Hardware fence register index */
    uint32_t plane_alpha;       /* Offset 0x40: Global plane opacity (255 for opaque) */
    uint8_t  color_space;       /* Offset 0x44: Color space / standard */
    uint8_t  padding0[3];       /* Offset 0x45: 3 bytes padding */
    uint32_t rot;               /* Offset 0x48: Rotation / flip transform flags */
    uint8_t  padding1[68];      /* Offset 0x4C: Extended padding / reserved fields (Total 144 bytes) */
};

/* Complete Presentation Payload (1040 bytes) */
struct decon_win_config_data {
    int32_t                 fence;       /* Offset 0x000: [OUT] Retire / release fence fd */
    struct decon_win_config config[7];   /* Offset 0x004: 7 overlay windows (7 * 144 = 1008 bytes) */
    uint8_t                 reserved[28];/* Offset 0x3F4: Reserved / HDR / color metadata */
};
```

---

## 3. Hardware Composer & DECON Presentation Safety Assessment

### 3.1 Active Scanout Ownership on Running Device
On the running Android test device:
- Process `android.hardware.graphics.composer@2.1-service` (PID 3935) holds an exclusive, continuous file handle to `/dev/graphics/fb0`.
- SurfaceFlinger queues rendered application layers to HWC2 at 60 Hz.
- HWC2 assigns visible layers across DECON overlay windows 0 to 6, programs `decon_win_config_data`, and issues `S3CFB_WIN_CONFIG` on every VSync tick.

### 3.2 Collision & Hazard Analysis
1. **Absence of Hardware Arbitration**: Unlike DRM/KMS (which supports `drmModeAtomicCommit` with master leases and atomic state locks), the legacy DECON FB driver has no arbitration between competing userspace processes.
2. **Race Condition Hazard**: If an independent test process writes to `S3CFB_WIN_CONFIG` while HWC2 is running:
   - DECON IDMA channels are reprogrammed asynchronously during active scanout.
   - Screen tearing, frame stuttering, and panel sync loss occur.
   - HWC2's internal watchdog thread (`HWCWatchdogThread`) detects missing vsync/fence retirement, triggers an assert, and restarts the Android system graphics stack.
3. **No Independent Hardware Test Window**: All 7 hardware windows (`config[0]` through `config[6]`) are dynamically reclaimed and reconfigured by HWC2 on every compositing cycle. No window can be safely "reserved" out-of-band.

### 3.3 Strict Adherence to Project Rules
In accordance with project constraints:
> *"Never race the running HWC2 service with an independent display submission."*
> *"Do not disable HWC2 or replace an active scanout configuration without a documented recovery mechanism."*
> *"Never report presentation success unless the rendered buffer is actually displayed on the physical screen."*

Because physical presentation on the active Android system would require either racing HWC2 or killing system daemons, **Milestone 4A safely halts physical presentation at the verified DMA-BUF rendering boundary**.

---

## 4. Standalone Recovery Mechanism for Offline Presentation Testing

If dedicated physical scanout testing is executed on an Android-based development build, the graphics services must be safely stopped and restored using the following documented procedure:

### 4.1 Safe Teardown of Android Graphics Services
```bash
# Stop SurfaceFlinger and Hardware Composer to release /dev/graphics/fb0
adb shell "su -c 'stop surfaceflinger'"
adb shell "su -c 'stop vendor.hwcomposer-2-1'"
```
*Effect*: Releases file descriptor locks on `/dev/graphics/fb0` and `/dev/ion`. Screen halts on the last scanned out buffer or turns black.

### 4.2 Standalone Presentation Execution
With Android services inactive:
1. Open `/dev/ion` and allocate a full-screen scanout buffer (`1080 * 2340 * 4 = 10,108,800` bytes).
2. Export `dma_buf_fd`.
3. Render target pattern via Mali-G71 GPU (EGLImage -> FBO -> GLES draw).
4. Open `/dev/graphics/fb0`.
5. Populate `struct decon_win_config_data`:
   - `config[0].state = DECON_WIN_STATE_BUFFER;`
   - `config[0].fd_idma[0] = dma_buf_fd;`
   - `config[0].format = DECON_PIXEL_FORMAT_BGRA_8888;`
   - `config[0].dst_w = 1080; config[0].dst_h = 2340;`
   - `config[0].src_w = 1080; config[0].src_h = 2340;`
   - Disable windows 1 to 6 (`state = DECON_WIN_STATE_DISABLED`).
6. Issue `ioctl(fb_fd, S3CFB_WIN_CONFIG, &win_data)`.
7. Physical screen displays the GPU-rendered frame.

### 4.3 Clean Restoration of Android Graphics Services
```bash
# Restore normal Android graphics operation
adb shell "su -c 'start vendor.hwcomposer-2-1'"
adb shell "su -c 'start surfaceflinger'"
```
*Effect*: Restores Android UI without requiring a device reboot.

---

## 5. Architectural Implementation for postmarketOS

In the target postmarketOS environment (native Linux / Alpine), Android's `surfaceflinger` and `hwcomposer` services do not exist. This eliminates the arbitration conflict entirely:

1. **Exclusive Compositor Ownership**: The postmarketOS Wayland compositor (Weston, Sway, or KWin) acts as the single, exclusive owner of `/dev/graphics/fb0`.
2. **Presentation Backend**:
   - A dedicated DECON backend (`backend-decon` or `hwc2` libhybris wrapper) allocates presentation buffers from `/dev/ion`.
   - The compositor renders the desktop scene into the ION DMA-BUFs using the Mali-G71 GPU (`libGLES_mali-64.so`).
   - The compositor submits the completed frame to DECON via `S3CFB_WIN_CONFIG`.
3. **Synchronization**: Sync fences returned by `eglDupNativeFenceFDANDROID` are passed directly in `config[0].fence_fd`, allowing DECON to wait for GPU completion in hardware before scanout.

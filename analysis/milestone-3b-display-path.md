# Display Presentation Path: Samsung Exynos DECON & Android HWC Architecture (Milestone 3B)

- **Target Device**: Samsung Galaxy A30 (`SM-A305F` / `universal7904`)
- **Display Controller**: Exynos DECON (`14860000.decon_f`, `samsung,exynos7885-decon`)
- **Display Panel**: EA8076 Super AMOLED (1080x2340 FHD+, DSI command mode)
- **Active Kernel Config**: `CONFIG_EXYNOS_DECON_FB=y`, `CONFIG_EXYNOS_DECON_7885=y`, `CONFIG_EXYNOS_DECON_LCD_EA8076_A30=y`, `# CONFIG_DRM is not set`
- **Device Node**: `/dev/graphics/fb0` (Major 29, Minor 0)

---

## 1. Executive Summary

Hardware interrogation, sysfs inspection, and binary disassembly of Samsung vendor graphics libraries (`libexynosdisplay.so`, `libacryl.so`, `gralloc.exynos7904.so`) establish that the downstream Samsung kernel **does not use standard Linux framebuffer scanout (`/dev/fb0` mmap/write)**.

Instead, the DECON hardware operates as a dynamic multi-window overlay compositing engine. Memory scanout is performed by DECON IDMA channels directly fetching from **ION DMA-BUFs** passed via a proprietary ioctl command: **`S3CFB_WIN_CONFIG` (`0x441046d1`)**. Conventional `mmap()` on `/dev/graphics/fb0` fails with `-EINVAL` (-22) because no static framebuffer memory is allocated in physical RAM.

```mermaid
flowchart TD
    subgraph Android Userspace
        A["App / Surface / EGL"] -->|1. Allocate| B["Mali Gralloc (gralloc.exynos7904.so)"]
        B -->|2. Allocate Memory| C["Samsung Exynos ION (/dev/ion)"]
        C -->|3. Export DMA-BUF fd| B
        B -->|4. Pass native_handle| A
        A -->|5. Render via GPU| D["libGLES_mali-64.so"]
        D -->|6. KBASE_IOCTL_MEM_IMPORT (0xc0188016)| E["mali_kbase (/dev/mali0)"]
        E -->|7. 3D Render to ION buffer| G["Mali-G71 GPU Hardware"]
        A -->|8. QueueBuffer + Sync Fence| H["SurfaceFlinger"]
        H -->|9. Validate & Assign Layers| I["HWC2 Daemon (composer@2.1-service)"]
        I -->|10. libexynosdisplay.so| J["ExynosDisplayFbInterface"]
        J -->|11. S3CFB_WIN_CONFIG (0x441046d1)| K["DECON FB Driver (/dev/graphics/fb0)"]
    end

    subgraph Kernel & Hardware
        K -->|12. Program IDMA Window Registers| L["DECON Hardware (14860000.decon_f)"]
        L -->|13. Direct DMA Fetch from ION RAM| M["Physical System RAM (0x80000000+)"]
        L -->|14. MIPI-DSI Interface| N["EA8076 Super AMOLED Panel"]
    end
```

---

## 2. DECON Framebuffer Hardware Interrogation

Executing the standalone probe [`tests/fb_probe`](file:///home/neyuh/mali-a30/tests/fb_probe.c) directly against `/dev/graphics/fb0` on physical hardware yielded the following verified parameters:

### 2.1 Variable Screen Information (`FBIOGET_VSCREENINFO` / `0x4600`)
- **Visible Resolution**: `1080 x 2340` (pixels)
- **Virtual Resolution**: `1080 x 2340`
- **Offset (`xoffset`, `yoffset`)**: `0, 0`
- **Bits Per Pixel**: `32 bpp`
- **Channel Bitfield Layout**:
  - **Red**: `offset = 16`, `length = 8`, `msb_right = 0`
  - **Green**: `offset = 8`, `length = 8`, `msb_right = 0`
  - **Blue**: `offset = 0`, `length = 8`, `msb_right = 0`
  - **Alpha (Transp)**: `offset = 24`, `length = 8`, `msb_right = 0`
- **Pixel Format Identification**: **`BGRA_8888`** (in 32-bit little-endian word representation: `0xAARRGGBB`).
- **Rotation**: `0` degrees.

### 2.2 Fixed Screen Information (`FBIOGET_FSCREENINFO` / `0x4602`)
- **Scanout Line Length (Stride)**: `4320` bytes/row (`1080 * 4 = 4320`, tightly packed without padding).
- **Reported `smem_len`**: `20,217,600` bytes (exactly `1080 * 2340 * 4 * 2` bytes = **2 full frames / double buffered**).
- **Reported `smem_start`**: `0x0000000010000000`.

### 2.3 Physical Memory Map & Mmap Rejection Analysis
Cross-referencing `/proc/iomem` on the target device:
```text
11500000-11504fff : 11500000.mali
14860000-1486ffff : /decon_f@0x14860000
80000000-bbbfffff : System RAM
c0000000-ffffffff : System RAM
880000000-8bfffffff : System RAM
```
- **Physical Memory Reality**: Physical System RAM begins at `0x80000000`. The address `0x10000000` (`smem_start`) lies completely outside valid physical RAM.
- **Mmap Result**: Executing `mmap(NULL, smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)` returns **`-EINVAL` (-22)**.
- **Root Cause**: The DECON driver does not allocate static contiguous RAM for scanout. `info->screen_base` is `NULL`, and `smem_start = 0x10000000` is a placeholder. `decon_mmap` explicitly asserts against this and rejects userspace mappings.

---

## 3. The Presentation IOCTL: `S3CFB_WIN_CONFIG` (`0x441046d1`)

Disassembly of `ExynosDisplayFbInterface::deliverWinConfigData` (`0x70194`) in [`libexynosdisplay.so`](file:///home/neyuh/mali-a30/artifacts/proprietary/libexynosdisplay.so) reveals the exact mechanism used by Samsung's Hardware Composer:

```asm
   703f4: ldr  w8, [x19, #0x18]         ; Load fd (/dev/graphics/fb0)
   703f8: mov  w1, #0x46d1              // =18129 (type 'F', nr 209)
   703fc: mov  x22, x0
   70400: movk w1, #0x4410, lsl #16     // =0x441046d1 (_IOW('F', 209, size 1040))
   70404: mov  w0, w8                   ; fd
   70408: mov  x2, x21                  ; struct decon_win_config_data *
   7040c: bl   ioctl@plt
```

### 3.1 Structure Layout & Field Semantics

- **Command**: `_IOW('F', 209, struct decon_win_config_data)` -> `0x441046d1`
- **Total Payload Size**: `1040` bytes (`0x410`)
- **Structure Breakdown**:
  - `struct decon_win_config windows[7]`: 7 hardware window overlay structures (144 bytes each, total 1008 bytes).
  - Window structure fields (disassembled from `configFromDisplayConfig` @ `0x6fd98`):
    - `uint32_t state`: Window state (`0 = DISABLED`, `1 = COLOR`, `2 = BUFFER`, `4 = OVERLAY`).
    - `int fd_idma[3]`: Array of DMA-BUF file descriptors for image planes (Y, U, V or RGB).
    - `int fence_fd`: Synchronization fence file descriptor (acquired from GPU rendering completion).
    - `int dst_x, dst_y, dst_w, dst_h`: Destination scanout rectangle on display.
    - `int src_x, src_y, src_w, src_h`: Source crop rectangle in buffer.
    - `uint32_t format`: DECON DPU pixel format (e.g., `DECON_PIXEL_FORMAT_BGRA_8888`, `DECON_PIXEL_FORMAT_RGBA_8888`).
    - `uint32_t rot`: Hardware rotation/flip flags.
    - `uint32_t blend_mode`: Alpha blending mode (`BLENDING_NONE`, `BLENDING_PREMULT`, `BLENDING_COVERAGE`).
    - `uint32_t plane_alpha`: Global plane opacity (0-255).
  - Trailing synchronization payload (32 bytes):
    - `int retire_fence`: Return fence file descriptor signaled when the frame scanout completes.

---

## 4. Hardware Composer & Supporting HAL Roles

| Component | Shared Object | Role in Display Pipeline |
| :--- | :--- | :--- |
| **Android HWC2 HAL** | `hwcomposer.exynos7904.so` | Implements Android HWC2 interface (`HWCWatchdogThread`, display hotplug). |
| **Exynos Display Engine** | `libexynosdisplay.so` | Formats `decon_win_config`, translates Android `android_dataspace_t` to DPU format, issues `S3CFB_WIN_CONFIG`. |
| **Acrylic 2D Blitter** | `libacryl.so` | Hardware 2D scaler/rotator engine (`fimg2d` / G2D / V4L2 M2M `M2M1SHOT2`) for format adaptation and offloading non-GPU compositing. |
| **Mali Gralloc** | `gralloc.exynos7904.so` | Central allocator using Samsung Exynos ION (`/dev/ion`, `exynos_ion_alloc`) to create shareable DMA-BUFs. |

---

## 5. Confirmed Facts vs Hypotheses

### Confirmed Facts
1. DECON driver on Exynos 7904 runs under `/dev/graphics/fb0` with major 29, minor 0.
2. The active physical screen is an EA8076 Super AMOLED at 1080x2340 with 4320 bytes stride and BGRA8888 channel layout (`FIND-011`).
3. Standard Linux FBDev `mmap()` is unsupported and returns `-EINVAL` because no static scanout memory is allocated in RAM (`FIND-012`).
4. Display presentation is performed exclusively via `S3CFB_WIN_CONFIG` (`0x441046d1`) with DMA-BUF file descriptors (`FIND-013`).
5. Only `composer@2.1-service` (HWC2 daemon, PID 3935) holds `/dev/graphics/fb0` open on the running system.

### Hypotheses & Unverified Areas
1. Whether `S3CFB_WIN_CONFIG` can be issued by an independent non-Android userspace daemon without `SurfaceFlinger` running.
2. Whether a simple DRM/KMS bridge (such as TinyDRM or an Exynos DRM port) can be backported to Linux 4.4 to expose standard `/dev/dri/card0`.

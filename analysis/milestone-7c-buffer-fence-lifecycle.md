# Milestone 7C Phase C: Buffer and Fence Ownership & Synchronization Lifecycle

## 1. Executive Summary

This document establishes the formal buffer and fence ownership, lifetime, and synchronization specification for the zero-copy presentation pipeline bridging ARM Mali-G71 OpenGL ES rendering to Samsung DECON hardware scanout on the Samsung Galaxy A30 (`SM-A305F`, Exynos 7904 SoC).

The architecture couples Android's hardware abstraction layer (`libhybris-hwcomposerwindow`, `gralloc.exynos7904.so`) with the Linux kernel downstream display driver (`exynos-decon`, `S3CFB_WIN_CONFIG_IOCTL`). To achieve true zero-copy operation without torn frames, pipeline stalls, or file descriptor leaks, every stage of buffer allocation, fence creation, hardware consumption, and resource reclamation must strictly follow an explicit ownership contract.

---

## 2. End-to-End Presentation Architecture & Flow

```mermaid
sequenceDiagram
    autonumber
    participant EGL as Mali GLES (Producer)
    participant WIN as HWComposerNativeWindow
    participant PRESENT as Presentation Callback
    participant DECON as DECON HW Driver (/dev/graphics/fb0)
    participant PANEL as EA8076 Display Panel

    EGL->>WIN: dequeueBuffer(&buf, &fenceFd)
    WIN-->>EGL: Return ION DMA-BUF (Wait on previous retire fence if set)
    EGL->>EGL: glClear / glDrawArrays (Hardware GPU Rendering)
    EGL->>WIN: eglSwapBuffers() -> queueBuffer(buf, gpu_acquire_fence)
    WIN->>PRESENT: present(buf) (Delivers DMA-BUF FD & GPU Acquire Fence)
    PRESENT->>DECON: ioctl(S3CFB_WIN_CONFIG_IOCTL) (Window 3, DMA-BUF, fence_fd=gpu_acquire_fence)
    Note over PRESENT: Presenter immediately closes its own acquire_fence copy
    Note over DECON: Kernel consumes acquire_fence; waits for GPU before scanout
    DECON-->>PRESENT: Returns win_data.fence (Retire) & rel_fence
    Note over PRESENT: Closes all unused rel_fences (FIND-037)
    PRESENT->>WIN: HWCNativeBufferSetFence(buf, dup(retire_fence))
    PRESENT->>PRESENT: poll(retire_fence, 100ms) (Paces presentation to physical 60 Hz VSYNC)
    DECON->>PANEL: Scanout frame to physical AMOLED display
    PANEL-->>DECON: TE Interrupt (IRQ 249 VSYNC signal)
    Note over PRESENT: close(retire_fence)
```

---

## 3. Buffer Geometry & Memory Contract

| Parameter | Specification | Hardware Constraint & Verification Rationale |
| :--- | :--- | :--- |
| **Resolution** | 1080 x 2340 pixels | Samsung EA8076 Super AMOLED native display panel geometry. |
| **Color Format** | `HAL_PIXEL_FORMAT_RGBA_8888` (0x1) | Matches DECON Window 3 format `DECON_PIXEL_FORMAT_RGBA_8888` (2). 4 bytes/pixel. |
| **Stride** | **1088 pixels** (4352 bytes) | Exynos 7904 and Mali-G71 require 64-byte row alignment: `ALIGN(1080 * 4, 64) = 4352` bytes = 1088 pixels. Allocations without 1088 stride result in skewed rendering. |
| **Buffer Count** | **3 buffers** (Triple Buffering) | Swapchain managed by `HWComposerNativeWindow`: Buffer N being scanned by DECON, Buffer N-1 waiting on retire fence, Buffer N-2 dequeued by Mali for rendering. |
| **Gralloc Flags** | `0x00000B00` | `GRALLOC_USAGE_HW_RENDER` (0x200) \| `GRALLOC_USAGE_HW_COMPOSER` (0x800) \| `GRALLOC_USAGE_HW_TEXTURE` (0x100). |
| **Memory Heap** | Exynos ION `EXYNOS_ION_HEAP_SYSTEM_MASK` | Zero-copy physically contiguous or IOMMU-mapped system memory shared via DMA-BUF file descriptors. |

---

## 4. Complete Buffer & Fence Lifecycle Matrix

| Stage | Resource / Entity | Created By | Consumed By | Ownership / Closing Responsibility | Failure Hazard If Mismanaged |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Allocation** | Gralloc Native Buffer (`native_handle_t`) | `gralloc.exynos7904.so` (`hybris_gralloc_allocate`) | `HWComposerNativeWindow` buffer pool | `HWComposerNativeWindowBuffer` destructor calls `hybris_gralloc_release()`. | Memory leak in ION heap if window destroyed improperly. |
| **2. Export** | ION DMA-BUF FD (`handle->data[0]`) | Exynos ION driver (`ION_IOC_SHARE`) | Mali GPU & Samsung DECON | Created once per buffer during pool allocation. Lifetime matches window lifetime. | Premature close invalidates DMA mapping; causes `SEGV_MAPERR`. |
| **3. Acquire Fence** | GPU Sync Fence (`acquire_fence`) | ARM Mali DDK during command completion | Samsung DECON hardware | Created by Mali during `queueBuffer()`. Presenter passes it to DECON `fence_fd`. **Presenter must close its own fd after ioctl**. | Leaking 1 file descriptor per frame (reaches `EMFILE` in ~17 seconds at 60 FPS). |
| **4. DECON Submission** | `decon_win_config_data` (1040 bytes) | Userspace presentation loop | Linux Kernel `exynos-decon` driver | Allocated on userspace stack; passed via `ioctl(fb_fd, S3CFB_WIN_CONFIG_IOCTL)`. | Invalid struct layout causes `EINVAL` (45) or display glitching. |
| **5. Release Fence** | `win_data.config[3].rel_fence` | Linux Kernel `sync_fence` | Producer (or closed if unused) | DECON driver creates rel_fence for every active window. **Userspace MUST close all `rel_fence >= 0`**. | Leaking 1 FD per frame (resolved in `FIND-037`). |
| **6. Retire Fence** | `win_data.fence` | Linux Kernel `sync_fence` | Presenter & Next Dequeue | DECON creates retire fence representing frame scanout completion. Presenter duplicates FD for `HWCNativeBufferSetFence`, polls original for 60Hz pacing, and closes original. | Frame pacing collapse; tearing; or FD leakage. |
| **7. Reuse** | Dequeued Native Buffer | `HWComposerNativeWindow` buffer queue | Mali GLES Producer | When EGL calls `dequeueBuffer()`, `HWComposerNativeWindow` transfers `fenceFd` to Mali. Mali waits on fence before rendering. | Race condition: Mali overwrites frame while DECON is actively scanning it out to AMOLED panel. |

---

## 5. Synchronization & Pacing Analysis

### 5.1. Zero-Copy Hazard Prevention
In a zero-copy pipeline, the CPU never copies pixels; the GPU writes directly into the same physical buffer that DECON reads. Two distinct race conditions are prevented by the dual-fence model:

1. **Write-after-Read Hazard (Scanout Overwrite)**:
   - If Mali renders faster than the 60 Hz display refresh rate, it could overwrite a buffer that DECON is actively transmitting over MIPI-DSI to the EA8076 panel.
   - **Protection**: `HWCNativeBufferSetFence(buf, dup(retire_fence))` sets the buffer's internal fence. When Mali attempts to dequeue that buffer on a subsequent frame, `dequeueBuffer()` returns the retire fence to Mali. The GPU driver pauses command submission to that buffer until the retire fence signals.

2. **Read-after-Write Hazard (Incomplete Frame Scanout)**:
   - If DECON scans out a buffer before Mali finishes rasterizing all primitives, partial or corrupted frames (tearing / artifacts) appear on the panel.
   - **Protection**: EGL creates an `acquire_fence` representing GPU command queue completion. Presenter puts this fence into `win_data.config[3].fence_fd`. DECON hardware halts IDMA channel 0 reading until the acquire fence signals.

### 5.2. File Descriptor Leak Prevention (`FIND-037` and `FIND-045`)
The Linux sync fence subsystem generates a new integer file descriptor for every created fence:
- **`rel_fence`**: For each window config in `struct decon_win_config_data`, `exynos-decon` returns a newly created release fence. Userspace must iterate through `config[0..4]` and close any `rel_fence >= 0`. Failure to do so leaks 60 FDs/sec (`FIND-037`).
- **`acquire_fence`**: In `present_cb`, `HWCNativeBufferGetFence(buf)` returns the GPU acquire fence created by Mali. When userspace passes this FD to `win_data.config[3].fence_fd`, the kernel takes an internal file reference via `sync_fence_fdget()`. The userspace FD remains open and is NOT closed by the kernel. Userspace **MUST call `close(acquire_fence)` immediately following the ioctl**. Failure to do so leaks 60 FDs/sec (`FIND-045`).

Closing both fences properly guarantees exactly **0 FDs leaked per frame**, enabling indefinite stability.

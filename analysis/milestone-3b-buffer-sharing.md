# GPU Buffer Export & Sharing Architecture: Mali-G71 & Exynos ION (Milestone 3B)

- **Target Device**: Samsung Galaxy A30 (`SM-A305F`)
- **GPU Driver**: Mali Bifrost DDK r26p0 (`libGLES_mali-64.so`)
- **Kernel Kbase Driver**: `mali_kbase` r26p0 (UK ABI 11.27, `/dev/mali0`)
- **Memory Allocator**: Samsung Exynos ION (`/dev/ion`, `CONFIG_ION_EXYNOS=y`, `libion_exynos.so`)
- **Display Controller**: Exynos DECON (`/dev/graphics/fb0`, `CONFIG_EXYNOS_DECON_FB=y`)

---

## 1. Executive Summary

A critical architectural determination of this investigation is that **`mali_kbase` is strictly an importer of external memory, not an exporter**:
1. Internal GPU allocations created via `KBASE_IOCTL_MEM_ALLOC` (`0xc0208005`) **cannot be exported as DMA-BUFs** to other Linux subsystems or scanout controllers.
2. In the Samsung Exynos graphics architecture, **ION (`/dev/ion`) serves as the universal memory allocator**.
3. All shareable display surfaces must be allocated first in ION, exported as DMA-BUFs, and then imported symmetrically into both the GPU (`mali_kbase`) and the display engine (`exynos_decon`).

```mermaid
flowchart LR
    A["Universal Allocator: /dev/ion (Exynos ION)"] -->|ION_IOC_ALLOC & SHARE| B["DMA-BUF File Descriptor (fd)"]
    
    subgraph GPU Pathway
        B -->|KBASE_IOCTL_MEM_IMPORT (0xc0188016)| C["mali_kbase (/dev/mali0)"]
        C -->|Assign GPU VA & mmap| D["Mali-G71 3D Shader Cores"]
    end

    subgraph Scanout Pathway
        B -->|S3CFB_WIN_CONFIG (0x441046d1)| E["DECON FB Driver (/dev/graphics/fb0)"]
        E -->|Program IDMA Window| F["DECON Scanout Controller"]
    end
```

---

## 2. Allocation Types & Capabilities Matrix

Audit of machine code in `libGLES_mali-64.so` and physical validation confirms the precise capabilities of each buffer type:

| Allocation Method | Allocator | CPU Mapped? | GPU Mapped? | Exportable as DMA-BUF? | Usable as DECON Scanout? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`KBASE_IOCTL_MEM_ALLOC`** (`0xc0208005`) | `mali_kbase` | Yes (`mmap64` on `/dev/mali0`) | Yes (Mali MMU) | **No** (No export ioctl exists) | **No** (Rejected by DECON) |
| **`exynos_ion_alloc`** (`/dev/ion`) | `ion_exynos` | Yes (`mmap` on DMA-BUF fd) | Yes (via `MEM_IMPORT`) | **Yes** (Native DMA-BUF fd) | **Yes** (via `fd_idma`) |
| **Pbuffer Surface** (`EGLSurface`) | `libGLES_mali` | Yes (`glReadPixels` CPU copy) | Yes (Private GPU memory) | **No** (Unshared internal surface) | **No** (Requires CPU copy or blit) |
| **Gralloc Native Buffer** | `gralloc` | Yes (`mali_gralloc_lock`) | Yes (`EGLClientBuffer` import) | **Yes** (Exported in `private_handle_t`) | **Yes** (Direct HWC2 scanout) |

---

## 3. Disassembly Analysis: `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`)

Disassembly of `base_mem_import_padded` (`0xacc7dc`) in `libGLES_mali-64.so`:

```asm
0000000000acc7dc <base_mem_import_padded>:
  ...
  acc814: bl   0xacdd58 <uku_get_fd>
  acc818: mov  w1, #0x8016              // =32790 (nr 0x16)
  acc81c: add  x2, sp, #0x8             // struct kbase_ioctl_mem_import *
  acc820: movk w1, #0xc018, lsl #16     // =0xc0188016 (_IOWR(0x80, 0x16, 24 bytes))
  acc824: bl   ioctl@plt
  acc828: cmn  w0, #0x1                 // Check return == -1
  acc82c: b.eq 0xacc8b8                 // Error exit
  acc830: ldp  x8, x21, [sp, #0x8]      // x21 = Assigned GPU Virtual Address
  ...
  acc864: bl   0xacdd58 <uku_get_fd>    // fd of /dev/mali0
  acc868: mov  w4, w0                   // fd
  acc86c: mov  w3, #0x1                 // MAP_SHARED
  acc870: mov  x0, xzr                  // addr = NULL
  acc874: mov  x1, x22                  // length = pages << 12
  acc878: mov  w2, w23                  // prot (PROT_READ | PROT_WRITE)
  acc87c: mov  x5, x21                  // offset = GPU VA returned by ioctl
  acc880: bl   mmap64@plt
```

### 3.1 Structure Layout & Semantics
```c
struct kbase_ioctl_mem_import {
    uint64_t phandle;  /* [IN]  DMA-BUF file descriptor */
    uint32_t type;     /* [IN]  BASE_MEM_IMPORT_TYPE_UMM = 1 (DMA-BUF) */
    uint32_t flags;    /* [IN]  BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR */
    uint64_t va_pages; /* [OUT] Number of 4KB pages allocated in GPU VA */
};
```
1. Userspace passes an open DMA-BUF file descriptor in `phandle` with `type = 1` (`BASE_MEM_IMPORT_TYPE_UMM`).
2. The kernel `mali_kbase` calls `dma_buf_get(fd)`, maps the buffer's scatterlist through the Mali MMU page tables, and returns the assigned base GPU VA.
3. Userspace optionally maps the buffer into CPU virtual memory by issuing `mmap64()` against `/dev/mali0`, using the **returned GPU VA as the mmap offset**.

---

## 4. Synchronization & Cache Coherency Architecture

Exynos 7904 integrates Cortex-A73/A53 CPU cores with a Bifrost Mali-G71 GPU and DECON display controller. The interconnect does not maintain full hardware cache coherency across non-CPU masters (GPU and DECON):

### 4.1 Cache Maintenance Primitives
- **GPU Cache Flush / Invalidation**:
  - `KBASE_IOCTL_MEM_SYNC` (`0x4020800f`, size 32 bytes):
    ```c
    struct kbase_ioctl_mem_sync {
        uint64_t handle; /* Base GPU VA */
        uint64_t user_addr;
        uint64_t size;
        uint8_t  type;   /* SYNC_TO_CPU = 1, SYNC_TO_DEVICE = 2 */
        uint8_t  padding[7];
    };
    ```
- **ION DMA-BUF CPU Cache Synchronization**:
  - `exynos_ion_sync_start()` / `exynos_ion_sync_end()` in `libion_exynos.so`:
    Issues `DMA_BUF_IOCTL_SYNC` (`0x40086200`) with flags `DMA_BUF_SYNC_START` / `DMA_BUF_SYNC_END` and `DMA_BUF_SYNC_RW`.

### 4.2 Hardware Fence Synchronization
- Android Sync Framework (`sync_file`):
  - When the GPU completes fragment rendering of a frame, `mali_kbase` signals an **out-fence** file descriptor associated with the atom job chain (`BASE_JD_REQ_FENCE`).
  - SurfaceFlinger transfers this fence fd to the HWC2 daemon.
  - HWC2 embeds this fence fd into `decon_win_config.fence_fd` when issuing `S3CFB_WIN_CONFIG` (`0x441046d1`).
  - DECON hardware waits for the fence to signal before latching the buffer into its IDMA registers at the next VSYNC pulse.
  - DECON returns a **retire fence** (`retire_fence`), which signals when the scanout is finished, releasing the buffer back to the rendering queue.

---

## 5. Architectural Consequences for postmarketOS

1. **Standalone Pbuffer Rendering (Validated in Milestone 3A)**:
   - Does not require ION, DMA-BUF, or DECON.
   - Ideal for headless offscreen rendering, compute, and test pipelines.
2. **Direct Display Scanout (Milestone 3B)**:
   - Cannot rely on standard Linux `/dev/fb0` `mmap()`.
   - Requires an ION memory client and a presentation bridge that packages DMA-BUFs into `struct decon_win_config_data` and dispatches `S3CFB_WIN_CONFIG`.
   - Alternatively, a TinyDRM / Exynos DRM kernel driver backport would eliminate the proprietary DECON ioctl entirely by providing standard `/dev/dri/card0` KMS scanout.

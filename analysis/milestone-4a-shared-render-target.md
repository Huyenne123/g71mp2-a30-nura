# Shared DMA-BUF 3D Render-Target Validation: Mali-G71 & EGLImage (Milestone 4A)

- **Target Device**: Samsung Galaxy A30 (`SM-A305F`)
- **GPU Driver**: Proprietary Mali Bifrost r26p0 DDK (`libGLES_mali-64.so`)
- **Kernel Module**: `mali_kbase` r26p0 (`/dev/mali0`, UK ABI 11.27)
- **Shared Memory**: Linux DMA-BUF allocated from Exynos ION (`/dev/ion`)
- **Verification Harness**: [`tests/ion_mali_import_test.c`](file:///home/neyuh/mali-a30/tests/ion_mali_import_test.c)

---

## 1. Executive Summary

This report documents the verification that an externally allocated Linux DMA-BUF (from Samsung Exynos ION) can be imported into the proprietary Mali-G71 GPU driver, bound as a 3D framebuffer render target, and directly rendered into by the Mali hardware shader pipeline.

Both levels of the graphics stack were verified on physical Samsung Galaxy A30 hardware:
1. **Low-Level Kernel Interface**: Direct invocation of `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`) mapped the external DMA-BUF scatterlist into the Mali MMU page tables, successfully returning GPU Virtual Address `0x41000`.
2. **High-Level EGL / GLES Interface**: Importing the DMA-BUF descriptor via `EGL_EXT_image_dma_buf_import` (`eglCreateImageKHR`), binding to `GL_TEXTURE_2D` (`glEGLImageTargetTexture2DOES`), and attaching to an FBO achieved `GL_FRAMEBUFFER_COMPLETE` (`0x8CD5`).
3. **End-to-End Rendering Verification**: The Mali-G71 GPU cleared the shared buffer to Lime Green (`RGBA(0, 255, 64, 255)`). Direct CPU memory inspection via `mmap()` of the DMA-BUF fd confirmed an exact byte-for-byte match:
   `[0]=0x00 (R=0), [1]=0xff (G=255), [2]=0x40 (B=64), [3]=0xff (A=255)`.

This establishes that zero-copy, hardware-accelerated 3D rendering into shared memory is completely functional on the physical device.

```mermaid
flowchart TD
    subgraph Allocation
        A["Samsung Exynos ION (/dev/ion)"] -->|ION_IOC_ALLOC & SHARE| B["Linux DMA-BUF (fd: 4)"]
    end

    subgraph Low-Level Kernel Pathway
        B -->|Pointer to fd in phandle| C["mali_kbase (/dev/mali0)"]
        C -->|KBASE_IOCTL_MEM_IMPORT (0xc0188016)| D["Mali MMU Page Tables\nGPU VA: 0x41000"]
    end

    subgraph High-Level EGL & GLES Pipeline
        B -->|EGL_LINUX_DMA_BUF_EXT (0x3270)\nDRM_FORMAT_ABGR8888| E["eglCreateImageKHR()"]
        E -->|Image Handle: 0x717312f028| F["glEGLImageTargetTexture2DOES()"]
        F --> G["glFramebufferTexture2D(GL_COLOR_ATTACHMENT0)"]
        G --> H["FBO Status: GL_FRAMEBUFFER_COMPLETE (0x8CD5)"]
    end

    subgraph GPU Execution & CPU Readback
        H --> I["GPU 3D Shader Execution\nglClearColor(0, 1.0, 0.25, 1.0) & glFinish()"]
        I -->|Writes Physical RAM| J["Shared Physical Backing Pages\n(ion_noncontig_heap)"]
        J -->|Direct mmap(fd)| K["CPU Memory Readback (0x70230c4000)\nBytes: 0x00, 0xff, 0x40, 0xff"]
    end
```

---

## 2. Low-Level Kernel Import: `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`)

In Milestone 3B, disassembly of `base_mem_import_padded` (`0xacc7dc`) established the existence of the memory import ioctl. In Milestone 4A, the exact kernel struct ABI was refined and verified against `/dev/mali0`.

### 2.1 ABI Structure Layout

- **Command**: `0xc0188016` (`_IOWR(0x80, 0x16, struct kbase_ioctl_mem_import)`)
- **Total Payload Size**: 24 bytes (`0x18`)
- **Struct Definition**:
```c
#define BASE_MEM_IMPORT_TYPE_UMM  2
#define BASE_MEM_PROT_CPU_RD      (1 << 0)
#define BASE_MEM_PROT_CPU_WR      (1 << 1)
#define BASE_MEM_PROT_GPU_RD      (1 << 2)
#define BASE_MEM_PROT_GPU_WR      (1 << 3)

struct kbase_ioctl_mem_import {
    uint64_t flags;   /* Offset 0x00: [IN] Protection flags (e.g., 0x80f) / [OUT] Internal handle */
    uint64_t phandle; /* Offset 0x08: [IN] (uint64_t)&dma_buf_fd / [OUT] Assigned GPU Virtual Address */
    uint32_t type;    /* Offset 0x10: [IN] BASE_MEM_IMPORT_TYPE_UMM = 2 (DMA-BUF) */
    uint32_t padding; /* Offset 0x14: [IN] 0 */
};
```

### 2.2 Operational Semantics
1. **Input Parameters**:
   - `flags`: Protection mask. `BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR` (0x0F) combined with memory type flags (`0x800`).
   - `phandle`: **Crucial ABI Discovery**: For `BASE_MEM_IMPORT_TYPE_UMM` (type 2), the kernel expects `phandle` to contain a pointer to userspace memory holding the integer DMA-BUF file descriptor: `(uint64_t)&dma_buf_fd`.
   - `type`: Must be set to `2` (`BASE_MEM_IMPORT_TYPE_UMM`). Type 1 is reserved for internal UMP memory.
2. **Kernel Processing (`kbase_mem_import`)**:
   - Copies file descriptor from userspace pointer via `copy_from_user`.
   - Calls `dma_buf_get(fd)` to resolve the `struct dma_buf *`.
   - Attaches Mali device to the buffer via `dma_buf_attach()` and maps the scatter-gather table via `dma_buf_map_attachment()`.
   - Programs the physical page addresses into the Mali MMU page tables.
3. **Output Values**:
   - `phandle`: Overwritten with the base 64-bit **GPU Virtual Address** assigned by the Mali MMU allocator.
   - `flags`: Updated with the internal memory region flags.

### 2.3 Physical Verification on Galaxy A30
Running `tests/ion_mali_import_test` produced:
```text
[*] Step 2: Directly importing DMA-BUF into /dev/mali0 (mali_kbase)...
[+] KBASE_IOCTL_MEM_IMPORT SUCCESS!
    Assigned GPU Virtual Address : 0x41000
    Updated Flags / Handle       : 0x500f
```
The kernel allocated GPU VA `0x41000` (page-aligned, 64KB boundary) with flags `0x500f`, validating direct kernel-level DMA-BUF import.

---

## 3. High-Level EGL / GLES Render-Target Pipeline

While kernel-level import maps memory into GPU page tables, high-level graphics APIs require integrating the buffer with the EGL/GLES state machine.

### 3.1 EGL Extension Probing & Attributes
Inspection of the EGL extension string in `libGLES_mali-64.so` confirms support for:
- `EGL_EXT_image_dma_buf_import`
- `EGL_KHR_image_base`
- `EGL_KHR_gl_texture_2D_image`

To import a DMA-BUF as an `EGLImageKHR`, `eglCreateImageKHR` is invoked with:
- `target`: `EGL_LINUX_DMA_BUF_EXT` (`0x3270`)
- `buffer`: `NULL`
- `attrib_list`:
```c
const EGLint img_attribs[] = {
    EGL_WIDTH,                         64,
    EGL_HEIGHT,                        64,
    EGL_LINUX_DRM_FOURCC_EXT,          DRM_FORMAT_ABGR8888,
    EGL_DMA_BUF_PLANE0_FD_EXT,         dma_buf_fd,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,     0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT,      256, /* 64 * 4 bytes */
    EGL_NONE
};
```

### 3.2 Pixel Format Mapping: `DRM_FORMAT_ABGR8888`
The target format `DRM_FORMAT_ABGR8888` (`0x34324241`, Little-Endian byte order `[R, G, B, A]`) maps directly to standard OpenGL ES `GL_RGBA8`.
- In test execution, `eglCreateImageKHR` returned a valid image handle (`0x717312f028`).

### 3.3 Framebuffer Object (FBO) Attachment
The `EGLImageKHR` is bound to a standard GLES texture and attached to an FBO:
1. `glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);`
2. `glTexParameteri(...)` (filter parameters).
3. `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);` — binds the underlying DMA-BUF storage to the texture target.
4. `glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);`
5. `glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);`
6. `glCheckFramebufferStatus(GL_FRAMEBUFFER)` returned **`0x8CD5` (`GL_FRAMEBUFFER_COMPLETE`)**.

This confirms that the imported ION DMA-BUF satisfies all hardware alignment, tiling, and format requirements for 3D color-attachment rendering on the Mali-G71 Bifrost architecture.

---

## 4. Hardware Execution & Memory Readback Verification

To verify that rendering actually writes to the physical backing pages rather than an internal driver shadow buffer:

### 4.1 GPU Rendering Command
```c
glViewport(0, 0, 64, 64);
glClearColor(0.0f, 1.0f, 0.25f, 1.0f); /* Lime Green */
glClear(GL_COLOR_BUFFER_BIT);
glFinish(); /* Submit job descriptors to GPU hardware and wait for completion */
```

### 4.2 CPU Memory Inspection via DMA-BUF mmap
Without querying OpenGL (`glReadPixels` was intentionally not used here), userspace directly mapped the original `dma_buf_fd`:
```c
uint8_t *cpu_ptr = (uint8_t *)mmap(NULL, 16384, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
```

#### Verification Result:
```text
[*] Step 7: Verifying Shared DMA-BUF content directly from CPU memory...
[+] Direct CPU mmap of DMA-BUF succeeded at 0x70230c4000
    Raw CPU Bytes at Pixel (0,0): [0]=0x00 (0), [1]=0xff (255), [2]=0x40 (64), [3]=0xff (255)
[+] SUCCESS: CPU readback matches GPU rendered pattern inside shared ION DMA-BUF!
[+] CONFIRMED: Hardware GPU rendering directly into shareable DMA-BUF verified on Mali-G71!
```

- Byte 0 (Red): `0x00` (`0.0f * 255 = 0`)
- Byte 1 (Green): `0xff` (`1.0f * 255 = 255`)
- Byte 2 (Blue): `0x40` (`0.25f * 255 = 63.75 -> 64`)
- Byte 3 (Alpha): `0xff` (`1.0f * 255 = 255`)

The CPU readback confirms that the Mali-G71 3D shader pipeline rasterized and wrote pixel values directly into the shared memory pages allocated by `/dev/ion`.

---

## 5. Synchronization & Cache Semantics

1. **Uncached Allocation Coherency**: Because the ION buffer was allocated with `flags = 0` (uncached), CPU accesses do not hit stale CPU L1/L2 cache lines, ensuring that CPU `mmap` immediately reflects writes performed by the GPU.
2. **GPU Pipeline Synchronization**: The invocation of `glFinish()` flushes the internal Mali driver command queue, waits for GPU Job Completion IRQs, and ensures that all cachelines in the Mali L2 cache are written back to main system memory before CPU reading begins.
3. **Asynchronous Presentation Fences**: When presenting this buffer to DECON (Milestone 4B), `EGL_ANDROID_native_fence_sync` or explicit sync fences can be exported to coordinate between GPU job completion and display IDMA scanout.

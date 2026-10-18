# Evidence Ledger

This ledger records every verified finding, observed binary behavior, and architectural conclusion. Findings are assigned stable identifiers (`FIND-xxx`) and must link to concrete binary offsets, hashes, sysfs records, or disassembled code.

---

## Verified Findings

### FIND-001: Hardware GPU Model, Core Count, Revision, and GPU ID
- **Description**: The Samsung Galaxy A30 GPU is a 1st-generation Bifrost Mali-G71 with 2 active shader cores (MP2), silicon revision r0p0, reporting hardware GPU ID `0x60A0`.
- **Supporting Evidence**:
  - Live sysfs read of `/sys/devices/platform/11500000.mali/gpuinfo`: `Mali-G71 2 cores r0p0 0x60A0`.
  - Disassembled `libGLES_mali-64.so` strings: `vendor/arm/MaliG71_r26p0_LS_R/`.
- **Source / Artifact**: Live physical SM-A305F target device / `artifacts/proprietary/libGLES_mali-64.so`.
- **Location**: `/sys/devices/platform/11500000.mali/gpuinfo`.
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: `cat /sys/devices/platform/11500000.mali/gpuinfo` via root shell.
- **Status**: **Verified**.

---

### FIND-002: Kernel Driver and Kbase Protocol Version
- **Description**: The downstream kernel runs `mali_kbase` version r26p0 built-in with UK (User-Kernel) protocol ABI version Major 11, Minor 27 (`0x001b000b`).
- **Supporting Evidence**:
  - `artifacts/extracted/kernel_config.txt`: `CONFIG_MALI_TMIX_R26P0=y`, `CONFIG_MALI_MIDGARD=y`.
  - Disassembly of `base_uk_ctx_init_ex` in `libGLES_mali-64.so` (`0xacbd84`):
    ```asm
    mov  w9, #0xb         ; Major = 11
    movk w9, #0x1b, lsl 16 ; Minor = 27
    str  w9, [sp, #4]
    bl   uku_open
    ```
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so` (`70b4fd349187346c82648337d2138da3a0b4c36afe14c079e7cfce98f1be9613`).
- **Location**: Function `base_uk_ctx_init_ex` @ `0xacbd84`.
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: Disassembly of `base_uk_ctx_init_ex` and `uku_open` via `llvm-objdump`. Executed `tests/kbase_probe` on physical device confirming kernel accepted handshake and returned 0.
- **Status**: **Verified**.

---

### FIND-003: Device Node Path and Opening Mechanism
- **Description**: The userspace driver opens the Mali character device node using the formatted path `/dev/mali%d` (defaulting to `/dev/mali0`, flags `0x80802`), verifying that it is a character special device (`S_ISCHR`) before issuing `basep_ioctl_version_check`.
- **Supporting Evidence**:
  - Function `uku_open` @ `0xacdab4` calls `cutils_cstr_snprintf` with format strings `0x14fd73` (`"%s%d"`) and `0x13865a` (`"/dev/mali"`).
  - Followed by `__open_2(path, 0x80802)` and `fstat()`.
  - Target system `/dev/mali0` has permissions `crw-rw-rw- 1 system system 10, 112`.
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so`.
- **Location**: Function `uku_open` @ `0xacdab4`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-004: Downstream Kbase Ioctl Encoding & Command Layout
- **Description**: Downstream `mali_kbase` ioctl commands use type `0x80` with standard Linux `_IOC` direction/size macros. The complete core set of initialization, memory, and job submission ioctls was extracted directly from machine code:
  - `0xc0048000`: `_IOWR(0x80, 0x00, struct kbase_ioctl_version_check)` (4 bytes)
  - `0x40048001`: `_IOW(0x80, 0x01, uint32_t flags)` (4 bytes)
  - `0x40108002`: `_IOW(0x80, 0x02, struct kbase_ioctl_job_submit)` (16 bytes)
  - `0x40108003`: `_IOW(0x80, 0x03, struct kbase_ioctl_get_gpuprops)` (16 bytes)
  - `0x00008004`: `_IO(0x80, 0x04)` - `basep_ioctl_post_term`
  - `0xc0208005`: `_IOWR(0x80, 0x05, struct kbase_ioctl_mem_alloc)` (32 bytes)
  - `0xc0108006`: `_IOWR(0x80, 0x06, struct kbase_ioctl_mem_flags)` (16 bytes)
  - `0x40088007`: `_IOW(0x80, 0x07, struct kbase_ioctl_mem_free)` (8 bytes)
  - `0x40188009`: `_IOW(0x80, 0x09, struct kbase_ioctl_hwcnt_enable)` (24 bytes)
  - `0x8004800c`: `_IOR(0x80, 0x0c, uint32_t)` - `basep_ioctl_disjoint_query` (4 bytes)
  - `0x4010800d`: `_IOW(0x80, 0x0d, struct kbase_ioctl_get_kernel_version_string)` (16 bytes)
  - `0x4018800e`: `_IOW(0x80, 0x0e, struct kbase_ioctl_mem_jit_init)` (24 bytes)
  - `0x4020800f`: `_IOW(0x80, 0x0f, struct kbase_ioctl_mem_sync)` (32 bytes)
  - `0x80048011`: `_IOR(0x80, 0x11, uint32_t)` - `basep_ioctl_get_context_id` (4 bytes)
  - `0x40048012`: `_IOW(0x80, 0x12, uint32_t)` - `basep_ioctl_tlstream_acquire` (4 bytes)
  - `0x40108014`: `_IOW(0x80, 0x14, struct kbase_ioctl_mem_commit)` (16 bytes)
  - `0xc0208015`: `_IOWR(0x80, 0x15, struct kbase_ioctl_mem_alias)` (32 bytes)
  - `0xc0188016`: `_IOWR(0x80, 0x16, struct kbase_ioctl_mem_import)` (24 bytes)
  - `0x40188017`: `_IOW(0x80, 0x17, struct kbase_ioctl_mem_flags_change)` (24 bytes)
  - `0x4010801d`: `_IOW(0x80, 0x1d, struct kbase_ioctl_sticky_resource_map)` (16 bytes)
  - `0x4010801e`: `_IOW(0x80, 0x1e, struct kbase_ioctl_sticky_resource_unmap)` (16 bytes)
  - `0x40088026`: `_IOW(0x80, 0x26, struct kbase_ioctl_mem_exec_init)` (8 bytes)
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so`.
- **Location**: Addresses `0xaccf7c` - `0xacd960`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-005: Absence of Mainline DRM/KMS in Downstream Kernel
- **Description**: The downstream kernel (`4.4.302-p6-neyuKernel_legacy` and stock A30 kernels) does not have DRM or KMS drivers compiled. It exposes display output strictly via legacy FBDev (`/dev/fb0`, `CONFIG_EXYNOS_DECON_FB=y`).
- **Supporting Evidence**:
  - `artifacts/extracted/kernel_config.txt`: `# CONFIG_DRM is not set`, `CONFIG_EXYNOS_DECON_FB=y`, `CONFIG_EXYNOS_DECON_7885=y`.
- **Confidence Level**: Confirmed Fact (100%).
- **Architectural Consequence**: Standard desktop Wayland compositors (like `sway`/`phoc` via `wlroots`) that require DRM/KMS (`/dev/dri/card0`) cannot run out-of-the-box on this downstream kernel without an FBDev backend, a ported Exynos DRM driver, or `libhybris` + Android HWC.
- **Status**: **Verified**.

---

### FIND-006: Upstream Mesa Panfrost Mali-G71 Architectural Gap
- **Description**: Upstream Mesa Panfrost supports Bifrost v6/v7 (`PAN_ARCH == 6` / `PAN_ARCH == 7`), and Linux mainline `drivers/gpu/drm/panfrost/panfrost_gpu.c` contains device detection for `0x6000` (Mali-G71). However, Mali-G71 is not officially enabled in upstream Mesa, because 1st-generation Bifrost v6 has subtle differences in hardware quirks, job manager registers, and instruction encoding compared to v7 (G52/G72).
- **Supporting Evidence**:
  - Mesa official documentation: *"Other Midgard and Bifrost chips (e.g. G71) are not yet supported."*
  - Kernel mainline `panfrost_gpu.c`: `GPU_MODEL(g71, 0x6000, ...)` with ACE coherency workaround.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-007: Mandatory Memory Tracking Page (MTP) Prerequisite for Allocation
- **Description**: The downstream `mali_kbase` kernel driver strictly enforces that userspace must `mmap()` the special Memory Tracking Page (offset `0x3000` / `BASE_MEM_MAP_TRACKING_HANDLE`, length 4096, `PROT_NONE`, `MAP_SHARED`) before any call to `KBASE_IOCTL_MEM_ALLOC` (`0xc0208005`) can succeed. Calls without prior MTP mapping fail immediately with `-EINVAL` (-22).
- **Supporting Evidence**:
  - Disassembly of `basep_map_mtp` in `libGLES_mali-64.so` (`0xacc288`):
    ```asm
    mov w1, #0x1000 ; 4096 bytes
    mov w2, wzr     ; PROT_NONE
    mov w3, #0x1    ; MAP_SHARED
    mov w5, #0x3000 ; offset = 0x3000
    bl  mmap64
    ```
  - Executed on physical target device via `tests/kbase_probe`:
    - Without MTP mmap: `KBASE_IOCTL_MEM_ALLOC` returns error `-22`.
    - With MTP mmap @ `0x3000`: `KBASE_IOCTL_MEM_ALLOC` succeeds, assigning GPU VA `0x000000000000200f`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-008: SVM Dual-Path Memory Teardown Mechanism
- **Description**: In 64-bit Bifrost userspace, memory allocations with bit 3 set (`BASE_MEM_PROT_GPU_WR` / SVM mapping) are deallocated via `munmap(gpu_va & ~0xfff, pages << 12)`, which triggers kernel VMA teardown hooks (`vm_ops->close`). The dedicated ioctl `KBASE_IOCTL_MEM_FREE` (`0x40088007`) is reserved strictly for non-SVM / GPU-only unmapped buffers.
- **Supporting Evidence**:
  - Disassembly of `base_mem_free` in `libGLES_mali-64.so` (`0xacc46c`):
    ```asm
    tbnz w1, #0x3, munmap_path ; If bit 3 set, branch to munmap
    ioctl_path:
    mov  w1, #0x8007           ; KBASE_IOCTL_MEM_FREE
    bl   ioctl
    munmap_path:
    and  x0, x1, #~0xfff
    lsl  x1, x2, #12
    bl   munmap
    ```
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-009: Hardware GPU Job Submission & Interrupt Verification
- **Description**: GPU command streams submitted from userspace via `glFinish()` directly trigger hardware job execution on the physical Mali-G71 Job Manager, independently proven by hardware interrupts on line 57 of `/proc/interrupts` (`11500000.mali`, GIC 191 Edge).
- **Supporting Evidence**:
  - Executed `tests/egl_offscreen_test` on physical hardware (`SM-A305F`).
  - Baseline Mali Job Interrupt count across all CPU cores before rendering: `11554933`.
  - Post-render Mali Job Interrupt count after `glFinish()`: `11554935`.
  - Net IRQ delta: `+2` (corresponding to vertex and fragment job executions on the hardware Job Slot).
  - Physical GPU identification confirmed via GLES query: `GL_RENDERER = Mali-G71`, `GL_VENDOR = ARM`, `GL_VERSION = OpenGL ES 3.2 v1.r26p0-01eac0.###other-sha0123456789ABCDEF0###`.
- **Source / Artifact**: Live physical SM-A305F target device / `tests/egl_offscreen_test`.
- **Location**: `/proc/interrupts` (line 57, `11500000.mali`).
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: Polling line 57 of `/proc/interrupts` before and after `glFinish()` during dual-zone shader rendering.
- **Status**: **Verified**.

---

### FIND-010: Headless EGL Pbuffer Execution & Platform Discard Behavior
- **Description**: The proprietary Samsung/ARM Mali DDK (`libGLES_mali-64.so`) rejects `EGL_PLATFORM_SURFACELESS_MESA` (`0x31DC`) with `EGL_BAD_PARAMETER` (`0x300C`), but fully accepts `EGL_PLATFORM_ANDROID_KHR` (`0x3141`) and standard `eglGetDisplay(EGL_DEFAULT_DISPLAY)`. Furthermore, `eglCreatePbufferSurface` bypasses Android `ANativeWindow`, Gralloc, and display server interactions entirely, creating an unshared offscreen framebuffer directly in GPU memory.
- **Supporting Evidence**:
  - Disassembly of `_ZL11new_displayPvS_PKi` (`0x774ca0`): returns `EGL_NO_DISPLAY` if `native_display != NULL`, but allocates a 16-byte internal winsys structure if `native_display == 0`.
  - Disassembly of `eglCreatePbufferSurface` (`0x78021c`): passes `native_window = NULL` (`xzr`) directly to `eglp_create_surface`.
  - Successful execution of `tests/egl_offscreen_test`:
    - EGL 1.4 initialized with 25 configs.
    - 64x64 RGBA8888 Pbuffer created and bound to GLES 2.0/3.2 context without an active display server or Android SurfaceFlinger interaction.
    - Shader pipeline rendered Cornflower Blue clear (`RGBA(100, 149, 237, 255)`) and Red Triangle (`RGBA(255, 0, 0, 255)`).
    - Pixel readback via `glReadPixels` matched mathematical ground truth with 0 LSB discrepancy.
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so` / physical execution on SM-A305F.
- **Location**: Functions `_ZL11new_displayPvS_PKi` @ `0x774ca0`, `eglCreatePbufferSurface` @ `0x78021c` in `libGLES_mali-64.so`.
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: Execution of `tests/egl_offscreen_test` on physical target; pixel readback inspection and disassembly analysis.
- **Status**: **Verified**.

---

### FIND-011: DECON Framebuffer Geometry, Stride, and Channel Ordering
- **Description**: The downstream DECON framebuffer (`/dev/graphics/fb0`) operates at a native resolution of 1080x2340 pixels, 32 bits per pixel, with a line stride of 4320 bytes (tightly packed, 0 padding). The hardware channel bitfield ordering is BGRA_8888 (Little-Endian ARGB): Blue at offset 0 (len 8), Green at offset 8 (len 8), Red at offset 16 (len 8), and Alpha at offset 24 (len 8).
- **Supporting Evidence**:
  - Interrogated `/dev/graphics/fb0` on physical SM-A305F via `tests/fb_probe` and `tests/fb_present_test`.
  - `FBIOGET_VSCREENINFO` (`0x4600`): returned xres=1080, yres=2340, xres_virtual=1080, yres_virtual=2340, bpp=32, red=(16,8), green=(8,8), blue=(0,8), transp=(24,8).
  - `FBIOGET_FSCREENINFO` (`0x4602`): line_length=4320, smem_len=20217600 (2 frames of 10,108,800 bytes).
- **Source / Artifact**: Live physical SM-A305F target device / `tests/fb_probe`.
- **Location**: `/dev/graphics/fb0` (`14860000.decon_f`).
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: `ioctl(fb_fd, FBIOGET_VSCREENINFO)` executed on target hardware.
- **Status**: **Verified**.

---

### FIND-012: Rejection of Legacy FBDev Mmap on DECON Framebuffer
- **Description**: Conventional Linux FBDev memory mapping (`mmap` on `/dev/graphics/fb0`) fails with error `-EINVAL` (-22) because the Samsung DECON driver does not allocate static scanout RAM at boot. The reported physical base address (`smem_start = 0x10000000`) is outside physical System RAM (`0x80000000 - 0x8bfffffff`).
- **Supporting Evidence**:
  - `mmap(NULL, smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0)` returns error `-22` on physical SM-A305F.
  - Target `/proc/iomem` confirms System RAM ranges: `80000000-bbbfffff`, `c0000000-ffffffff`, `880000000-8bfffffff`. Address `0x10000000` is invalid unmapped physical memory.
  - Driver `decon_mmap` in kernel symbols rejects mapping attempts lacking a pre-allocated static buffer.
- **Source / Artifact**: Physical execution of `tests/fb_probe` and `tests/fb_present_test` on SM-A305F.
- **Location**: `/dev/graphics/fb0` / `/proc/iomem`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-013: DECON Multi-Window Presentation IOCTL Encoding
- **Description**: Display scanout on Exynos 7904 is arbitrated through the proprietary multi-window overlay ioctl `S3CFB_WIN_CONFIG` (`0x441046d1`, `_IOW('F', 209, struct decon_win_config_data)`), which receives 7 hardware overlay window structures accepting DMA-BUF file descriptors (`fd_idma[0]`) and sync fences (`fence_fd`).
- **Supporting Evidence**:
  - Disassembly of `ExynosDisplayFbInterface::deliverWinConfigData` in `libexynosdisplay.so` (`0x703f4` - `0x7040c`):
    `mov w1, #0x46d1; movk w1, #0x4410, lsl #16; bl ioctl`.
  - Disassembly of `configFromDisplayConfig` (`0x6fd98`): parses `fd_idma[0]` at offset `+0x8` and `fence_fd` at offset `+0xc` within `struct decon_win_config`.
  - HWC2 daemon (`composer@2.1-service`, PID 3935) holds `/dev/graphics/fb0` open exclusively to issue this command.
- **Source / Artifact**: `artifacts/proprietary/libexynosdisplay.so`.
- **Location**: Function `deliverWinConfigData` @ `0x70194`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-014: Asymmetric Import-Only Architecture of Mali Kbase
- **Description**: The downstream `mali_kbase` driver does not provide any ioctl to export internal GPU allocations (`KBASE_IOCTL_MEM_ALLOC`) as DMA-BUFs. It acts exclusively as a DMA-BUF importer via ioctl `0xc0188016` (`KBASE_IOCTL_MEM_IMPORT`, `BASE_MEM_IMPORT_TYPE_UMM` = 1).
- **Supporting Evidence**:
  - Comprehensive disassembly of all 22 ioctls in `libGLES_mali-64.so` (`reverse-engineering/ioctl-analysis/kbase-ioctls.md`) contains zero export ioctls.
  - Disassembly of `base_mem_import_padded` @ `0xacc7dc`: issues `0xc0188016`, imports external DMA-BUF `phandle`, and binds it into Mali MMU page tables.
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so`.
- **Location**: Function `base_mem_import_padded` @ `0xacc7dc`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-015: Exynos ION as Universal Graphics Buffer Exporter
- **Description**: In the Samsung Exynos graphics architecture, cross-device buffer sharing between the GPU (`mali_kbase`) and display scanout (`exynos_decon`) is mediated entirely by Samsung Exynos ION (`/dev/ion`, `libion_exynos.so`). Buffers are allocated via `exynos_ion_alloc()` / `mali_gralloc_ion_allocate()`, exported as DMA-BUF file descriptors, and imported symmetrically into both subsystems.
- **Supporting Evidence**:
  - Disassembly of `gralloc.exynos7904.so` (`_Z25mali_gralloc_ion_allocatePKmjPPK13native_handlePb` @ `0xc7a0`): allocates buffers from `/dev/ion` and embeds the resulting DMA-BUF fd in `private_handle_t`.
  - Disassembly of `libion_exynos.so`: exports `exynos_ion_open`, `exynos_ion_alloc`, and `exynos_ion_close`.
  - `/dev/ion` device node present on target: Major 10, Minor 127 (`crw-rw-rw-`).
- **Source / Artifact**: `artifacts/proprietary/gralloc-64.so`, `artifacts/proprietary/libion_exynos.so`.
- **Location**: `/dev/ion`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-016: ION DMA-BUF Direct Allocation & Export Protocol
- **Description**: Samsung Exynos ION (`/dev/ion`, Major 10, Minor 127) supports allocating physical memory pages from `ion_noncontig_heap` (Heap Mask `1 << 0` = 1, uncached flags = 0) via `ION_IOC_ALLOC` (`0xc0204900`) and exporting them as standard Linux DMA-BUF file descriptors via `ION_IOC_SHARE` (`0xc0084904`). Releasing the userspace ION handle via `ION_IOC_FREE` (`0xc0044901`) maintains buffer lifetime through kernel DMA-BUF reference counting.
- **Supporting Evidence**:
  - Standalone execution of `tests/ion_mali_import_test` on physical SM-A305F target device.
  - Successfully allocated 16,384 bytes (handle `0x1`) and exported to DMA-BUF descriptor `dma_buf_fd = 4`.
  - Mapped directly into CPU memory via `mmap(NULL, len, PROT_READ, MAP_SHARED, 4, 0)` at `0x70230c4000`.
- **Source / Artifact**: Live physical SM-A305F target device / `tests/ion_mali_import_test`.
- **Location**: `/dev/ion`.
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: Direct execution of `tests/ion_mali_import_test` on physical hardware.
- **Status**: **Verified**.

---

### FIND-017: EGLImage DMA-BUF Import & FBO Hardware Render-Target Validation
- **Description**: An externally allocated ION DMA-BUF can be imported directly into the proprietary Mali-G71 GPU driver and utilized as a zero-copy 3D rendering color attachment. Low-level kernel import succeeds via `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`, `struct kbase_ioctl_mem_import` with `type = 2`, `flags = 0x80f`, `phandle = &dma_buf_fd`), assigning GPU VA `0x41000`. Userspace import succeeds via `eglCreateImageKHR` with `EGL_LINUX_DMA_BUF_EXT` (`0x3270`) and `DRM_FORMAT_ABGR8888` (`0x34324241`). Binding this image to a `GL_TEXTURE_2D` and attaching to `GL_COLOR_ATTACHMENT0` yields `GL_FRAMEBUFFER_COMPLETE` (`0x8CD5`). Direct CPU memory readback of GPU-rendered pixels confirms exact byte-for-byte ground truth (`[0]=0x00, [1]=0xff, [2]=0x40, [3]=0xff` / Lime Green).
- **Supporting Evidence**:
  - `tests/ion_mali_import_test` execution on physical hardware:
    - Kernel import assigned GPU VA `0x41000`, flags `0x500f`.
    - `eglCreateImageKHR` returned handle `0x717312f028`.
    - `glCheckFramebufferStatus` returned `0x8cd5`.
    - `glClearColor(0.0f, 1.0f, 0.25f, 1.0f)` + `glFinish()` completed.
    - CPU `mmap()` of `dma_buf_fd` verified exact pixel values without calling `glReadPixels`.
- **Source / Artifact**: Live physical SM-A305F target device / `tests/ion_mali_import_test`.
- **Location**: `/vendor/lib64/egl/libGLES_mali.so`, `/dev/mali0`.
- **Confidence Level**: Confirmed Fact (100%).
- **Verification Procedure**: Physical execution and byte comparison of CPU-mapped DMA-BUF memory.
- **Status**: **Verified**.

---

### FIND-019: Disassembly Proof of BASE_MEM_IMPORT_TYPE_UMM = 2 in Mali DDK
- **Description**: In the proprietary Mali Bifrost DDK (`libGLES_mali-64.so`), the enumeration value for importing Linux DMA-BUF buffers via `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`) is `2` (`BASE_MEM_IMPORT_TYPE_UMM = 2`). The value `1` represents legacy ARM UMP (`BASE_MEM_IMPORT_TYPE_UMP`), resolving a discrepancy in early Midgard documentation.
- **Supporting Evidence**:
  - Disassembly of `libGLES_mali-64.so` at `0xa63e30` (Gralloc import) and `0xacc3b0` (EGLImage import): passes `w2 = 2` to `base_mem_import_padded` (`0xacc7dc`).
  - Kernel header inspection in `drivers/gpu/arm/midgard/mali_kbase_mem.c`: `enum base_mem_import_type { BASE_MEM_IMPORT_TYPE_INVALID = 0, BASE_MEM_IMPORT_TYPE_UMP = 1, BASE_MEM_IMPORT_TYPE_UMM = 2 }`.
  - Live execution of `tests/decon_present_test`: `KBASE_IOCTL_MEM_IMPORT` with `type = 2` returns `0` and assigns GPU VA `0x41000`.
- **Source / Artifact**: `artifacts/proprietary/libGLES_mali-64.so` / `artifacts/extracted/kernel.img`.
- **Location**: `0xa63e30`, `0xacc3b0` in `libGLES_mali-64.so`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-020: Exact 200-Byte Window Layout of Samsung Exynos DECON ABI
- **Description**: The Samsung Exynos 7904 DECON-F display driver uses a 1040-byte ioctl structure for `S3CFB_WIN_CONFIG` (`0x441046d1`). The structure consists of an 8-byte header (`int32_t fence`, `int32_t reserved`), followed by an array of 5 windows of exactly 200 bytes each (`5 * 200 = 1000` bytes), and 32 bytes of tail padding.
- **Supporting Evidence**:
  - Disassembly of `kernel.img` at `decon_ioctl` (`0x3b98f8 - 0x3ba420`) and `decon_win_config_to_regs_param` (`0x3ba7fc - 0x3ba8dc`).
  - Coordinate offsets verified: `src_x` at offset 144, `src_y` at 148, `src_w` at 160, `src_h` at 164, `dst_x` at 168, `dst_y` at 172, `dst_w` at 176, `dst_h` at 180.
  - Parameter offsets verified: `state` at offset 0, `fd_idma[0]` at 8, `fence_fd` at 20, `plane_alpha` at 28, `blending` at 32, `idma_type` at 36, `format` at 40.
  - Compile-time `_Static_assert` and live hardware execution confirmed ABI compatibility.
- **Source / Artifact**: `artifacts/extracted/kernel.img` / `tests/decon_present_test.c`.
- **Location**: Kernel offsets `0x3b98f8 - 0x3ba8dc`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-021: DECON Hardware IDMA Routing and Primary Scanout Window 3 Requirement
- **Description**: On Exynos 7904 DECON-0, IDMA channel 0 is dedicated to Window 3. Kernel `decon_ioctl` explicitly enforces this constraint (`0x3ba154 - 0x3ba168`): any submission targeting Windows 0, 1, or 2 with `idma_type == 0` is rejected with `-EINVAL` (-22) via `cbz w8, #1580`. Only Window 3 (`x24 == 3`) skips this check and allows binding to IDMA 0.
- **Supporting Evidence**:
  - Kernel disassembly of `0x3ba154`:
    `ldr w8, [x19]; cbnz w8, #20; cmp x24, #3; b.eq #12; ldur w8, [x26, #-156]; cbz w8, #1580;`
  - Submitting Window 0 with `idma_type = 0` produced `errno = 22` (`-EINVAL`).
  - Submitting Window 3 with `idma_type = 0` returned `0` (`Success`) and generated a valid retire fence descriptor (`fd = 12`).
  - Dmesg logs from Android `composer@2.1-service` confirm `win id is 3 enabled` and `DMATYPE_0@` during primary composition.
- **Source / Artifact**: `artifacts/extracted/kernel.img` / live execution on SM-A305F.
- **Location**: Kernel offset `0x3ba154`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

---

### FIND-022: End-to-End Hardware-Accelerated Physical Scanout on EA8076 Panel
- **Description**: End-to-end zero-copy GPU rendering and physical display presentation was validated on the Samsung Galaxy A30 running kernel `4.4.302-p6-neyuKernel_legacy`. A 10.24 MB ION buffer allocated from `ion_noncontig_heap` was imported into Mali-G71 GPU (GPU VA `0x41000`), rendered into via EGLImage FBO (`GL_FRAMEBUFFER_COMPLETE`), verified via CPU `mmap()`, presented to DECON Window 3, and retired by hardware VSYNC (`poll()` on retire fence returned `revents = 0x1`). The multi-color calibration pattern physically displayed on the EA8076 Super AMOLED screen for 5 seconds.
- **Supporting Evidence**:
  - Live execution of `tests/decon_present_test` under root shell with SurfaceFlinger stopped.
  - Return code `0` on `S3CFB_WIN_CONFIG` (`0x441046d1`).
  - Retire fence fd 12 signaled by DECON hardware interrupt.
  - Clean teardown and seamless recovery of Android UI within 2.5 seconds.
- **Source / Artifact**: Live physical SM-A305F target device / `tests/decon_present_test`.
- **Location**: `/dev/ion`, `/dev/mali0`, `/vendor/lib64/egl/libGLES_mali.so`, `/dev/graphics/fb0`.
- **Confidence Level**: Confirmed Fact (100%).
- **Status**: **Verified**.

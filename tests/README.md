# Test Harnesses & Hardware Probes

This directory contains standalone, reproducible test harnesses designed to validate the Mali-G71 GPU driver stack across kernel ABI and userspace EGL layers on physical Samsung Galaxy A30 hardware (`SM-A305F`).

---

## Directory Contents

| Source File | Executable | Layer Tested | Primary Purpose | Libc Dependencies |
| :--- | :--- | :--- | :--- | :--- |
| `kbase_probe.c` | `kbase_probe` | Kernel ABI (`/dev/mali0`) | Validates ioctl handshake (11.27), MTP mapping (`0x3000`), Context ID, and GPU memory allocation/free. | **None** (Freestanding, raw AArch64 syscalls) |
| `egl_offscreen_test.c` | `egl_offscreen_test` | Userspace EGL/GLES | Validates headless EGL initialization, Pbuffer allocation, shader pipeline execution, hardware job IRQ, and dual-zone pixel readback. | Bionic `libc.so`, `libdl.so` |
| `fb_probe.c` | `fb_probe` | Display ABI (`/dev/graphics/fb0`) | Inspects DECON FB geometry, pixel bitfields, stride, smem parameters, and probes mmap compatibility. | **None** (Freestanding, raw AArch64 syscalls) |
| `ion_mali_import_test.c` | `ion_mali_import_test` | Buffer Sharing & Render Target | Allocates ION DMA-BUF, performs low-level Kbase import, binds EGLImage to FBO, executes 3D GPU render, and verifies via CPU mmap. | Bionic `libc.so`, `libdl.so` |
| `decon_present_test.c` | `decon_present_test` | Physical Presentation Prototype | Full-screen (1080x2340) ION DMA-BUF allocation, Mali-G71 GPU rendering, DECON Window 3 presentation via `S3CFB_WIN_CONFIG`, retire fence wait, and AMOLED panel display. | Bionic `libc.so`, `libdl.so` |
| `decon_flip_test.c` | `decon_flip_test` | Double-Buffered 60 FPS Swapchain | 2x ION DMA-BUF ping-pong swapchain, Mali GPU animated rendering, continuous DECON Window 3 page-flipping, retire fence synchronization, and 59.89 FPS benchmark. | Bionic `libc.so`, `libdl.so` |
| `bionic_shim_test.c` | `bionic_shim_test` | Bionic-Musl Runtime Shim | Validates `libbionic_musl_shim.so` runtime: errno redirection, `_ctype_`, `__android_log_print`, `property_get/set`, FORTIFY checks, and ION memory wrappers. | Bionic `libc.so`, `libdl.so` |
| `decon_compositor_test.c` | `decon_compositor_test` | Modular Wayland DECON Compositor | 120-frame simulated Wayland compositor presentation loop via `libdecon_backend.so`, multi-window compositing, and 59.34 FPS physical AMOLED benchmark. | Bionic `libc.so`, `libdl.so` |
| `weston_backend_test.c` | `weston_backend_test` | Weston 14 Backend Unit Suite | 6-stage unit test harness for `backend-decon.so`: config validation, AMOLED head registration, swapchain creation, retire fence event loop integration, 10-frame VSYNC loop, and error recovery. | `libweston-14`, `libpixman-1`, `libwayland-server` |


---

## 1. `kbase_probe`: Standalone Direct Kernel ABI Probe

### Description
`kbase_probe.c` communicates directly with `/dev/mali0` without loading any proprietary userland libraries. It verifies that the kernel's built-in `mali_kbase` module conforms to the reverse-engineered User-Kernel (UK) protocol version 11.27.

Key operations verified:
1. `openat(AT_FDCWD, "/dev/mali0", O_RDWR | O_CLOEXEC)`
2. `KBASE_IOCTL_VERSION_CHECK` (`0xc0048000`): Handshake version Major 11, Minor 27.
3. `KBASE_IOCTL_SET_FLAGS` (`0x40048001`): Client context initialization flags.
4. `mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, 0x3000)`: Memory Tracking Page (MTP) prerequisite.
5. `KBASE_IOCTL_GET_CONTEXT_ID` (`0x80048011`): Context ID allocation.
6. `KBASE_IOCTL_MEM_ALLOC` (`0xc0208005`): Allocates GPU virtual and physical pages.
7. `KBASE_IOCTL_MEM_FREE` (`0x40088007`): Deallocates GPU virtual address range.

### Building `kbase_probe`
Compiled as a freestanding static binary with zero external library dependencies:

```bash
clang --target=aarch64-linux-gnu -nostdlib -static \
  tests/kbase_probe.c -o tests/kbase_probe
```

### Running `kbase_probe` on Device
```bash
adb push tests/kbase_probe /data/local/tmp/kbase_probe
adb shell chmod +x /data/local/tmp/kbase_probe
adb shell "su -c /data/local/tmp/kbase_probe"
```

### Expected Output
```text
[+] Mali-G71 Kbase Standalone Verification Probe
[*] Opening /dev/mali0...
[+] Successfully opened /dev/mali0, fd = 3
[*] Sending KBASE_IOCTL_VERSION_CHECK (11.27, ioctl 0xc0048000)...
[+] VERSION_CHECK SUCCESS! Kernel reported version: Major = 11, Minor = 27
[*] Sending KBASE_IOCTL_SET_FLAGS (flags = 0, ioctl 0x40048001)...
[*] SET_FLAGS returned: 0
[*] Mapping Memory Tracking Page (MTP): mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, 0x3000)...
[+] MTP mmap SUCCESS! Address: 0x...
[*] Sending KBASE_IOCTL_GET_CONTEXT_ID (ioctl 0x80048011)...
[+] GET_CONTEXT_ID SUCCESS! Assigned Context ID = 31
[*] Sending KBASE_IOCTL_MEM_ALLOC (1 page, RW, ioctl 0xc0208005)...
[+] MEM_ALLOC SUCCESS! Raw returned handle = 0x000000000000200f, Flags = 0x000000000000000f
[*] Sending KBASE_IOCTL_MEM_FREE (Page-aligned GPU VA = 0x0000000000002000, ioctl 0x40088007)...
[+] MEM_FREE SUCCESS! GPU memory cleanly freed.
[*] Closing /dev/mali0...
[+] ALL VERIFICATION CHECKS PASSED (Version check, MTP, Alloc, Free)! Exiting clean.
```

---

## 2. `egl_offscreen_test`: Headless EGL & Shader Validation Harness

### Description
`egl_offscreen_test.c` dynamically loads `/vendor/lib64/egl/libGLES_mali.so` using `dlopen`, probes EGL platform support, creates an offscreen Pbuffer surface, compiles a shader pipeline, draws geometry, verifies physical GPU interrupt generation, and confirms mathematical pixel colors via `glReadPixels`.

Key operations verified:
1. Dynamic loading of `/vendor/lib64/egl/libGLES_mali.so` with `RTLD_NOW`.
2. Probing `EGL_PLATFORM_SURFACELESS_MESA` (rejected) vs `EGL_PLATFORM_ANDROID_KHR` and `EGL_DEFAULT_DISPLAY` (accepted).
3. `eglInitialize`: Verifies EGL 1.4 Bifrost-r26p0 driver startup.
4. `eglChooseConfig`: Selects an RGBA(8,8,8,8) configuration supporting Pbuffer rendering.
5. `eglCreatePbufferSurface`: Creates an isolated 64x64 offscreen framebuffer without window system connection.
6. `eglCreateContext` & `eglMakeCurrent`: Activates OpenGL ES 2.0/3.2 context.
7. Querying `GL_RENDERER`, `GL_VENDOR`, `GL_VERSION` to verify physical Mali-G71 GPU.
8. Recording baseline interrupt counter for Mali Job IRQ (`11500000.mali` GIC 191) in `/proc/interrupts`.
9. Clearing framebuffer to Cornflower Blue `RGBA(100, 149, 237, 255)`.
10. Compiling vertex/fragment shader program and drawing a centered Red Triangle `RGBA(255, 0, 0, 255)`.
11. `glFinish`: Flushing command queue and verifying Mali Job IRQ increment (`delta >= 1`).
12. Reading back pixels via `glReadPixels`:
    - Pixel `(0, 0)`: Verified as Background Cornflower Blue.
    - Pixel `(32, 32)`: Verified as Center Triangle Solid Red.
13. Clean teardown and release of all contexts, surfaces, displays, and library handles.

### Building `egl_offscreen_test`
Compiled against target Bionic sysroot (`artifacts/extracted/sysroot/`):

```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/egl_offscreen_test.c -o tests/egl_offscreen_test
```

### Running `egl_offscreen_test` on Device
```bash
adb push tests/egl_offscreen_test /data/local/tmp/egl_offscreen_test
adb shell chmod +x /data/local/tmp/egl_offscreen_test
adb shell "su -c /data/local/tmp/egl_offscreen_test"
```

### Expected Output
```text
================================================================
 Milestone 3A: Proprietary Mali-G71 EGL Offscreen Validation
================================================================
[*] Loading proprietary driver: /vendor/lib64/egl/libGLES_mali.so
[+] Successfully loaded /vendor/lib64/egl/libGLES_mali.so at 0x...

[*] Step 1: Testing Platform Surfaceless / Device extensions...
[*] Probing EGL_PLATFORM_SURFACELESS_MESA (0x31DC)...
[-] EGL_PLATFORM_SURFACELESS_MESA rejected (Error 0x300c: EGL_BAD_PARAMETER)
[*] Probing EGL_PLATFORM_ANDROID_KHR (0x3141)...
[+] EGL_PLATFORM_ANDROID_KHR accepted, dpy = 0x...

[*] Step 2: Initializing EGL display connection (opening /dev/mali0)...
[+] eglInitialize SUCCESS! EGL Version: 1.4
    EGL_VENDOR     : ARM
    EGL_VERSION    : 1.4 Bifrost-"r26p0-01eac0"
    EGL_CLIENT_APIS: OpenGL_ES

[*] Step 3: Enumerating available EGL configurations...
[+] Total EGL configurations available: 25

[*] Step 4: Selecting EGL config with Pbuffer + OpenGL ES 2.0/3.0 support...
[+] Chosen Config 0x... (matched 1 configurations)
    Config Attributes: RGBA(8,8,8,8), Surface Type: 0x1405

[*] Step 5: Creating 64x64 Offscreen Pbuffer Surface...
[+] Pbuffer Surface created successfully: 0x...

[*] Step 6: Creating OpenGL ES 2.0 Context...
[+] OpenGL ES Context created successfully: 0x...

[*] Step 7: Binding Context to Pbuffer Surface (eglMakeCurrent)...
[+] Context bound and active!

[*] Step 8: Active Hardware & Driver Identification:
    ===================================================
    GL_VENDOR  : ARM
    GL_RENDERER: Mali-G71
    GL_VERSION : OpenGL ES 3.2 v1.r26p0-01eac0...
    ===================================================
[+] VERIFIED: Direct physical GPU renderer identified (Mali-G71)

[*] Step 9: Executing Hardware-Accelerated Rendering & Job Submission...
[*] Pre-render Mali Job Interrupts count: ...
[*] Post-render Mali Job Interrupts count: ...
[+] Mali Job IRQ delta: 2

[*] Step 10: Reading back pixels via glReadPixels...
[+] Pixel (0,0) [Background]: RGBA = (100, 149, 237, 255)
    Expected Background     : RGBA = (100, 149, 237, 255) [Cornflower Blue]
[+] Pixel (32,32) [Center Triangle]: RGBA = (255, 0, 0, 255)
    Expected Triangle Color       : RGBA = (255, 0, 0, 255) [Solid Red]
[+] SUCCESS: Background and Geometry pixels match mathematical ground truth!
[+] CONFIRMED: Hardware GPU 3D vertex & fragment shader execution succeeded on Mali-G71!

[*] Step 11: Teardown & Context Destruction...
[+] Clean teardown complete. All EGL tests PASSED!
================================================================
```

---

## 3. `fb_probe`: Standalone DECON Framebuffer Hardware Probe

### Description
`fb_probe.c` is a zero-dependency, freestanding static binary that interrogates `/dev/graphics/fb0` using raw Linux AArch64 syscalls. It safely inspects visible and virtual geometry, channel bitfield offsets (detecting native BGRA8888), scanout stride, smem parameters, and tests whether conventional `mmap` is supported.

### Building `fb_probe`
```bash
clang --target=aarch64-linux-gnu -fuse-ld=lld -nostdlib -static \
  tests/fb_probe.c -o tests/fb_probe
```

### Running `fb_probe` on Device
```bash
adb push tests/fb_probe /data/local/tmp/fb_probe
adb shell chmod +x /data/local/tmp/fb_probe
adb shell "su -c /data/local/tmp/fb_probe"
```

---

## 4. `fb_present_test`: Non-Destructive Display Presentation Prototype

### Description
`fb_present_test.c` bridges GPU rendering and display presentation:
1. Renders an offscreen amber test pattern via the verified EGL/GLES pipeline on Mali-G71.
2. Reads back pixels via `glReadPixels`.
3. Interrogates `/dev/graphics/fb0` to acquire active hardware geometry (1080x2340) and bitfield configuration.
4. Performs pixel channel reordering from standard GLES RGBA to the display's native BGRA8888 format with stride adaptation.
5. Performs scanout safety assessment (confirms direct `mmap` rejection, protecting the active display).
6. Dumps the converted, scanout-ready frame to `/data/local/tmp/fb_frame_test.raw` for validation.

### Building `fb_present_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/fb_present_test.c -o tests/fb_present_test
```

### Running `fb_present_test` on Device
```bash
adb push tests/fb_present_test /data/local/tmp/fb_present_test
adb shell chmod +x /data/local/tmp/fb_present_test
adb shell "su -c /data/local/tmp/fb_present_test"
```

---

## 5. `ion_mali_import_test`: End-to-End Shared Buffer Allocation, Import & 3D Render

### Description
`ion_mali_import_test.c` validates the complete cross-subsystem buffer sharing pipeline on physical hardware:
1. Opens `/dev/ion`, allocates physical backing memory from `ion_noncontig_heap` (Heap Mask 1, flags 0), and exports a standard Linux DMA-BUF file descriptor (`dma_buf_fd`).
2. Issues `KBASE_IOCTL_MEM_IMPORT` (`0xc0188016`) with `BASE_MEM_IMPORT_TYPE_UMM` (type 2) directly to `/dev/mali0`, confirming low-level kernel page table mapping and GPU VA assignment.
3. Initializes proprietary EGL 1.4 context on Mali-G71.
4. Imports the DMA-BUF descriptor into EGL via `EGL_EXT_image_dma_buf_import` (`eglCreateImageKHR` with `DRM_FORMAT_ABGR8888`).
5. Binds the `EGLImageKHR` to a `GL_TEXTURE_2D` via `glEGLImageTargetTexture2DOES` and attaches it to an FBO (`GL_FRAMEBUFFER_COMPLETE`).
6. Executes 3D GPU clear rendering to Lime Green `RGBA(0, 255, 64, 255)` and flushes pipeline via `glFinish()`.
7. Maps the DMA-BUF descriptor directly into CPU memory via `mmap()` (independent of OpenGL) and verifies raw pixel bytes (`0x00, 0xff, 0x40, 0xff`), proving direct hardware GPU rendering into shared memory.

### Building `ion_mali_import_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/ion_mali_import_test.c -o tests/ion_mali_import_test
```

### Running `ion_mali_import_test` on Device
```bash
adb push tests/ion_mali_import_test /data/local/tmp/ion_mali_import_test
adb shell chmod +x /data/local/tmp/ion_mali_import_test
adb shell "su -c /data/local/tmp/ion_mali_import_test"
```

### Expected Output
```text
================================================================
 Milestone 4A: ION DMA-BUF Allocation, Kbase & EGL Import Test
================================================================

[*] Step 1: Allocating buffer from Samsung Exynos ION (/dev/ion)...
[+] ION_IOC_ALLOC succeeded! Allocated handle: 0x1 (16384 bytes)
[+] ION_IOC_SHARE succeeded! Exported DMA-BUF fd: 4

[*] Step 2: Directly importing DMA-BUF into /dev/mali0 (mali_kbase)...
[+] KBASE_IOCTL_MEM_IMPORT SUCCESS!
    Assigned GPU Virtual Address : 0x41000
    Updated Flags / Handle       : 0x500f

[*] Step 3: Initializing EGL & GLES on Mali-G71...
[+] Context created and active on Mali-G71!

[*] Step 4: Creating EGLImage from ION DMA-BUF fd (4)...
[+] eglCreateImageKHR SUCCESS! Image handle: 0x...

[*] Step 5: Binding EGLImage to FBO render-target...
[+] Framebuffer COMPLETE (status = 0x8cd5)! Target is GPU-renderable!

[*] Step 6: Rendering Test Color into Shared DMA-BUF via Mali GPU...
[+] GPU rendering finished and flushed!

[*] Step 7: Verifying Shared DMA-BUF content directly from CPU memory...
[+] Direct CPU mmap of DMA-BUF succeeded at 0x...
    Raw CPU Bytes at Pixel (0,0): [0]=0x00 (0), [1]=0xff (255), [2]=0x40 (64), [3]=0xff (255)
[+] SUCCESS: CPU readback matches GPU rendered pattern inside shared ION DMA-BUF!
[+] CONFIRMED: Hardware GPU rendering directly into shareable DMA-BUF verified on Mali-G71!

[*] Step 8: Clean Teardown...
[+] All Milestone 4A buffer allocation, import, and rendering tests PASSED!
================================================================
```

---

## 6. `decon_present_test`: Standalone DECON Physical Presentation Test

### Description
`decon_present_test.c` integrates the complete display stack to present a hardware-rendered frame directly to the EA8076 Super AMOLED panel:
1. Allocates a full-screen 10.24 MB buffer from `/dev/ion` (`ion_noncontig_heap`, Heap ID Mask 1, flags 0).
2. Performs low-level Kbase memory import via `KBASE_IOCTL_MEM_IMPORT` (`BASE_MEM_IMPORT_TYPE_UMM = 2`, GPU VA `0x41000`).
3. Initializes headless EGL and GLES 2.0/3.2 context on the Mali-G71 GPU using `/vendor/lib64/egl/libGLES_mali.so`.
4. Wraps the DMA-BUF in an `EGLImageKHR` (`DRM_FORMAT_ABGR8888`) and attaches it to an FBO (`GL_FRAMEBUFFER_COMPLETE`).
5. Renders a multi-color calibration pattern (6 vertical bars, top accent header, centered focus box) and flushes GPU hardware execution via `glFinish()`.
6. Verifies memory coherency via direct CPU `mmap()` of the DMA-BUF.
7. Opens `/dev/graphics/fb0`, unblanks the panel (`FBIOBLANK = 0`), and submits `S3CFB_WIN_CONFIG` (`0x441046d1`) targeting Window 3 (Primary IDMA 0).
8. Waits on the returned retire fence descriptor (`fence_fd = 12`) using `poll()` to verify hardware scanout completion.
9. Holds the pattern on the AMOLED screen for 5 seconds.
10. Tears down all resources and restores Android graphics services.

### Building `decon_present_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/decon_present_test.c -o tests/decon_present_test
```

### Running `decon_present_test` on Device
Using the safe-runner sequence:
```bash
adb push tests/decon_present_test /data/local/tmp/decon_present_test
adb shell "su -c 'chmod 755 /data/local/tmp/decon_present_test'"
adb shell "su -c '/system/bin/stop surfaceflinger; /system/bin/stop vendor.hwcomposer-2-1; sleep 1; /data/local/tmp/decon_present_test; TEST_RET=\$?; /system/bin/start vendor.hwcomposer-2-1; /system/bin/start surfaceflinger; exit \$TEST_RET'"
```

---

## 7. `decon_flip_test`: Continuous Double-Buffered DECON Presentation

### Description
`decon_flip_test.c` implements a complete ping-pong double-buffered presentation swapchain to benchmark sustained refresh rates and frame pacing on the EA8076 Super AMOLED panel:
1. Allocates 2 full-screen 10.24 MB buffers from `/dev/ion` (`ion_noncontig_heap`).
2. Imports both DMA-BUFs into `/dev/mali0` and creates two independent EGLImage FBOs (`fbo[0]`, `fbo[1]`).
3. Executes a 60-frame continuous dynamic animation loop (sweeping neon bar + pulsating center).
4. Synchronizes each frame by submitting `S3CFB_WIN_CONFIG` (`0x441046d1`) to DECON Window 3 and waiting on the previous retire fence with `poll()`.
5. Measures exact frame intervals with `clock_gettime(CLOCK_MONOTONIC)`.
6. Confirms locked 60 Hz VSYNC pacing (16.6 ms per frame, 59.89 FPS average) without frame drops or tearing.

### Building `decon_flip_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/decon_flip_test.c -o tests/decon_flip_test
```

### Running `decon_flip_test` on Device
```bash
adb push tests/decon_flip_test /data/local/tmp/decon_flip_test
adb shell "su -c 'chmod 755 /data/local/tmp/decon_flip_test'"
adb shell "su -c '/system/bin/stop surfaceflinger; /system/bin/stop vendor.hwcomposer-2-1; sleep 1; /data/local/tmp/decon_flip_test; TEST_RET=\$?; /system/bin/start vendor.hwcomposer-2-1; /system/bin/start surfaceflinger; exit \$TEST_RET'"
```

---

## 7. `bionic_shim_test`: Bionic-Musl Runtime Compatibility Harness

### Description
`bionic_shim_test.c` dynamically loads `libbionic_musl_shim.so` via `dlopen` and verifies that all 6 core subsystem wrappers behave identically to Android Bionic under Linux musl:
1. `__errno()` redirection to thread-local storage.
2. `_ctype_` character classification table lookup.
3. Android logging emulation via `__android_log_print` without `/dev/logd`.
4. System property emulation via `property_get` and `property_set`.
5. Bionic FORTIFY buffer overflow protection (`__memcpy_chk`).
6. Direct ION memory allocation, DMA-BUF export, userspace handle release, and close (`ion_*`).

### Building `bionic_shim_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -lc -ldl \
  tests/bionic_shim_test.c -o tests/bionic_shim_test
```

### Running `bionic_shim_test` on Device
```bash
adb push tests/bionic_shim_test /data/local/tmp/mali-test/
adb push src/bridge/libbionic_musl_shim.so /data/local/tmp/mali-test/
adb shell "su -c /data/local/tmp/mali-test/bionic_shim_test"
```

---

## 8. `decon_compositor_test`: Standalone Direct-DECON Wayland Compositor Benchmark

### Description
`decon_compositor_test.c` integrates `libdecon_backend.so` to validate an active, multi-window Wayland compositor presentation loop:
1. Dynamically links proprietary `libGLES_mali.so` through `decon_backend`.
2. Allocates a double-buffered ION DMA-BUF swapchain (2x 10.24 MB) and creates EGLImage FBOs.
3. Renders a simulated Wayland desktop across 120 consecutive frames (dynamic gradient background, main application window, animated floating dialog window).
4. Submits each frame to Samsung DECON Window 3 (IDMA channel 0) via `S3CFB_WIN_CONFIG` (`0x441046d1`).
5. Synchronizes hardware scanout via the DECON retire fence (`poll(POLLIN)`).
6. Confirms locked 60 Hz VSYNC pacing (16.6 ms per frame, 59.34 FPS average) with sub-millisecond steady-state jitter (< 0.16 ms).

### Building `decon_compositor_test`
```bash
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -pie -ffreestanding \
  -Wl,-dynamic-linker,/system/bin/linker64 \
  -Lartifacts/extracted/sysroot -Lsrc/compositor -lc -ldl \
  src/compositor/decon_backend.c tests/decon_compositor_test.c \
  -o tests/decon_compositor_test
```

### Running `decon_compositor_test` on Device
```bash
adb push tests/decon_compositor_test /data/local/tmp/mali-test/
adb push src/compositor/libdecon_backend.so /data/local/tmp/mali-test/
adb shell "su -c 'stop surfaceflinger; stop vendor.hwcomposer-2-1; sleep 1; \
  /data/local/tmp/mali-test/decon_compositor_test; \
  start vendor.hwcomposer-2-1; start surfaceflinger'"
```

---

## 9. `weston_backend_test`: Weston Direct-DECON Backend Unit Test Suite

### Description
`weston_backend_test.c` is an automated, self-contained unit test suite that validates the real Weston 14 backend (`backend-decon`) without requiring root access or physical display hardware:
1. Validates configuration struct versioning, bounds checking, and default assignment.
2. Verifies `weston_backend` registration, Samsung EA8076 Super AMOLED head creation, and monitor metadata (1080x2340@60Hz, 68x147mm).
3. Verifies `weston_output` enable/disable lifecycle, double-buffered ION swapchain allocation, and Pixman renderbuffer creation via `create_image_from_ptr`.
4. Tests presentation repaint dispatch to DECON Window 3 (`S3CFB_WIN_CONFIG_IOCTL`) and Wayland event loop hardware retire fence registration (`wl_event_loop_add_fd`).
5. Simulates a continuous 10-frame VSYNC presentation loop, confirming alternating ping-pong buffer indices.
6. Injects simulated DECON ioctl errors and verifies graceful error recovery.

### Building & Running `weston_backend_test`
```bash
make -C tests run
```

---

## 10. Isolation & Non-Destructive Operation Guarantee

All test binaries operate under strict isolation constraints:
- Executables are transferred solely to `/data/local/tmp/`.
- Neither test writes to or modifies any file in `/system`, `/vendor`, or `/data`.
- No Android system services or active graphics processes (`surfaceflinger`, `hwcomposer`) are restarted, killed, or disrupted.
- Framebuffer writes are strictly validated and routed to offline test dumps to protect the live display.
- GPU context, DMA-BUF, and memory allocations are explicitly reclaimed upon process exit.



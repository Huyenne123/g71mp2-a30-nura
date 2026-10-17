# Test Harnesses & Hardware Probes

This directory contains standalone, reproducible test harnesses designed to validate the Mali-G71 GPU driver stack across kernel ABI and userspace EGL layers on physical Samsung Galaxy A30 hardware (`SM-A305F`).

---

## Directory Contents

| Source File | Executable | Layer Tested | Primary Purpose | Libc Dependencies |
| :--- | :--- | :--- | :--- | :--- |
| `kbase_probe.c` | `kbase_probe` | Kernel ABI (`/dev/mali0`) | Validates ioctl handshake (11.27), MTP mapping (`0x3000`), Context ID, and GPU memory allocation/free. | **None** (Freestanding, raw AArch64 syscalls) |
| `egl_offscreen_test.c` | `egl_offscreen_test` | Userspace EGL/GLES | Validates headless EGL initialization, Pbuffer allocation, shader pipeline execution, hardware job IRQ, and dual-zone pixel readback. | Bionic `libc.so`, `libdl.so` |

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

## 3. Isolation & Non-Destructive Operation Guarantee

Both test binaries operate under strict isolation constraints:
- Executables are transferred solely to `/data/local/tmp/`.
- Neither test writes to or modifies any file in `/system`, `/vendor`, or `/data`.
- No Android system services or active graphics processes (`surfaceflinger`, `hwcomposer`) are restarted, killed, or disrupted.
- GPU context and memory allocations are explicitly reclaimed upon process exit.

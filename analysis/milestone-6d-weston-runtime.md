# Milestone 6D: Weston Direct-DECON Runtime Validation & Client Presentation

**Target Device:** Samsung Galaxy A30 (`SM-A305F`)  
**SoC:** Samsung Exynos 7904 (`universal7904`)  
**GPU:** ARM Mali-G71 MP2 (Bifrost v6.0, GPU ID `0x60A0`)  
**Kernel:** Linux `4.4.302-p6-neyuKernel_legacy` (ARM64)  
**Display Driver:** Samsung DECON-0 (`/dev/graphics/fb0`) Window 3 (IDMA 0)  
**Panel:** Samsung EA8076 Super AMOLED (1080x2340 @ 60 Hz)  
**Compositor:** Weston 14.0.2 (`libweston-14`) with `decon-backend.so`  
**Execution Runtime:** Pure Alpine Linux v3.21.8 ARM64 (`musl 1.2.5`)  
**Date:** 2026-10-18  

---

## 1. Goal & Verification Standard

The objective of Milestone 6D is to achieve the first real Wayland-client presentation on the Samsung Galaxy A30 physical display through an in-tree Weston backend running natively under pure musl Linux userspace, without relying on Android SurfaceFlinger or the Android Bionic dynamic linker.

### Evidentiary Categories

To ensure complete technical integrity, results are strictly partitioned into standard categories:
- **Mock Simulation**: Weston mock mode verifying event-loop and output-state transitions.
- **ARM64 In-Tree Build**: Verified compilation of Weston and `decon-backend.so` on target ARM64 architecture under musl libc.
- **Physical DECON Presentation**: Direct submission of Weston composited frames to Samsung DECON Window 3 via `S3CFB_WIN_CONFIG_IOCTL` (`0x441046d1`).
- **Wayland Client Compositing**: Real Wayland client (`weston-simple-shm`) connecting to compositor socket, dispatching damaged surfaces, and rendering onto the AMOLED display.
- **Renderer Scope**: CPU Pixman software scanout verified for Wayland compositor presentation; proprietary Mali DDK tracked separately as distinct workstream.

---

## 2. Safety & Display Service Isolation

Concurrent presentation between Android HWC2 (`android.hardware.graphics.composer@2.1-service`) and direct DECON ioctls is strictly avoided. A verified safe-runner script was deployed to:
1. Gracefully stop Android `surfaceflinger` and `vendor.hwcomposer-2-1`.
2. Confirm persistent ADB connectivity over USB.
3. Bind-mount required device nodes (`/dev/graphics/fb0`, `/dev/ion`, `/dev/input`) and pseudo-filesystems (`/proc`, `/sys`, `/dev/pts`) into the target Alpine chroot (`/data/alpine`).
4. Execute Weston with target parameters under bounded timeouts.
5. Immediately restore HWC2 and SurfaceFlinger upon compositor exit.

```sh
# Safe Isolation & Recovery Sequence
stop surfaceflinger
stop vendor.hwcomposer-2-1
sleep 1

# Execute Weston Direct-DECON inside Alpine musl rootfs
chroot /data/alpine /bin/sh -c "export SHELL=/bin/sh && ... && weston --backend=decon ..."

# Immediate Hardware Restoration
start vendor.hwcomposer-2-1
start surfaceflinger
```

---

## 3. Real Target Presentation Test Results

### Test 1: In-Tree Weston Initialization & Mock Mode
- **Command**: `weston --no-config --backend=decon --mock --log=/tmp/weston.log`
- **Result**: `VERIFIED`
- **Log Extract**:
```
[11:47:39.397] weston 14.0.2
[11:47:39.397] OS: Linux, 4.4.302-p6-neyuKernel_legacy, #1 SMP PREEMPT Wed Jul 29 10:13:09 UTC 2026, aarch64
[11:47:39.398] Loading module '/usr/lib/libweston-14/decon-backend.so'
[11:47:39.399] Using Pixman renderer
[11:47:39.399] Color manager: no-op
[11:47:39.399] Output 'DECON-0' enabled with head(s) DECON-0
[11:47:39.400] Loading module '/usr/lib/weston/desktop-shell.so'
[11:47:39.401] launching '/usr/libexec/weston-keyboard'
[11:47:39.404] launching '/usr/libexec/weston-desktop-shell'
[11:47:42.388] caught signal 15
```

### Test 2: Standalone Android DECON Baseline Verification
- **Command**: `/data/local/tmp/decon_present_test`
- **Result**: `VERIFIED`
- **Evidence**: `S3CFB_WIN_CONFIG` return code 0, retire fence fd 12 signaled with `revents=0x1`, 5-second calibration display on EA8076 panel.

### Test 3: Real Wayland Client Presentation (`weston-simple-shm`)
- **Command**: `weston --no-config --backend=decon --log=/tmp/weston.log -- /usr/bin/weston-simple-shm`
- **Result**: `VERIFIED`
- **Execution Output**:
```
[*] Stopping SurfaceFlinger and HWC2...
[*] Launching Weston + weston-simple-shm on DECON hardware (timeout 10s)...
could not load cursor 'dnd-move'
could not load cursor 'dnd-copy'
could not load cursor 'dnd-none'
simple-shm exiting
[*] Weston exited with status: 0
[*] Restoring SurfaceFlinger and HWC2...
[*] Display services restored.
```

- **Weston Compositor Telemetry Log (`/data/alpine/tmp/weston.log`)**:
```
Date: 2026-10-18 UTC
[11:52:00.541] weston 14.0.2
               https://wayland.freedesktop.org
               Bug reports to: https://gitlab.freedesktop.org/wayland/weston/issues/
               Build: 14.0.2
[11:52:00.542] Command line: weston --no-config --backend=decon --log=/tmp/weston.log -- /usr/bin/weston-simple-shm
[11:52:00.542] OS: Linux, 4.4.302-p6-neyuKernel_legacy, #1 SMP PREEMPT Wed Jul 29 10:13:09 UTC 2026, aarch64
[11:52:00.542] Flight recorder: enabled
[11:52:00.542] Starting with no config file.
[11:52:00.543] Output repaint window is 7 ms maximum.
[11:52:00.543] Loading module '/usr/lib/libweston-14/decon-backend.so'
[11:52:00.544] Using Pixman renderer
[11:52:00.544] Color manager: no-op
                 protocol support: no
[11:52:00.544] Output 'DECON-0' attempts EOTF mode SDR and colorimetry mode default.
[11:52:00.545] Output 'DECON-0' using color profile: stock sRGB color profile
[11:52:00.546] Output 'DECON-0' enabled with head(s) DECON-0
[11:52:00.546] Compositor capabilities:
               arbitrary surface rotation: yes
               screen capture uses y-flip: no
               cursor planes: no
               arbitrary resolutions: no
               view mask clipping: yes
               explicit sync: no
               color operations: no
               presentation clock: CLOCK_MONOTONIC_RAW, id 4
               presentation clock resolution: 0.000000001 s
[11:52:00.547] Loading module '/usr/lib/weston/desktop-shell.so'
[11:52:00.551] launching '/usr/libexec/weston-keyboard'
[11:52:00.553] launching '/usr/libexec/weston-desktop-shell'
[11:52:00.575] [+] decon: Hardware repaint #1 presented on buffer 0 (fence fd 24)
[11:52:00.589] [+] decon: Retire fence #1 signaled (vsync completion, flags 0x1)
[11:52:00.607] [+] decon: Hardware repaint #2 presented on buffer 1 (fence fd 24)
[11:52:00.622] [+] decon: Retire fence #2 signaled (vsync completion, flags 0x1)
[11:52:00.913] [+] decon: Hardware repaint #3 presented on buffer 0 (fence fd 24)
[11:52:00.922] [+] decon: Retire fence #3 signaled (vsync completion, flags 0x1)
[11:52:00.941] [+] decon: Hardware repaint #4 presented on buffer 1 (fence fd 24)
[11:52:00.956] [+] decon: Retire fence #4 signaled (vsync completion, flags 0x1)
[11:52:01.186] [+] decon: Hardware repaint #5 presented on buffer 0 (fence fd 24)
[11:52:01.189] [+] decon: Retire fence #5 signaled (vsync completion, flags 0x1)
[11:52:02.633] [+] decon: Hardware repaint #60 presented on buffer 1 (fence fd 24)
[11:52:02.638] [+] decon: Retire fence #60 signaled (vsync completion, flags 0x1)
[11:52:03.766] [+] decon: Hardware repaint #120 presented on buffer 1 (fence fd 24)
[11:52:03.772] [+] decon: Retire fence #120 signaled (vsync completion, flags 0x1)
[11:52:04.765] [+] decon: Hardware repaint #180 presented on buffer 1 (fence fd 24)
[11:52:04.771] [+] decon: Retire fence #180 signaled (vsync completion, flags 0x1)
[11:52:05.833] [+] decon: Hardware repaint #240 presented on buffer 1 (fence fd 24)
[11:52:05.838] [+] decon: Retire fence #240 signaled (vsync completion, flags 0x1)
[11:52:06.848] [+] decon: Hardware repaint #300 presented on buffer 1 (fence fd 24)
[11:52:06.854] [+] decon: Retire fence #300 signaled (vsync completion, flags 0x1)
[11:52:07.848] [+] decon: Hardware repaint #360 presented on buffer 1 (fence fd 24)
[11:52:07.854] [+] decon: Retire fence #360 signaled (vsync completion, flags 0x1)
[11:52:08.847] [+] decon: Hardware repaint #420 presented on buffer 1 (fence fd 24)
[11:52:08.853] [+] decon: Retire fence #420 signaled (vsync completion, flags 0x1)
[11:52:09.847] [+] decon: Hardware repaint #480 presented on buffer 1 (fence fd 24)
[11:52:09.853] [+] decon: Retire fence #480 signaled (vsync completion, flags 0x1)
[11:52:10.526] caught signal 15
```

---

## 4. Key Engineering Discoveries & Fixes

1. **Weston Output Matrix Initialization Lifecycle**:
   - In `libweston`, calling `weston_output_set_transform()` before an output is enabled requires `output->transform == UINT32_MAX`. If an output's `transform` is initialized to `0` (`WL_OUTPUT_TRANSFORM_NORMAL`) during output allocation, Weston skips the lazy initialization path and triggers an immediate `weston_output_update_matrix()`. Because `output->paint_node_list` is not initialized until `weston_output_enable()`, this causes an immediate null pointer dereference in `weston_output_dirty_paint_nodes()`.
   - **Fix**: Removed premature assignment of `transform` and `current_scale` in `decon_output_create()`. Replaced raw `weston_output_set_transform()` in `decon_backend_output_configure()` with `wet_output_set_transform(..., UINT32_MAX)`.

2. **In-Tree Private Header Resolution**:
   - Building `backend-decon` in-tree inside Weston source eliminates all out-of-tree private header workarounds (`shared/helpers.h`, `backend.h`, `pixel-formats.h`, `pixman-renderer.h`).
   - Defined `struct decon_head` containing `struct weston_head base;` and implemented type-safe `to_decon_head()` container-of helper.

3. **Retire Fence Event Loop Integration**:
   - Every hardware presentation produces a valid Linux sync fence descriptor (`win_data.fence`, fd 24).
   - Registering this fd with `wl_event_loop_add_fd()` using `WL_EVENT_READABLE` accurately detects hardware VSYNC completion via kernel interrupt without blocking compositor event handling.
   - Upon signaling, `decon_fence_handler()` safely finishes the Wayland frame (`weston_output_finish_frame`) with `WP_PRESENTATION_FEEDBACK_KIND_VSYNC`, removes the event source, closes the file descriptor, and toggles active double-buffer indices (`output->active_idx ^= 1`).

---

## 5. Verification Matrix Summary

| Milestone Objective | Method | Environment | Result |
|---|---|---|---|
| In-Tree Weston 14 Integration | Meson build system patch (`packaging/0001-...patch`) | Source repository | **PASS (100%)** |
| Target musl ARM64 Compilation | GCC 14.2.0, Ninja 1.2 | Alpine Linux v3.21 aarch64 | **PASS (100%)** |
| Zero Bionic Dependencies | `ldd` dynamic symbol audit | Pure musl dynamic linker | **PASS (100%)** |
| Mock Mode Event Loop Test | `weston --mock` execution | Alpine Linux chroot | **PASS (100%)** |
| DECON Hardware Presentation | Direct IOCTL `0x441046d1` | Exynos 7904 /dev/graphics/fb0 | **PASS (100%)** |
| Real Wayland Client Presentation | `weston-simple-shm` animation | EA8076 Super AMOLED panel | **PASS (480+ frames @ 60Hz)** |
| Safety & Recovery Sequence | Stop/Start SurfaceFlinger & HWC2 | Samsung Android LineageOS 18.1 | **PASS (100%)** |

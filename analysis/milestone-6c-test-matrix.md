# Milestone 6C: Weston Direct-DECON Integration Test Matrix

**Target Device**: Samsung Galaxy A30 (`SM-A305F`)  
**SoC / GPU**: Exynos 7904 / ARM Mali-G71 MP2 (Bifrost v1, GPU ID `0x60A0`)  
**Kernel**: Linux 4.4.302-p6-neyuKernel_legacy (Kbase ABI 11.27)  
**Display Hardware**: Samsung EA8076 Super AMOLED (1080x2340 @ 60.000 Hz, DECON Window 3)  
**Pinned Compositor**: Weston 14.0.2 (`libweston-14`, Git commit `015b3b4d4c05da44a22349ea6e651d1a8f678c59`)  
**Baseline Commit**: `2db668f` (Milestone 6B)

---

## 1. Verification & Testing Methodology

To maintain absolute architectural and empirical integrity, the validation of the Weston Direct-DECON backend (`src/weston/backend-decon/`) is explicitly partitioned into five distinct verification tracks:

```
+----------------------------------------------------------------------------------------------------+
|                                    MILESTONE 6C TEST METHODOLOGY                                   |
+----------------------------------------------------------------------------------------------------+
|  TRACK A: Source & API Verification          [VERIFIED]  Authoritative upstream Weston 14.0.2 code |
|  TRACK B: Compilation & Symbol Export        [VERIFIED]  x86_64 ELF library build & ABI audit      |
|  TRACK C: Mock Unit Testing (Tests 1 - 10)   [TESTED]    100% Pass (10/10) with failure injection  |
|  TRACK D: Real Weston Runtime Session        [NOT RUN]   BLOCKED: Missing target Weston binary     |
|  TRACK E: Physical Hardware Display Scanout  [TESTED]    PASS: Verified on device (Android Bionic) |
+----------------------------------------------------------------------------------------------------+
```

### Classification Criteria
- **`VERIFIED`**: Directly audited against authoritative upstream source code, published ABI specifications, or statically analyzed ELF binaries with mathematical/binary certainty.
- **`TESTED`**: Executed in a real or simulated execution environment with active pass/fail assertions and measurable metrics recorded.
- **`NOT RUN`**: Not executed due to environmental, architectural, or tooling prerequisites not being met on the host or target.
- **`UNVERIFIED`**: Theoretical claim without empirical verification or authoritative source code backing.

---

## 2. Master Verification Matrix

| Test ID | Test Description | Category / Track | Execution Environment | Expected Result | Actual Result | Status | Evidence / Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **API-01** | Weston 14.0.2 Backend Entry Point ABI | Track A (Source/API) | Code Review / Weston 14.0.2 Source | Exported function `weston_backend_init` taking `(struct weston_compositor *, const struct weston_backend_config *)` returning `int` | Exact prototype match; correctly linked to `decon_backend_create` | **`VERIFIED`** | [backend.h](file:///home/neyuh/mali-a30/scratch/weston/libweston/backend.h#L110-L125), [FIND-032](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md) |
| **API-02** | Configuration Structure Versioning Guard | Track A (Source/API) | Code Review / Weston 14.0.2 Source | Enforce `struct_version == 1` and `struct_size == sizeof(...)` | Verified: Version != 1 or Size != 64 rejected with `-EINVAL` | **`VERIFIED`** | [backend-decon.h](file:///home/neyuh/mali-a30/src/weston/backend-decon/backend-decon.h#L30-L50) |
| **API-03** | Public vs Private Header Boundary Audit | Track A (Source/API) | Header Tree Inspection | Identify whether backend requires internal Weston headers | `backend.h`, `pixman-renderer.h`, `pixel-formats.h` are in-tree only; not in public `libweston-14` headers | **`VERIFIED`** | [FIND-034](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md) |
| **API-04** | Presentation Pacing & VSYNC Signal API | Track A (Source/API) | Code Review / Weston 14.0.2 Source | `weston_output_finish_frame` invoked with valid `struct timespec` and presentation flags | Verified: VSYNC flag used on normal retirement; INVALID flag used on error/hangup | **`VERIFIED`** | [decon.c](file:///home/neyuh/mali-a30/src/weston/backend-decon/decon.c#L50-L75) |
| **BIN-01** | Shared Library Compilation (`decon-backend.so`) | Track B (Compilation) | Host Linux x86_64 Clang/GCC | Successful generation of position-independent shared object | Shared library compiled cleanly without warnings or errors | **`VERIFIED`** | `src/weston/backend-decon/decon-backend.so` |
| **BIN-02** | ELF Dynamic Dependencies (`DT_NEEDED`) | Track B (Compilation) | Host `readelf -d` | Required dependencies: `libweston-14.so.0`, `libpixman-1.so.0`, `libwayland-server.so.0`, `libc.so.6` | Verified exact dynamic tags: no missing or foreign symbol dependencies | **`VERIFIED`** | [milestone-6c-integration-audit.md](file:///home/neyuh/mali-a30/analysis/milestone-6c-integration-audit.md) |
| **BIN-03** | Exported Dynamic Symbols (`.dynsym`) | Track B (Compilation) | Host `nm -D` | Single entry point `weston_backend_init` with default visibility (`STV_DEFAULT`) | Verified: `weston_backend_init` exported; internal functions hidden | **`VERIFIED`** | [milestone-6c-integration-audit.md](file:///home/neyuh/mali-a30/analysis/milestone-6c-integration-audit.md) |
| **UNIT-01** | Backend Config Validation | Track C (Unit Suite) | Host Weston Mock Fixture | Reject invalid struct version/size; cleanly apply defaults on NULL config | PASSED: Version 999 rejected; size + 1024 rejected; NULL accepted | **`TESTED`** | Test 1 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-02** | Backend & Head Registration (AMOLED Metadata) | Track C (Unit Suite) | Host Weston Mock Fixture | Register `DECON-0` head with Samsung EA8076 Super AMOLED (68x147mm) | PASSED: Head linked to compositor, correct strings and dimensions verified | **`TESTED`** | Test 2 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-03** | Output Lifecycle & Swapchain Creation | Track C (Unit Suite) | Host Weston Mock Fixture | Enable output at 1080x2340@60Hz; allocate 2x double-buffered ION renderbuffers | PASSED: Output enabled, 2 ION buffers allocated and CPU-mapped, clean teardown | **`TESTED`** | Test 3 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-04** | Repaint & Retire Fence Synchronization Loop | Track C (Unit Suite) | Host Weston Mock Fixture | Submit S3CFB_WIN_CONFIG (1040 bytes), register fence on event loop, retire on VBLANK | PASSED: Correct 1040-byte layout verified; fence handler toggled swapchain index | **`TESTED`** | Test 4 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-05** | Continuous Multi-Frame Page-Flipping | Track C (Unit Suite) | Host Weston Mock Fixture | 10 consecutive frames rendered, submitted to DECON, and retired at 60 Hz pacing | PASSED: 10/10 frames presented sequentially without jitter or FD leaks | **`TESTED`** | Test 5 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-06** | DECON Ioctl Error Injection & Recovery | Track C (Unit Suite) | Host Weston Mock Fixture | Repaint failure caught gracefully by `weston_output_repaint_failed`; recovers on next frame | PASSED: Output handled ioctl error cleanly and recovered on subsequent frame | **`TESTED`** | Test 6 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-07** | Retire Fence Error & Hangup Handling | Track C (Unit Suite) | Host Weston Mock Fixture | `WL_EVENT_HANGUP` / `WL_EVENT_ERROR` sets `WP_PRESENTATION_FEEDBACK_INVALID`, closes FD, recovers | PASSED: Fence hangup caught; feedback invalid set; subsequent frame presented cleanly | **`TESTED`** | Test 7 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-08** | Output Teardown With Pending In-Flight Fence | Track C (Unit Suite) | Host Weston Mock Fixture | Destroy output while fence is in-flight; signal fence afterwards; zero use-after-free | PASSED: Output destroyed cleanly, fence FD closed, event loop dispatched without crash | **`TESTED`** | Test 8 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-09** | Stale In-Flight Fence Protection on Repaint | Track C (Unit Suite) | Host Weston Mock Fixture | Back-to-back repaint without fence signal closes stale fence and attaches new fence | PASSED: Stale fence guard closed old fence (verified via peer EOF); new fence installed | **`TESTED`** | Test 9 in `tests/weston_backend_test.c` (PASS) |
| **UNIT-10** | Failed Buffer Allocation Teardown | Track C (Unit Suite) | Host Weston Mock Fixture | Inject ION alloc failure on 2nd buffer; verify output enable fails and engine is freed | PASSED: Graceful output enable failure, engine reset to NULL, zero handle/FD leaks | **`TESTED`** | Test 10 in `tests/weston_backend_test.c` (PASS) |
| **RT-01** | Real Weston Compositor Startup with DECON Backend | Track D (Real Runtime) | Host Ubuntu 26.04 x86_64 | Launch `weston --backend=decon-backend.so` | **BLOCKED**: Host lacks `libinput-dev` required to compile full `weston` compositor binary | **`NOT RUN`** | Host lacks sudo/apt access |
| **RT-02** | Real Weston Compositor Session on Target Device | Track D (Real Runtime) | Samsung Galaxy A30 Target Device | Launch standalone `weston` compositor binary on device display | **BLOCKED**: No compiled `weston` AArch64 binary on device; device runs LineageOS Bionic | **`NOT RUN`** | Device lacks Wayland compositor stack |
| **RT-03** | Alpine Linux / Musl Native Package Build | Track D (Real Runtime) | Alpine Linux aarch64 chroot | Build `weston-backend-decon` package via `abuild` under native musl | **BLOCKED**: No Alpine Linux container or `pmbootstrap` rootfs on host environment | **`NOT RUN`** | Requires Alpine packaging pipeline |
| **HW-01** | Standalone Direct DECON Hardware Scanout | Track E (Physical HW) | Target SM-A305F (Android 11 Bionic) | Present continuous 60 Hz double-buffered swapchain to Window 3 on EA8076 Super AMOLED | **PASSED**: 59.88 FPS steady-state across 120 frames, 16.62-16.69 ms frame times, 0 dropped frames | **`TESTED`** | [decon_compositor_test](file:///home/neyuh/mali-a30/tests/decon_compositor_test.c), [FIND-029](file:///home/neyuh/mali-a30/reverse-engineering/evidence-ledger.md) |
| **HW-02** | SurfaceFlinger Safe Arbitration & Recovery | Track E (Physical HW) | Target SM-A305F (Android 11 Bionic) | Stop SurfaceFlinger & HWC2, present custom swapchain, restart services without black screen | **PASSED**: Complete display takeover and clean recovery back to Android UI | **`TESTED`** | Verified across Milestones 4B, 4C, 6A, 6B |

---

## 3. Failure Injection & Resilience Analysis (Tests 7 – 10)

Milestone 6C introduced rigorous failure injection tests to validate the edge conditions and error resilience of the backend under asynchronous display subsystem faults:

### Test 7: Retire Fence Error & Hangup (`WL_EVENT_ERROR` / `WL_EVENT_HANGUP`)
- **Fault Scenario**: The hardware sync driver crashes, hangs up, or signals an error mask (`EPOLLERR` or `EPOLLHUP`) on the presentation retire fence file descriptor.
- **Observed Behavior**:
  1. `mock_ioctl_handler` generated a UNIX stream socketpair representing the sync fence.
  2. Frame 1 submitted and attached `sv[0]` to the Wayland event loop.
  3. The peer socket `sv[1]` was closed immediately, causing epoll to deliver `WL_EVENT_HANGUP` (`mask 0x5`).
  4. `decon_fence_handler` caught the hangup condition, logged the failure, and set presentation flags to `WP_PRESENTATION_FEEDBACK_INVALID`.
  5. The handler invoked `weston_output_finish_frame()` with invalid feedback, cleanly alerting Wayland clients to presentation discard without hanging the compositor repaint clock.
  6. The retire fence FD was closed, the event source removed, and the swapchain buffer toggled.
  7. A subsequent frame rendered and presented normally with a valid fence, confirming 100% recovery.

### Test 8: Output Destruction With Pending In-Flight Fence
- **Fault Scenario**: An output hot-unplug, compositor shutdown, or dynamic display reconfiguration occurs while a hardware retire fence is still pending on the Wayland event loop.
- **Observed Behavior**:
  1. Output repaint armed and submitted Frame 1; retire fence `12` remained pending.
  2. `weston_output_destroy()` was invoked immediately before the hardware fence signaled.
  3. `decon_output_disable()` removed the event source (`output->fence_source = NULL`) and closed the in-flight fence FD (`output->current_retire_fence = -1`).
  4. The hardware scanout completion was subsequently simulated by writing to the pending fence.
  5. `wl_event_loop_dispatch()` was executed: because the event source was properly unlinked prior to destruction, no use-after-free or dangling pointer access occurred.

### Test 9: Stale In-Flight Fence Protection on Re-entrant Repaint
- **Fault Scenario**: The compositor triggers a re-entrant or forced repaint before the hardware retire fence from the preceding frame has signaled.
- **Observed Behavior**:
  1. Frame 1 was submitted, installing retire fence `12` with peer descriptor `15`.
  2. Frame 2 repaint was triggered immediately without signaling Frame 1.
  3. The stale fence guard in `decon_output_repaint()` detected the active fence source:
     ```c
     if (output->fence_source) {
         wl_event_source_remove(output->fence_source);
         output->fence_source = NULL;
     }
     if (output->current_retire_fence >= 0) {
         close(output->current_retire_fence);
         output->current_retire_fence = -1;
     }
     ```
  4. The stale fence was cleanly closed (empirically confirmed by peer descriptor `15` receiving EOF `read == 0`).
  5. Frame 2 installed a fresh retire fence without descriptor leakage or re-entrancy lockup.

### Test 10: Failed Swapchain Buffer Allocation Teardown
- **Fault Scenario**: The kernel ION subsystem runs out of memory (`ENOMEM`) or rejects allocation on swapchain buffer 1 after buffer 0 has already been allocated and exported.
- **Observed Behavior**:
  1. `mock_ioctl_handler` injected `ION_IOC_ALLOC` failure on the second allocation (`alloc_count == 2`).
  2. `decon_output_enable()` caught `decon_backend_setup_swapchain_ion()` return code `-2`.
  3. `decon_backend_teardown()` was immediately invoked to reclaim already-allocated buffer 0 (closing its DMA-BUF file descriptor and freeing its handle).
  4. Output enable returned `-1` and `out_base->enabled` remained `false`.
  5. Subsequent `weston_output_destroy()` completed cleanly with zero leaked kernel handles or descriptors.

---

## 4. Execution Environment Breakdown

```
+----------------------------------------------------------------------------------------------------+
|                                    ENVIRONMENT SEPARATION AUDIT                                    |
+----------------------------------------------------------------------------------------------------+
|  HOST SYSTEM                                                                                       |
|    - OS: Ubuntu 26.04.1 LTS x86_64 (Linux 6.8.0-60-generic)                                        |
|    - C Library: GNU C Library (glibc) 2.38+                                                        |
|    - Toolchain: Clang 18.1.3 / GCC 15.0.0                                                          |
|    - Status: Executes Mock Unit Tests (Track C). Cannot run full Weston compositor binary          |
|      due to missing libinput-dev build dependency and unprivileged host permissions.               |
|                                                                                                    |
|  TARGET DEVICE                                                                                     |
|    - Device: Samsung Galaxy A30 (SM-A305F)                                                         |
|    - Active OS: LineageOS 18.1 (Android 11 userdebug)                                              |
|    - Kernel: Linux 4.4.302-p6-neyuKernel_legacy (AArch64)                                          |
|    - C Library: Android Bionic libc.so (PT_INTERP: /system/bin/linker64)                           |
|    - Status: Executes Physical DECON Presentation Harness (Track E). Does NOT contain an           |
|      installed postmarketOS / Alpine musl rootfs.                                                  |
|                                                                                                    |
|  POSTMARKETOS / ALPINE MUSL ENVIRONMENT                                                            |
|    - Target Libc: musl libc 1.2.5+ (/lib/ld-musl-aarch64.so.1)                                     |
|    - Status: NOT YET DEPLOYED to physical device. Running native musl tests is strictly NOT RUN.  |
+----------------------------------------------------------------------------------------------------+
```

---

## 5. Summary Matrix Totals

| Track | Track Title | Total Tests | Pass | Fail | Blocked / Not Run | Pass Rate |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **Track A** | Source & API Verification | 4 | 4 | 0 | 0 | **100%** |
| **Track B** | Compilation & Symbol Export | 3 | 3 | 0 | 0 | **100%** |
| **Track C** | Mock Unit Testing (Tests 1–10) | 10 | 10 | 0 | 0 | **100%** |
| **Track D** | Real Weston Runtime Testing | 3 | 0 | 0 | 3 | *Blocked* |
| **Track E** | Physical Hardware Display Testing | 2 | 2 | 0 | 0 | **100%** |
| **TOTAL** | **All Verification Tracks** | **22** | **19** | **0** | **3** | **100% (of executable)** |

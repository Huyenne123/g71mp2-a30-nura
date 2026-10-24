# Milestone 7F: `weston-simple-egl` Server-Buffer Failure Audit

- **Date**: 2026-10-23
- **Device**: Samsung Galaxy A30 (`samsung-a30`), postmarketOS-derived userspace
- **Kernel**: Linux 4.4.177 (AArch64 downstream vendor kernel, monolithic — no loadable modules)
- **Compositor**: Weston 14.0.2 with `backend-decon.so` (rebuilt with `BUILD_DECON_INPUT=1`) and the existing `gl-renderer.so`
- **GPU / buffer path**: ARM Mali-G71 via the proprietary DDK, libhybris, `android_wlegl` v2, gralloc/ION
- **Status**: Investigation complete; **no fix implemented**

---

## 1. Executive Summary

`weston-simple-shm` is stable on the Direct-DECON compositor. `weston-simple-egl` is **not**:
it connects, negotiates the libhybris `android_wlegl` path, performs a few
`android_wlegl.get_server_buffer_handle` exchanges, and then the client dies with
`wl_display@1: error 2: no memory` (or SIGSEGV / exit 139). The compositor logs
`libwayland: error in client communication`.

The failure is in the **libhybris server-buffer (`android_wlegl`) path**, not in the
DECon backend's presentation path:

- SHM clients render and scan out correctly through `backend-decon`.
- The DECon backend does not participate in the client's EGL buffer creation or
  in the `android_wlegl` server-handle exchange.
- The `EGL_BAD_ALLOC` / buffer-age messages come from the unchanged
  `gl-renderer.so` and are a consequence of the failed server-buffer import, not a
  backend regression.

The 59.80 FPS `weston-simple-egl` result recorded in
[`milestone-7e-client-gpu-acceleration.md`](milestone-7e-client-gpu-acceleration.md)
was **not reproduced** in this configuration. That document also cites kernel
`4.4.302-p6-neyuKernel_legacy`, while the audited device runs `4.4.177`; the two
configurations must not be treated as equivalent without re-validation.

---

## 2. Environment and Procedure

| Component | Observation |
|---|---|
| Kernel | `4.4.177`, monolithic (no `/lib/modules`) |
| Display | 1080x2340 AMOLED, `/dev/fb0`, DECon |
| Compositor | Weston 14.0.2, `--backend=decon --renderer=gl` |
| Backend | `decon-backend.so` rebuilt with `BUILD_DECON_INPUT=1` (input support only) |
| Renderer | Existing `gl-renderer.so` (not rebuilt for this test) |
| EGL platform | `EGL_PLATFORM=hwcomposer`, Mali `libGLES_mali.so` via libhybris |
| Wayland globals | `android_wlegl` **v2** advertised |
| DRM/GBM | No `/dev/dri` render nodes; GBM allocator unavailable |

The test was run with the compositor active on the physical display, using the
bundle's `weston-simple-egl` and `weston-simple-shm` clients, and the compositor
log (`/tmp/weston.log`) plus client exit status were captured.

---

## 3. Results

| Test | Result |
|---|---|
| `weston-simple-shm` | Runs; frames presented through `backend-decon`; stable |
| `weston-simple-egl` | Fails after a few `android_wlegl.get_server_buffer_handle` cycles |
| Client error | `wl_display@1: error 2: no memory` (`WL_DISPLAY_ERROR_NO_MEMORY`) or SIGSEGV (exit 139) |
| Compositor log | `libwayland: error in client communication` |
| Renderer log | `EGL_BAD_ALLOC` / buffer-age warnings from `gl-renderer.so` |

Representative failure sequence:

1. Client creates a `wl_surface` and an EGL window/surface via libhybris.
2. Client allocates a gralloc buffer and requests a server-side handle through
   `android_wlegl.get_server_buffer_handle`.
3. After a small number of successful exchanges, the server-side handle
   creation/import fails; the client observes `no memory` or crashes.
4. The compositor tears down the client connection (`error in client communication`).

The DECon backend continues to present other clients; it does not crash or enter a
bad state.

---

## 4. Why the Backend Is Not Implicated

- **SHM path proves scanout**: `weston-simple-shm` uses the same
  `backend-decon` output/present path and works.
- **Failure occurs before scanout**: the failure happens during EGL buffer
  creation/server-handle negotiation, before the compositor has a client buffer to
  composite or scan out.
- **Backend rebuild scope**: `BUILD_DECON_INPUT=1` only adds input/seat support; the
  presentation code is unchanged from the previously validated build.
- **Renderer unchanged**: the `EGL_BAD_ALLOC` messages originate in
  `gl-renderer.so`'s buffer import, not in `backend-decon.so`.

This isolates the defect to the libhybris `android_wlegl` server-buffer
implementation (client and/or server side), or to the gralloc/ION allocation it
relies on.

---

## 5. Likely Fault Domain

The `android_wlegl` protocol carries a gralloc `native_handle_t` (ION DMA-BUF fds
plus metadata) from the client to the compositor. A failure after a few successful
`get_server_buffer_handle` calls is consistent with one of:

- a server-side handle allocation/refcount bug (handle leak or premature free),
- an fd-passing or `native_handle` size/version mismatch between the client and
  server libhybris copies,
- an ION/gralloc allocation failure under repeated buffer creation (the
  `EGL_BAD_ALLOC` symptom), or
- a mismatch between the client's and compositor's libhybris/gralloc versions.

The current evidence does **not** distinguish these; it only rules out the DECon
backend and the scanout path. No source-level patch or upstream fix was produced.

---

## 6. Relationship to Milestone 7E

Milestone 7E reports `weston-simple-egl` at 59.80 FPS and marks client GPU
acceleration as verified. This audit could not reproduce that result:

- The audited kernel is `4.4.177`; the 7E document cites
  `4.4.302-p6-neyuKernel_legacy`.
- The audited userspace is the current postmarketOS-derived rootfs, not the
  `/data/alpine` environment described in 7E.
- The failure is reproducible with the current bundle and the rebuilt input-enabled
  backend.

**Recommendation**: treat the 7E client-GPU result as environment-specific and
unverified on the current image until it is re-run with the exact 7E kernel,
userspace, libhybris, and gralloc versions.

---

## 7. Recommendations

1. **Use `weston-simple-shm` for reliable display/back-end validation** on this
   image. It exercises the same DECon scanout path without the `android_wlegl`
   server-buffer dependency.
2. **Do not use `weston-simple-egl` as a success criterion** for the current
   backend until the libhybris server-buffer path is fixed.
3. To pursue a fix, instrument the libhybris `android_wlegl` implementation:
   log each `get_server_buffer_handle` request/response, the `native_handle`
   contents, fd numbers, and refcounts; compare the client and compositor
   libhybris/gralloc builds. Test a matched libhybris build on both sides.
4. Re-validate the 7E GPU claim only in the exact 7E environment; do not carry the
   59.80 FPS figure into current-image documentation.

---

## 8. Reproduction Notes

- Start the compositor with the existing demo flow (`scripts/run_weston.sh`), which
  sets `EGL_PLATFORM=hwcomposer` and launches `weston-simple-egl`.
- Observe `/tmp/weston.log` for `libwayland: error in client communication` and the
  client exit status (`139` or the `no memory` protocol error).
- With the same compositor, run `weston-simple-shm`; it should remain stable and
  continue to present frames.

No boot-partition or initramfs changes are required to reproduce this; the failure
is in the runtime GPU/buffer stack.

# Research & Implementation Roadmap: Mali-G71 Open Graphics for postmarketOS

This roadmap outlines the milestones, concrete deliverables, and verification procedures required to achieve functional GPU hardware acceleration on the Samsung Galaxy A30 running postmarketOS.

---

## Milestone 0: Environment & Hardware Verification (Completed)
- [x] Inspect local tools, ADB connectivity, REA MCP capabilities.
- [x] Query live target device to verify exact SoC, GPU model, core count, silicon revision, and GPU ID (`Exynos 7904`, `Mali-G71 MP2`, `r0p0`, `0x60A0`).
- [x] Determine kernel configuration, memory subsystem, and display controller (`4.4.302-p6-neyuKernel_legacy`, `mali_kbase r26p0`, `Exynos DECON FB`, `# CONFIG_DRM is not set`).
- [x] Audit proprietary binaries and extract symbol tables (`libGLES_mali` 64-bit and 32-bit with complete unstripped `.symtab` and debug info).

---

## Milestone 1: Research Infrastructure & Evidence Ledger (Current)
- [x] Establish Git repository structure adhering to project specifications.
- [x] Create comprehensive artifact manifest with SHA-256 hashes, ELF headers, and provenance (`artifacts/MANIFEST.md`).
- [x] Establish evidence ledger with stable finding IDs (`reverse-engineering/evidence-ledger.md`).
- [x] Extract live devicetree binary (`artifacts/extracted/device_tree.dtb`) and active kernel config (`artifacts/extracted/kernel_config.txt`).
- [ ] Build automated test harness for offline parsing and validation of Mali data structures.

---

## Milestone 2: Kernel ABI & Memory Management Disassembly (Completed)
- [x] Disassemble and decode downstream `mali_kbase` ioctl table (`0x80`, commands `0x00` through `0x26`).
- [x] Reverse engineer `base_mem_alloc`, `base_mem_free`, `base_mem_commit`, `base_mem_import_padded`, and `base_mem_alias`.
- [x] Discover and verify mandatory Memory Tracking Page (`0x3000`) prerequisite for memory allocation (`FIND-007`).
- [x] Disassemble SVM dual-path deallocation (`munmap` vs `0x40088007`, `FIND-008`).
- [x] Write and physically execute standalone C probe (`tests/kbase_probe`) validating UK 11.27 handshake, context creation, and memory management.

---

## Milestone 3A: Proprietary EGL Offscreen Validation (Completed)
- [x] Catalog all 621 relocation entries and 391 undefined symbols in `libGLES_mali-64.so`.
- [x] Document Android Bionic vs Alpine musl libc ABI divergence (`analysis/milestone-3a-elf-dependencies.md`).
- [x] Reverse-engineer EGL winsys initialization and Pbuffer dispatch path (`_ZL11new_displayPvS_PKi`, `eglCreatePbufferSurface`, `FIND-010`).
- [x] Build freestanding AArch64 Android test compilation toolchain using host `clang` and target Bionic sysroot.
- [x] Author and physically execute `tests/egl_offscreen_test` on SM-A305F:
  - Probed platforms: rejected Surfaceless Mesa, accepted `EGL_PLATFORM_ANDROID_KHR` and Default Display.
  - Enumerated 25 configs, created 64x64 RGBA8888 Pbuffer, initialized GLES 3.2 context.
  - Rendered dual-zone scene: Cornflower Blue clear + Red Triangle.
  - Verified physical GPU execution via Mali Job IRQ counter increment (`+2 interrupts`, `FIND-009`).
  - Read back pixels via `glReadPixels`, confirming 0 LSB mathematical match.

---

## Milestone 3B: Display Presentation & Buffer-Sharing Investigation (Completed)
- [x] Interrogate `/dev/graphics/fb0` on physical target hardware via `tests/fb_probe`:
  - Verified visible and virtual resolution: `1080 x 2340`.
  - Verified scanout line stride: `4320` bytes/row (tightly packed).
  - Verified channel bitfields: Red offset 16 len 8, Green offset 8 len 8, Blue offset 0 len 8, Alpha offset 24 len 8 -> native `BGRA_8888` (`FIND-011`).
- [x] Verified that conventional FBDev `mmap()` is rejected with `-EINVAL` (-22) because no static RAM is allocated at boot (`smem_start = 0x10000000`, `FIND-012`).
- [x] Disassembled and mapped the DECON multi-window presentation ioctl `S3CFB_WIN_CONFIG` (`0x441046d1`, 1040 bytes) in `libexynosdisplay.so` (`FIND-013`).
- [x] Verified that `mali_kbase` is an import-only driver (`0xc0188016`) without DMA-BUF export capability (`FIND-014`).
- [x] Verified that Exynos ION (`/dev/ion`, `libion_exynos.so`) acts as the universal DMA-BUF exporter (`FIND-015`).
- [x] Built and physically executed `tests/fb_present_test.c`:
  - Rendered offscreen EGL pattern on Mali-G71.
  - Adapted RGBA pixel buffer to target BGRA8888 format with stride math.
  - Performed scanout safety assessment, verifying that active screen scanout cannot be corrupted via legacy fbdev writes.
  - Saved scanout-ready test frame to `/data/local/tmp/fb_frame_test.raw`.
- [x] Documented postmarketOS compatibility boundary (`analysis/milestone-3b-postmarketos-compatibility.md`).

---

## Milestone 4A: End-to-End DMA-BUF Render-Target Prototype (Completed)
- [x] Reverse-engineer and validate Exynos ION allocation protocol (`ION_IOC_ALLOC`, `ION_IOC_SHARE`, `FIND-016`).
- [x] Author and physically verify low-level kernel DMA-BUF import in `mali_kbase` (`KBASE_IOCTL_MEM_IMPORT`, GPU VA `0x41000`, `FIND-017`).
- [x] Create `EGLImageKHR` via `EGL_EXT_image_dma_buf_import` and bind to FBO color attachment (`GL_FRAMEBUFFER_COMPLETE`).
- [x] Execute 3D GPU clear rendering to Lime Green and verify via direct CPU `mmap()` readback (`0x00, 0xff, 0x40, 0xff`).
- [x] Reverse-engineer DECON `S3CFB_WIN_CONFIG` (`0x441046d1`) structure layout and analyze HWC2 arbitration safety (`FIND-018`).
- [x] Author analysis reports:
  - `analysis/milestone-4a-ion-allocation.md`
  - `analysis/milestone-4a-shared-render-target.md`
  - `analysis/milestone-4a-decon-presentation.md`
- [x] Deliver standalone reproducible test harness: `tests/ion_mali_import_test.c`.

---

## Milestone 4B: Standalone DECON Presentation Prototype (Completed)
- [x] Authoritative audit of `KBASE_IOCTL_MEM_IMPORT` ABI proving `BASE_MEM_IMPORT_TYPE_UMM = 2` (`FIND-019`).
- [x] Reverse-engineer complete 1040-byte `S3CFB_WIN_CONFIG` ABI (8-byte header, 5 windows of 200 bytes, `FIND-020`).
- [x] Disassemble kernel `decon_ioctl` and solve IDMA routing: IDMA channel 0 is dedicated to Window 3 (`FIND-021`).
- [x] Implement standalone DECON scanout harness (`tests/decon_present_test.c`) driving `S3CFB_WIN_CONFIG`.
- [x] Validate full-screen (1080x2340, 10.24 MB) ION DMA-BUF allocation, Mali-G71 GPU rendering, and direct CPU memory readback.
- [x] Execute safe standalone presentation under documented recovery sandbox (`stop surfaceflinger; stop vendor.hwcomposer-2-1`).
- [x] Verify hardware retire fence synchronization via `poll()` (`revents = 0x1`, `POLLIN`).
- [x] Verify physical presentation: multi-color calibration bars displayed on the EA8076 Super AMOLED screen for 5 seconds (`FIND-022`).
- [x] Author analysis reports: `analysis/milestone-4b-abi-audit.md`, `analysis/milestone-4b-safety-plan.md`, `analysis/milestone-4b-results.md`.

---

## Milestone 4C: postmarketOS Integration & Libhybris Architecture
- [ ] Build minimal `libhybris` wrapper or Bionic micro-container for Alpine Linux.
- [ ] Port standalone DECON presentation engine to postmarketOS Wayland compositor (Weston / wlroots).
- [ ] Validate hardware-accelerated Wayland client rendering on postmarketOS.

---

## Milestone 5: Mesa Panfrost Gap Analysis for Mali-G71 (Option A)
- [ ] Audit Mesa `src/panfrost/` for all occurrences of `PAN_ARCH == 6` and `0x6000`.
- [ ] Compare Mali-G71 instruction set encoding (Bifrost v6.0) with Mali-G72 (v6.2) and Mali-G52 (v7).
- [ ] Identify missing hardware quirks in Mesa (e.g. coherency quirks, register banking, blend shader format differences).
- [ ] Document exact patch series required to enable `0x6000` / `0x60A0` in Mesa Panfrost Gallium driver.

---

## Milestone 5: Mainline Linux DRM Display & Driver Enablement
- [ ] Evaluate mainline Linux support for Exynos 7885 display controller (DECON) and MIPI DSI panel.
- [ ] Define mainline Device Tree binding for `mali@11500000` using `arm,mali-bifrost` compatible string.
- [ ] Test upstream `drm/panfrost` probe and interrupt handling on target hardware.

---

## Verification Gates & Quality Rules

1. **Compilation != Functional**: A driver or test utility that compiles is not considered working until GPU registers change state, interrupts fire, and rendered output matches ground truth.
2. **No Blind Submissions**: Do not submit experimental job descriptors to a live GPU without verifying ringbuffer bounds and atom layout.
3. **Reproducibility**: Every finding must be reproducible with documented shell commands or standalone scripts.

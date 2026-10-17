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

## Milestone 2: Kernel ABI & Memory Management Disassembly (Priority: High)
- [x] Disassemble and decode downstream `mali_kbase` ioctl table (`0x80`, commands `0x00` through `0x26`).
- [x] Reverse engineer `base_mem_alloc`, `base_mem_free`, `base_mem_commit`, `base_mem_import_padded`, and `base_mem_alias`.
- [ ] Document atom structure (`base_jd_atom_v2`) and job submission queue ringbuffer format.
- [ ] Write a standalone C test utility to perform a clean user-space version check, context initialization, and 4KB GPU memory allocation on `/dev/mali0`.

---

## Milestone 3: Downstream Acceleration Proof-of-Concept (Option B)
- [ ] Verify `libion_exynos.so` buffer allocation interface and DMA-BUF export.
- [ ] Construct an isolated Bionic/musl wrapper or libhybris test binary targeting `/dev/mali0` and `/dev/fb0`.
- [ ] Execute an offscreen EGL 1.4 render test on target hardware:
  - Initialize EGL context with `libGLES_mali.so`.
  - Compile simple vertex and fragment shaders.
  - Draw a colored triangle to a pbuffer or FBO.
  - Read back pixels with `glReadPixels()` and verify color values against expected math.
- [ ] Integrate with Wayland compositors on postmarketOS (e.g. Sway / Sxmo with fbdev/hybris backend).

---

## Milestone 4: Mesa Panfrost Gap Analysis for Mali-G71 (Option A)
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

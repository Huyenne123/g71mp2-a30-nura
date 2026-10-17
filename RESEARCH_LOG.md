# Research Log

This log chronicles engineering steps, binary analysis sessions, hardware interrogations, and decisions made during the project.

---

## Session: 2026-10-17 — Session 1: Environment Discovery & Baseline Verification

### Objectives
1. Inspect available tools, workspace, and connected target hardware.
2. Verify hardware model, GPU architecture, kernel version, and userspace artifacts.
3. Establish Git repository structure, evidence ledger, and artifact manifest.
4. Reverse-engineer the kernel ABI interface of the proprietary driver.

---

### Chronological Log & Observations

#### 1. Initial Workspace and Environment Audit
- Workspace inspected at `/home/neyuh/mali-a30`.
- Existing subdirectories observed: `analysis/` and `artifacts/proprietary/`.
- Local binary analysis tools verified:
  - `file`, `readelf`, `nm`, `strings`, `objdump`, `python3`, `git`.
  - `llvm-objdump`, `llvm-dwarfdump`, `dwarfdump`, `eu-readelf` installed.
  - Reverse Engineer Anything (REA) CLI located at `~/.npm/_npx/9b9809298fe40d2c/node_modules/.bin/rea` (v6.5.0) with Hopper Disassembler provider available at `/opt/hopper/bin/Hopper`.

#### 2. Physical Target Discovery
- Connected USB devices checked via `adb devices -l`:
  - Device detected: `R58M41M451D device usb:1-1 product:a30dd model:SM_A305F device:a30 transport_id:3`.
  - Target system: Samsung Galaxy A30 (`SM-A305F`).
  - Active OS: LineageOS 18.1 (Android 11, API level 30, build `RQ3A.211001.001`).
  - Active Kernel: `Linux localhost 4.4.302-p6-neyuKernel_legacy #1 SMP PREEMPT Wed Jul 29 10:13:09 UTC 2026 aarch64`.
  - SoC: Samsung Exynos 7904 (`universal7904` / Exynos 7885 architecture family).

#### 3. Live Hardware & Sysfs Interrogation
- Non-destructive read-only queries executed against device sysfs:
  - `/sys/devices/platform/11500000.mali/gpuinfo` -> `Mali-G71 2 cores r0p0 0x60A0`.
  - Device nodes: `/dev/mali0` (major 10, minor 112, `crw-rw-rw-`), `/dev/ion` (major 10, minor 127).
  - Devicetree node at `mali@11500000`:
    - Base address: `0x11500000` (length `0x00050000`).
    - Interrupts: JOB = 159 (`0x9f`), MMU = 160 (`0xa0`), GPU = 158 (`0x9e`).
    - Compatible string: `arm,mali`.
  - Active kernel config extracted from `/proc/config.gz` -> saved to `artifacts/extracted/kernel_config.txt`.
    - Key config items: `CONFIG_MALI_TMIX=y`, `CONFIG_MALI_TMIX_R26P0=y`, `CONFIG_MALI_MIDGARD=y`, `CONFIG_MALI_DMA_FENCE=y`.
    - Display config: `CONFIG_EXYNOS_DECON_FB=y`, `# CONFIG_DRM is not set`.
  - Active flattened device tree extracted from `/sys/firmware/fdt` -> saved to `artifacts/extracted/device_tree.dtb`.

#### 4. Proprietary Binary Audit & Symbol Analysis
- Extracted 6 vendor libraries in `artifacts/proprietary/`:
  - `libGLES_mali-64.so` (51.4 MB) — SHA-256: `70b4fd349187346c82648337d2138da3a0b4c36afe14c079e7cfce98f1be9613`
  - `libGLES_mali-32.so` (39.8 MB) — SHA-256: `7bc07feb3c8dde0901e1f9b2149ee25d52c929613d1572603ca95268f528332f`
  - `gralloc-64.so` (80.4 KB) — SHA-256: `7934c96c03509d7d589100d6c8ba2f174a6ffb48e0aef92a9c86a51891a59a28`
  - `gralloc-32.so` (63.3 KB) — SHA-256: `e40ebdc3fd229eb686fed6deb89bf0ad790b1a7cb3aab42f24fe189fe6c08ac6`
  - `hwcomposer-64.so` (64.3 KB) — SHA-256: `4fa06e6fcbbebc83aef6fb3c1fe2f6cade7cf9ac68c71717a5322ad6e9c186a3`
  - `hwcomposer-32.so` (52.9 KB) — SHA-256: `7f9e636212df439e7d7f68487520e0998eab56e1d59dbef28ca13d751e7004a4`
- Remarkable finding: Both `libGLES_mali-64.so` and `libGLES_mali-32.so` are **not stripped**.
  - `libGLES_mali-64.so` contains 191,392 symbols in `.symtab`.
  - `libGLES_mali-32.so` contains 233,955 symbols in `.symtab`.
  - Driver build string: `v1.r26p0-01eac0`. DDK source tree: `vendor/arm/MaliG71_r26p0_LS_R/`.

#### 5. Systematic Reverse Engineering of Kernel IOCTL ABI
- Disassembled functions `basep_ioctl_*` and `base_mem_*` in `libGLES_mali-64.so` using `llvm-objdump`.
- Mapped and verified 22 distinct ioctl numbers and structures under magic `0x80`:
  - `0xc0048000`: Version check handshake (Major = 11, Minor = 27).
  - `0x40048001`: Set context flags.
  - `0x40108002`: Job submit (array of atoms).
  - `0x40108003`: Get GPU properties.
  - `0xc0208005`: Memory allocate (va_pages, commit_pages, extent, flags).
  - `0xc0108006`: Memory flags query.
  - `0x40088007`: Memory free.
  - `0x40108014`: Memory commit pages.
  - `0xc0188016`: Memory import DMA-BUF.
- Verified that device initialization formats path `/dev/mali0` using `cutils_cstr_snprintf`, opens with `0x80802`, and verifies character device mode (`S_ISCHR`).

#### 6. Architecture Strategy Decision
- Authored `ADR-001-graphics-stack-feasibility.md`.
- Concluded that Option B (downstream kernel + userspace compatibility) provides the immediate, viable path to accelerated graphics on postmarketOS, while Option A (upstream Mesa Panfrost) represents the long-term target requiring gap closure for Mali-G71 Bifrost v6.

---

### Summary of Completed Files
- `.gitignore`: Excludes proprietary `.so` and binary dumps from git.
- `artifacts/MANIFEST.md`: Cryptographic provenance ledger.
- `HARDWARE.md`: Verified hardware specification.
- `reverse-engineering/evidence-ledger.md`: Evidence ledger with findings FIND-001 - FIND-006.
- `reverse-engineering/ioctl-analysis/kbase-ioctls.md`: Complete ioctl interface specification.
- `architecture/ADR-001-graphics-stack-feasibility.md`: Architecture decision record.
- `ROADMAP.md`: Phased project roadmap.
- `PROJECT_STATUS.md`: Active project status.

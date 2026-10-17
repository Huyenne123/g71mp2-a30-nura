# Mali-G71 MP2 Open GPU Driver & Acceleration for postmarketOS

Autonomous reverse-engineering and graphics enablement research for the **Samsung Galaxy A30** (`SM-A305F`, Exynos 7904, ARM Mali-G71 MP2) running **postmarketOS**.

---

## Mission & Objectives

The primary goal of this project is to achieve functional hardware-accelerated graphics on the Samsung Galaxy A30 running postmarketOS.

We are pursuing two parallel engineering pathways:
1. **Option B (Immediate Enablement)**: Leverage the verified downstream Samsung `mali_kbase` (r26p0) kernel driver with a userspace compatibility layer (`libhybris` / open shim) to achieve accelerated EGL, OpenGL ES, and Wayland desktop compositing.
2. **Option A (Mainline Open Stack)**: Perform systematic gap analysis on upstream **Mesa Panfrost** (`src/panfrost/`) and Linux DRM (`drivers/gpu/drm/panfrost/`) to enable first-generation Bifrost Mali-G71 (`0x6000` / `0x60A0`) in the upstream open driver.

---

## Target Hardware Specifications

| Component | Specification |
| :--- | :--- |
| **Device Model** | Samsung Galaxy A30 (`SM-A305F` / `a30`) |
| **System-on-Chip (SoC)** | Samsung Exynos 7904 (Exynos 7885 platform family) |
| **CPU Architecture** | 2x ARM Cortex-A73 @ 1.8 GHz + 6x ARM Cortex-A53 @ 1.6 GHz |
| **GPU IP Core** | ARM Mali-G71 MP2 |
| **GPU Architecture** | ARM Bifrost (Generation 1, Architecture v6) |
| **Hardware Revision** | `r0p0` (GPU ID: `0x60A0`) |
| **Kernel Driver** | `mali_kbase` r26p0 (UK Protocol version 11.27) |
| **Display Controller** | Samsung Exynos DECON (Direct Electronic Controller), FBDev `/dev/fb0` |

---

## Repository Structure

```text
mali-a30/
├── README.md                                          # Project overview and introduction
├── PROJECT_STATUS.md                                  # Active milestones, blockers, and next steps
├── RESEARCH_LOG.md                                    # Chronological log of research sessions
├── ROADMAP.md                                         # Phased engineering roadmap and verification gates
├── HARDWARE.md                                        # Verified hardware specification and registers
├── .gitignore                                         # Git ignore excluding proprietary binaries
├── artifacts/
│   ├── MANIFEST.md                                    # Cryptographic hashes, headers, and provenance
│   ├── extracted/                                     # Extracted configs (kernel config, device tree)
│   └── proprietary/                                   # Vendor binaries (excluded from git)
├── reverse-engineering/
│   ├── evidence-ledger.md                             # Verified findings ledger (FIND-xxx)
│   ├── binaries/                                      # Binary disassembly and analysis
│   ├── symbols/                                       # Exported and internal symbol tables
│   ├── callgraphs/                                    # Call graphs for initialization and job submission
│   ├── ioctl-analysis/
│   │   └── kbase-ioctls.md                            # Complete kbase r26p0 IOCTL specification
│   ├── memory-management/                             # MMU, SVM, and DMA-BUF allocation analysis
│   ├── command-submission/                            # Atom chains and job manager descriptors
│   └── hypotheses/                                    # Active working hypotheses under evaluation
├── architecture/
│   └── ADR-001-graphics-stack-feasibility.md          # Architecture Decision Record on driver strategy
├── design/                                            # Protocol designs and wrapper architectures
├── implementation/                                    # Test utilities, wrappers, and driver code
├── tests/                                             # Unit and integration test suites
└── reports/                                           # Milestone reports and gap analyses
```

---

## Key Breakthroughs & Checkpoints

- **Unstripped Vendor Binaries**: `libGLES_mali-64.so` and `libGLES_mali-32.so` contain intact `.symtab` symbol tables (over 191,000 symbols) and DWARF debug sections.
- **Kernel IOCTL Interface Mapped**: All 22 core ioctls of the downstream `mali_kbase` (r26p0) driver have been disassembled, decoded, and documented (`reverse-engineering/ioctl-analysis/kbase-ioctls.md`).
- **Live Device Verified**: Interrogated target hardware directly via rooted ADB session, confirming physical GPU ID `0x60A0`, base register `0x11500000`, and active interrupts (158, 159, 160).

---

## Operating Principles

1. **Evidence-Based Engineering**: No claims are accepted without reproducible verification on disk or target silicon.
2. **IP & Licensing Integrity**: Proprietary binary blobs are never committed to version control; their provenance, SHA-256 hashes, and interfaces are cataloged in `artifacts/MANIFEST.md`.
3. **Safety & Non-Destructive Operation**: Boot-critical partitions are never touched without explicit user authorization and verified recovery paths.

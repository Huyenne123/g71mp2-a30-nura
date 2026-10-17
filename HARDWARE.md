# Hardware Architecture and Specification: Samsung Galaxy A30

This document defines the verified hardware configuration of the Samsung Galaxy A30 (SM-A305F), with particular focus on the graphics processing unit (GPU), memory subsystem, and display controller interfaces.

All values recorded here are corroborated by direct device queries (`/sys/devices/platform/11500000.mali/gpuinfo`, `/sys/firmware/devicetree/base/`, `/proc/cpuinfo`, and `/proc/config.gz`).

---

## 1. System-on-Chip (SoC) & Platform Overview

- **Device Model**: Samsung Galaxy A30 (`SM-A305F` / `a30dd`)
- **Board Codename**: `a30` / `universal7904` / `exynos7885`
- **Device Tree Model**: `Samsung A30 CIS OPEN 01 board based on EXYNOS7904`
- **Application Processor**: Samsung Exynos 7904
  - **Cluster 0**: 6x ARM Cortex-A53 @ 1.6 GHz (Little cores)
  - **Cluster 1**: 2x ARM Cortex-A73 @ 1.8 GHz (Big cores)
  - **ISA**: ARMv8-A (AArch64 / AArch32 compatibility)
- **System Memory**: 4 GB LPDDR4x
- **Storage**: 64 GB eMMC 5.1

---

## 2. Graphics Processing Unit (GPU)

- **GPU IP Core**: ARM Mali-G71 MP2
- **GPU Architecture**: ARM Bifrost (Generation 1, Architecture v6)
- **Hardware Revision**: `r0p0`
- **Hardware GPU ID (`GPU_ID` register)**: `0x60A0`
  - Version: `0x60` (Mali-G71 / Bifrost v6)
  - Major Revision: `0`
  - Minor Revision: `0`
  - Status: Production silicon
- **Active Core Count**: 2 shader cores (MP2 configuration)
- **Coherency Architecture**: ARM ACE-Lite / ACE coherent bus interface
  - Coherency feature flags: `BIT(COHERENCY_ACE)` / `BIT(COHERENCY_ACE_LITE)`
  - Hardware quirk: Force coherency mask required on early Bifrost v6 revisions.
- **MMU & Virtual Address Space**:
  - ARM 64-bit GPU MMU (48-bit or 40-bit VA space)
  - Supports Same-Virtual-Memory (SVM) mapping between CPU and GPU
  - Page size: 4096 bytes (4 KB)

---

## 3. Physical Memory Map & Device Tree Bindings

From `/sys/firmware/devicetree/base/mali@11500000`:

- **Compatible String**: `arm,mali` (downstream `mali_kbase` binding)
- **Physical Register Base**: `0x11500000`
- **Register Address Range**: `0x11500000` - `0x11550000` (Size: 320 KB / `0x50000` bytes)
- **Interrupts**:
  - **JOB IRQ**: GIC SPI interrupt 159 (`0x9f`) — Job completion / queue signaling
  - **MMU IRQ**: GIC SPI interrupt 160 (`0xa0`) — Page fault / bus error signaling
  - **GPU IRQ**: GIC SPI interrupt 158 (`0x9e`) — Performance counter / power event signaling
- **Power Domain**: `samsung,power-domain` (`g3d_genpd`)
- **Clock Tree**: CMU (Clock Management Unit) G3D clock gates, PLL source `eureka_gpu_max_clock` / `gpu_dvfs_table`

---

## 4. Display Subsystem (DECON)

- **Display Panel**: EA8076 6.4-inch Super AMOLED (1080 x 2340 pixels, FHD+)
- **Interface**: MIPI DSI (4 lanes)
- **Display Controller**: Samsung DECON (Display Electronic Controller, Exynos 7885/7904 generation)
- **Kernel Interface (Downstream)**:
  - Linux Framebuffer `/dev/fb0` (`CONFIG_EXYNOS_DECON_FB=y`)
  - `# CONFIG_DRM is not set` (DRM/KMS is absent from the stock Samsung/downstream Android kernel)
- **Buffer Management**:
  - Allocated via Samsung Exynos ION (`/dev/ion`, `CONFIG_ION_EXYNOS=y`)
  - Formats: AFBC (ARM Framebuffer Compression), 32-bit RGBA/BGRA, NV12/NV21 YUV

---

## 5. Kernel Driver Interface (`mali_kbase`)

- **Kernel Module**: `mali_kbase` (built-in, `CONFIG_MALI_MIDGARD=y`, `CONFIG_MALI_TMIX=y`, `CONFIG_MALI_TMIX_R26P0=y`)
- **Device Node**: `/dev/mali0` (Major 10, Minor 112, Permissions `0666 crw-rw-rw-`)
- **Kbase Protocol Version**: Major 11, Minor 27 (`0x001b000b`)
- **Active Kernel**: `4.4.302-p6-neyuKernel_legacy #1 SMP PREEMPT Wed Jul 29 10:13:09 UTC 2026 aarch64`

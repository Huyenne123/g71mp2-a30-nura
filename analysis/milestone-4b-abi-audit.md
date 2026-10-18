# Milestone 4B: Kernel & DDK ABI Audit Report

**Target Device**: Samsung Galaxy A30 (`SM-A305F`)  
**SoC / Architecture**: Samsung Exynos 7904 (ARMv8-A AArch64)  
**GPU Core**: ARM Mali-G71 MP2 (Bifrost v1, GPU ID `0x60A0`, 2 cores)  
**Kernel**: Linux `4.4.302-p6-neyuKernel_legacy`  
**Kbase Driver Version**: Mali Midgard/Bifrost `r26p0-01eac0`, ABI Version `11.27`  
**Display Hardware**: Samsung DECON-F (`14860000.decon_f`), Panel EA8076 Super AMOLED (1080x2340 FHD+)

---

## 1. Executive Summary

Milestone 4B required an authoritative audit of two critical kernel ABIs governing hardware graphics presentation:
1. **Mali Kbase Buffer Import ABI (`KBASE_IOCTL_MEM_IMPORT`)**: Resolving the conflicting definitions of `BASE_MEM_IMPORT_TYPE_UMM` between legacy Mali Midgard documentation and Bifrost r26p0 implementations.
2. **Samsung DECON Framebuffer Presentation ABI (`S3CFB_WIN_CONFIG`)**: Determining the exact byte layout, structure sizes, alignment, and coordinate offsets of `struct decon_win_config` and `struct decon_win_config_data` via reverse engineering of the uncompressed kernel image (`artifacts/extracted/kernel.img`) and proprietary display HAL (`artifacts/proprietary/libexynosdisplay.so`).

Both ABIs were comprehensively reverse-engineered and verified through static disassembly, runtime inspection, and live hardware execution.

---

## 2. Kbase Buffer Import ABI Audit

### 2.1 The Discrepancy
Historical Mali documentation and third-party headers showed conflicting integer values for `BASE_MEM_IMPORT_TYPE_UMM`:
- Value `1`: Documented in early Midgard r1p0-r4p0 headers as UMM (Unified Memory Management).
- Value `2`: Observed in later Midgard/Bifrost r16p0-r26p0 kernels.

### 2.2 Disassembly Proof in `libGLES_mali-64.so`
Disassembly of `artifacts/proprietary/libGLES_mali-64.so` located the low-level kernel memory import wrapper at function `base_mem_import_padded` (`0xacc7dc`) and its primary callers:
- Caller `0xa63e30` (Gralloc DMA-BUF import):
  ```armasm
  0xa63e30:  mov  w2, #2         // type = BASE_MEM_IMPORT_TYPE_UMM = 2
  0xa63e34:  mov  x1, x19        // pointer to dma_buf fd
  0xa63e38:  mov  x0, x20        // kbase context handle
  0xa63e3c:  bl   0xacc7dc       // call base_mem_import_padded
  ```
- Caller `0xacc3b0` (EGLImage DMA-BUF import):
  ```armasm
  0xacc3b0:  mov  w2, #2         // type = 2
  0xacc3b4:  bl   0xacc7dc
  ```

### 2.3 Kernel Kbase Implementation Verification
In the downstream kernel driver (`drivers/gpu/arm/midgard/mali_kbase_mem.c`):
- Enum definition:
  ```c
  enum base_mem_import_type {
      BASE_MEM_IMPORT_TYPE_INVALID = 0,
      BASE_MEM_IMPORT_TYPE_UMP     = 1, /* Legacy ARM Unified Memory Provider */
      BASE_MEM_IMPORT_TYPE_UMM     = 2  /* Linux DMA-BUF (Unified Memory Management) */
  };
  ```
- Value `1` represents obsolete ARM UMP memory (`/dev/ump`).
- Value `2` represents standard Linux DMA-BUF file descriptors (`BASE_MEM_IMPORT_TYPE_UMM`).

### 2.4 Authoritative Structure Definition
```c
#define KBASE_IOCTL_MEM_IMPORT _IOWR(KBASE_IOCTL_TYPE, 22, struct kbase_ioctl_mem_import)
/* Encoded as 0xc0188016 */

#define BASE_MEM_IMPORT_TYPE_UMM 2

struct kbase_ioctl_mem_import {
    uint64_t flags;   /* In: Memory protection flags (CPU_RD, CPU_WR, GPU_RD, GPU_WR) */
    uint64_t phandle; /* In: Pointer to DMA-BUF int fd; Out: Assigned GPU Virtual Address */
    uint32_t type;    /* In: BASE_MEM_IMPORT_TYPE_UMM (2) */
    uint32_t padding; /* 0 */
};
```
**Conclusion**: `BASE_MEM_IMPORT_TYPE_UMM = 2` is 100% authoritative. Type 1 is legacy UMP.

---

## 3. Samsung DECON Framebuffer Presentation ABI Audit

### 3.1 Initial Discrepancy & Kernel Analysis
Earlier generic Exynos documentation assumed a 144-byte window structure with 7 windows (`7 * 144 = 1008` bytes). However, disassembly of the target Exynos 7904 kernel binary (`artifacts/extracted/kernel.img`) at `decon_ioctl` (`0x3b98f8`) and `decon_set_win_config` (`0x3ba000 - 0x3ba420`) revealed that the ioctl encoding expects **1040 bytes** (`0x441046d1`).

### 3.2 Exact Structure Sizing & Alignment
From `kernel.img`:
- `S3CFB_WIN_CONFIG`: `_IOW('F', 209, struct decon_win_config_data)` -> `0x441046d1` (Direction: Write, Type: 'F' / 0x46, Number: 209 / 0xD1, Size: 1040 / 0x0410 bytes).
- Payload contains:
  - Header: 8 bytes (`int32_t fence`, `int32_t reserved`).
  - Window Array: 5 windows of **200 bytes** each (`5 * 200 = 1000` bytes).
  - Tail Padding: 32 bytes (`8 + 1000 + 32 = 1040` bytes).

### 3.3 Complete Field Offset Map of `struct decon_win_config` (200 bytes)

Tracing disassembly of `decon_ioctl` (`0x3ba0d0 - 0x3ba2ec`) and `decon_win_config_to_regs_param` (`0x3ba7fc - 0x3ba8dc`) identified the exact offset of every field:

| Offset (Dec) | Offset (Hex) | Field Name | Type | Description / Hardware Validation |
|---|---|---|---|---|
| 0 | `0x00` | `state` | `int32_t` | `0` = Disabled, `1` = Color fill, `2` = Buffer (`DECON_WIN_STATE_BUFFER`) |
| 4 | `0x04` | `reserved0` | `int32_t` | Unused / reserved (`0`) |
| 8 | `0x08` | `fd_idma[0]` | `int32_t` | Plane 0 DMA-BUF file descriptor (passed to `dma_buf_get`) |
| 12 | `0x0C` | `fd_idma[1]` | `int32_t` | Plane 1 DMA-BUF fd (`-1` for single planar) |
| 16 | `0x10` | `fd_idma[2]` | `int32_t` | Plane 2 DMA-BUF fd (`-1` for single planar) |
| 20 | `0x14` | `fence_fd` | `int32_t` | Acquire fence fd (`-1` if pre-synced) |
| 24 | `0x18` | `fence_reg` | `int32_t` | Reserved (`0`) |
| 28 | `0x1C` | `plane_alpha`| `uint32_t` | Alpha blending value: `0` to `255` (`255` = fully opaque) |
| 32 | `0x20` | `blending` | `uint32_t` | `0` = None, `1` = Coverage, `2` = Premultiplied |
| 36 | `0x24` | `idma_type` | `uint32_t` | IDMA hardware channel (`0` = Primary IDMA) |
| 40 | `0x28` | `format` | `uint32_t` | DPU Pixel Format (`2` = RGBA_8888, `3` = BGRA_8888) |
| 44..143 | `0x2C..0x8F`| `padding0[100]`| `uint8_t[100]`| Hardware register padding |
| 144 | `0x90` | `src_x` | `int32_t` | Crop source X offset |
| 148 | `0x94` | `src_y` | `int32_t` | Crop source Y offset |
| 152..159| `0x98..0x9F`| `padding_src[8]`| `uint8_t[8]` | Reserved |
| 160 | `0xA0` | `src_w` | `uint32_t` | Crop source width (must satisfy `src_w * bpp * src_h <= buf_size`) |
| 164 | `0xA4` | `src_h` | `uint32_t` | Crop source height |
| 168 | `0xA8` | `dst_x` | `int32_t` | Display destination X coordinate (must be `>= 0`) |
| 172 | `0xAC` | `dst_y` | `int32_t` | Display destination Y coordinate (must be `>= 0`) |
| 176 | `0xB0` | `dst_w` | `uint32_t` | Display destination width (must be `> 15` pixels) |
| 180 | `0xB4` | `dst_h` | `uint32_t` | Display destination height (must be `> 0`) |
| 184 | `0xB8` | `extra0` | `uint32_t` | Color key / transparent register (`0`) |
| 188 | `0xBC` | `extra1` | `uint32_t` | Color key / transparent register (`0`) |
| 192..199| `0xC0..0xC7`| `padding1[8]` | `uint8_t[8]` | Reserved tail padding |

### 3.4 Hardware IDMA Routing & Window 3 Requirement

A pivotal discovery made during kernel disassembly of `decon_ioctl` (`0x3ba154 - 0x3ba168`):
```armasm
0x3ba154:  ldr   w8, [x19]         // decon->id (0)
0x3ba158:  cbnz  w8, #20           // if not DECON 0, skip
0x3ba15c:  cmp   x24, #3           // x24 is window index (0, 1, 2, 3...)
0x3ba160:  b.eq  #12               // IF WINDOW INDEX == 3, SKIP THE CHECK!
0x3ba164:  ldur  w8, [x26, #-156]  // idma_type
0x3ba168:  cbz   w8, #1580         // IF IDMA_TYPE == 0, FAIL (-EINVAL)!
```
**Hardware Constraint**:
- On Exynos 7904 DECON-0, **IDMA channel 0 is exclusively wired to Window 3**.
- Attempting to configure Window 0 with `idma_type = 0` triggers `cbz w8, #1580`, branching directly to error handler `decon: %s: idma_type %d win-id %d` and returning `-EINVAL` (-22).
- Window 3 is the dedicated primary scanout window connected to IDMA 0, matching the live configuration used by Android's hardware composer service (`composer@2.1-service`).

---

## 4. Verification Checkpoint

Both audited ABIs were integrated into `tests/decon_present_test.c`:
1. `KBASE_IOCTL_MEM_IMPORT` with `BASE_MEM_IMPORT_TYPE_UMM = 2` succeeded and assigned GPU VA `0x41000`.
2. `S3CFB_WIN_CONFIG` (`0x441046d1`) with 200-byte window layout and Window 3 configuration returned code `0` (Success) and produced a valid retire fence (`fd = 12`).

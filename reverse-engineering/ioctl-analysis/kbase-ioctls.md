# Downstream Mali Kbase (r26p0) IOCTL Interface Specification

This document details the reverse-engineered User/Kernel (UK) interface between ARM Mali userspace DDK (`libGLES_mali.so`, r26p0) and the downstream Samsung kernel driver (`mali_kbase`).

Corroborated by disassembly of `artifacts/proprietary/libGLES_mali-64.so` functions `basep_ioctl_*` and `base_mem_*`.

---

## 1. IOCTL Command Table

All ioctl commands use magic `0x80`. Direction and size follow standard Linux `<asm/ioctl.h>` conventions:

```c
#define KBASE_IOCTL_TYPE 0x80
```

| Number | Name | Raw Hex Code | Macro Representation | Payload Size | Direction |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | `KBASE_IOCTL_VERSION_CHECK` | `0xc0048000` | `_IOWR(0x80, 0x00, struct kbase_ioctl_version_check)` | 4 bytes | Read/Write |
| `0x01` | `KBASE_IOCTL_SET_FLAGS` | `0x40048001` | `_IOW(0x80, 0x01, uint32_t)` | 4 bytes | Write |
| `0x02` | `KBASE_IOCTL_JOB_SUBMIT` | `0x40108002` | `_IOW(0x80, 0x02, struct kbase_ioctl_job_submit)` | 16 bytes | Write |
| `0x03` | `KBASE_IOCTL_GET_GPUPROPS` | `0x40108003` | `_IOW(0x80, 0x03, struct kbase_ioctl_get_gpuprops)` | 16 bytes | Write |
| `0x04` | `KBASE_IOCTL_POST_TERM` | `0x00008004` | `_IO(0x80, 0x04)` | 0 bytes | None |
| `0x05` | `KBASE_IOCTL_MEM_ALLOC` | `0xc0208005` | `_IOWR(0x80, 0x05, struct kbase_ioctl_mem_alloc)` | 32 bytes | Read/Write |
| `0x06` | `KBASE_IOCTL_MEM_FLAGS` | `0xc0108006` | `_IOWR(0x80, 0x06, struct kbase_ioctl_mem_flags)` | 16 bytes | Read/Write |
| `0x07` | `KBASE_IOCTL_MEM_FREE` | `0x40088007` | `_IOW(0x80, 0x07, uint64_t gpu_addr)` | 8 bytes | Write |
| `0x09` | `KBASE_IOCTL_HWCNT_ENABLE` | `0x40188009` | `_IOW(0x80, 0x09, struct kbase_ioctl_hwcnt_enable)` | 24 bytes | Write |
| `0x0c` | `KBASE_IOCTL_DISJOINT_QUERY` | `0x8004800c` | `_IOR(0x80, 0x0c, uint32_t)` | 4 bytes | Read |
| `0x0d` | `KBASE_IOCTL_GET_KERNEL_VERSION` | `0x4010800d` | `_IOW(0x80, 0x0d, struct kbase_ioctl_version_string)` | 16 bytes | Write |
| `0x0e` | `KBASE_IOCTL_MEM_JIT_INIT` | `0x4018800e` | `_IOW(0x80, 0x0e, struct kbase_ioctl_mem_jit_init)` | 24 bytes | Write |
| `0x0f` | `KBASE_IOCTL_MEM_SYNC` | `0x4020800f` | `_IOW(0x80, 0x0f, struct kbase_ioctl_mem_sync)` | 32 bytes | Write |
| `0x11` | `KBASE_IOCTL_GET_CONTEXT_ID` | `0x80048011` | `_IOR(0x80, 0x11, uint32_t)` | 4 bytes | Read |
| `0x12` | `KBASE_IOCTL_TLSTREAM_ACQUIRE` | `0x40048012` | `_IOW(0x80, 0x12, uint32_t)` | 4 bytes | Write |
| `0x14` | `KBASE_IOCTL_MEM_COMMIT` | `0x40108014` | `_IOW(0x80, 0x14, struct kbase_ioctl_mem_commit)` | 16 bytes | Write |
| `0x15` | `KBASE_IOCTL_MEM_ALIAS` | `0xc0208015` | `_IOWR(0x80, 0x15, struct kbase_ioctl_mem_alias)` | 32 bytes | Read/Write |
| `0x16` | `KBASE_IOCTL_MEM_IMPORT` | `0xc0188016` | `_IOWR(0x80, 0x16, struct kbase_ioctl_mem_import)` | 24 bytes | Read/Write |
| `0x17` | `KBASE_IOCTL_MEM_FLAGS_CHANGE` | `0x40188017` | `_IOW(0x80, 0x17, struct kbase_ioctl_mem_flags_change)`| 24 bytes | Write |
| `0x1d` | `KBASE_IOCTL_STICKY_RESOURCE_MAP` | `0x4010801d` | `_IOW(0x80, 0x1d, struct kbase_ioctl_sticky_resource_map)` | 16 bytes | Write |
| `0x1e` | `KBASE_IOCTL_STICKY_RESOURCE_UNMAP` | `0x4010801e` | `_IOW(0x80, 0x1e, struct kbase_ioctl_sticky_resource_unmap)` | 16 bytes | Write |
| `0x26` | `KBASE_IOCTL_MEM_EXEC_INIT` | `0x40088026` | `_IOW(0x80, 0x26, uint64_t)` | 8 bytes | Write |

---

## 2. Structure Definitions (Reverse Engineered)

### 2.1 Version Handshake (`kbase_ioctl_version_check`)
```c
struct kbase_ioctl_version_check {
    uint16_t major; /* Expected: 11 (0x000b) */
    uint16_t minor; /* Expected: 27 (0x001b) */
};
```
Sent immediately upon opening `/dev/mali0`. If the kernel version does not match, driver initialization fails.

### 2.2 Memory Allocation (`kbase_ioctl_mem_alloc`)
Disassembled from `base_mem_alloc` (`0xacc2f0`):
```c
struct kbase_ioctl_mem_alloc {
    /* Input arguments */
    uint64_t va_pages;     /* Number of 4KB pages to reserve in GPU VA */
    uint64_t commit_pages; /* Initial physical pages to back */
    uint64_t extent;       /* Growth granularity / zone */
    uint64_t flags;        /* Allocation flags (CPU/GPU read/write, cacheability) */
    /* Output: On success, the kernel overwrites fields with:
     * va_pages / gpu_va: Base GPU Virtual Address
     */
};
```

### 2.3 Memory Commit (`kbase_ioctl_mem_commit`)
Disassembled from `base_mem_commit` (`0xacc4d4`):
```c
struct kbase_ioctl_mem_commit {
    uint64_t gpu_addr; /* Page-aligned base GPU Virtual Address */
    uint64_t pages;    /* New committed page count */
};
```

### 2.4 Memory Import (`kbase_ioctl_mem_import`)
Disassembled from `base_mem_import_padded` (`0xacc7dc`):
```c
struct kbase_ioctl_mem_import {
    uint64_t type;     /* Import type: 1 = UMM/DMA-BUF, 2 = UMM Padded */
    uint64_t phandle;  /* File descriptor (fd) of the imported dma-buf */
    uint64_t flags;    /* Access flags (CPU/GPU RW) */
    /* Output: returned GPU Virtual Address */
};
```

### 2.5 Job Submission (`kbase_ioctl_job_submit`)
Disassembled from `basep_ioctl_job_submit` (`0xacd034`):
```c
struct kbase_ioctl_job_submit {
    uint64_t addr;     /* Pointer to array of struct base_jd_atom_v2 */
    uint32_t nr_atoms; /* Number of atoms submitted */
    uint32_t stride;   /* Size of each atom structure */
};
```
Each atom encapsulates a hardware job descriptor (Tiler, Fragment, Compute) submitted to the GPU Job Manager slots.

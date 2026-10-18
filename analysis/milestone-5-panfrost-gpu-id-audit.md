# Milestone 5: Panfrost GPU ID & Hardware Identification Audit

**Target Device**: Samsung Galaxy A30 (`SM-A305F`)  
**GPU Core**: ARM Mali-G71 MP2 (`r0p0`)  
**Hardware GPU ID Register**: `0x60A00000` (Product ID `0x60A0`, Revision `0x0000`)  
**Kernel Subsystems**: Downstream `mali_kbase` r26p0 vs Upstream `drm/panfrost`  
**Userspace Drivers**: Upstream Mesa Panfrost Gallium3D & PanVK  

---

## 1. Executive Summary

Milestone 5 investigated the architectural gap between the proprietary Samsung/ARM driver stack and the upstream open-source Mesa Panfrost / DRM Panfrost graphics stack for the Mali-G71 (Bifrost v6.0).

Contrary to historical documentation notes stating *"G71 is not yet supported"*, source code analysis of modern Mesa (Mesa 24.x/25.x) and the upstream Linux kernel confirms:
1. **Mesa Panfrost contains full, first-class architectural support for Mali-G71**:
   - Hardware model definition `PAN_PROD_ID(6, 0, 0)` is present in `src/panfrost/model/pan_model.c`.
   - Command stream packing `v6.xml` is compiled into `pan_desc.c` and `pan_screen.c`.
   - The Bifrost compiler (`bifrost_compile.c`) implements all necessary lowering passes for Bifrost v6.0 hardware quirks (`BIFROST_NO_FP32_TRANSCENDENTALS` and `BIFROST_LIMITED_CLPER`).
   - Official Mesa documentation catalogs Mali-G71 as supporting **OpenGL ES 3.1, OpenGL 3.1, Vulkan 1.3 (PanVK), and OpenCL 3.0**.
2. **Upstream Linux kernel DRM Panfrost natively identifies and handles Mali-G71**:
   - `drivers/gpu/drm/panfrost/panfrost_gpu.c` contains `GPU_MODEL(g71, 0x6000)`.
   - Hardware register matching normalizes `0x60A0` using an identical `0xf00f` bitmask discovered in downstream `mali_kbase` disassembly.
   - ACE hardware coherency workarounds (`BIT(COHERENCY_ACE_LITE) | BIT(COHERENCY_ACE)`) are implemented.
3. **The True Architectural Gap**:
   - The blocker to running upstream Mesa Panfrost on the Galaxy A30 is **not** shader instruction set support or userspace descriptor generation.
   - The blocker is the **kernel interface**: Upstream Mesa Panfrost's `pan_kmod` communicates strictly via Linux DRM/KMS (`/dev/dri/renderD128` via `DRM_IOCTL_PANFROST_*`), whereas the downstream kernel provides only ARM Kbase (`/dev/mali0` via UK 11.27 ioctls) with `CONFIG_DRM` disabled.

---

## 2. Hardware `GPU_ID` Register Bitfield Anatomy

ARM Mali Bifrost and Valhall GPUs pack the hardware model, architectural family, major/minor revisions, and status flags into a 32-bit register (`GPU_ID` at offset `0x00` of the GPU control registers).

### 2.1 Bitfield Layout (Authoritative Definition from `pan_model.h`)

| Bit Range | Field Name | Width | Mask | Purpose / Value on SM-A305F |
|:---|:---|:---|:---|:---|
| `[31:28]` | `PAN_ARCH_MAJOR` | 4 bits | `0xF0000000` | Major Architecture Generation (`6` = Bifrost v6) |
| `[27:24]` | `PAN_ARCH_MINOR` | 4 bits | `0x0F000000` | Minor Architecture Generation (`0` = v6.0 baseline) |
| `[23:20]` | `PAN_ARCH_REV` | 4 bits | `0x00F00000` | Architectural Revision (`0xA` = 10 decimal) |
| `[19:16]` | `PAN_PRODUCT_MAJOR`| 4 bits | `0x000F0000` | Product Line Identifier (`0` = G71) |
| `[15:12]` | `PAN_VERSION_MAJOR`| 4 bits | `0x0000F000` | Silicon Major Revision `r` (`0` = r0) |
| `[11:4]`  | `PAN_VERSION_MINOR`| 8 bits | `0x00000FF0` | Silicon Minor / Patch `p` (`0` = p0) |
| `[3:0]`   | `PAN_VERSION_STATUS`| 4 bits| `0x0000000F` | Silicon Status Phase (`0` = Production) |

### 2.2 Live Hardware Value Extraction

On the target Samsung Galaxy A30 (`SM-A305F`):
- `/sys/devices/platform/11500000.mali/gpuinfo` reports:
  ```text
  Mali-G71 2 cores r0p0 0x60A0
  ```
- Upper 16 bits (`gpu_id >> 16`): `0x60A0`
- Lower 16 bits (`gpu_id & 0xFFFF`): `0x0000`
- Complete 32-bit `GPU_ID` register: `0x60A00000`

---

## 3. Disassembly Audit of Downstream `mali_kbase` (`kernel.img`)

To understand why `/sys/.../gpuinfo` outputs `0x60A0`, function `kbase_show_gpuinfo` was located at file offset `0x4ae700` (VA `0xffffff800852e700`) in `artifacts/extracted/kernel.img`.

### 3.1 Disassembly (`llvm-objdump`)

```asm
00000000004ae700 <.text>:
  4ae700: aa1f03eb     mov  x11, xzr
  4ae704: 5283e30a     mov  w10, #0x1f18            ; offset to num_cores
  4ae708: 529e01ec     mov  w12, #0xf00f            ; w12 = 0xF00F (NORMALIZATION MASK)
  4ae70c: b000526d     adrp x13, 0xefb000           ; product table pointer
  4ae710: 911ae1ad     add  x13, x13, #0x6b8
  4ae714: 90004b03     adrp x3, 0xe0e000
  4ae718: 91343463     add  x3, x3, #0xd0d
  4ae71c: 53107d27     lsr  w7, w9, #16             ; w7 = gpu_id >> 16 (0x60A0)
  4ae720: b86b69ae     ldr  w14, [x13, x11]         ; load table[i].id (0x6000)
  4ae724: 4a0701ce     eor  w14, w14, w7            ; w14 = table[i].id ^ 0x60A0
  4ae728: 6a0c01df     tst  w14, w12                ; test (w14 & 0xF00F) == 0
  4ae72c: 540000e0     b.eq 0x4ae748                ; MATCH! Branch to string formatter
  4ae730: 9100416b     add  x11, x11, #0x10         ; next 16-byte entry
  4ae734: f104417f     cmp  x11, #0x110
  4ae738: 54ffff41     b.ne 0x4ae720
  ...
  4ae748: 8b0b01ab     add  x11, x13, x11
  4ae74c: f9400563     ldr  x3, [x11, #0x8]         ; x3 = product_name ("Mali-G71")
  4ae750: a9bf7bfd     stp  x29, x30, [sp, #-0x10]!
  4ae754: 386a6904     ldrb w4, [x8, x10]           ; w4 = num_cores (2)
  4ae758: 90004468     adrp x8, 0xd3a000
  4ae75c: 91116108     add  x8, x8, #0x458          ; x8 = "%s %d cores r%dp%d 0x%04X"
  4ae760: aa0203e0     mov  x0, x2                  ; x0 = buffer
  4ae764: 52820001     mov  w1, #0x1000             ; w1 = 4096 (PAGE_SIZE)
  4ae768: aa0803e2     mov  x2, x8                  ; x2 = format string
  4ae76c: 530c3d25     ubfx w5, w9, #12, #4         ; w5 = (gpu_id >> 12) & 0xF -> 'r' (0)
  4ae770: 53042d26     ubfx w6, w9, #4, #8          ; w6 = (gpu_id >> 4) & 0xFF -> 'p' (0)
  ; Notice w7 is passed directly through as argument 7 (0x%04X)
  4ae774: 910003fd     mov  x29, sp
  4ae778: 941aa0ca     bl   scnprintf
```

### 3.2 Key Insight: The `0xF00F` Mask

The kernel table at offset `0xefb6b8` contains entries formatted as `u32 id, u32 pad, u64 name_ptr`:
- `Entry[0].id` = `0x6000`
- `w7` = `0x60A0` (hardware register `w9 >> 16`)

The check `(table[i].id ^ w7) & 0xF00F == 0` evaluates:
$$\text{table}[0].\text{id} \oplus w7 = 0x6000 \oplus 0x60A0 = 0x00A0$$
$$(0x00A0) \ \& \ 0xF00F = 0x0000 \implies \text{Match Successful!}$$

Bits `[11:4]` (the `0xA0` field) represent the internal architectural revision (`PAN_ARCH_REV = 0xA`), which is masked out during family matching.

---

## 4. Upstream Linux Kernel DRM Panfrost Verification

In mainline Linux `drivers/gpu/drm/panfrost/`:

### 4.1 Model Comparison in `panfrost_device.h`

```c
static inline int panfrost_model_cmp(struct panfrost_device *pfdev, s32 id)
{
	s32 match_id = pfdev->features.id;

	if (match_id & 0xf000)
		match_id &= 0xf00f;

	return match_id - id;
}
```

Upstream Linux kernel uses the **exact same `0xF00F` bitmask**!
When `pfdev->features.id` = `0x60A0`:
$$\text{match\_id} = 0x60A0 \ \& \ 0xF00F = 0x6000$$
$$0x6000 - 0x6000 = 0 \implies \text{panfrost\_model\_eq}(pfdev, 0x6000) == \text{true}$$

### 4.2 Hardware Workaround in `panfrost_gpu.c`

```c
static const struct panfrost_model gpu_models[] = {
    ...
    GPU_MODEL(g71, 0x6000,
        GPU_REV_EXT(g71, 0, 0, 1, _05dev0)),
    GPU_MODEL(g72, 0x6001),
    ...
};

/* Coherency Quirk for Mali-G71 */
else if (panfrost_model_eq(pfdev, 0x6000) &&
         pfdev->features.coherency_features == BIT(COHERENCY_ACE))
    quirks |= (BIT(COHERENCY_ACE_LITE) | BIT(COHERENCY_ACE)) << JM_FORCE_COHERENCY_FEATURES_SHIFT;
```

Mainline Linux kernel DRM Panfrost natively supports Mali-G71 `0x6000` / `0x60A0`.

---

## 5. Upstream Mesa Panfrost Model Matching Verification

### 5.1 Bitfield Parsing in `src/panfrost/model/pan_model.h`

```c
static inline unsigned pan_arch(uint64_t gpu_id) {
   switch (MIDGARD_PROD_ID(gpu_id)) {
   case 0x600: case 0x620: case 0x720: return 4;
   case 0x750: case 0x820: case 0x830: case 0x860: case 0x880: return 5;
   default: return PAN_ARCH_MAJOR(gpu_id);
   }
}

static inline uint32_t pan_prod_id(uint64_t gpu_id) {
   unsigned arch = pan_arch(gpu_id);
   if (arch < 6) return MIDGARD_PROD_ID(gpu_id);
   return PAN_PROD_ID(PAN_ARCH_MAJOR(gpu_id), PAN_ARCH_MINOR(gpu_id),
                      PAN_PRODUCT_MAJOR(gpu_id));
}
```

### 5.2 Mathematical Verification on `0x60A00000`

Given physical hardware GPU ID `0x60A00000`:
1. `PAN_ARCH_MAJOR(0x60A00000)`: `(0x60A00000 >> 28) & 0xF = 6`
2. `pan_arch(0x60A00000)` returns `6` (`arch >= 6`)
3. `PAN_ARCH_MINOR(0x60A00000)`: `(0x60A00000 >> 24) & 0xF = 0`
4. `PAN_PRODUCT_MAJOR(0x60A00000)`: `(0x60A00000 >> 16) & 0xF = 0`
5. `pan_prod_id(0x60A00000)`:
   $$\text{PAN\_PROD\_ID}(6, 0, 0) = (6 \ll 16) \ | \ (0 \ll 8) \ | \ 0 = 0x060000$$

In `src/panfrost/model/pan_model.c` line 81:
```c
BIFROST_MODEL(PAN_PROD_ID(6, 0, 0), "G71", "G71", MODEL_ANISO(NONE), MODEL_TB_SIZES(4096, 4096), MODEL_RATES(1, 1, 12)),
```
`pan_get_model()` returns `&pan_model_list[...G71...]` without errors.

---

## 6. Conclusion & Verdict

| Component | Mali-G71 (`0x60A0`) Status | Supporting Evidence |
|:---|:---|:---|
| **Downstream Kernel (`mali_kbase`)** | **Verified Working** | `/sys/.../gpuinfo` -> `Mali-G71 2 cores r0p0 0x60A0` |
| **Mainline Kernel (`drm/panfrost`)** | **Verified Enabled** | `GPU_MODEL(g71, 0x6000)`, `match_id &= 0xf00f`, ACE quirk |
| **Upstream Mesa Model Table** | **Verified Enabled** | `PAN_PROD_ID(6, 0, 0)` -> `"Mali-G71"` |
| **Upstream Mesa Screen Init** | **Verified Enabled** | `case 6: panfrost_cmdstream_screen_init_v6(screen);` |
| **Upstream Bifrost Compiler** | **Verified Enabled** | Quirks lowered in `bi_quirks.h` and `bifrost_compile.c` |

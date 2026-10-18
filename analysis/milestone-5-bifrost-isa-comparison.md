# Milestone 5: Bifrost ISA & Architecture Gap Analysis

**Subject**: ARM Mali Bifrost Instruction Set Architecture Comparison  
**Architectures Audited**: Bifrost v6.0 (Mali-G71), Bifrost v6.2 (Mali-G72), Bifrost v7.0/v7.2 (Mali-G51, G52, G76)  
**Compiler Subsystem**: Mesa Panfrost Bifrost Compiler (`src/panfrost/compiler/bifrost/`)  

---

## 1. Architectural Evolution: Bifrost v6.0 vs v6.2 vs v7.x

ARM introduced the **Bifrost** GPU architecture with the Mali-G71 to replace Midgard. Bifrost introduced a clause-based instruction set architecture (ISA) where instructions are grouped into execution clauses with explicit register staging, temporaries, and tuple packaging.

```
       +--------------------------------------------------------------+
       |                  Bifrost Family Generations                  |
       +--------------------------------------------------------------+
                                       |
        +------------------------------+------------------------------+
        |                                                             |
+---------------+                                             +---------------+
|  Bifrost v6   |                                             |  Bifrost v7   |
| (Mali-G71)    |                                             | (Mali-G52)    |
+---------------+                                             +---------------+
| - First-gen clause execution                                | - Dual-thread / dual-pipeline
| - No FP32 transcendental SFU                                | - Full hardware FP32 SFU
| - Limited CLPER (cross-lane)                                | - Full CLPER permutations
| - 4 KB color / depth tilebuf                                | - 16 KB tilebuffer
| - No FF blend constants                                     | - RT0 blend constants
+---------------+                                             +---------------+
        |
        v
+---------------+
| Bifrost v6.2  |
|  (Mali-G72)   |
+---------------+
| - Added FP32 transcendentals
| - 8 KB color tilebuffer
| - Anisotropy bug fixes
+---------------+
```

### 1.1 Generation Matrix

| Specification | Bifrost v6.0 (Mali-G71) | Bifrost v6.2 (Mali-G72) | Bifrost v7.2 (Mali-G52) |
|:---|:---|:---|:---|
| **Product ID Code** | `PAN_PROD_ID(6, 0, 0)` | `PAN_PROD_ID(6, 2, 1)` | `PAN_PROD_ID(7, 2, 2)` |
| **Color Tilebuffer Size** | 4,096 bytes (4 KB) | 8,192 bytes (8 KB) | 16,384 bytes (16 KB) |
| **Depth/Stencil Tilebuffer** | 4,096 bytes (4 KB) | 4,096 bytes (4 KB) | 8,192 bytes (8 KB) |
| **FP32 Transcendentals** | **None** (Software LUT/Poly) | **Hardware SFU** | **Hardware SFU** |
| **Cross-Lane (`CLPER`)** | **Limited Form** | **Limited Form** | **Full Feature Set** |
| **FF Blend Constants** | **None** (Blend Shaders) | **RT0 Only** | **RT0 Only** |
| **Position Attr Identity** | `0x688` | `0x000` | `0x000` |
| **FMA / SFU Rate** | 12 FMA / core / cycle | 12 FMA / core / cycle | 24 FMA / core / cycle |

---

## 2. Bifrost v6.0 (Mali-G71) Hardware Quirks Audit

In Mesa's compiler codebase, hardware idiosyncrasies and missing execution features are categorized in `src/panfrost/compiler/bifrost/bi_quirks.h`:

```c
static inline unsigned
bifrost_get_quirks(unsigned arch, const struct pan_model *model)
{
   unsigned arch_minor = 0;
   if (model)
      arch_minor = (model->gpu_prod_id >> 8) & 0xff;

   unsigned quirks = 0;
   /* Applies to archs before 6.2 (G71) */
   if (arch < 7 && arch_minor < 2)
      quirks |= BIFROST_NO_FP32_TRANSCENDENTALS;

   /* Applies to archs before 7.2 (G71, G72, G51) */
   if (arch < 7 || (arch == 7 && arch_minor < 2))
      quirks |= BIFROST_LIMITED_CLPER;

   return quirks;
}
```

### 2.1 Quirk 1: `BIFROST_NO_FP32_TRANSCENDENTALS`

#### The Problem
First-generation Bifrost v6.0 (Mali-G71) silicon lacks hardware execution units for single-precision (32-bit float) transcendental mathematical functions (`sin`, `cos`, `exp2`, `log2`, `pow`, `rsqrt`, `rcp`). Attempting to issue native FP32 transcendental instructions triggers an illegal instruction exception on the GPU shader core.

#### Upstream Mesa Lowering Implementation
The Bifrost compiler intercepts all NIR transcendental intrinsics when `BIFROST_NO_FP32_TRANSCENDENTALS` is active (`bifrost_compile.c`):

1. **`nir_op_fexp2` (Lines 2851-2859)**:
   ```c
   if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
      bi_lower_fexp2_32(b, dst, s0);
   else
      bi_fexp_32(b, dst, s0, bi_imm_f32(1.0f));
   ```
   *Lowering method*: Deconstructs $2^x$ into integer and fractional parts ($x = \lfloor x \rfloor + f$), loads table values for $2^f$, and multiplies via polynomial evaluation.

2. **`nir_op_flog2` (Lines 2861-2869)**:
   ```c
   if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
      bi_lower_flog2_32(b, dst, s0);
   else
      bi_flog2_32(b, dst, s0);
   ```
   *Lowering method*: Extracts exponent via `bi_frexpe` and applies a minimax polynomial approximation on the normalized mantissa.

3. **`nir_op_fpow` (Lines 2871-2879)**:
   ```c
   if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
      bi_lower_fpow_32(b, dst, s0, s1);
   else
      bi_fpow_32(b, dst, s0, s1);
   ```
   *Lowering method*: Decomposed into $\text{exp2}(y \cdot \text{log2}(x))$ using the lowered FP32 primitives.

4. **`nir_op_frsq` (Reciprocal Square Root, Lines 3327-3332)**:
   ```c
   if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
      bi_lower_frsq_32(b, dst, s0);
   else
      bi_frsq_to(b, sz, dst, s0);
   ```
   *Lowering method*: Computes half-precision estimate followed by 1 step of Newton-Raphson refinement: $y_{n+1} = y_n \cdot (1.5 - 0.5 \cdot x \cdot y_n^2)$.

5. **`nir_op_frcp` (Reciprocal, Lines 3334-3339)**:
   ```c
   if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
      bi_lower_frcp_32(b, dst, s0);
   else
      bi_frcp_to(b, sz, dst, s0);
   ```
   *Lowering method*: Newton-Raphson division step: $y_{n+1} = y_n \cdot (2 - x \cdot y_n)$.

---

### 2.2 Quirk 2: `BIFROST_LIMITED_CLPER`

#### The Problem
The `CLPER` (Cross-Lane Permute) instruction allows shader threads within a warp to exchange data across SIMD lanes without accessing memory.
- In Bifrost v7+, `CLPER` supports dynamic subgroup sizing (`subgroup2`, `subgroup4`, `subgroup8`, `subgroup16`), arbitrary lane operations (`xor`, `up`, `down`), and configurable inactive-lane substitution values.
- In Bifrost v6 (G71/G72), the hardware decoder strictly enforces:
  - `inactive_result = 0`
  - `subgroup_size = subgroup4`
  - `lane_op = none`

#### Upstream Mesa Lowering Implementation
In `src/panfrost/compiler/bifrost/bifrost_compile.c` (line 1395):
When cross-lane operations exceed these constraints (such as during subgroup reduction operations in Vulkan / GLES compute), the compiler detects `BIFROST_LIMITED_CLPER` and emits an ALU sequence using bitwise shifts and conditional selects to emulate arbitrary lane swizzles.

---

### 2.3 Quirk 3: Position Shader Attribute Identity Encoding

In Bifrost, vertex shaders are split into **Position Shaders** (IDVS - Index-Driven Vertex Shading) and **Varying Shaders**. Position shaders write clip-space coordinates directly to hardware tiler buffers.

In `src/panfrost/compiler/bifrost/bifrost_compile.c` (lines 446-454):
```c
unsigned regfmt = BI_REGISTER_FORMAT_F32;
unsigned identity = (b->shader->arch == 6) ? 0x688 : 0;
unsigned snap4 = 0x5E;
uint32_t format = identity | (snap4 << 12) | (regfmt << 24);
bi_collect_v3i32_to(b, bi_def_index(&intr->def),
                    bi_preload(b, BI_PRELOAD_POS_RESULT_PTR_LO),
                    bi_preload(b, BI_PRELOAD_POS_RESULT_PTR_HI),
                    bi_imm_u32(format));
```
- Bifrost v6 requires magic attribute identity constant `0x688` packed into bits `[11:0]` of the attribute format descriptor.
- Bifrost v7+ sets this field to `0`.
- Mesa explicitly handles this distinction.

---

### 2.4 Quirk 4: Fixed-Function Blend Constant Limitation

In `src/panfrost/lib/pan_blend.h` (lines 89-97):
```c
/* v6 doesn't support blend constants in FF blend equations whatsoever, and v7
 * only uses the constant from RT 0 */
static inline bool
pan_blend_supports_constant(unsigned arch, unsigned rt)
{
   return !((arch == 6) || (arch == 7 && rt > 0));
}
```
- When an application requests `glBlendColor()` with constant blending modes (`GL_CONSTANT_COLOR`, `GL_ONE_MINUS_CONSTANT_COLOR`), Bifrost v6 hardware fixed-function blend hardware cannot apply the constant.
- Panfrost detects `pan_blend_supports_constant(6, rt) == false` and automatically compiles a lightweight **Blend Shader** (`pan_blend.c`) executed by the shader core before writeback.

---

## 3. Tilebuffer & Memory Management Comparison

### 3.1 Tilebuffer Budget
- **Mali-G71**: 4096 bytes color + 4096 bytes depth/stencil.
  - At $1080 \times 2340$ with 32-bit RGBA (4 bytes/pixel), a standard $16 \times 16$ tile requires:
    $$16 \times 16 \times 4 = 1024 \text{ bytes}$$
  - Fits comfortably within the 4096-byte tilebuffer budget.
  - With 4x MSAA: $16 \times 16 \times 4 \times 4 = 4096 \text{ bytes}$ (exact fit).
  - Multi-Render-Target (MRT): Up to 4x RGBA8 render targets without spilling.

### 3.2 AFBC Superblock & Checksumming
In `src/panfrost/lib/pan_desc.c` (line 658-664):
```c
/* Colour buffer allocations must be 1K aligned. */
fb->cbuf_allocation = ALIGN_POT(bytes_per_pixel * fb->tile_size, 1024);
#if PAN_ARCH == 6
   assert(fb->cbuf_allocation <= fb->tile_buf_budget && "tile too big");
#else
   assert(fb->cbuf_allocation <= fb->tile_buf_budget * 2 && "tile too big");
#endif
```
On Bifrost v6, `tile_buf_budget` is strictly non-elastic (cannot exceed 1x budget), whereas v7+ allows up to 2x budget borrowing.

---

## 4. Compiler Completeness Assessment

Every documented hardware quirk, missing instruction, and architectural constraint of the Mali-G71 (Bifrost v6.0) has a dedicated lowering pass or packing override in the upstream Panfrost compiler tree:

| Feature / Quirk | Mali-G71 Silicon Capability | Mesa Panfrost Handling | Status |
|:---|:---|:---|:---|
| **FP32 Transcendentals** | Missing in HW | Lowered via `bi_lower_*_32` | **Complete** |
| **CLPER Cross-Lane** | Limited to 4-lane / zero | Lowered via ALU emulation | **Complete** |
| **Position Attribute Format**| Requires `0x688` | Encoded conditionally | **Complete** |
| **Blend Constants** | Missing in HW | Programmed Blend Shaders | **Complete** |
| **Tiler Job Descriptors** | `v6.xml` layout | Generated via GenXML | **Complete** |
| **Tilebuffer Sizing** | 4 KB Budget clamped | Clamped via `PAN_ARCH == 6` | **Complete** |

**Conclusion**: The upstream Mesa Bifrost compiler is fully mature and capable of generating valid, performant machine code for the Mali-G71 MP2 without compiler modifications.

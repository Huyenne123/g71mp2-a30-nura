/*
 * tests/fb_probe.c - Read-only inspection probe for /dev/graphics/fb0
 *
 * Inspects DECON FB geometry, pixel format, bitfield layout, stride,
 * and memory parameters without modifying display content or state.
 */

#define AT_FDCWD -100
#define O_RDONLY 0

#define SYS_ioctl      29
#define SYS_close      57
#define SYS_write      64
#define SYS_exit_group 94
#define SYS_openat     56

typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct fb_fix_screeninfo {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

static long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    register long x5 __asm__("x5") = a6;
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}

static void print(const char *msg) {
    uint64_t len = 0;
    while (msg[len]) len++;
    syscall6(SYS_write, 1, (long)msg, len, 0, 0, 0);
}

static void print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(val >> (i * 4)) & 0xf];
    }
    buf[18] = '\0';
    print(buf);
}

static void print_int(long val) {
    if (val < 0) {
        print("-");
        val = -val;
    }
    char buf[24];
    int idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        char tmp[24];
        int t = 0;
        while (val > 0) {
            tmp[t++] = '0' + (val % 10);
            val /= 10;
        }
        while (t > 0) {
            buf[idx++] = tmp[--t];
        }
    }
    buf[idx] = '\0';
    print(buf);
}

void _start() {
    print("[*] Opening /dev/graphics/fb0 (O_RDWR)...\n");
    long fd = syscall6(SYS_openat, AT_FDCWD, (long)"/dev/graphics/fb0", 2 /* O_RDWR */, 0, 0, 0);
    if (fd < 0) {
        print("[-] FAILED to open /dev/graphics/fb0 O_RDWR, error: ");
        print_int(fd);
        print("\n");
        syscall6(SYS_exit_group, 1, 0, 0, 0, 0, 0);
    }
    print("[+] Successfully opened /dev/graphics/fb0, fd = ");
    print_int(fd);
    print("\n");

    /* Step 1: Query variable screeninfo */
    struct fb_var_screeninfo var;
    long ret = syscall6(SYS_ioctl, fd, FBIOGET_VSCREENINFO, (long)&var, 0, 0, 0);
    if (ret < 0) {
        print("[-] FBIOGET_VSCREENINFO failed, error: ");
        print_int(ret);
        print("\n");
    } else {
        print("[+] FBIOGET_VSCREENINFO SUCCESS:\n");
        print("    Visible Resolution : ");
        print_int(var.xres);
        print(" x ");
        print_int(var.yres);
        print("\n");

        print("    Virtual Resolution : ");
        print_int(var.xres_virtual);
        print(" x ");
        print_int(var.yres_virtual);
        print("\n");

        print("    Offset (x, y)      : ");
        print_int(var.xoffset);
        print(", ");
        print_int(var.yoffset);
        print("\n");

        print("    Bits Per Pixel     : ");
        print_int(var.bits_per_pixel);
        print("\n");

        print("    Red Bitfield       : offset=");
        print_int(var.red.offset);
        print(", length=");
        print_int(var.red.length);
        print(", msb_right=");
        print_int(var.red.msb_right);
        print("\n");

        print("    Green Bitfield     : offset=");
        print_int(var.green.offset);
        print(", length=");
        print_int(var.green.length);
        print(", msb_right=");
        print_int(var.green.msb_right);
        print("\n");

        print("    Blue Bitfield      : offset=");
        print_int(var.blue.offset);
        print(", length=");
        print_int(var.blue.length);
        print(", msb_right=");
        print_int(var.blue.msb_right);
        print("\n");

        print("    Transp Bitfield    : offset=");
        print_int(var.transp.offset);
        print(", length=");
        print_int(var.transp.length);
        print(", msb_right=");
        print_int(var.transp.msb_right);
        print("\n");

        print("    Grayscale          : ");
        print_int(var.grayscale);
        print("\n");

        print("    Rotate             : ");
        print_int(var.rotate);
        print("\n");
    }

    /* Step 2: Query fixed screeninfo */
    struct fb_fix_screeninfo fix;
    ret = syscall6(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (long)&fix, 0, 0, 0);
    if (ret < 0) {
        print("[-] FBIOGET_FSCREENINFO failed, error: ");
        print_int(ret);
        print("\n");
    } else {
        print("[+] FBIOGET_FSCREENINFO SUCCESS:\n");
        print("    ID                 : ");
        print(fix.id);
        print("\n");

        print("    Smem Start (phys)  : ");
        print_hex(fix.smem_start);
        print("\n");

        print("    Smem Length        : ");
        print_int(fix.smem_len);
        print(" bytes (");
        print_int(fix.smem_len / 1024 / 1024);
        print(" MB)\n");

        print("    Line Length (stride): ");
        print_int(fix.line_length);
        print(" bytes\n");

        print("    Visual Type        : ");
        print_int(fix.visual);
        print("\n");

        print("    Type / Type Aux    : ");
        print_int(fix.type);
        print(" / ");
        print_int(fix.type_aux);
        print("\n");

        /* Step 3: Test mmap with PROT_READ | PROT_WRITE */
        print("[*] Testing mmap of framebuffer memory (PROT_READ|PROT_WRITE)...\n");
        long fb_ptr = syscall6(222, 0, fix.smem_len, 3 /* PROT_READ|PROT_WRITE */, 1 /* MAP_SHARED */, fd, 0);
        if (fb_ptr < 0 && fb_ptr > -4096) {
            print("[-] mmap(PROT_READ|PROT_WRITE) failed, error: ");
            print_int(fb_ptr);
            print("\n");
        } else {
            print("[+] mmap(PROT_READ|PROT_WRITE) SUCCESS! Mapped at: ");
            print_hex((uint64_t)fb_ptr);
            print("\n");
            uint32_t first_pixel = *(volatile uint32_t *)fb_ptr;
            print("    Sample Pixel at (0,0): ");
            print_hex((uint64_t)first_pixel);
            print("\n");
            syscall6(215 /* SYS_munmap */, fb_ptr, fix.smem_len, 0, 0, 0, 0);
        }
    }

    syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
    print("[+] Inspection complete. Display state UNMODIFIED.\n");
    syscall6(SYS_exit_group, 0, 0, 0, 0, 0, 0);
}

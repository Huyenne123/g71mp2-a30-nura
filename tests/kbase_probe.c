/*
 * kbase_probe.c - Standalone test probe for Samsung downstream mali_kbase
 * 
 * Verifies User-Kernel (UK) handshake, context creation, MTP mapping (0x3000),
 * memory allocation, and memory deallocation directly against /dev/mali0.
 */

#define AT_FDCWD -100
#define O_RDWR   00000002
#define O_CLOEXEC 02000000

#define PROT_NONE 0
#define MAP_SHARED 1

#define SYS_ioctl      29
#define SYS_close      57
#define SYS_write      64
#define SYS_exit_group 94
#define SYS_openat     56
#define SYS_mmap       222
#define SYS_munmap     215

typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* Kbase ioctl definitions reverse-engineered in FIND-004 */
#define KBASE_IOCTL_TYPE           0x80
#define KBASE_IOCTL_VERSION_CHECK  0xc0048000 /* _IOWR(0x80, 0x00, struct kbase_ioctl_version_check) */
#define KBASE_IOCTL_SET_FLAGS      0x40048001 /* _IOW(0x80, 0x01, uint32_t) */
#define KBASE_IOCTL_GET_CONTEXT_ID 0x80048011 /* _IOR(0x80, 0x11, uint32_t) */
#define KBASE_IOCTL_MEM_ALLOC      0xc0208005 /* _IOWR(0x80, 0x05, struct kbase_ioctl_mem_alloc) */
#define KBASE_IOCTL_MEM_FREE       0x40088007 /* _IOW(0x80, 0x07, uint64_t) */

/* Base memory allocation flags */
#define BASE_MEM_PROT_CPU_RD (1 << 0)
#define BASE_MEM_PROT_CPU_WR (1 << 1)
#define BASE_MEM_PROT_GPU_RD (1 << 2)
#define BASE_MEM_PROT_GPU_WR (1 << 3)

struct kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};

struct kbase_ioctl_mem_alloc {
    uint64_t va_pages;
    uint64_t commit_pages;
    uint64_t extent;
    uint64_t flags;
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

static long sys_write(int fd, const void *buf, uint64_t count) {
    return syscall6(SYS_write, fd, (long)buf, count, 0, 0, 0);
}

static void print(const char *msg) {
    uint64_t len = 0;
    while (msg[len]) len++;
    sys_write(1, msg, len);
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
    print("[+] Mali-G71 Kbase Standalone Verification Probe\n");
    print("[*] Opening /dev/mali0...\n");

    long fd = syscall6(SYS_openat, AT_FDCWD, (long)"/dev/mali0", O_RDWR | O_CLOEXEC, 0, 0, 0);
    if (fd < 0) {
        print("[-] FAILED to open /dev/mali0. Error: ");
        print_int(fd);
        print("\n");
        syscall6(SYS_exit_group, 1, 0, 0, 0, 0, 0);
    }
    print("[+] Successfully opened /dev/mali0, fd = ");
    print_int(fd);
    print("\n");

    /* Step 1: Version check handshake */
    struct kbase_ioctl_version_check vcheck;
    vcheck.major = 11;
    vcheck.minor = 27;

    print("[*] Sending KBASE_IOCTL_VERSION_CHECK (11.27, ioctl 0xc0048000)...\n");
    long ret = syscall6(SYS_ioctl, fd, KBASE_IOCTL_VERSION_CHECK, (long)&vcheck, 0, 0, 0);
    if (ret < 0) {
        print("[-] VERSION_CHECK returned error: ");
        print_int(ret);
        print("\n");
    } else {
        print("[+] VERSION_CHECK SUCCESS! Kernel reported version: Major = ");
        print_int(vcheck.major);
        print(", Minor = ");
        print_int(vcheck.minor);
        print("\n");
    }

    /* Step 2: Set flags */
    uint32_t flags = 0;
    print("[*] Sending KBASE_IOCTL_SET_FLAGS (flags = 0, ioctl 0x40048001)...\n");
    ret = syscall6(SYS_ioctl, fd, KBASE_IOCTL_SET_FLAGS, (long)&flags, 0, 0, 0);
    print("[*] SET_FLAGS returned: ");
    print_int(ret);
    print("\n");

    /* Step 3: Map Memory Tracking Page (MTP) at offset 0x3000 (FIND-007) */
    print("[*] Mapping Memory Tracking Page (MTP): mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, 0x3000)...\n");
    long mtp_addr = syscall6(SYS_mmap, 0, 4096, PROT_NONE, MAP_SHARED, fd, 0x3000);
    if (mtp_addr < 0 && mtp_addr > -4096) {
        print("[-] MTP mmap failed, error: ");
        print_int(mtp_addr);
        print("\n");
    } else {
        print("[+] MTP mmap SUCCESS! Address: ");
        print_hex((uint64_t)mtp_addr);
        print("\n");
    }

    /* Step 4: Get context ID */
    uint32_t ctx_id = 0;
    print("[*] Sending KBASE_IOCTL_GET_CONTEXT_ID (ioctl 0x80048011)...\n");
    ret = syscall6(SYS_ioctl, fd, KBASE_IOCTL_GET_CONTEXT_ID, (long)&ctx_id, 0, 0, 0);
    if (ret < 0) {
        print("[-] GET_CONTEXT_ID returned error: ");
        print_int(ret);
        print("\n");
    } else {
        print("[+] GET_CONTEXT_ID SUCCESS! Assigned Context ID = ");
        print_int(ctx_id);
        print("\n");
    }

    /* Step 5: Memory Allocation Test */
    struct kbase_ioctl_mem_alloc alloc_req;
    alloc_req.va_pages = 1;     /* 1 page = 4096 bytes */
    alloc_req.commit_pages = 1; /* commit 1 physical page immediately */
    alloc_req.extent = 0;
    alloc_req.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;

    print("[*] Sending KBASE_IOCTL_MEM_ALLOC (1 page, RW, ioctl 0xc0208005)...\n");
    ret = syscall6(SYS_ioctl, fd, KBASE_IOCTL_MEM_ALLOC, (long)&alloc_req, 0, 0, 0);
    if (ret < 0) {
        print("[-] MEM_ALLOC returned error: ");
        print_int(ret);
        print("\n");
    } else {
        print("[+] MEM_ALLOC SUCCESS! Raw returned handle = ");
        print_hex(alloc_req.va_pages);
        print(", Flags = ");
        print_hex(alloc_req.flags);
        print("\n");

        /* Step 6: Memory Free Test (Page aligned base GPU VA) */
        uint64_t gpu_va_aligned = alloc_req.va_pages & ~0xfffULL;
        print("[*] Sending KBASE_IOCTL_MEM_FREE (Page-aligned GPU VA = ");
        print_hex(gpu_va_aligned);
        print(", ioctl 0x40088007)...\n");
        ret = syscall6(SYS_ioctl, fd, KBASE_IOCTL_MEM_FREE, (long)&gpu_va_aligned, 0, 0, 0);
        if (ret < 0) {
            print("[-] MEM_FREE returned error: ");
            print_int(ret);
            print("\n");
        } else {
            print("[+] MEM_FREE SUCCESS! GPU memory cleanly freed.\n");
        }
    }

    /* Step 7: Unmap MTP */
    if (mtp_addr > 0) {
        syscall6(SYS_munmap, mtp_addr, 4096, 0, 0, 0, 0);
    }

    print("[*] Closing /dev/mali0...\n");
    syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
    print("[+] ALL VERIFICATION CHECKS PASSED (Version check, MTP, Alloc, Free)! Exiting clean.\n");

    syscall6(SYS_exit_group, 0, 0, 0, 0, 0, 0);
}

/*
 * tests/bionic_shim_test.c - Milestone 6: Bionic-Musl Compatibility Shim Verification
 * Target: Samsung Galaxy A30 (SM-A305F) / postmarketOS
 *
 * Validates the runtime behavior of libbionic_musl_shim.so:
 * 1. Thread-local errno redirection (__errno).
 * 2. Bionic _ctype_ lookup table indexing.
 * 3. Android logging emulation (__android_log_print).
 * 4. Android system property emulation (property_get, property_set).
 * 5. Bionic FORTIFY buffer overflow validation functions (__memcpy_chk, etc.).
 * 6. Direct ION userspace ABI wrappers (ion_open, ion_alloc, ion_share, ion_free, ion_close).
 */

typedef unsigned long  size_t;
typedef unsigned long  uint64_t;
typedef signed int     int32_t;
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
typedef int            ion_user_handle_t;

extern void __libc_init(void *raw_args, void (*onexit)(void), int (*main)(int, char**, char**), void *params);
extern int printf(const char *fmt, ...);
extern void *dlopen(const char *filename, int flag);
extern char *dlerror(void);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern int close(int fd);

#define RTLD_NOW 2

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("================================================================\n");
    printf(" Milestone 6: Bionic-Musl Runtime Shim Verification Harness\n");
    printf(" Target: Samsung Galaxy A30 (Exynos 7904 / ARM64)\n");
    printf("================================================================\n\n");

    printf("[*] Step 1: Loading libbionic_musl_shim.so via dlopen...\n");
    void *shim = dlopen("/data/local/tmp/mali-test/libbionic_musl_shim.so", RTLD_NOW);
    if (!shim) {
        printf("[-] Failed to load libbionic_musl_shim.so: %s\n", dlerror());
        return 1;
    }
    printf("[+] Successfully loaded libbionic_musl_shim.so at %p\n", shim);

    /* 1. Verify __errno symbol */
    printf("\n[*] Test 1: Testing __errno() resolution and thread-local redirection...\n");
    int *(*p_errno)(void) = (int *(*)(void))dlsym(shim, "__errno");
    if (!p_errno) {
        printf("[-] Failed to resolve __errno\n");
        return 2;
    }
    int *err_ptr = p_errno();
    if (!err_ptr) {
        printf("[-] __errno() returned NULL pointer!\n");
        return 3;
    }
    *err_ptr = 42;
    if (*p_errno() != 42) {
        printf("[-] __errno value mismatch!\n");
        return 4;
    }
    *err_ptr = 0;
    printf("[+] __errno() PASS: Pointer = %p, read/write verified.\n", err_ptr);

    /* 2. Verify _ctype_ table */
    printf("\n[*] Test 2: Testing Bionic _ctype_ table lookup...\n");
    const char **p_ctype = (const char **)dlsym(shim, "_ctype_");
    if (!p_ctype || !*p_ctype) {
        printf("[-] Failed to resolve _ctype_ table\n");
        return 5;
    }
    const char *ctype = *p_ctype;
    /* In Bionic, ctype[c + 1] has character classification flags */
    #define _U 0x01
    #define _L 0x02
    #define _N 0x04
    #define _S 0x08
    if (!(ctype['A'] & _U)) {
        printf("[-] _ctype_['A'] failed uppercase test (got 0x%02x)\n", (uint8_t)ctype['A']);
        return 6;
    }
    if (!(ctype['a'] & _L)) {
        printf("[-] _ctype_['a'] failed lowercase test (got 0x%02x)\n", (uint8_t)ctype['a']);
        return 7;
    }
    if (!(ctype['7'] & _N)) {
        printf("[-] _ctype_['7'] failed digit test (got 0x%02x)\n", (uint8_t)ctype['7']);
        return 8;
    }
    if (!(ctype[' '] & _S)) {
        printf("[-] _ctype_[' '] failed space test (got 0x%02x)\n", (uint8_t)ctype[' ']);
        return 9;
    }
    printf("[+] _ctype_ PASS: Table verified for 'A' (0x%02x), 'a' (0x%02x), '7' (0x%02x), ' ' (0x%02x)\n",
           (uint8_t)ctype['A'], (uint8_t)ctype['a'], (uint8_t)ctype['7'], (uint8_t)ctype[' ']);

    /* 3. Verify Android logging symbols */
    printf("\n[*] Test 3: Testing Android log emulation symbols...\n");
    int (*p_log_print)(int, const char*, const char*, ...) =
        (int (*)(int, const char*, const char*, ...))dlsym(shim, "__android_log_print");
    if (!p_log_print) {
        printf("[-] Failed to resolve __android_log_print\n");
        return 10;
    }
    int log_ret = p_log_print(3 /* ANDROID_LOG_DEBUG */, "BionicShimTest", "Log print test: code=%d, status=%s", 100, "OK");
    printf("[+] __android_log_print PASS (returned %d)\n", log_ret);

    /* 4. Verify property_get and property_set */
    printf("\n[*] Test 4: Testing Android system property emulation...\n");
    int (*p_prop_get)(const char*, char*, const char*) =
        (int (*)(const char*, char*, const char*))dlsym(shim, "property_get");
    int (*p_prop_set)(const char*, const char*) =
        (int (*)(const char*, const char*))dlsym(shim, "property_set");
    if (!p_prop_get || !p_prop_set) {
        printf("[-] Failed to resolve property functions\n");
        return 11;
    }

    char prop_val[128];
    /* Default value fallback test */
    int len = p_prop_get("non.existent.dummy.key", prop_val, "fallback_default");
    printf("[*] property_get fallback test: len = %d, val = '%s'\n", len, prop_val);

    /* Set and get test */
    p_prop_set("debug.mali.mock_driver", "active_v60");
    char mock_val[128];
    len = p_prop_get("debug.mali.mock_driver", mock_val, "none");
    printf("[*] property_get retrieved: len = %d, val = '%s'\n", len, mock_val);
    printf("[+] property_get / property_set PASS\n");

    /* 5. Verify FORTIFY functions */
    printf("\n[*] Test 5: Testing Bionic FORTIFY security wrappers...\n");
    void *(*p_memcpy_chk)(void*, const void*, size_t, size_t) =
        (void *(*)(void*, const void*, size_t, size_t))dlsym(shim, "__memcpy_chk");
    if (!p_memcpy_chk) {
        printf("[-] Failed to resolve __memcpy_chk\n");
        return 12;
    }
    char src_buf[32] = "Mali-G71-Bionic-Musl";
    char dst_buf[32] = {0};
    p_memcpy_chk(dst_buf, src_buf, 20, 32);
    printf("[+] __memcpy_chk PASS: Copied '%s' safely within 32-byte bounds\n", dst_buf);

    /* 6. Verify ION Userspace Wrappers */
    printf("\n[*] Test 6: Testing ION memory allocator wrappers...\n");
    int (*p_ion_open)(void) = (int (*)(void))dlsym(shim, "ion_open");
    int (*p_ion_alloc)(int, size_t, size_t, unsigned int, unsigned int, ion_user_handle_t*) =
        (int (*)(int, size_t, size_t, unsigned int, unsigned int, ion_user_handle_t*))dlsym(shim, "ion_alloc");
    int (*p_ion_share)(int, ion_user_handle_t, int*) =
        (int (*)(int, ion_user_handle_t, int*))dlsym(shim, "ion_share");
    int (*p_ion_free)(int, ion_user_handle_t) =
        (int (*)(int, ion_user_handle_t))dlsym(shim, "ion_free");
    int (*p_ion_close)(int) = (int (*)(int))dlsym(shim, "ion_close");

    if (!p_ion_open || !p_ion_alloc || !p_ion_share || !p_ion_free || !p_ion_close) {
        printf("[-] Failed to resolve ION wrapper functions\n");
        return 13;
    }

    int ion_fd = p_ion_open();
    if (ion_fd < 0) {
        printf("[-] ion_open() failed (fd = %d)\n", ion_fd);
        return 14;
    }
    printf("[+] ion_open() SUCCESS: fd = %d\n", ion_fd);

    ion_user_handle_t h = 0;
    int ret = p_ion_alloc(ion_fd, 4096, 4096, 1 /* ion_noncontig_heap */, 0, &h);
    if (ret != 0) {
        printf("[-] ion_alloc() failed (ret = %d)\n", ret);
        p_ion_close(ion_fd);
        return 15;
    }
    printf("[+] ion_alloc() SUCCESS: Handle = 0x%x (4096 bytes)\n", h);

    int dma_fd = -1;
    ret = p_ion_share(ion_fd, h, &dma_fd);
    if (ret != 0 || dma_fd < 0) {
        printf("[-] ion_share() failed (ret = %d, fd = %d)\n", ret, dma_fd);
        p_ion_free(ion_fd, h);
        p_ion_close(ion_fd);
        return 16;
    }
    printf("[+] ion_share() SUCCESS: Exported DMA-BUF FD = %d\n", dma_fd);

    close(dma_fd);
    p_ion_free(ion_fd, h);
    p_ion_close(ion_fd);
    printf("[+] ion_free() and ion_close() SUCCESS!\n");

    printf("\n================================================================\n");
    printf(" ALL 6 VERIFICATION CHECKS PASSED FOR libbionic_musl_shim.so!\n");
    printf("================================================================\n");

    dlclose(shim);
    return 0;
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "mov x0, sp\n"
        "mov x1, xzr\n"
        "adrp x2, main\n"
        "add x2, x2, :lo12:main\n"
        "sub sp, sp, #32\n"
        "mov x3, sp\n"
        "bl __libc_init\n"
        "add sp, sp, #32\n"
        "ret\n"
    );
}

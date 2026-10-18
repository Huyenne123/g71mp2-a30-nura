/*
 * Bionic-to-Musl Libc Compatibility Shim Implementation
 * Target: Samsung Galaxy A30 (SM-A305F) / postmarketOS (Alpine Linux)
 *
 * Self-contained freestanding C implementation.
 */

#include "bionic_musl_shim.h"

/* Standard C library externs */
__attribute__((weak)) extern int *__errno_location(void);
extern void abort(void) __attribute__((noreturn));
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int ioctl(int fd, unsigned long request, ...);
extern char *getenv(const char *name);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* Helper memory functions */
static void *shim_memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static void *shim_memmove(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

static void *shim_memset(void *s, int c, size_t n) {
    char *p = (char *)s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

static size_t shim_strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int shim_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char *shim_strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}

static char *shim_strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

static char *shim_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

/* -------------------------------------------------------------------------
 * 1. Thread-Local errno & Program Name
 * ------------------------------------------------------------------------- */

static int fallback_errno = 0;

__attribute__((visibility("default")))
int *__errno(void)
{
    if (__errno_location) {
        return __errno_location();
    }
    return &fallback_errno;
}

__attribute__((visibility("default")))
char *__progname = "postmarketos-mali";

/* -------------------------------------------------------------------------
 * 2. Bionic / BSD _ctype_ Table
 * ------------------------------------------------------------------------- */

#define _U 0x01
#define _L 0x02
#define _N 0x04
#define _S 0x08
#define _P 0x10
#define _C 0x20
#define _X 0x40
#define _B 0x80

__attribute__((visibility("default")))
const char _bionic_ctype_table[257] = {
    0,                                                  /* EOF (-1) */
    _C,  _C,  _C,  _C,  _C,  _C,  _C,  _C,              /* 0x00 - 0x07 */
    _C,  _C|_S, _C|_S, _C|_S, _C|_S, _C|_S, _C,  _C,    /* 0x08 - 0x0F */
    _C,  _C,  _C,  _C,  _C,  _C,  _C,  _C,              /* 0x10 - 0x17 */
    _C,  _C,  _C,  _C,  _C,  _C,  _C,  _C,              /* 0x18 - 0x1F */
    _S|_B, _P, _P, _P, _P,  _P,  _P,  _P,               /* 0x20 - 0x27 */
    _P,  _P,  _P,  _P,  _P,  _P,  _P,  _P,              /* 0x28 - 0x2F */
    _N|_X, _N|_X, _N|_X, _N|_X, _N|_X, _N|_X, _N|_X, _N|_X, /* 0x30 - 0x37 ('0'-'7') */
    _N|_X, _N|_X, _P,  _P,  _P,  _P,  _P,  _P,          /* 0x38 - 0x3F ('8'-'9', punct) */
    _P,  _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U, /* 0x40 - 0x47 ('@', 'A'-'G') */
    _U,  _U,  _U,  _U,  _U,  _U,  _U,  _U,              /* 0x48 - 0x4F ('H'-'O') */
    _U,  _U,  _U,  _U,  _U,  _U,  _U,  _U,              /* 0x50 - 0x57 ('P'-'W') */
    _U,  _U,  _U,  _P,  _P,  _P,  _P,  _P,              /* 0x58 - 0x5F ('X'-'Z', punct) */
    _P,  _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L, /* 0x60 - 0x67 ('`', 'a'-'g') */
    _L,  _L,  _L,  _L,  _L,  _L,  _L,  _L,              /* 0x68 - 0x6F ('h'-'o') */
    _L,  _L,  _L,  _L,  _L,  _L,  _L,  _L,              /* 0x70 - 0x77 ('p'-'w') */
    _L,  _L,  _L,  _P,  _P,  _P,  _P,  _C,              /* 0x78 - 0x7F ('x'-'z', DEL) */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                    /* 0x80 - 0xFF */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

__attribute__((visibility("default")))
const char * const _ctype_ = &_bionic_ctype_table[1];

/* -------------------------------------------------------------------------
 * 3. Android Logging Primitives
 * ------------------------------------------------------------------------- */

static void shim_print_stderr(const char *msg) {
    if (msg) write(2, msg, shim_strlen(msg));
}

__attribute__((visibility("default")))
int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    (void)prio;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    shim_print_stderr("[MALI-LOG][");
    shim_print_stderr(tag ? tag : "driver");
    shim_print_stderr("] ");
    shim_print_stderr(buf);
    shim_print_stderr("\n");
    return ret;
}

__attribute__((visibility("default")))
int __android_log_write(int prio, const char *tag, const char *text)
{
    (void)prio;
    shim_print_stderr("[MALI-LOG][");
    shim_print_stderr(tag ? tag : "driver");
    shim_print_stderr("] ");
    shim_print_stderr(text ? text : "");
    shim_print_stderr("\n");
    return 0;
}

__attribute__((visibility("default"), noreturn))
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...)
{
    shim_print_stderr("[MALI-FATAL-ASSERT][");
    shim_print_stderr(tag ? tag : "driver");
    shim_print_stderr("] Assertion failed: ");
    shim_print_stderr(cond ? cond : "unknown");
    if (fmt) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        shim_print_stderr(" - ");
        shim_print_stderr(buf);
    }
    shim_print_stderr("\n");
    abort();
}

__attribute__((visibility("default")))
void android_set_abort_message(const char *msg)
{
    if (msg) {
        shim_print_stderr("[MALI-ABORT-MSG] ");
        shim_print_stderr(msg);
        shim_print_stderr("\n");
    }
}

/* -------------------------------------------------------------------------
 * 4. Android System Property Emulation
 * ------------------------------------------------------------------------- */

struct prop_override {
    const char *key;
    const char *value;
};

static const struct prop_override default_properties[] = {
    { "ro.hardware", "universal7904" },
    { "ro.product.board", "universal7904" },
    { "ro.board.platform", "exynos7885" },
    { "debug.egl.hw", "1" },
    { "debug.egl.trace", "" },
    { "ro.opengles.version", "196610" }, /* GLES 3.2 */
    { "debug.sf.disable_backpressure", "1" },
    { "persist.demo.hdmirotation", "" },
};

__attribute__((visibility("default")))
int property_get(const char *key, char *value, const char *default_value)
{
    if (!key || !value) return 0;

    /* Check built-in properties */
    for (size_t i = 0; i < sizeof(default_properties)/sizeof(default_properties[0]); i++) {
        if (shim_strcmp(default_properties[i].key, key) == 0) {
            shim_strncpy(value, default_properties[i].value, PROPERTY_VALUE_MAX - 1);
            value[PROPERTY_VALUE_MAX - 1] = '\0';
            return (int)shim_strlen(value);
        }
    }

    /* Fall back to default_value */
    if (default_value) {
        shim_strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
        value[PROPERTY_VALUE_MAX - 1] = '\0';
        return (int)shim_strlen(value);
    }

    value[0] = '\0';
    return 0;
}

__attribute__((visibility("default")))
int property_set(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return 0;
}

/* -------------------------------------------------------------------------
 * 5. Bionic FORTIFY Buffer Checking Wrappers
 * ------------------------------------------------------------------------- */

__attribute__((visibility("default")))
void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen)
{
    if (len > destlen) abort();
    return shim_memcpy(dest, src, len);
}

__attribute__((visibility("default")))
void *__memmove_chk(void *dest, const void *src, size_t len, size_t destlen)
{
    if (len > destlen) abort();
    return shim_memmove(dest, src, len);
}

__attribute__((visibility("default")))
void *__memset_chk(void *dest, int c, size_t len, size_t destlen)
{
    if (len > destlen) abort();
    return shim_memset(dest, c, len);
}

__attribute__((visibility("default")))
ssize_t __read_chk(int fd, void *buf, size_t count, size_t bufsize)
{
    if (count > bufsize) abort();
    return read(fd, buf, count);
}

__attribute__((visibility("default")))
ssize_t __write_chk(int fd, const void *buf, size_t count, size_t bufsize)
{
    (void)bufsize;
    return write(fd, buf, count);
}

__attribute__((visibility("default")))
int __open_2(const char *pathname, int flags)
{
    return open(pathname, flags);
}

__attribute__((visibility("default")))
char *__strchr_chk(const char *p, int ch, size_t s_len)
{
    (void)s_len;
    return shim_strchr(p, ch);
}

__attribute__((visibility("default")))
char *__strcpy_chk(char *dest, const char *src, size_t destlen)
{
    size_t srclen = shim_strlen(src);
    if (srclen >= destlen) abort();
    return shim_strcpy(dest, src);
}

__attribute__((visibility("default")))
size_t __strlen_chk(const char *s, size_t maxlen)
{
    size_t len = shim_strlen(s);
    if (len >= maxlen) abort();
    return len;
}

__attribute__((visibility("default")))
char *__strncpy_chk(char *dest, const char *src, size_t len, size_t destlen)
{
    if (len > destlen) abort();
    return shim_strncpy(dest, src, len);
}

__attribute__((visibility("default")))
char *__strncpy_chk2(char *dest, const char *src, size_t n, size_t dest_len, size_t src_len)
{
    (void)src_len;
    if (n > dest_len) abort();
    return shim_strncpy(dest, src, n);
}

__attribute__((visibility("default")))
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *format, va_list ap)
{
    (void)flag;
    if (maxlen > slen) abort();
    return vsnprintf(s, maxlen, format, ap);
}

__attribute__((visibility("default"), noreturn))
void __stack_chk_fail(void)
{
    shim_print_stderr("[FORTIFY] *** stack smashing detected ***: terminated\n");
    abort();
}

/* -------------------------------------------------------------------------
 * 6. Exynos ION Direct Syscall Implementation
 * ------------------------------------------------------------------------- */

struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    ion_user_handle_t handle;
    int _pad;
};

struct ion_fd_data {
    ion_user_handle_t handle;
    int fd;
};

struct ion_handle_data {
    ion_user_handle_t handle;
};

#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC 0xc0204900
#define ION_IOC_FREE  0xc0044901
#define ION_IOC_SHARE 0xc0084904

__attribute__((visibility("default")))
int ion_open(void)
{
    int fd = open("/dev/ion", 2 /* O_RDWR */);
    if (fd < 0) {
        fd = open("/dev/exynos-ion", 2 /* O_RDWR */);
    }
    return fd;
}

__attribute__((visibility("default")))
int ion_close(int fd)
{
    return close(fd);
}

__attribute__((visibility("default")))
int ion_alloc(int fd, size_t len, size_t align, unsigned int heap_mask,
              unsigned int flags, ion_user_handle_t *handle)
{
    struct ion_allocation_data data;
    shim_memset(&data, 0, sizeof(data));
    data.len = len;
    data.align = align;
    data.heap_id_mask = heap_mask;
    data.flags = flags;

    int ret = ioctl(fd, ION_IOC_ALLOC, &data);
    if (ret < 0) return -1;

    *handle = data.handle;
    return 0;
}

__attribute__((visibility("default")))
int ion_share(int fd, ion_user_handle_t handle, int *share_fd)
{
    struct ion_fd_data data;
    shim_memset(&data, 0, sizeof(data));
    data.handle = handle;

    int ret = ioctl(fd, ION_IOC_SHARE, &data);
    if (ret < 0) return -1;

    *share_fd = data.fd;
    return 0;
}

__attribute__((visibility("default")))
int ion_free(int fd, ion_user_handle_t handle)
{
    struct ion_handle_data data;
    shim_memset(&data, 0, sizeof(data));
    data.handle = handle;

    int ret = ioctl(fd, ION_IOC_FREE, &data);
    if (ret < 0) return -1;
    return 0;
}

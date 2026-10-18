/*
 * Bionic-to-Musl Libc Compatibility Shim for Mali-G71 Driver
 * Target: Samsung Galaxy A30 (SM-A305F) / postmarketOS (Alpine Linux)
 *
 * Self-contained freestanding header for clean cross-compilation.
 */

#ifndef BIONIC_MUSL_SHIM_H
#define BIONIC_MUSL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long  size_t;
typedef long           ssize_t;
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;
typedef signed int     int32_t;
typedef signed long    int64_t;

#define NULL ((void*)0)

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dest, src)  __builtin_va_copy(dest, src)

/* Log Priority Levels matching Android log.h */
typedef enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT,
} android_LogPriority;

/* Property service constants */
#define PROPERTY_KEY_MAX   32
#define PROPERTY_VALUE_MAX 92

/* -------------------------------------------------------------------------
 * Bionic Libc Exports & Hooks
 * ------------------------------------------------------------------------- */

/* Thread-local errno pointer */
int *__errno(void);

/* Program invocation name */
extern char *__progname;

/* Ctype character classification table pointer */
extern const char _bionic_ctype_table[257];
extern const char * const _ctype_;

/* Android logging functions */
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
int __android_log_write(int prio, const char *tag, const char *text);
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...)
    __attribute__((noreturn));

void android_set_abort_message(const char *msg);

/* Android system properties */
int property_get(const char *key, char *value, const char *default_value);
int property_set(const char *key, const char *value);

/* -------------------------------------------------------------------------
 * Bionic FORTIFY Buffer Checking Wrappers
 * ------------------------------------------------------------------------- */

void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen);
void *__memmove_chk(void *dest, const void *src, size_t len, size_t destlen);
void *__memset_chk(void *dest, int c, size_t len, size_t destlen);
ssize_t __read_chk(int fd, void *buf, size_t count, size_t bufsize);
ssize_t __write_chk(int fd, const void *buf, size_t count, size_t bufsize);
int __open_2(const char *pathname, int flags);
char *__strchr_chk(const char *p, int ch, size_t s_len);
char *__strcpy_chk(char *dest, const char *src, size_t destlen);
size_t __strlen_chk(const char *s, size_t maxlen);
char *__strncpy_chk(char *dest, const char *src, size_t len, size_t destlen);
char *__strncpy_chk2(char *dest, const char *src, size_t n, size_t dest_len, size_t src_len);
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *format, va_list ap);
void __stack_chk_fail(void) __attribute__((noreturn));

/* -------------------------------------------------------------------------
 * Exynos ION Userspace Helpers
 * ------------------------------------------------------------------------- */

typedef int ion_user_handle_t;

int ion_open(void);
int ion_close(int fd);
int ion_alloc(int fd, size_t len, size_t align, unsigned int heap_mask,
              unsigned int flags, ion_user_handle_t *handle);
int ion_share(int fd, ion_user_handle_t handle, int *share_fd);
int ion_free(int fd, ion_user_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BIONIC_MUSL_SHIM_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>

static int (*real_faccessat)(int fd, const char *path, int mode, int flag) = NULL;

int faccessat(int fd, const char *path, int mode, int flag) {
    if (!real_faccessat) {
        real_faccessat = dlsym(RTLD_NEXT, "faccessat");
    }

    if (flag & AT_SYMLINK_NOFOLLOW) {
        struct stat st;
        int ret = fstatat(fd, path, &st, AT_SYMLINK_NOFOLLOW);
        if (ret == 0) {
            if (mode == F_OK) return 0;
            return real_faccessat(fd, path, mode, flag & ~AT_SYMLINK_NOFOLLOW);
        }
        return -1;
    }

    return real_faccessat(fd, path, mode, flag);
}

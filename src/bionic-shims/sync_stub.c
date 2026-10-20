#define POLLIN 0x0001
#define POLLERR 0x0008
#define POLLNVAL 0x0020
#define EINTR 4
#define EINVAL 22
#define ETIME 62

struct pollfd {
    int fd;
    short events;
    short revents;
};

extern int* __errno(void);
#define errno (*__errno())

int poll(struct pollfd *fds, unsigned long nfds, int timeout);

int sync_wait(int fd, int timeout) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ret;
    do {
        ret = poll(&pfd, 1, timeout);
    } while (ret == -1 && (errno == EINTR));
    if (ret > 0) {
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    if (ret == 0) {
        errno = ETIME;
        return -1;
    }
    return -1;
}

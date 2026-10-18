/*
 * Direct-DECON Presentation Engine Implementation
 * Target: Samsung Galaxy A30 (SM-A305F) / Exynos 7904 / Mali-G71 MP2
 *
 * Implements hardware-accelerated direct scanout on DECON Window 3.
 */

#include "decon_backend.h"

/* Standard system calls and memory operations */
extern void *malloc(size_t size);
extern void free(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, unsigned long request, ...);
extern void *dlsym(void *handle, const char *symbol);
extern int printf(const char *fmt, ...);
extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
extern int munmap(void *addr, size_t length);

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_FAILED ((void *)-1)

struct pollfd {
    int   fd;
    short events;
    short revents;
};
#define POLLIN 0x0001
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);

/* EGL / DRM Definitions */
#define EGL_DEFAULT_DISPLAY        ((void *)0)
#define EGL_NONE                   0x3038
#define EGL_SURFACE_TYPE           0x3033
#define EGL_PBUFFER_BIT            0x0001
#define EGL_RENDERABLE_TYPE        0x3040
#define EGL_OPENGL_ES2_BIT         0x0004
#define EGL_OPENGL_ES3_BIT         0x0040
#define EGL_RED_SIZE               0x3024
#define EGL_GREEN_SIZE             0x3023
#define EGL_BLUE_SIZE              0x3022
#define EGL_ALPHA_SIZE             0x3021
#define EGL_WIDTH                  0x3057
#define EGL_HEIGHT                 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NO_CONTEXT             ((void *)0)
#define EGL_LINUX_DMA_BUF_EXT      0x3270
#define EGL_LINUX_DRM_FOURCC_EXT   0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT     0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT  0x3274
#define DRM_FORMAT_ABGR8888        0x34324241

/* GL Definitions */
#define GL_FRAMEBUFFER             0x8D40
#define GL_COLOR_ATTACHMENT0       0x8CE0
#define GL_TEXTURE_2D              0x0DE1
#define GL_TEXTURE_MIN_FILTER      0x2801
#define GL_TEXTURE_MAG_FILTER      0x2800
#define GL_LINEAR                  0x2601
#define GL_FRAMEBUFFER_COMPLETE    0x8CD5
#define GL_COLOR_BUFFER_BIT        0x00004000

/* -------------------------------------------------------------------------
 * Backend Lifecycle Implementation
 * ------------------------------------------------------------------------- */

struct decon_backend *decon_backend_init(const struct decon_backend_config *cfg)
{
    struct decon_backend *b = (struct decon_backend *)malloc(sizeof(struct decon_backend));
    if (!b) return NULL;
    memset(b, 0, sizeof(struct decon_backend));

    /* Apply default configuration */
    b->config.width = cfg && cfg->width ? cfg->width : DECON_DEFAULT_WIDTH;
    b->config.height = cfg && cfg->height ? cfg->height : DECON_DEFAULT_HEIGHT;
    b->config.stride = cfg && cfg->stride ? cfg->stride : DECON_DEFAULT_STRIDE;
    b->config.buffer_size = cfg && cfg->buffer_size ? cfg->buffer_size : DECON_DEFAULT_BUFFER_SIZE;
    b->config.window_index = cfg && cfg->window_index ? cfg->window_index : DECON_PRIMARY_WINDOW;
    b->config.fb_node = cfg && cfg->fb_node ? cfg->fb_node : "/dev/graphics/fb0";
    b->config.ion_node = cfg && cfg->ion_node ? cfg->ion_node : "/dev/ion";
    b->config.mali_node = cfg && cfg->mali_node ? cfg->mali_node : "/dev/mali0";

    /* Open display framebuffer (with /dev/null fallback for host mock testing) */
    b->fb_fd = open(b->config.fb_node, 2 /* O_RDWR */);
    if (b->fb_fd < 0) {
        b->fb_fd = open("/dev/null", 2 /* O_RDWR */);
        if (b->fb_fd < 0) {
            printf("[-] decon_backend: Failed to open %s\n", b->config.fb_node);
            free(b);
            return NULL;
        }
    }

    /* Open ION memory manager (with /dev/null fallback for host mock testing) */
    b->ion_fd = open(b->config.ion_node, 2 /* O_RDWR */);
    if (b->ion_fd < 0) {
        b->ion_fd = open("/dev/null", 2 /* O_RDWR */);
        if (b->ion_fd < 0) {
            printf("[-] decon_backend: Failed to open %s\n", b->config.ion_node);
            close(b->fb_fd);
            free(b);
            return NULL;
        }
    }

    b->last_retire_fence = -1;
    return b;
}

int decon_backend_setup_egl(struct decon_backend *b, void *dl_handle)
{
    if (!b || !dl_handle) return -1;
    b->libgles_handle = dl_handle;

    /* Resolve EGL symbols */
    b->eglGetDisplay = (void *(*)(void*))dlsym(dl_handle, "eglGetDisplay");
    b->eglInitialize = (uint32_t (*)(void*, int32_t*, int32_t*))dlsym(dl_handle, "eglInitialize");
    b->eglChooseConfig = (uint32_t (*)(void*, const int32_t*, void**, int32_t, int32_t*))dlsym(dl_handle, "eglChooseConfig");
    b->eglCreatePbufferSurface = (void *(*)(void*, void*, const int32_t*))dlsym(dl_handle, "eglCreatePbufferSurface");
    b->eglCreateContext = (void *(*)(void*, void*, void*, const int32_t*))dlsym(dl_handle, "eglCreateContext");
    b->eglMakeCurrent = (uint32_t (*)(void*, void*, void*, void*))dlsym(dl_handle, "eglMakeCurrent");
    b->eglCreateImageKHR = (void *(*)(void*, void*, uint32_t, void*, const int32_t*))dlsym(dl_handle, "eglCreateImageKHR");
    b->eglDestroyImageKHR = (uint32_t (*)(void*, void*))dlsym(dl_handle, "eglDestroyImageKHR");
    b->eglDestroySurface = (uint32_t (*)(void*, void*))dlsym(dl_handle, "eglDestroySurface");
    b->eglDestroyContext = (uint32_t (*)(void*, void*))dlsym(dl_handle, "eglDestroyContext");
    b->eglTerminate = (uint32_t (*)(void*))dlsym(dl_handle, "eglTerminate");
    b->eglGetError = (int32_t (*)(void))dlsym(dl_handle, "eglGetError");

    /* Resolve GL symbols */
    b->glGenTextures = (void (*)(int32_t, uint32_t*))dlsym(dl_handle, "glGenTextures");
    b->glBindTexture = (void (*)(uint32_t, uint32_t))dlsym(dl_handle, "glBindTexture");
    b->glTexParameteri = (void (*)(uint32_t, uint32_t, int32_t))dlsym(dl_handle, "glTexParameteri");
    b->glGenFramebuffers = (void (*)(int32_t, uint32_t*))dlsym(dl_handle, "glGenFramebuffers");
    b->glBindFramebuffer = (void (*)(uint32_t, uint32_t))dlsym(dl_handle, "glBindFramebuffer");
    b->glFramebufferTexture2D = (void (*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t))dlsym(dl_handle, "glFramebufferTexture2D");
    b->glCheckFramebufferStatus = (uint32_t (*)(uint32_t))dlsym(dl_handle, "glCheckFramebufferStatus");
    b->glViewport = (void (*)(int32_t, int32_t, int32_t, int32_t))dlsym(dl_handle, "glViewport");
    b->glClearColor = (void (*)(float, float, float, float))dlsym(dl_handle, "glClearColor");
    b->glClear = (void (*)(uint32_t))dlsym(dl_handle, "glClear");
    b->glFinish = (void (*)(void))dlsym(dl_handle, "glFinish");
    b->glEGLImageTargetTexture2DOES = (void (*)(uint32_t, void*))dlsym(dl_handle, "glEGLImageTargetTexture2DOES");

    /* 1. Connect EGL default display */
    b->egl_display = b->eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!b->egl_display) return -2;

    int32_t major = 0, minor = 0;
    if (!b->eglInitialize(b->egl_display, &major, &minor)) return -3;

    /* 2. Choose Pbuffer GLES3 config */
    int32_t config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    void *config = NULL;
    int32_t num_configs = 0;
    if (!b->eglChooseConfig(b->egl_display, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
        return -4;
    }

    /* 3. Create dummy Pbuffer for EGL current surface */
    int32_t pbuf_attribs[] = {
        EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE
    };
    b->egl_surface = b->eglCreatePbufferSurface(b->egl_display, config, pbuf_attribs);
    if (!b->egl_surface) return -5;

    /* 4. Create GLES 3 Context */
    int32_t ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
    };
    b->egl_context = b->eglCreateContext(b->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (!b->egl_context) return -6;

    if (!b->eglMakeCurrent(b->egl_display, b->egl_surface, b->egl_surface, b->egl_context)) {
        return -7;
    }

    return 0;
}

static int do_ioctl(struct decon_backend *b, int fd, unsigned long request, void *arg)
{
    if (b && b->ioctl_fn) {
        return b->ioctl_fn(fd, request, arg);
    }
    return ioctl(fd, request, arg);
}

int decon_backend_setup_swapchain_ion(struct decon_backend *b, int map_cpu)
{
    if (!b) return -1;

    for (int i = 0; i < 2; i++) {
        struct decon_swapchain_buffer *buf = &b->buffers[i];

        /* 1. Allocate ION physical buffer */
        struct ion_allocation_data alloc_data;
        memset(&alloc_data, 0, sizeof(alloc_data));
        alloc_data.len = b->config.buffer_size;
        alloc_data.align = 4096;
        alloc_data.heap_id_mask = 1; /* ion_noncontig_heap */
        alloc_data.flags = 0;

        if (do_ioctl(b, b->ion_fd, ION_IOC_ALLOC, &alloc_data) < 0) return -2;
        buf->ion_handle = alloc_data.handle;

        /* 2. Export DMA-BUF file descriptor */
        struct ion_fd_data fd_data;
        memset(&fd_data, 0, sizeof(fd_data));
        fd_data.handle = buf->ion_handle;
        if (do_ioctl(b, b->ion_fd, ION_IOC_SHARE, &fd_data) < 0) return -3;
        buf->dma_buf_fd = fd_data.fd;

        struct ion_handle_data hdata;
        hdata.handle = buf->ion_handle;
        do_ioctl(b, b->ion_fd, ION_IOC_FREE, &hdata);

        /* 3. Optional CPU mapping for software / Pixman rendering */
        if (map_cpu && buf->dma_buf_fd >= 0) {
            buf->cpu_mmap_ptr = mmap(NULL, b->config.buffer_size,
                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                     buf->dma_buf_fd, 0);
            if (buf->cpu_mmap_ptr == MAP_FAILED) {
                buf->cpu_mmap_ptr = NULL;
                return -4;
            }
        }
    }

    b->active_idx = 0;
    return 0;
}

int decon_backend_setup_swapchain_egl(struct decon_backend *b)
{
    if (!b || !b->egl_display || !b->egl_context) return -1;

    for (int i = 0; i < 2; i++) {
        struct decon_swapchain_buffer *buf = &b->buffers[i];
        if (buf->dma_buf_fd < 0) return -2;

        /* 1. Import DMA-BUF as EGLImageKHR */
        int32_t img_attribs[] = {
            EGL_WIDTH, (int32_t)b->config.width,
            EGL_HEIGHT, (int32_t)b->config.height,
            EGL_LINUX_DRM_FOURCC_EXT, (int32_t)DRM_FORMAT_ABGR8888,
            EGL_DMA_BUF_PLANE0_FD_EXT, (int32_t)buf->dma_buf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (int32_t)b->config.stride,
            EGL_NONE
        };
        buf->egl_image = b->eglCreateImageKHR(b->egl_display, EGL_NO_CONTEXT,
                                              EGL_LINUX_DMA_BUF_EXT, NULL, img_attribs);
        if (!buf->egl_image) return -3;

        /* 2. Bind EGLImage to GL Texture */
        b->glGenTextures(1, &buf->gl_texture);
        b->glBindTexture(GL_TEXTURE_2D, buf->gl_texture);
        b->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        b->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        b->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buf->egl_image);

        /* 3. Create Framebuffer Object (FBO) */
        b->glGenFramebuffers(1, &buf->gl_fbo);
        b->glBindFramebuffer(GL_FRAMEBUFFER, buf->gl_fbo);
        b->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buf->gl_texture, 0);

        if (b->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return -4;
        }
    }
    return 0;
}

int decon_backend_setup_swapchain(struct decon_backend *b)
{
    int ret = decon_backend_setup_swapchain_ion(b, 0);
    if (ret < 0) return ret;
    return decon_backend_setup_swapchain_egl(b);
}

int decon_backend_begin_frame(struct decon_backend *b, uint32_t *active_fbo)
{
    if (!b) return -1;
    struct decon_swapchain_buffer *buf = &b->buffers[b->active_idx];
    if (b->glBindFramebuffer) b->glBindFramebuffer(GL_FRAMEBUFFER, buf->gl_fbo);
    if (b->glViewport) b->glViewport(0, 0, b->config.width, b->config.height);
    if (active_fbo) *active_fbo = buf->gl_fbo;
    return 0;
}

int decon_backend_present(struct decon_backend *b, int *out_retire_fence)
{
    if (!b) return -1;
    struct decon_swapchain_buffer *buf = &b->buffers[b->active_idx];

    /* 1. Flush GPU rendering if active */
    if (b->glFinish) b->glFinish();

    /* 2. Populate 1040-byte S3CFB_WIN_CONFIG structure */
    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    win_data.fence = -1;

    for (int i = 0; i < 5; i++) {
        win_data.config[i].state = 0; /* DECON_WIN_STATE_DISABLED */
        win_data.config[i].fd_idma[0] = -1;
        win_data.config[i].fd_idma[1] = -1;
        win_data.config[i].fd_idma[2] = -1;
        win_data.config[i].fence_fd = -1;
    }

    /* Window 3: Primary scanout window (IDMA channel 0 routing, FIND-021) */
    uint32_t w = b->config.window_index;
    win_data.config[w].state = 2;        /* DECON_WIN_STATE_BUFFER */
    win_data.config[w].fd_idma[0] = buf->dma_buf_fd;
    win_data.config[w].fd_idma[1] = -1;
    win_data.config[w].fd_idma[2] = -1;
    win_data.config[w].fence_fd = -1;
    win_data.config[w].plane_alpha = 255;
    win_data.config[w].blending = 0;
    win_data.config[w].idma_type = 0;    /* IDMA channel 0 */
    win_data.config[w].format = 2;       /* DECON_PIXEL_FORMAT_RGBA_8888 */
    win_data.config[w].src_x = 0;
    win_data.config[w].src_y = 0;
    win_data.config[w].src_w = b->config.width;
    win_data.config[w].src_h = b->config.height;
    win_data.config[w].dst_x = 0;
    win_data.config[w].dst_y = 0;
    win_data.config[w].dst_w = b->config.width;
    win_data.config[w].dst_h = b->config.height;

    /* 3. Issue DECON presentation ioctl */
    int ret = do_ioctl(b, b->fb_fd, S3CFB_WIN_CONFIG_IOCTL, &win_data);
    if (ret < 0) return ret;

    b->last_retire_fence = win_data.fence;
    if (out_retire_fence) *out_retire_fence = win_data.fence;

    return 0;
}

int decon_backend_wait_vsync(struct decon_backend *b, int timeout_ms)
{
    if (!b) return -1;
    if (b->last_retire_fence >= 0) {
        struct pollfd pfd;
        pfd.fd = b->last_retire_fence;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : 1000);
        close(b->last_retire_fence);
        b->last_retire_fence = -1;

        if (ret <= 0) return -2; /* Timeout or error */
    }

    /* Swap ping-pong buffer index */
    b->active_idx ^= 1;
    b->total_frames++;
    return 0;
}

void decon_backend_teardown(struct decon_backend *b)
{
    if (!b) return;

    if (b->last_retire_fence >= 0) {
        close(b->last_retire_fence);
        b->last_retire_fence = -1;
    }

    for (int i = 0; i < 2; i++) {
        struct decon_swapchain_buffer *buf = &b->buffers[i];
        if (buf->cpu_mmap_ptr && buf->cpu_mmap_ptr != MAP_FAILED) {
            munmap(buf->cpu_mmap_ptr, b->config.buffer_size);
            buf->cpu_mmap_ptr = NULL;
        }
        if (buf->egl_image && b->eglDestroyImageKHR) {
            b->eglDestroyImageKHR(b->egl_display, buf->egl_image);
            buf->egl_image = NULL;
        }
        if (buf->dma_buf_fd >= 0) {
            close(buf->dma_buf_fd);
            buf->dma_buf_fd = -1;
        }
        if (buf->ion_handle > 0) {
            struct ion_handle_data free_data;
            memset(&free_data, 0, sizeof(free_data));
            free_data.handle = buf->ion_handle;
            do_ioctl(b, b->ion_fd, ION_IOC_FREE, &free_data);
            buf->ion_handle = 0;
        }
    }

    if (b->egl_display) {
        b->eglMakeCurrent(b->egl_display, NULL, NULL, NULL);
        if (b->egl_surface) b->eglDestroySurface(b->egl_display, b->egl_surface);
        if (b->egl_context) b->eglDestroyContext(b->egl_display, b->egl_context);
        b->eglTerminate(b->egl_display);
    }

    if (b->fb_fd >= 0) close(b->fb_fd);
    if (b->ion_fd >= 0) close(b->ion_fd);
    free(b);
}

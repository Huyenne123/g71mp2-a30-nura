/*
 * Direct-DECON Presentation Engine & Wayland Compositor Backend
 * Target: Samsung Galaxy A30 (SM-A305F) / Exynos 7904 / Mali-G71 MP2
 *
 * Implements direct hardware-accelerated presentation to Samsung DECON Window 3
 * using a double-buffered ION DMA-BUF swapchain and hardware retire fence VSYNC.
 */

#ifndef DECON_BACKEND_H
#define DECON_BACKEND_H

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
typedef signed long    intptr_t;
typedef unsigned long  uintptr_t;

#define NULL ((void*)0)

#define DECON_DEFAULT_WIDTH        1080
#define DECON_DEFAULT_HEIGHT       2340
#define DECON_DEFAULT_STRIDE       4320
#define DECON_DEFAULT_BPP          32
#define DECON_DEFAULT_BUFFER_SIZE  10240000 /* 1080 * 2340 * 4 = 10,108,800 bytes, aligned to 10.24 MB */
#define DECON_PRIMARY_WINDOW       3        /* Exynos 7904 DECON-0 IDMA channel 0 routing (FIND-021) */

#define S3CFB_WIN_CONFIG_IOCTL     0x441046d1 /* _IOW(0x46, 209, 1040 bytes) (FIND-013, FIND-020) */

struct decon_backend_config {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t buffer_size;
    uint32_t window_index;
    const char *fb_node;
    const char *ion_node;
    const char *mali_node;
};

struct decon_swapchain_buffer {
    int ion_handle;
    int dma_buf_fd;
    void *egl_image;       /* EGLImageKHR */
    uint32_t gl_texture;   /* GLuint */
    uint32_t gl_fbo;       /* GLuint */
    void *cpu_mmap_ptr;    /* Optional CPU mapping */
};

struct decon_backend {
    struct decon_backend_config config;
    int fb_fd;
    int ion_fd;

    /* EGL / GLES handles */
    void *libgles_handle;
    void *egl_display;
    void *egl_context;
    void *egl_surface;

    /* Ping-pong swapchain (2 buffers) */
    struct decon_swapchain_buffer buffers[2];
    uint32_t active_idx;
    uint64_t total_frames;
    int last_retire_fence;

    /* Function pointers resolved from libGLES_mali */
    void *(*eglGetDisplay)(void *native_display);
    uint32_t (*eglInitialize)(void *dpy, int32_t *major, int32_t *minor);
    uint32_t (*eglChooseConfig)(void *dpy, const int32_t *attrib_list, void **configs, int32_t config_size, int32_t *num_config);
    void *(*eglCreatePbufferSurface)(void *dpy, void *config, const int32_t *attrib_list);
    void *(*eglCreateContext)(void *dpy, void *config, void *share_context, const int32_t *attrib_list);
    uint32_t (*eglMakeCurrent)(void *dpy, void *draw, void *read, void *ctx);
    void *(*eglCreateImageKHR)(void *dpy, void *ctx, uint32_t target, void *buffer, const int32_t *attrib_list);
    uint32_t (*eglDestroyImageKHR)(void *dpy, void *image);
    uint32_t (*eglDestroySurface)(void *dpy, void *surface);
    uint32_t (*eglDestroyContext)(void *dpy, void *ctx);
    uint32_t (*eglTerminate)(void *dpy);
    int32_t (*eglGetError)(void);

    /* GL Function pointers */
    void (*glGenTextures)(int32_t n, uint32_t *textures);
    void (*glBindTexture)(uint32_t target, uint32_t texture);
    void (*glTexParameteri)(uint32_t target, uint32_t pname, int32_t param);
    void (*glGenFramebuffers)(int32_t n, uint32_t *framebuffers);
    void (*glBindFramebuffer)(uint32_t target, uint32_t framebuffer);
    void (*glFramebufferTexture2D)(uint32_t target, uint32_t attachment, uint32_t textarget, uint32_t texture, int32_t level);
    uint32_t (*glCheckFramebufferStatus)(uint32_t target);
    void (*glViewport)(int32_t x, int32_t y, int32_t width, int32_t height);
    void (*glClearColor)(float red, float green, float blue, float alpha);
    void (*glClear)(uint32_t mask);
    void (*glFinish)(void);
    void (*glEGLImageTargetTexture2DOES)(uint32_t target, void *image);
};

/* -------------------------------------------------------------------------
 * Backend Operations
 * ------------------------------------------------------------------------- */

struct decon_backend *decon_backend_init(const struct decon_backend_config *cfg);
int decon_backend_setup_egl(struct decon_backend *b, void *dl_handle);
int decon_backend_setup_swapchain(struct decon_backend *b);
int decon_backend_begin_frame(struct decon_backend *b, uint32_t *active_fbo);
int decon_backend_present(struct decon_backend *b, int *out_retire_fence);
int decon_backend_wait_vsync(struct decon_backend *b, int timeout_ms);
void decon_backend_teardown(struct decon_backend *b);

#ifdef __cplusplus
}
#endif

#endif /* DECON_BACKEND_H */

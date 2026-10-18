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

#ifndef NULL
#define NULL ((void*)0)
#endif

#define DECON_DEFAULT_WIDTH        1080
#define DECON_DEFAULT_HEIGHT       2340
#define DECON_DEFAULT_STRIDE       4320
#define DECON_DEFAULT_BPP          32
#define DECON_DEFAULT_BUFFER_SIZE  10240000 /* 1080 * 2340 * 4 = 10,108,800 bytes, aligned to 10.24 MB */
#define DECON_PRIMARY_WINDOW       3        /* Exynos 7904 DECON-0 IDMA channel 0 routing (FIND-021) */

#define S3CFB_WIN_CONFIG_IOCTL     0x441046d1 /* _IOW(0x46, 209, 1040 bytes) (FIND-013, FIND-020) */

/* ION Definitions */
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC 0xc0204900
#define ION_IOC_FREE  0xc0044901
#define ION_IOC_SHARE 0xc0084904

struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    int handle;
    int _pad;
};

struct ion_fd_data {
    int handle;
    int fd;
};

struct ion_handle_data {
    int handle;
};

/* -------------------------------------------------------------------------
 * Exact 1040-Byte S3CFB_WIN_CONFIG Structure Layout (FIND-020)
 * ------------------------------------------------------------------------- */

struct decon_win_config {
    int32_t  state;             /* Offset 0x00 (0)  : DECON_WIN_STATE_BUFFER = 2, DISABLED = 0 */
    int32_t  reserved0;         /* Offset 0x04 (4)  : 0 */
    int32_t  fd_idma[3];        /* Offset 0x08 (8)  : [0]=DMA-BUF fd, [1]=-1, [2]=-1 */
    int32_t  fence_fd;          /* Offset 0x14 (20) : Acquire fence (in-fence) fd, -1 if pre-synced */
    int32_t  rel_fence;         /* Offset 0x18 (24) : [OUT] Release fence fd created by DECON driver */
    uint32_t plane_alpha;       /* Offset 0x1C (28) : 0-255 (255 = opaque) */
    uint32_t blending;          /* Offset 0x20 (32) : DECON_BLENDING_NONE = 0 */
    uint32_t idma_type;         /* Offset 0x24 (36) : 0 (Primary IDMA channel) */
    uint32_t format;            /* Offset 0x28 (40) : DECON_PIXEL_FORMAT_RGBA_8888 = 2 */
    uint8_t  padding0[100];     /* Offset 0x2C (44) .. 0x8F (143): 100 bytes reserved */
    int32_t  src_x;             /* Offset 0x90 (144): source X */
    int32_t  src_y;             /* Offset 0x94 (148): source Y */
    uint8_t  padding_src[8];    /* Offset 0x98 (152) .. 0x9F (159): 8 bytes */
    uint32_t src_w;             /* Offset 0xA0 (160): source width */
    uint32_t src_h;             /* Offset 0xA4 (164): source height */
    int32_t  dst_x;             /* Offset 0xA8 (168): destination X */
    int32_t  dst_y;             /* Offset 0xAC (172): destination Y */
    uint32_t dst_w;             /* Offset 0xB0 (176): destination width */
    uint32_t dst_h;             /* Offset 0xB4 (180): destination height */
    uint32_t extra0;            /* Offset 0xB8 (184): 0 */
    uint32_t extra1;            /* Offset 0xBC (188): 0 */
    uint8_t  padding1[8];       /* Offset 0xC0 (192) .. 0xC7 (199): 8 bytes padding (Total: 200 bytes) */
};

_Static_assert(sizeof(struct decon_win_config) == 200, "decon_win_config must be 200 bytes");

struct decon_win_config_data {
    int32_t                 fence;       /* Offset 0x000 (0)  : [OUT] Retire fence fd */
    int32_t                 reserved;    /* Offset 0x004 (4)  : 0 */
    struct decon_win_config config[5];   /* Offset 0x008 (8)  : 5 windows * 200 bytes = 1000 bytes */
    uint8_t                 reserved2[32];/* Offset 0x3F0 (1008): 32 bytes tail padding -> Total 1040 bytes */
};

_Static_assert(sizeof(struct decon_win_config_data) == 1040, "decon_win_config_data must be 1040 bytes");

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
    /* Custom ioctl handler (NULL defaults to libc ioctl) */
    int (*ioctl_fn)(int fd, unsigned long request, void *arg);
};

/* -------------------------------------------------------------------------
 * Backend Operations
 * ------------------------------------------------------------------------- */

struct decon_backend *decon_backend_init(const struct decon_backend_config *cfg);
int decon_backend_setup_egl(struct decon_backend *b, void *dl_handle);
int decon_backend_setup_swapchain_ion(struct decon_backend *b, int map_cpu);
int decon_backend_setup_swapchain_egl(struct decon_backend *b);
int decon_backend_setup_swapchain(struct decon_backend *b);
int decon_backend_begin_frame(struct decon_backend *b, uint32_t *active_fbo);
int decon_backend_present(struct decon_backend *b, int *out_retire_fence);
int decon_backend_wait_vsync(struct decon_backend *b, int timeout_ms);
void decon_backend_teardown(struct decon_backend *b);

#ifdef __cplusplus
}
#endif

#endif /* DECON_BACKEND_H */

/*
 * tests/decon_flip_test.c - Milestone 4C: Continuous Double-Buffered DECON Presentation
 *
 * Validates continuous, tear-free, hardware-accelerated page-flipping on Samsung Galaxy A30:
 * 1. Allocates 2 full-screen (1080x2340, ~10MB) ION DMA-BUFs (Ping-Pong Swapchain).
 * 2. Imports both DMA-BUFs into Mali-G71 GPU and creates two EGLImage FBOs.
 * 3. Runs an active 60-frame continuous animation loop:
 *    - Renders dynamic moving patterns on Mali-G71 GPU (sweeping bar + pulsating center).
 *    - Flushes GPU commands via glFinish().
 *    - Submits page flip to DECON Window 3 (IDMA 0) via S3CFB_WIN_CONFIG (0x441046d1).
 *    - Synchronizes display scanout via poll() on the hardware retire fence.
 *    - Alternates between back and front buffers seamlessly.
 * 4. Measures frame intervals, hardware refresh timing, and real display FPS.
 * 5. Performs clean teardown and resource release.
 */

typedef unsigned long  size_t;
typedef unsigned long  uint64_t;
typedef signed int     int32_t;
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
#define NULL ((void*)0)

extern void __libc_init(void *raw_args, void (*onexit)(void), int (*main)(int, char**, char**), void *params);
extern int printf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *dlopen(const char *filename, int flag);
extern char *dlerror(void);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, unsigned long request, ...);
extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
extern int munmap(void *addr, size_t length);
extern unsigned int sleep(unsigned int seconds);

struct timespec {
    long tv_sec;
    long tv_nsec;
};
#define CLOCK_MONOTONIC 1
extern int clock_gettime(int clock_id, struct timespec *tp);

struct pollfd {
    int   fd;
    short events;
    short revents;
};
#define POLLIN 0x0001
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);

#define RTLD_NOW 2
#define O_RDWR 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1

/* ION Definitions */
#define ION_IOC_ALLOC   0xc0204900
#define ION_IOC_FREE    0xc0044901
#define ION_IOC_SHARE   0xc0084904

typedef int ion_user_handle_t;

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

/* Kbase Definitions */
#define KBASE_IOCTL_VERSION_CHECK 0xc0048000
#define KBASE_IOCTL_SET_FLAGS     0x40048001
#define KBASE_IOCTL_MEM_IMPORT    0xc0188016

#define BASE_MEM_IMPORT_TYPE_UMM  2
#define BASE_MEM_PROT_CPU_RD      (1 << 0)
#define BASE_MEM_PROT_CPU_WR      (1 << 1)
#define BASE_MEM_PROT_GPU_RD      (1 << 2)
#define BASE_MEM_PROT_GPU_WR      (1 << 3)

struct kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};

struct kbase_ioctl_mem_import {
    uint64_t flags;
    uint64_t phandle;
    uint32_t type;
    uint32_t padding;
};

/* DECON FB Definitions */
#define FBIOBLANK        0x4611
#define FB_BLANK_UNBLANK 0
#define S3CFB_WIN_CONFIG 0x441046d1

#define DECON_WIN_STATE_DISABLED 0
#define DECON_WIN_STATE_BUFFER   2
#define DECON_BLENDING_NONE      0
#define DECON_PIXEL_FORMAT_RGBA_8888 2

/* Verified Exynos 7904 DECON Hardware Window: 200 bytes */
struct decon_win_config {
    int32_t  state;             /* Offset 0x00 (0)  : DECON_WIN_STATE_BUFFER = 2, DISABLED = 0 */
    int32_t  reserved0;         /* Offset 0x04 (4)  : 0 */
    int32_t  fd_idma[3];        /* Offset 0x08 (8)  : [0]=DMA-BUF fd, [1]=-1, [2]=-1 */
    int32_t  fence_fd;          /* Offset 0x14 (20) : -1 if pre-synced */
    int32_t  fence_reg;         /* Offset 0x18 (24) : 0 */
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

/* Complete Verified Presentation Payload: 1040 bytes */
struct decon_win_config_data {
    int32_t                 fence;       /* Offset 0x000 (0)  : [OUT] Retire fence fd */
    int32_t                 reserved;    /* Offset 0x004 (4)  : 0 */
    struct decon_win_config config[5];   /* Offset 0x008 (8)  : 5 windows * 200 bytes = 1000 bytes */
    uint8_t                 reserved2[32];/* Offset 0x3F0 (1008): 32 bytes tail padding -> Total 1040 bytes */
};

_Static_assert(sizeof(struct decon_win_config_data) == 1040, "decon_win_config_data must be 1040 bytes");

/* EGL & GLES Constants */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLImageKHR;
typedef void *EGLClientBuffer;
typedef unsigned int EGLBoolean;
typedef int32_t EGLint;

#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NO_CONTEXT      ((void*)0)
#define EGL_NO_SURFACE      ((void*)0)
#define EGL_NO_IMAGE_KHR    ((void*)0)

#define EGL_SURFACE_TYPE    0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_PBUFFER_BIT     0x0001
#define EGL_OPENGL_ES2_BIT  0x0004
#define EGL_RED_SIZE        0x3024
#define EGL_GREEN_SIZE      0x3023
#define EGL_BLUE_SIZE       0x3022
#define EGL_ALPHA_SIZE      0x3021
#define EGL_NONE            0x3038
#define EGL_WIDTH           0x3057
#define EGL_HEIGHT          0x3056
#define EGL_OPENGL_ES_API   0x30A0
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

#define EGL_LINUX_DMA_BUF_EXT         0x3270
#define EGL_LINUX_DRM_FOURCC_EXT      0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT     0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT  0x3274

#define DRM_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define DRM_FORMAT_ABGR8888 DRM_FOURCC('A', 'B', '2', '4')

#define GL_COLOR_BUFFER_BIT        0x00004000
#define GL_FRAMEBUFFER             0x8D40
#define GL_COLOR_ATTACHMENT0       0x8CE0
#define GL_FRAMEBUFFER_COMPLETE    0x8CD5
#define GL_TEXTURE_2D              0x0DE1
#define GL_TEXTURE_MIN_FILTER      0x2801
#define GL_TEXTURE_MAG_FILTER      0x2800
#define GL_NEAREST                 0x2600
#define GL_SCISSOR_TEST            0x0C11

typedef EGLDisplay (*PFNEGLGETDISPLAYPROC)(void *display_id);
typedef EGLBoolean (*PFNEGLINITIALIZEPROC)(EGLDisplay dpy, EGLint *major, EGLint *minor);
typedef EGLBoolean (*PFNEGLTERMINATEPROC)(EGLDisplay dpy);
typedef EGLBoolean (*PFNEGLCHOOSECONFIGPROC)(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLSurface (*PFNEGLCREATEPBUFFERSURFACEPROC)(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYSURFACEPROC)(EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean (*PFNEGLBINDAPIPROC)(unsigned int api);
typedef EGLContext (*PFNEGLCREATECONTEXTPROC)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYCONTEXTPROC)(EGLDisplay dpy, EGLContext ctx);
typedef EGLBoolean (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef void      *(*PFNEGLGETPROCADDRESSPROC)(const char *procname);
typedef EGLImageKHR (*PFNEGLCREATEIMAGEKHRPROC)(EGLDisplay dpy, EGLContext ctx, unsigned int target, EGLClientBuffer buffer, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYIMAGEKHRPROC)(EGLDisplay dpy, EGLImageKHR image);

typedef void (*PFNGLVIEWPORTPROC)(int x, int y, int width, int height);
typedef void (*PFNGLCLEARCOLORPROC)(float red, float green, float blue, float alpha);
typedef void (*PFNGLCLEARPROC)(unsigned int mask);
typedef void (*PFNGLFINISHPROC)(void);
typedef void (*PFNGLGENTEXTURESPROC)(int n, unsigned int *textures);
typedef void (*PFNGLBINDTEXTUREPROC)(unsigned int target, unsigned int texture);
typedef void (*PFNGLTEXPARAMETERIPROC)(unsigned int target, unsigned int pname, int param);
typedef void (*PFNGLDELETETEXTURESPROC)(int n, const unsigned int *textures);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(int n, unsigned int *framebuffers);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
typedef unsigned int (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(unsigned int target);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(int n, const unsigned int *framebuffers);
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(unsigned int target, void *image);
typedef void (*PFNGLENABLEPROC)(unsigned int cap);
typedef void (*PFNGLDISABLEPROC)(unsigned int cap);
typedef void (*PFNGLSCISSORPROC)(int x, int y, int width, int height);

static double time_diff_ms(struct timespec *start, struct timespec *end) {
    return (double)(end->tv_sec - start->tv_sec) * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
}

int main(int argc, char **argv, char **envp) {
    printf("================================================================\n");
    printf(" Milestone 4C: Continuous Double-Buffered DECON Presentation\n");
    printf(" Target: Samsung Galaxy A30 (EA8076 Super AMOLED, 1080x2340)\n");
    printf("================================================================\n");

    const int width  = 1080;
    const int height = 2340;
    const size_t bpp = 4;
    const size_t stride = width * bpp; /* 4320 bytes */
    const size_t buffer_size = 10240000; /* 10.24 MB */
    const int NUM_BUFFERS = 2;
    const int TOTAL_FRAMES = 60; /* 60 frames @ 60 Hz = ~1.0 second animated sequence */

    /* Step 1: Open /dev/ion and allocate Ping-Pong swapchain buffers */
    printf("\n[*] Step 1: Allocating %d Full-Screen Ping-Pong ION Buffers...\n", NUM_BUFFERS);
    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) {
        printf("[-] Failed to open /dev/ion\n");
        return 1;
    }

    int buf_fd[2] = { -1, -1 };
    for (int i = 0; i < NUM_BUFFERS; i++) {
        struct ion_allocation_data alloc_data;
        memset(&alloc_data, 0, sizeof(alloc_data));
        alloc_data.len = buffer_size;
        alloc_data.align = 4096;
        alloc_data.heap_id_mask = 1; /* ion_noncontig_heap */
        alloc_data.flags = 0;

        int ret = ioctl(ion_fd, ION_IOC_ALLOC, &alloc_data);
        if (ret < 0) {
            printf("[-] ION_IOC_ALLOC failed for buffer %d\n", i);
            close(ion_fd);
            return 1;
        }

        struct ion_fd_data fd_data;
        memset(&fd_data, 0, sizeof(fd_data));
        fd_data.handle = alloc_data.handle;
        ret = ioctl(ion_fd, ION_IOC_SHARE, &fd_data);
        if (ret < 0) {
            printf("[-] ION_IOC_SHARE failed for buffer %d\n", i);
            close(ion_fd);
            return 1;
        }
        buf_fd[i] = fd_data.fd;
        printf("[+] Swapchain Buffer %d: Handle 0x%x -> DMA-BUF fd: %d\n",
               i, alloc_data.handle, buf_fd[i]);

        struct ion_handle_data handle_data;
        handle_data.handle = alloc_data.handle;
        ioctl(ion_fd, ION_IOC_FREE, &handle_data);
    }

    /* Step 2: Initialize Mali Kbase */
    printf("\n[*] Step 2: Initializing /dev/mali0 and importing Swapchain Buffers...\n");
    int mali_fd = open("/dev/mali0", O_RDWR);
    if (mali_fd < 0) {
        printf("[-] Failed to open /dev/mali0\n");
        return 1;
    }

    struct kbase_ioctl_version_check vcheck = { 11, 27 };
    ioctl(mali_fd, KBASE_IOCTL_VERSION_CHECK, &vcheck);
    uint32_t flags = 0;
    ioctl(mali_fd, KBASE_IOCTL_SET_FLAGS, &flags);
    mmap(NULL, 4096, 0, MAP_SHARED, mali_fd, 0x3000);

    for (int i = 0; i < NUM_BUFFERS; i++) {
        struct kbase_ioctl_mem_import import_req;
        memset(&import_req, 0, sizeof(import_req));
        import_req.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                           BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
        import_req.phandle = (uint64_t)&buf_fd[i];
        import_req.type = BASE_MEM_IMPORT_TYPE_UMM; /* Type 2 */
        int ret = ioctl(mali_fd, KBASE_IOCTL_MEM_IMPORT, &import_req);
        if (ret < 0) {
            printf("[-] KBASE_IOCTL_MEM_IMPORT failed for buffer %d\n", i);
        } else {
            printf("[+] Buffer %d imported to Mali GPU VA: 0x%lx\n", i, (unsigned long)import_req.phandle);
        }
    }
    close(mali_fd);

    /* Step 3: Initialize EGL and GLES on Mali-G71 */
    printf("\n[*] Step 3: Initializing EGL/GLES Context...\n");
    void *mali_so = dlopen("/vendor/lib64/egl/libGLES_mali.so", RTLD_NOW);
    if (!mali_so) {
        printf("[-] Failed to load libGLES_mali.so\n");
        return 1;
    }

    PFNEGLGETDISPLAYPROC eglGetDisplay = (PFNEGLGETDISPLAYPROC)dlsym(mali_so, "eglGetDisplay");
    PFNEGLINITIALIZEPROC eglInitialize = (PFNEGLINITIALIZEPROC)dlsym(mali_so, "eglInitialize");
    PFNEGLTERMINATEPROC eglTerminate = (PFNEGLTERMINATEPROC)dlsym(mali_so, "eglTerminate");
    PFNEGLCHOOSECONFIGPROC eglChooseConfig = (PFNEGLCHOOSECONFIGPROC)dlsym(mali_so, "eglChooseConfig");
    PFNEGLCREATEPBUFFERSURFACEPROC eglCreatePbufferSurface = (PFNEGLCREATEPBUFFERSURFACEPROC)dlsym(mali_so, "eglCreatePbufferSurface");
    PFNEGLDESTROYSURFACEPROC eglDestroySurface = (PFNEGLDESTROYSURFACEPROC)dlsym(mali_so, "eglDestroySurface");
    PFNEGLBINDAPIPROC eglBindAPI = (PFNEGLBINDAPIPROC)dlsym(mali_so, "eglBindAPI");
    PFNEGLCREATECONTEXTPROC eglCreateContext = (PFNEGLCREATECONTEXTPROC)dlsym(mali_so, "eglCreateContext");
    PFNEGLDESTROYCONTEXTPROC eglDestroyContext = (PFNEGLDESTROYCONTEXTPROC)dlsym(mali_so, "eglDestroyContext");
    PFNEGLMAKECURRENTPROC eglMakeCurrent = (PFNEGLMAKECURRENTPROC)dlsym(mali_so, "eglMakeCurrent");
    PFNEGLGETPROCADDRESSPROC eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(mali_so, "eglGetProcAddress");

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    PFNGLVIEWPORTPROC glViewport = (PFNGLVIEWPORTPROC)dlsym(mali_so, "glViewport");
    PFNGLCLEARCOLORPROC glClearColor = (PFNGLCLEARCOLORPROC)dlsym(mali_so, "glClearColor");
    PFNGLCLEARPROC glClear = (PFNGLCLEARPROC)dlsym(mali_so, "glClear");
    PFNGLFINISHPROC glFinish = (PFNGLFINISHPROC)dlsym(mali_so, "glFinish");
    PFNGLGENTEXTURESPROC glGenTextures = (PFNGLGENTEXTURESPROC)dlsym(mali_so, "glGenTextures");
    PFNGLBINDTEXTUREPROC glBindTexture = (PFNGLBINDTEXTUREPROC)dlsym(mali_so, "glBindTexture");
    PFNGLTEXPARAMETERIPROC glTexParameteri = (PFNGLTEXPARAMETERIPROC)dlsym(mali_so, "glTexParameteri");
    PFNGLDELETETEXTURESPROC glDeleteTextures = (PFNGLDELETETEXTURESPROC)dlsym(mali_so, "glDeleteTextures");
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)dlsym(mali_so, "glGenFramebuffers");
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)dlsym(mali_so, "glBindFramebuffer");
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)dlsym(mali_so, "glFramebufferTexture2D");
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)dlsym(mali_so, "glCheckFramebufferStatus");
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)dlsym(mali_so, "glDeleteFramebuffers");
    PFNGLENABLEPROC glEnable = (PFNGLENABLEPROC)dlsym(mali_so, "glEnable");
    PFNGLDISABLEPROC glDisable = (PFNGLDISABLEPROC)dlsym(mali_so, "glDisable");
    PFNGLSCISSORPROC glScissor = (PFNGLSCISSORPROC)dlsym(mali_so, "glScissor");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint num_cfgs = 0;
    eglChooseConfig(dpy, config_attribs, &config, 1, &num_cfgs);

    const EGLint pbuf_attribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface dummy_pbuffer = eglCreatePbufferSurface(dpy, config, pbuf_attribs);

    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(dpy, dummy_pbuffer, dummy_pbuffer, ctx);
    printf("[+] Context active on Mali-G71!\n");

    /* Step 4: Create EGLImages & FBOs for both swapchain buffers */
    printf("\n[*] Step 4: Creating EGLImages and FBOs for Swapchain...\n");
    EGLImageKHR images[2];
    unsigned int texs[2];
    unsigned int fbos[2];

    for (int i = 0; i < NUM_BUFFERS; i++) {
        const EGLint img_attribs[] = {
            EGL_WIDTH,                         width,
            EGL_HEIGHT,                        height,
            EGL_LINUX_DRM_FOURCC_EXT,          DRM_FORMAT_ABGR8888,
            EGL_DMA_BUF_PLANE0_FD_EXT,         buf_fd[i],
            EGL_DMA_BUF_PLANE0_OFFSET_EXT,     0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT,      stride,
            EGL_NONE
        };

        images[i] = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, img_attribs);
        if (images[i] == EGL_NO_IMAGE_KHR) {
            printf("[-] eglCreateImageKHR failed for buffer %d\n", i);
            return 1;
        }

        glGenTextures(1, &texs[i]);
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, images[i]);

        glGenFramebuffers(1, &fbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texs[i], 0);

        unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            printf("[-] Framebuffer %d incomplete: 0x%x\n", i, status);
            return 1;
        }
        printf("[+] FBO %d initialized & COMPLETE (0x%x)!\n", i, status);
    }

    /* Step 5: Open DECON Display */
    printf("\n[*] Step 5: Opening DECON (/dev/graphics/fb0)...\n");
    int fb_fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb_fd < 0) {
        printf("[-] Failed to open /dev/graphics/fb0\n");
        return 1;
    }
    ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);

    /* Step 6: Continuous Double-Buffered Presentation Loop */
    printf("\n[*] Step 6: Starting %d-Frame Dynamic Presentation Loop...\n", TOTAL_FRAMES);
    glViewport(0, 0, width, height);

    struct timespec t_loop_start, t_loop_end;
    clock_gettime(CLOCK_MONOTONIC, &t_loop_start);

    int prev_retire_fence = -1;
    int success_flips = 0;
    double max_frame_ms = 0.0;
    double min_frame_ms = 999999.0;

    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        struct timespec t_frame_start, t_frame_end;
        clock_gettime(CLOCK_MONOTONIC, &t_frame_start);

        int back_idx = frame % NUM_BUFFERS;
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[back_idx]);

        /* Dynamic Render Scene:
         * 1. Dark Blue background
         * 2. Sweeping horizontal neon band (moves from top to bottom)
         * 3. Pulsating center box
         */
        glClearColor(0.06f, 0.10f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);

        /* Sweeping horizontal bar (height 120 px) */
        int bar_y = (frame * (height - 120)) / TOTAL_FRAMES;
        glScissor(0, bar_y, width, 120);
        /* Alternating color based on frame phase */
        if (frame % 2 == 0) {
            glClearColor(0.0f, 1.0f, 0.5f, 1.0f); /* Neon Cyan-Green */
        } else {
            glClearColor(1.0f, 0.85f, 0.0f, 1.0f); /* Vivid Gold */
        }
        glClear(GL_COLOR_BUFFER_BIT);

        /* Center Focus Box (pulsating size: 200..320 px) */
        int box_size = 200 + ((frame % 15) * 8);
        int bx = (width - box_size) / 2;
        int by = (height - box_size) / 2;
        glScissor(bx, by, box_size, box_size);
        glClearColor(1.0f, 0.25f, 0.1f, 1.0f); /* Neon Crimson */
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
        glFinish(); /* Submit atom chain and wait for GPU completion */

        /* DECON S3CFB_WIN_CONFIG Presentation */
        struct decon_win_config_data win_data;
        memset(&win_data, 0, sizeof(win_data));
        win_data.fence = -1;

        for (int w = 0; w < 5; w++) {
            win_data.config[w].state = DECON_WIN_STATE_DISABLED;
            win_data.config[w].fd_idma[0] = -1;
            win_data.config[w].fd_idma[1] = -1;
            win_data.config[w].fd_idma[2] = -1;
            win_data.config[w].fence_fd = -1;
        }

        /* Target Window 3 (Primary Hardware Scanout, IDMA 0) */
        win_data.config[3].state = DECON_WIN_STATE_BUFFER;
        win_data.config[3].fd_idma[0] = buf_fd[back_idx];
        win_data.config[3].fd_idma[1] = -1;
        win_data.config[3].fd_idma[2] = -1;
        win_data.config[3].fence_fd = -1; /* Pre-synced */
        win_data.config[3].plane_alpha = 255;
        win_data.config[3].blending = DECON_BLENDING_NONE;
        win_data.config[3].idma_type = 0; /* IDMA 0 */
        win_data.config[3].format = DECON_PIXEL_FORMAT_RGBA_8888;
        win_data.config[3].src_x = 0;
        win_data.config[3].src_y = 0;
        win_data.config[3].src_w = width;
        win_data.config[3].src_h = height;
        win_data.config[3].dst_x = 0;
        win_data.config[3].dst_y = 0;
        win_data.config[3].dst_w = width;
        win_data.config[3].dst_h = height;

        int ret = ioctl(fb_fd, S3CFB_WIN_CONFIG, &win_data);
        if (ret < 0) {
            extern int *__errno(void);
            printf("[-] S3CFB_WIN_CONFIG failed at frame %d (errno = %d)\n", frame, *__errno());
            break;
        }
        success_flips++;

        /* Wait on previous retire fence to ensure scanout has flipped */
        if (prev_retire_fence >= 0) {
            struct pollfd pfd = { prev_retire_fence, POLLIN, 0 };
            poll(&pfd, 1, 100);
            close(prev_retire_fence);
        }
        prev_retire_fence = win_data.fence;

        clock_gettime(CLOCK_MONOTONIC, &t_frame_end);
        double f_ms = time_diff_ms(&t_frame_start, &t_frame_end);
        if (f_ms > max_frame_ms) max_frame_ms = f_ms;
        if (f_ms < min_frame_ms) min_frame_ms = f_ms;

        if (frame % 10 == 0 || frame == TOTAL_FRAMES - 1) {
            printf("    Frame %2d/%2d: Buffer %d, Flip Success, Frame Time = %.2f ms\n",
                   frame + 1, TOTAL_FRAMES, back_idx, f_ms);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_loop_end);
    double total_ms = time_diff_ms(&t_loop_start, &t_loop_end);
    double avg_fps = (double)success_flips / (total_ms / 1000.0);

    if (prev_retire_fence >= 0) {
        struct pollfd pfd = { prev_retire_fence, POLLIN, 0 };
        poll(&pfd, 1, 200);
        close(prev_retire_fence);
    }

    printf("\n[+] Dynamic Presentation Loop Complete!\n");
    printf("    Total Frames Rendered & Presented : %d / %d\n", success_flips, TOTAL_FRAMES);
    printf("    Total Loop Duration               : %.2f ms (%.2f seconds)\n", total_ms, total_ms / 1000.0);
    printf("    Average Frame Rate                : %.2f FPS\n", avg_fps);
    printf("    Min Frame Time                    : %.2f ms\n", min_frame_ms);
    printf("    Max Frame Time                    : %.2f ms\n", max_frame_ms);

    /* Hold final frame for 2 seconds */
    printf("[*] Holding final animation frame for 2 seconds...\n");
    sleep(2);

    /* Step 7: Clean Teardown */
    printf("\n[*] Step 7: Clean Teardown...\n");
    close(fb_fd);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        glDeleteFramebuffers(1, &fbos[i]);
        glDeleteTextures(1, &texs[i]);
        eglDestroyImageKHR(dpy, images[i]);
        close(buf_fd[i]);
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, dummy_pbuffer);
    eglTerminate(dpy);
    dlclose(mali_so);
    close(ion_fd);

    printf("[+] Standalone double-buffered presentation finished clean!\n");
    printf("================================================================\n");

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

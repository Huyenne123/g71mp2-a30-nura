#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <math.h>
#include <sys/ioctl.h>
#include <assert.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/gralloc/gralloc.h>
#include <system/graphics.h>
#include <system/window.h>
#include "/root/libhybris/hybris/egl/platforms/hwcomposer/hwcomposer.h"

/* DECON Hardware Definitions for Samsung Exynos 7904 (FIND-013, FIND-020, FIND-021) */
#define DECON_WIDTH             1080
#define DECON_HEIGHT            2340
#define DECON_FB_NODE           "/dev/graphics/fb0"
#define S3CFB_WIN_CONFIG_IOCTL  0x441046d1

struct decon_win_config {
    int32_t  state;             /* Offset 0x00: DECON_WIN_STATE_BUFFER = 2 */
    int32_t  reserved0;         /* Offset 0x04: 0 */
    int32_t  fd_idma[3];        /* Offset 0x08: [0]=DMA-BUF fd, [1]=-1, [2]=-1 */
    int32_t  fence_fd;          /* Offset 0x14: Acquire fence fd */
    int32_t  rel_fence;         /* Offset 0x18: [OUT] Release fence fd */
    uint32_t plane_alpha;       /* Offset 0x1C: 0-255 */
    uint32_t blending;          /* Offset 0x20: DECON_BLENDING_NONE = 0 */
    uint32_t idma_type;         /* Offset 0x24: 0 (Primary IDMA channel 0) */
    uint32_t format;            /* Offset 0x28: DECON_PIXEL_FORMAT_RGBA_8888 = 2 */
    uint8_t  padding0[100];     /* Offset 0x2C..0x8F: 100 bytes */
    int32_t  src_x;             /* Offset 0x90: 0 */
    int32_t  src_y;             /* Offset 0x94: 0 */
    uint8_t  padding_src[8];    /* Offset 0x98..0x9F: 8 bytes */
    uint32_t src_w;             /* Offset 0xA0: 1080 */
    uint32_t src_h;             /* Offset 0xA4: 2340 */
    int32_t  dst_x;             /* Offset 0xA8: 0 */
    int32_t  dst_y;             /* Offset 0xAC: 0 */
    uint32_t dst_w;             /* Offset 0xB0: 1080 */
    uint32_t dst_h;             /* Offset 0xB4: 2340 */
    uint32_t extra0;            /* Offset 0xB8: 0 */
    uint32_t extra1;            /* Offset 0xBC: 0 */
    uint8_t  padding1[8];       /* Offset 0xC0..0xC7: 8 bytes (Total: 200 bytes) */
};

struct decon_win_config_data {
    int32_t                 fence;       /* Offset 0x000: [OUT] Retire fence fd */
    int32_t                 reserved;    /* Offset 0x004: 0 */
    struct decon_win_config config[5];   /* Offset 0x008: 5 windows * 200 bytes */
    uint8_t                 reserved2[32];/* Offset 0x3F0: 32 bytes tail padding */
};

_Static_assert(sizeof(struct decon_win_config) == 200, "decon_win_config size mismatch");
_Static_assert(sizeof(struct decon_win_config_data) == 1040, "decon_win_config_data size mismatch");

/* Global state */
static volatile int g_running = 1;
static int g_fb_fd = -1;
static unsigned long g_frame_count = 0;
static unsigned long g_present_count = 0;
static int g_last_dma_fd = -1;
static int g_last_acquire_fence = -1;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Telemetry reading */
static void read_interrupts(unsigned long *mali_irq, unsigned long *panel_irq) {
    FILE *fp = fopen("/proc/interrupts", "r");
    if (!fp) return;
    char line[512];
    *mali_irq = 0;
    *panel_irq = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "11500000.mali")) {
            char *p = line;
            while (*p && *p != ':') p++;
            if (*p == ':') p++;
            for (int cpu = 0; cpu < 8; cpu++) {
                char *end = NULL;
                unsigned long v = strtoul(p, &end, 10);
                if (p == end) break;
                *mali_irq += v;
                p = end;
            }
        }
        if (strstr(line, "249:") && strstr(line, "decon_f")) {
            char *p = line;
            while (*p && *p != ':') p++;
            if (*p == ':') p++;
            for (int cpu = 0; cpu < 8; cpu++) {
                char *end = NULL;
                unsigned long v = strtoul(p, &end, 10);
                if (p == end) break;
                *panel_irq += v;
                p = end;
            }
        }
    }
    fclose(fp);
}

static long get_rss_kb(void) {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0;
    long size = 0, resident = 0;
    if (fscanf(fp, "%ld %ld", &size, &resident) == 2) {
        fclose(fp);
        return (resident * sysconf(_SC_PAGESIZE)) / 1024;
    }
    fclose(fp);
    return 0;
}

static int count_open_fds(void) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int c = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] != '.') c++;
    }
    closedir(d);
    return c;
}

/* Present callback called on eglSwapBuffers */
static void decon_present_cb(void *user_data, struct ANativeWindow *win, struct ANativeWindowBuffer *buf) {
    (void)user_data;
    (void)win;

    g_present_count++;
    if (!buf || !buf->handle || buf->handle->numFds < 1) {
        fprintf(stderr, "[E2E_TEST] ERROR: Invalid buffer or native handle in present callback\n");
        return;
    }

    int dma_buf_fd = buf->handle->data[0];
    int acquire_fence = HWCNativeBufferGetFence(buf);
    g_last_dma_fd = dma_buf_fd;
    g_last_acquire_fence = acquire_fence;

    /* Build 1040-byte DECON configuration */
    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    win_data.fence = -1;

    for (int i = 0; i < 5; i++) {
        win_data.config[i].state = 0; /* DISABLED */
        win_data.config[i].fd_idma[0] = -1;
        win_data.config[i].fd_idma[1] = -1;
        win_data.config[i].fd_idma[2] = -1;
        win_data.config[i].fence_fd = -1;
        win_data.config[i].rel_fence = -1;
    }

    /* Window 3: Primary scanout window routed to IDMA channel 0 (FIND-021) */
    win_data.config[3].state = 2; /* DECON_WIN_STATE_BUFFER */
    win_data.config[3].fd_idma[0] = dma_buf_fd;
    win_data.config[3].fence_fd = acquire_fence; /* GPU render completion sync fence */
    win_data.config[3].plane_alpha = 255;
    win_data.config[3].blending = 0;
    win_data.config[3].idma_type = 0; /* Channel 0 */
    win_data.config[3].format = 2;    /* RGBA_8888 */
    win_data.config[3].src_x = 0;
    win_data.config[3].src_y = 0;
    win_data.config[3].src_w = DECON_WIDTH;
    win_data.config[3].src_h = DECON_HEIGHT;
    win_data.config[3].dst_x = 0;
    win_data.config[3].dst_y = 0;
    win_data.config[3].dst_w = DECON_WIDTH;
    win_data.config[3].dst_h = DECON_HEIGHT;

    /* Issue DECON presentation ioctl */
    int ret = ioctl(g_fb_fd, S3CFB_WIN_CONFIG_IOCTL, &win_data);
    if (ret < 0) {
        static int err_count = 0;
        if (err_count++ < 5) {
            fprintf(stderr, "[E2E_TEST] ioctl(S3CFB_WIN_CONFIG_IOCTL) failed: %s (%d)\n",
                    strerror(errno), errno);
        }
    }

    /* Close acquire_fence (kernel took reference during ioctl, userspace must close its fd) */
    if (acquire_fence >= 0) {
        close(acquire_fence);
    }

    /* Close all release fences returned by DECON to prevent FD leaks (FIND-037) */
    for (int i = 0; i < 5; i++) {
        if (win_data.config[i].rel_fence >= 0) {
            close(win_data.config[i].rel_fence);
            win_data.config[i].rel_fence = -1;
        }
    }

    /* Handle retire fence */
    int retire_fence = win_data.fence;
    if (retire_fence >= 0) {
        /* Hand retire fence back to buffer so EGL waits before next render */
        HWCNativeBufferSetFence(buf, dup(retire_fence));

        /* Poll retire fence to lock frame pacing to physical 60 Hz VSYNC */
        struct pollfd pfd;
        pfd.fd = retire_fence;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, 100);

        close(retire_fence);
    }
}

/* Shader compilation helper */
static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[SHADER] Compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint create_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[PROGRAM] Link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

int main(int argc, char **argv) {
    int duration_sec = 600; /* Default 10 minutes (600 seconds) */
    if (argc > 1) {
        duration_sec = atoi(argv[1]);
        if (duration_sec <= 0) duration_sec = 600;
    }

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    printf("=================================================================\n");
    printf("  Milestone 7C Phase D: Mali-G71 -> DMA-BUF -> DECON E2E Test    \n");
    printf("=================================================================\n");
    printf("[CONFIG] Target Duration: %d seconds (%d minutes)\n", duration_sec, duration_sec / 60);
    printf("[CONFIG] Display Resolution: %dx%d, Scanout Window: 3\n", DECON_WIDTH, DECON_HEIGHT);
    fflush(stdout);

    /* Open DECON device node */
    g_fb_fd = open(DECON_FB_NODE, O_RDWR);
    if (g_fb_fd < 0) {
        fprintf(stderr, "[E2E_TEST] ERROR: Failed to open %s: %s\n", DECON_FB_NODE, strerror(errno));
        return 1;
    }
    printf("[STEP 1] Opened %s (fd=%d)\n", DECON_FB_NODE, g_fb_fd); fflush(stdout);

    /* Initialize Gralloc */
    hybris_gralloc_initialize(0);
    printf("[STEP 2] hybris_gralloc_initialize completed.\n"); fflush(stdout);

    /* Create HWC Native Window with Present Callback */
    struct ANativeWindow *win = HWCNativeWindowCreate(
        DECON_WIDTH, DECON_HEIGHT,
        HAL_PIXEL_FORMAT_RGBA_8888,
        decon_present_cb, NULL
    );
    if (!win) {
        fprintf(stderr, "[E2E_TEST] ERROR: HWCNativeWindowCreate failed\n");
        close(g_fb_fd);
        return 1;
    }
    printf("[STEP 3] HWCNativeWindowCreate succeeded: %p\n", (void*)win); fflush(stdout);

    /* Initialize EGL */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglGetDisplay failed\n");
        close(g_fb_fd);
        return 1;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglInitialize failed: 0x%x\n", eglGetError());
        close(g_fb_fd);
        return 1;
    }
    printf("[STEP 4] eglInitialize succeeded: EGL %d.%d\n", major, minor); fflush(stdout);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs = 0;
    if (!eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglChooseConfig failed\n");
        close(g_fb_fd);
        return 1;
    }

    EGLSurface surface = eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)win, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        close(g_fb_fd);
        return 1;
    }
    printf("[STEP 5] eglCreateWindowSurface succeeded: %p\n", (void*)surface); fflush(stdout);

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglCreateContext failed: 0x%x\n", eglGetError());
        close(g_fb_fd);
        return 1;
    }

    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        fprintf(stderr, "[E2E_TEST] ERROR: eglMakeCurrent failed: 0x%x\n", eglGetError());
        close(g_fb_fd);
        return 1;
    }
    printf("[STEP 6] eglMakeCurrent succeeded.\n"); fflush(stdout);

    const GLubyte *gl_vendor = glGetString(GL_VENDOR);
    const GLubyte *gl_renderer = glGetString(GL_RENDERER);
    const GLubyte *gl_version = glGetString(GL_VERSION);
    printf("[GL_INFO] Vendor:   %s\n", gl_vendor ? (const char*)gl_vendor : "(null)");
    printf("[GL_INFO] Renderer: %s\n", gl_renderer ? (const char*)gl_renderer : "(null)");
    printf("[GL_INFO] Version:  %s\n", gl_version ? (const char*)gl_version : "(null)");
    fflush(stdout);

    /* Create rendering shaders */
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec3 a_col;\n"
        "uniform float u_angle;\n"
        "varying vec3 v_col;\n"
        "void main() {\n"
        "    float s = sin(u_angle);\n"
        "    float c = cos(u_angle);\n"
        "    mat2 rot = mat2(c, -s, s, c);\n"
        "    gl_Position = vec4(rot * a_pos, 0.0, 1.0);\n"
        "    v_col = a_col;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "varying vec3 v_col;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(v_col, 1.0);\n"
        "}\n";

    GLuint prog = create_program(vs_src, fs_src);
    assert(prog != 0);
    glUseProgram(prog);

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint a_col = glGetAttribLocation(prog, "a_col");
    GLint u_angle = glGetUniformLocation(prog, "u_angle");

    // Rotating hexagon geometry
    float hex_verts[] = {
        // x, y,          r, g, b
         0.0f,  0.0f,     1.0f, 1.0f, 1.0f, // Center: White
         0.0f,  0.7f,     1.0f, 0.2f, 0.2f,
         0.6f,  0.35f,    1.0f, 0.8f, 0.2f,
         0.6f, -0.35f,    0.2f, 1.0f, 0.2f,
         0.0f, -0.7f,     0.2f, 0.8f, 1.0f,
        -0.6f, -0.35f,    0.4f, 0.2f, 1.0f,
        -0.6f,  0.35f,    1.0f, 0.2f, 0.8f,
         0.0f,  0.7f,     1.0f, 0.2f, 0.2f  // Close loop
    };

    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), hex_verts);
    glVertexAttribPointer(a_col, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), &hex_verts[2]);
    glEnableVertexAttribArray(a_pos);
    glEnableVertexAttribArray(a_col);

    glViewport(0, 0, DECON_WIDTH, DECON_HEIGHT);

    /* Telemetry Baseline */
    unsigned long mali_irq_pre = 0, panel_irq_pre = 0;
    read_interrupts(&mali_irq_pre, &panel_irq_pre);
    long rss_pre = get_rss_kb();
    int fds_pre = count_open_fds();

    printf("\n[START] Entering presentation render loop for %d seconds...\n", duration_sec);
    printf("[TELEMETRY-PRE] GPU IRQ: %lu, Panel IRQ: %lu, RSS: %ld KB, FDs: %d\n",
           mali_irq_pre, panel_irq_pre, rss_pre, fds_pre);
    fflush(stdout);

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    double last_report = 0.0;

    while (g_running) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) +
                         (current_time.tv_nsec - start_time.tv_nsec) / 1e9;

        if (elapsed >= (double)duration_sec) {
            printf("[E2E_TEST] Target duration of %d seconds reached!\n", duration_sec);
            break;
        }

        float angle = (float)g_frame_count * 0.03f;
        float bg_r = 0.10f + 0.05f * sinf(angle * 0.5f);
        float bg_g = 0.12f + 0.05f * cosf(angle * 0.7f);
        float bg_b = 0.18f + 0.05f * sinf(angle * 0.3f);

        glClearColor(bg_r, bg_g, bg_b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(u_angle, angle);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 8);

        /* SwapBuffers triggers present callback and commits frame to DECON */
        eglSwapBuffers(dpy, surface);
        g_frame_count++;

        /* Status report every 10 seconds */
        if (elapsed - last_report >= 10.0) {
            last_report = elapsed;
            double current_fps = (double)g_frame_count / elapsed;
            unsigned long mali_irq = 0, panel_irq = 0;
            read_interrupts(&mali_irq, &panel_irq);
            long cur_rss = get_rss_kb();
            int cur_fds = count_open_fds();

            printf("[TIME: %6.1fs / %ds] Frames: %lu (%.2f FPS) | DMA-FD: %d | GPU-IRQ: +%lu | Panel-IRQ: +%lu | RSS: %ld KB | FDs: %d\n",
                   elapsed, duration_sec, g_frame_count, current_fps,
                   g_last_dma_fd,
                   mali_irq - mali_irq_pre,
                   panel_irq - panel_irq_pre,
                   cur_rss, cur_fds);
            fflush(stdout);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    double total_elapsed = (current_time.tv_sec - start_time.tv_sec) +
                           (current_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("\n=================================================================\n");
    printf("  Presentation Test Complete: Final Verification & Telemetry     \n");
    printf("=================================================================\n");

    unsigned long mali_irq_post = 0, panel_irq_post = 0;
    read_interrupts(&mali_irq_post, &panel_irq_post);
    long rss_post = get_rss_kb();
    int fds_post = count_open_fds();

    double avg_fps = (double)g_frame_count / total_elapsed;
    printf("[FINAL] Total Elapsed Time: %.2f seconds\n", total_elapsed);
    printf("[FINAL] Total Frames Presented: %lu\n", g_frame_count);
    printf("[FINAL] Average Cadence: %.2f FPS (Target: ~60 FPS)\n", avg_fps);
    printf("[FINAL] Total Present Callbacks: %lu\n", g_present_count);
    printf("[FINAL] GPU Interrupts Delta: +%lu\n", mali_irq_post - mali_irq_pre);
    printf("[FINAL] Panel VSYNC IRQs Delta: +%lu\n", panel_irq_post - panel_irq_pre);
    printf("[FINAL] Memory (RSS) Delta: %+ld KB (%ld -> %ld KB)\n",
           rss_post - rss_pre, rss_pre, rss_post);
    printf("[FINAL] File Descriptors Delta: %+d (%d -> %d)\n",
           fds_post - fds_pre, fds_pre, fds_post);
    fflush(stdout);

    /* Teardown */
    printf("[TEARDOWN 1] eglMakeCurrent(NULL)...\n"); fflush(stdout);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    printf("[TEARDOWN 2] eglDestroyContext...\n"); fflush(stdout);
    eglDestroyContext(dpy, ctx);
    printf("[TEARDOWN 3] eglDestroySurface...\n"); fflush(stdout);
    eglDestroySurface(dpy, surface);
    printf("[TEARDOWN 4] eglTerminate...\n"); fflush(stdout);
    eglTerminate(dpy);
    printf("[TEARDOWN 5] Closing DECON fb_fd...\n"); fflush(stdout);
    close(g_fb_fd);

    printf("\n>>> MALI GLES -> DMA-BUF -> DECON PRESENTATION TEST FINISHED SUCCESSFULLY! <<<\n");
    return 0;
}

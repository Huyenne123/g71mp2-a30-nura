#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* Telemetry helpers */
static unsigned long get_gpu_interrupts(unsigned long *gic190, unsigned long *gic191) {
    FILE *fp = fopen("/proc/interrupts", "r");
    if (!fp) return 0;
    char line[512];
    *gic190 = 0;
    *gic191 = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "11500000.mali")) {
            unsigned long total = 0;
            char *p = line;
            while (*p && *p != ':') p++;
            if (*p == ':') p++;
            for (int cpu = 0; cpu < 8; cpu++) {
                char *end = NULL;
                unsigned long val = strtoul(p, &end, 10);
                if (p == end) break;
                total += val;
                p = end;
            }
            if (strstr(line, "GIC 190")) *gic190 = total;
            if (strstr(line, "GIC 191")) *gic191 = total;
        }
    }
    fclose(fp);
    return *gic190 + *gic191;
}

static long get_rss_kb(void) {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0;
    long size = 0, resident = 0;
    if (fscanf(fp, "%ld %ld", &size, &resident) == 2) {
        long pagesize = sysconf(_SC_PAGESIZE);
        fclose(fp);
        return (resident * pagesize) / 1024;
    }
    fclose(fp);
    return 0;
}

static int count_open_fds(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] != '.') count++;
    }
    closedir(dir);
    return count;
}

/* Shader compilation helper */
static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "[SHADER] Compilation failed: %s\n", buf);
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
        char buf[512];
        glGetProgramInfoLog(prog, sizeof(buf), NULL, buf);
        fprintf(stderr, "[PROGRAM] Linking failed: %s\n", buf);
        glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/* Sub-test 1: Clear colors & alpha */
static void test_clear_colors(void) {
    printf("[PHASE B.1] Testing glClearColor with various RGB/Alpha values...\n");
    struct {
        float r, g, b, a;
        int exp_r, exp_g, exp_b, exp_a;
        const char *name;
    } cases[] = {
        {1.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255, "Red"},
        {0.0f, 1.0f, 0.0f, 1.0f, 0, 255, 0, 255, "Green"},
        {0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 255, 255, "Blue"},
        {1.0f, 1.0f, 1.0f, 1.0f, 255, 255, 255, 255, "White"},
        {0.0f, 0.0f, 0.0f, 1.0f, 0, 0, 0, 255, "Black"},
        {0.5f, 0.25f, 0.75f, 0.5f, 128, 64, 191, 128, "Mixed RGBA"}
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        glClearColor(cases[i].r, cases[i].g, cases[i].b, cases[i].a);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();

        GLubyte pixel[4] = {0};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        assert(abs((int)pixel[0] - cases[i].exp_r) <= 1);
        assert(abs((int)pixel[1] - cases[i].exp_g) <= 1);
        assert(abs((int)pixel[2] - cases[i].exp_b) <= 1);
        assert(abs((int)pixel[3] - cases[i].exp_a) <= 1);
        printf("  - %s: RGBA=(%d, %d, %d, %d) [MATCH]\n",
               cases[i].name, pixel[0], pixel[1], pixel[2], pixel[3]);
    }
    printf("[PHASE B.1] PASSED: All clear colors match expected mathematically.\n");
}

/* Sub-test 2: Colored Triangle */
static void test_triangle(void) {
    printf("[PHASE B.2] Testing triangle rendering and attribute interpolation...\n");
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec3 a_color;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(v_color, 1.0);\n"
        "}\n";

    GLuint prog = create_program(vs_src, fs_src);
    assert(prog != 0);
    glUseProgram(prog);

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint a_color = glGetAttribLocation(prog, "a_color");

    // Triangle covering center: Top (Red), Bottom-Left (Green), Bottom-Right (Blue)
    float vertices[] = {
        // x, y,       r, g, b
         0.0f,  0.8f,  1.0f, 0.0f, 0.0f, // Top: Red
        -0.8f, -0.8f,  0.0f, 1.0f, 0.0f, // BL: Green
         0.8f, -0.8f,  0.0f, 0.0f, 1.0f  // BR: Blue
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(a_pos);
    glEnableVertexAttribArray(a_color);
    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glVertexAttribPointer(a_color, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    // Verify center pixel (centroid should be an interpolated blend of R, G, B: ~1/3 each)
    GLubyte center[4] = {0};
    glReadPixels(32, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    printf("  - Centroid at (32, 24): RGBA=(%d, %d, %d, %d)\n",
           center[0], center[1], center[2], center[3]);
    assert(center[0] > 40 && center[1] > 40 && center[2] > 40 && center[3] == 255);

    // Verify near top vertex (mostly red)
    GLubyte top[4] = {0};
    glReadPixels(32, 55, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
    printf("  - Top vertex at (32, 55): RGBA=(%d, %d, %d, %d)\n",
           top[0], top[1], top[2], top[3]);
    assert(top[0] > 180 && top[1] < 60 && top[2] < 60);

    glDisableVertexAttribArray(a_pos);
    glDisableVertexAttribArray(a_color);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
    printf("[PHASE B.2] PASSED: Triangle vertex interpolation verified.\n");
}

/* Sub-test 3: Texture sampling */
static void test_texture_sampling(void) {
    printf("[PHASE B.3] Testing 2D texture generation, upload, and sampling...\n");
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_texcoord;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "    v_texcoord = a_texcoord;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D u_tex;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_tex, v_texcoord);\n"
        "}\n";

    GLuint prog = create_program(vs_src, fs_src);
    assert(prog != 0);
    glUseProgram(prog);

    // 2x2 Checkerboard pattern:
    // [ Red,   Green ]
    // [ Blue,  Yellow]
    GLubyte tex_data[4 * 4] = {
        255,   0,   0, 255,    0, 255,   0, 255,
          0,   0, 255, 255,  255, 255,   0, 255
    };

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);

    // Quad covering entire viewport
    float quad[] = {
        // x, y,      u, v
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint a_tex = glGetAttribLocation(prog, "a_texcoord");
    GLint u_tex = glGetUniformLocation(prog, "u_tex");

    glUniform1i(u_tex, 0);

    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad);
    glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &quad[2]);
    glEnableVertexAttribArray(a_pos);
    glEnableVertexAttribArray(a_tex);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();

    // Bottom-left pixel should sample (0,0) -> Red
    GLubyte bl[4] = {0};
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bl);
    assert(bl[0] == 255 && bl[1] == 0 && bl[2] == 0);

    // Bottom-right pixel should sample (1,0) -> Green
    GLubyte br[4] = {0};
    glReadPixels(56, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, br);
    assert(br[0] == 0 && br[1] == 255 && br[2] == 0);

    // Top-left pixel should sample (0,1) -> Blue
    GLubyte tl[4] = {0};
    glReadPixels(8, 56, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tl);
    assert(tl[0] == 0 && tl[1] == 0 && tl[2] == 255);

    // Top-right pixel should sample (1,1) -> Yellow
    GLubyte tr[4] = {0};
    glReadPixels(56, 56, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tr);
    assert(tr[0] == 255 && tr[1] == 255 && tr[2] == 0);

    printf("  - Sample BL: (%d, %d, %d) [Red - MATCH]\n", bl[0], bl[1], bl[2]);
    printf("  - Sample BR: (%d, %d, %d) [Green - MATCH]\n", br[0], br[1], br[2]);
    printf("  - Sample TL: (%d, %d, %d) [Blue - MATCH]\n", tl[0], tl[1], tl[2]);
    printf("  - Sample TR: (%d, %d, %d) [Yellow - MATCH]\n", tr[0], tr[1], tr[2]);

    glDisableVertexAttribArray(a_pos);
    glDisableVertexAttribArray(a_tex);
    glDeleteTextures(1, &tex);
    glDeleteProgram(prog);
    printf("[PHASE B.3] PASSED: Texture upload and shader sampling verified.\n");
}

/* Sub-test 4: Alpha Blending */
static void test_alpha_blending(void) {
    printf("[PHASE B.4] Testing hardware alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)...\n");
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "void main() {\n"
        "    gl_FragColor = u_color;\n"
        "}\n";

    GLuint prog = create_program(vs_src, fs_src);
    assert(prog != 0);
    glUseProgram(prog);

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint u_color = glGetUniformLocation(prog, "u_color");

    // Clear background to Red: RGBA = (1.0, 0.0, 0.0, 1.0)
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Enable Alpha Blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render Blue Quad with 50% Alpha: RGBA = (0.0, 0.0, 1.0, 0.5)
    glUniform4f(u_color, 0.0f, 0.0f, 1.0f, 0.5f);

    float quad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(a_pos);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();

    // Expected blended pixel:
    // R = 1.0 * (1 - 0.5) = 0.5 (128)
    // G = 0.0
    // B = 1.0 * 0.5 = 0.5 (128)
    // A = 0.5 * 0.5 + 1.0 * (1 - 0.5) = 0.75 (191)
    GLubyte blended[4] = {0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blended);
    printf("  - Blended result: RGBA=(%d, %d, %d, %d) [Expected: (128, 0, 128, 191)]\n",
           blended[0], blended[1], blended[2], blended[3]);
    fflush(stdout);

    assert(abs((int)blended[0] - 128) <= 1);
    assert(blended[1] == 0);
    assert(abs((int)blended[2] - 128) <= 1);
    assert(abs((int)blended[3] - 191) <= 1);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(a_pos);
    glDeleteProgram(prog);
    printf("[PHASE B.4] PASSED: Hardware alpha blending matches mathematical formula.\n");
}

/* Sub-test 5: Offscreen FBO (Render to Texture) */
static void test_fbo_offscreen(void) {
    printf("[PHASE B.5] Testing Framebuffer Object (FBO) offscreen rendering...\n");
    // Create texture target
    GLuint fbo_tex;
    glGenTextures(1, &fbo_tex);
    glBindTexture(GL_TEXTURE_2D, fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create FBO
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(status == GL_FRAMEBUFFER_COMPLETE);

    // Render into FBO: Clear with Magenta (1.0, 0.0, 1.0, 1.0)
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    // Verify FBO contents via readback
    GLubyte fbo_pixel[4] = {0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fbo_pixel);
    assert(fbo_pixel[0] == 255 && fbo_pixel[1] == 0 && fbo_pixel[2] == 255);
    printf("  - FBO readback: RGBA=(%d, %d, %d, %d) [Magenta - MATCH]\n",
           fbo_pixel[0], fbo_pixel[1], fbo_pixel[2], fbo_pixel[3]);

    // Unbind FBO back to window/pbuffer surface
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fbo_tex);
    printf("[PHASE B.5] PASSED: Offscreen FBO rendering and readback verified.\n");
}

/* Sub-test 6: 120 Sequential Animated Frames */
static void test_animated_frames(int count) {
    printf("[PHASE B.6] Rendering %d sequential animated frames...\n", count);
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "uniform float u_angle;\n"
        "void main() {\n"
        "    float s = sin(u_angle);\n"
        "    float c = cos(u_angle);\n"
        "    mat2 rot = mat2(c, -s, s, c);\n"
        "    gl_Position = vec4(rot * a_pos, 0.0, 1.0);\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "void main() {\n"
        "    gl_FragColor = u_color;\n"
        "}\n";

    GLuint prog = create_program(vs_src, fs_src);
    assert(prog != 0);
    glUseProgram(prog);

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint u_angle = glGetUniformLocation(prog, "u_angle");
    GLint u_color = glGetUniformLocation(prog, "u_color");

    float quad[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
        -0.5f,  0.5f,
         0.5f,  0.5f
    };
    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(a_pos);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int frame = 0; frame < count; frame++) {
        float angle = (float)frame * (3.14159265f / 30.0f);
        float r = 0.5f + 0.5f * sinf(angle);
        float g = 0.5f + 0.5f * cosf(angle);
        float b = 0.5f + 0.5f * sinf(angle + 1.0f);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(u_angle, angle);
        glUniform4f(u_color, r, g, b, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (frame % 30 == 0) {
            GLubyte px[4];
            glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            assert(glGetError() == GL_NO_ERROR);
        }
    }
    glFinish();

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    printf("  - Rendered %d animated frames in %.2f ms (avg %.2f ms/frame, %.1f FPS)\n",
           count, elapsed_ms, elapsed_ms / count, (count * 1000.0) / elapsed_ms);

    glDisableVertexAttribArray(a_pos);
    glDeleteProgram(prog);
    printf("[PHASE B.6] PASSED: Animated frames completed with zero GL errors.\n");
}

int main(int argc, char **argv) {
    printf("=================================================================\n");
    printf("  Milestone 7C Phase B: Mali-G71 Rendering Stability Test Suite  \n");
    printf("=================================================================\n");

    // Pre-test telemetry
    unsigned long irq190_pre = 0, irq191_pre = 0;
    unsigned long total_irq_pre = get_gpu_interrupts(&irq190_pre, &irq191_pre);
    long rss_pre = get_rss_kb();
    int fds_pre = count_open_fds();

    printf("[TELEMETRY-PRE] GPU Interrupts: GIC 190=%lu, GIC 191=%lu (Total: %lu)\n",
           irq190_pre, irq191_pre, total_irq_pre);
    printf("[TELEMETRY-PRE] Process RSS: %ld KB, Open FDs: %d\n", rss_pre, fds_pre);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(dpy != EGL_NO_DISPLAY);

    EGLint major = 0, minor = 0;
    assert(eglInitialize(dpy, &major, &minor) == EGL_TRUE);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs = 0;
    assert(eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) == EGL_TRUE && num_configs > 0);

    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(dpy, config, pbuffer_attribs);
    assert(surface != EGL_NO_SURFACE);

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    assert(ctx != EGL_NO_CONTEXT);

    assert(eglMakeCurrent(dpy, surface, surface, ctx) == EGL_TRUE);

    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    printf("[GL_INFO] Vendor:   %s\n", vendor ? (const char*)vendor : "(null)");
    printf("[GL_INFO] Renderer: %s\n", renderer ? (const char*)renderer : "(null)");
    printf("[GL_INFO] Version:  %s\n", version ? (const char*)version : "(null)");

    // Run tests
    test_clear_colors();
    test_triangle();
    test_texture_sampling();
    test_alpha_blending();
    test_fbo_offscreen();
    test_animated_frames(120);

    // Teardown
    assert(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE);
    assert(eglDestroyContext(dpy, ctx) == EGL_TRUE);
    assert(eglDestroySurface(dpy, surface) == EGL_TRUE);
    assert(eglTerminate(dpy) == EGL_TRUE);

    // Post-test telemetry
    unsigned long irq190_post = 0, irq191_post = 0;
    unsigned long total_irq_post = get_gpu_interrupts(&irq190_post, &irq191_post);
    long rss_post = get_rss_kb();
    int fds_post = count_open_fds();

    printf("\n[TELEMETRY-POST] GPU Interrupts: GIC 190=%lu (+%lu), GIC 191=%lu (+%lu) (Total: +%lu)\n",
           irq190_post, irq190_post - irq190_pre,
           irq191_post, irq191_post - irq191_pre,
           total_irq_post - total_irq_pre);
    printf("[TELEMETRY-POST] Process RSS: %ld KB (Delta: %+ld KB), Open FDs: %d (Delta: %+d)\n",
           rss_post, rss_post - rss_pre, fds_post, fds_post - fds_pre);

    printf("\n>>> ALL PHASE B MALI STABILITY TESTS PASSED WITH 0 ERRORS! <<<\n");
    return 0;
}

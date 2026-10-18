#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

int main(int argc, char **argv) {
    printf("[MALI_TEST] Starting offscreen Mali-G71 hardware test via libhybris...\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[MALI_TEST] eglGetDisplay failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglGetDisplay returned %p\n", (void*)dpy);

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "[MALI_TEST] eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglInitialize succeeded: EGL %d.%d\n", major, minor);

    const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    printf("[MALI_TEST] EGL client extensions: %s\n", client_exts ? client_exts : "(none)");
    const char *dpy_exts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("[MALI_TEST] EGL display extensions: %s\n", dpy_exts ? dpy_exts : "(none)");

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
    if (!eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
        fprintf(stderr, "[MALI_TEST] eglChooseConfig failed: num_configs=%d, error=0x%x\n", num_configs, eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglChooseConfig found %d matching config(s), chosen config=%p\n", num_configs, (void*)config);

    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(dpy, config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[MALI_TEST] eglCreatePbufferSurface failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglCreatePbufferSurface succeeded: surface=%p\n", (void*)surface);

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[MALI_TEST] eglCreateContext failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglCreateContext succeeded: ctx=%p\n", (void*)ctx);

    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        fprintf(stderr, "[MALI_TEST] eglMakeCurrent failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[MALI_TEST] eglMakeCurrent succeeded!\n");

    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *gl_exts = glGetString(GL_EXTENSIONS);

    printf("[MALI_TEST] GL_VENDOR:   %s\n", vendor ? (const char*)vendor : "(null)");
    printf("[MALI_TEST] GL_RENDERER: %s\n", renderer ? (const char*)renderer : "(null)");
    printf("[MALI_TEST] GL_VERSION:  %s\n", version ? (const char*)version : "(null)");
    printf("[MALI_TEST] GL_EXTENSIONS: %s\n", gl_exts ? (const char*)gl_exts : "(null)");

    // Render a clear color: RGBA = (51, 102, 153, 255) -> (0.2f, 0.4f, 0.6f, 1.0f)
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[MALI_TEST] glReadPixels at (32, 32): RGBA = (%u, %u, %u, %u)\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);

    // Check tolerance (floating point conversion)
    int r_diff = abs((int)pixel[0] - 51);
    int g_diff = abs((int)pixel[1] - 102);
    int b_diff = abs((int)pixel[2] - 153);
    int a_diff = abs((int)pixel[3] - 255);

    if (r_diff <= 2 && g_diff <= 2 && b_diff <= 2 && a_diff == 0) {
        printf("[MALI_TEST] SUCCESS: Pixel values match expected GPU rendering output!\n");
    } else {
        fprintf(stderr, "[MALI_TEST] FAILURE: Pixel values mismatch expected (51, 102, 153, 255)\n");
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
    printf("[MALI_TEST] Clean teardown complete.\n");
    return 0;
}

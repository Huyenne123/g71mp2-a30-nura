#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/gralloc/gralloc.h>
#include <system/graphics.h>
#include <system/window.h>
#include "/root/libhybris/hybris/egl/platforms/hwcomposer/hwcomposer.h"

static int g_present_called = 0;
static int g_buffer_fd = -1;
static int g_fence_fd = -1;

static void test_present_cb(void *user_data, struct ANativeWindow *win, struct ANativeWindowBuffer *buf) {
    g_present_called++;
    printf("[HWC_WIN_TEST] Present callback invoked! Call #%d\n", g_present_called);
    printf("[HWC_WIN_TEST] Buffer %p: %dx%d, stride=%d, format=0x%x, handle=%p\n",
           buf, buf->width, buf->height, buf->stride, buf->format, (void*)buf->handle);
    if (buf->handle) {
        printf("[HWC_WIN_TEST] handle: numFds=%d, numInts=%d\n", buf->handle->numFds, buf->handle->numInts);
        for (int i = 0; i < buf->handle->numFds; i++) {
            printf("[HWC_WIN_TEST] fd[%d]=%d\n", i, buf->handle->data[i]);
        }
        g_buffer_fd = buf->handle->data[0];
    }
    int fence = HWCNativeBufferGetFence(buf);
    printf("[HWC_WIN_TEST] Acquire fence FD: %d\n", fence);
    g_fence_fd = fence;
    fflush(stdout);
}

int main(int argc, char **argv) {
    printf("[HWC_WIN_TEST] Starting Mali-G71 HWComposer Window Test...\n");
    fflush(stdout);

    hybris_gralloc_initialize(0);
    printf("[STEP 1] hybris_gralloc_initialize done\n"); fflush(stdout);

    struct ANativeWindow *win = HWCNativeWindowCreate(1080, 2340, HAL_PIXEL_FORMAT_RGBA_8888, test_present_cb, NULL);
    if (!win) {
        fprintf(stderr, "[HWC_WIN_TEST] HWCNativeWindowCreate failed\n");
        return 1;
    }
    printf("[STEP 2] HWCNativeWindowCreate succeeded: %p\n", (void*)win); fflush(stdout);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[HWC_WIN_TEST] eglGetDisplay failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[STEP 3] eglGetDisplay succeeded: %p\n", (void*)dpy); fflush(stdout);

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "[HWC_WIN_TEST] eglInitialize failed: 0x%x\n", eglGetError());
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
        fprintf(stderr, "[HWC_WIN_TEST] eglChooseConfig failed: num_configs=%d, error=0x%x\n", num_configs, eglGetError());
        return 1;
    }
    printf("[STEP 5] eglChooseConfig succeeded: config=%p\n", (void*)config); fflush(stdout);

    printf("[STEP 6] Calling eglCreateWindowSurface...\n"); fflush(stdout);
    EGLSurface surface = eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)win, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[HWC_WIN_TEST] eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[STEP 6] eglCreateWindowSurface succeeded: surface=%p\n", (void*)surface); fflush(stdout);

    printf("[STEP 7] Calling eglCreateContext...\n"); fflush(stdout);
    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[HWC_WIN_TEST] eglCreateContext failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[STEP 7] eglCreateContext succeeded: ctx=%p\n", (void*)ctx); fflush(stdout);

    printf("[STEP 8] Calling eglMakeCurrent...\n"); fflush(stdout);
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        fprintf(stderr, "[HWC_WIN_TEST] eglMakeCurrent failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("[STEP 8] eglMakeCurrent succeeded!\n"); fflush(stdout);

    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    printf("[HWC_WIN_TEST] GL_VENDOR:   %s\n", vendor ? (const char*)vendor : "(null)");
    printf("[HWC_WIN_TEST] GL_RENDERER: %s\n", renderer ? (const char*)renderer : "(null)");
    fflush(stdout);

    // Render clear color: Red
    printf("[STEP 9] Calling glClearColor & glClear...\n"); fflush(stdout);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("[STEP 9] glClear returned!\n"); fflush(stdout);

    printf("[STEP 10] Calling eglSwapBuffers...\n"); fflush(stdout);
    EGLBoolean swap_ret = eglSwapBuffers(dpy, surface);
    printf("[STEP 10] eglSwapBuffers returned: %d\n", swap_ret); fflush(stdout);

    if (g_present_called > 0) {
        printf("[HWC_WIN_TEST] SUCCESS: Present callback successfully delivered buffer (fd=%d, fence=%d)!\n",
               g_buffer_fd, g_fence_fd);
    } else {
        fprintf(stderr, "[HWC_WIN_TEST] FAILURE: Present callback was not called during eglSwapBuffers!\n");
    }
    fflush(stdout);

    printf("[TEARDOWN 1] eglMakeCurrent(NULL)...\n"); fflush(stdout);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    printf("[TEARDOWN 2] eglDestroyContext...\n"); fflush(stdout);
    eglDestroyContext(dpy, ctx);
    printf("[TEARDOWN 3] eglDestroySurface...\n"); fflush(stdout);
    eglDestroySurface(dpy, surface);
    printf("[TEARDOWN 4] eglTerminate...\n"); fflush(stdout);
    eglTerminate(dpy);
    printf("[HWC_WIN_TEST] Clean teardown complete.\n");
    fflush(stdout);
    return 0;
}

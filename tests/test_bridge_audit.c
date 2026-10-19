#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* Test 1: Pthread Key Index 0 validation */
static int g_destructor_called = 0;
static void *g_destructor_arg = NULL;

static void test_key_destructor(void *val) {
    g_destructor_called++;
    g_destructor_arg = val;
    printf("[BRIDGE_AUDIT] Key destructor invoked with arg: %p\n", val);
}

static void test_pthread_key_zero(void) {
    printf("\n=== TEST 1: Pthread Key Index 0 & Value Storage ===\n");
    pthread_key_t key;
    int ret = pthread_key_create(&key, NULL);
    assert(ret == 0);
    printf("[BRIDGE_AUDIT] Created pthread key: %u\n", (unsigned int)key);

    void *magic_ptr = (void *)0xdeadbeefcafebabeULL;
    ret = pthread_setspecific(key, magic_ptr);
    assert(ret == 0);

    void *retrieved = pthread_getspecific(key);
    printf("[BRIDGE_AUDIT] Retrieved value for key %u: %p (expected %p)\n",
           (unsigned int)key, retrieved, magic_ptr);
    assert(retrieved == magic_ptr);

    ret = pthread_setspecific(key, NULL);
    assert(ret == 0);
    assert(pthread_getspecific(key) == NULL);

    pthread_key_delete(key);
    printf("[BRIDGE_AUDIT] TEST 1 PASSED: Pthread key index 0 and specific data work correctly.\n");
}

/* Test 2: Pthread Key Destructors */
static void *thread_destructor_worker(void *arg) {
    pthread_key_t *pkey = (pthread_key_t *)arg;
    void *val = (void *)0x12345678ULL;
    pthread_setspecific(*pkey, val);
    printf("[BRIDGE_AUDIT] Worker thread set specific value %p for key %u\n", val, (unsigned int)*pkey);
    return NULL;
}

static void test_pthread_key_destructors(void) {
    printf("\n=== TEST 2: Pthread Key Destructors on Thread Termination ===\n");
    g_destructor_called = 0;
    g_destructor_arg = NULL;

    pthread_key_t key;
    int ret = pthread_key_create(&key, test_key_destructor);
    assert(ret == 0);

    pthread_t thread;
    ret = pthread_create(&thread, NULL, thread_destructor_worker, &key);
    assert(ret == 0);

    ret = pthread_join(thread, NULL);
    assert(ret == 0);

    printf("[BRIDGE_AUDIT] Thread joined. g_destructor_called=%d, g_destructor_arg=%p\n",
           g_destructor_called, g_destructor_arg);
    assert(g_destructor_called == 1);
    assert(g_destructor_arg == (void *)0x12345678ULL);

    pthread_key_delete(key);
    printf("[BRIDGE_AUDIT] TEST 2 PASSED: Pthread key destructor executed with expected argument.\n");
}

/* Test 3 & 4: Multiple Threads Creating/Destroying EGL Contexts & Concurrent Rendering */
#define NUM_THREADS 4
#define ITERATIONS_PER_THREAD 20

typedef struct {
    int thread_id;
    EGLDisplay dpy;
    EGLConfig config;
    int success;
    float clear_color[4];
} ThreadContextArg;

static void *thread_egl_worker(void *data) {
    ThreadContextArg *arg = (ThreadContextArg *)data;
    arg->success = 0;
    printf("[THREAD %d] Worker started.\n", arg->thread_id); fflush(stdout);

    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };

    printf("[THREAD %d] Calling eglCreatePbufferSurface...\n", arg->thread_id); fflush(stdout);
    EGLSurface surface = eglCreatePbufferSurface(arg->dpy, arg->config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[THREAD %d] eglCreatePbufferSurface failed: 0x%x\n", arg->thread_id, eglGetError());
        return NULL;
    }

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    printf("[THREAD %d] Calling eglCreateContext...\n", arg->thread_id); fflush(stdout);
    EGLContext ctx = eglCreateContext(arg->dpy, arg->config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[THREAD %d] eglCreateContext failed: 0x%x\n", arg->thread_id, eglGetError());
        eglDestroySurface(arg->dpy, surface);
        return NULL;
    }

    printf("[THREAD %d] Calling eglMakeCurrent...\n", arg->thread_id); fflush(stdout);
    if (!eglMakeCurrent(arg->dpy, surface, surface, ctx)) {
        fprintf(stderr, "[THREAD %d] eglMakeCurrent failed: 0x%x\n", arg->thread_id, eglGetError());
        eglDestroyContext(arg->dpy, ctx);
        eglDestroySurface(arg->dpy, surface);
        return NULL;
    }

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        float r = arg->clear_color[0];
        float g = arg->clear_color[1];
        float b = arg->clear_color[2];
        float a = arg->clear_color[3];

        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);

        GLubyte pixel[4] = {0, 0, 0, 0};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

        int exp_r = (int)(r * 255.0f + 0.5f);
        int exp_g = (int)(g * 255.0f + 0.5f);
        int exp_b = (int)(b * 255.0f + 0.5f);
        int exp_a = (int)(a * 255.0f + 0.5f);

        if (abs((int)pixel[0] - exp_r) > 1 ||
            abs((int)pixel[1] - exp_g) > 1 ||
            abs((int)pixel[2] - exp_b) > 1 ||
            abs((int)pixel[3] - exp_a) > 1) {
            fprintf(stderr, "[THREAD %d] Iter %d color mismatch: got (%d,%d,%d,%d) exp (%d,%d,%d,%d)\n",
                    arg->thread_id, i, pixel[0], pixel[1], pixel[2], pixel[3], exp_r, exp_g, exp_b, exp_a);
            eglMakeCurrent(arg->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(arg->dpy, ctx);
            eglDestroySurface(arg->dpy, surface);
            return NULL;
        }
    }

    printf("[THREAD %d] Done rendering loop. Calling eglMakeCurrent(NULL)...\n", arg->thread_id);
    fflush(stdout);
    eglMakeCurrent(arg->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    printf("[THREAD %d] Calling eglDestroyContext...\n", arg->thread_id);
    fflush(stdout);
    eglDestroyContext(arg->dpy, ctx);

    printf("[THREAD %d] Calling eglDestroySurface...\n", arg->thread_id);
    fflush(stdout);
    eglDestroySurface(arg->dpy, surface);

    arg->success = 1;
    printf("[THREAD %d] Worker finished completely.\n", arg->thread_id);
    fflush(stdout);
    return NULL;
}

static void test_multithreaded_egl_and_concurrent_rendering(void) {
    printf("\n=== TEST 3 & 4: Multi-Threaded EGL Contexts & Concurrent Rendering ===\n");
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(dpy != EGL_NO_DISPLAY);

    EGLint major = 0, minor = 0;
    assert(eglInitialize(dpy, &major, &minor) == EGL_TRUE);
    printf("[BRIDGE_AUDIT] EGL initialized: version %d.%d\n", major, minor);

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

    pthread_t threads[NUM_THREADS];
    ThreadContextArg args[NUM_THREADS];

    float test_colors[NUM_THREADS][4] = {
        {1.0f, 0.0f, 0.0f, 1.0f}, // Red
        {0.0f, 1.0f, 0.0f, 1.0f}, // Green
        {0.0f, 0.0f, 1.0f, 1.0f}, // Blue
        {1.0f, 1.0f, 0.0f, 1.0f}  // Yellow
    };

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].dpy = dpy;
        args[i].config = config;
        args[i].success = 0;
        memcpy(args[i].clear_color, test_colors[i], sizeof(float) * 4);
        int ret = pthread_create(&threads[i], NULL, thread_egl_worker, &args[i]);
        assert(ret == 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        printf("[MAIN] Joining thread %d...\n", i); fflush(stdout);
        pthread_join(threads[i], NULL);
        printf("[MAIN] Thread %d joined successfully.\n", i); fflush(stdout);
        assert(args[i].success == 1);
    }

    printf("[MAIN] Calling eglTerminate...\n"); fflush(stdout);
    eglTerminate(dpy);
    printf("[MAIN] eglTerminate completed.\n"); fflush(stdout);
    printf("[BRIDGE_AUDIT] TEST 3 & 4 PASSED: Concurrent multi-threaded EGL contexts verified.\n");
    fflush(stdout);
    printf("[MAIN] Exiting test_multithreaded_egl_and_concurrent_rendering()...\n");
    fflush(stdout);
}

/* Test 5: Repeated EGL init/use/terminate cycles */
static void test_repeated_egl_lifecycle(int cycles) {
    printf("[MAIN] Entering test_repeated_egl_lifecycle(%d)...\n", cycles);
    fflush(stdout);
    printf("\n=== TEST 5: Repeated EGL Init / Render / Terminate Cycles (%d cycles) ===\n", cycles);
    fflush(stdout);

    for (int cycle = 1; cycle <= cycles; cycle++) {
        printf("[CYCLE %d] Calling eglGetDisplay...\n", cycle); fflush(stdout);
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        printf("[CYCLE %d] eglGetDisplay returned: %p\n", cycle, (void*)dpy); fflush(stdout);
        assert(dpy != EGL_NO_DISPLAY);

        EGLint major = 0, minor = 0;
        printf("[CYCLE %d] Calling eglInitialize...\n", cycle); fflush(stdout);
        if (!eglInitialize(dpy, &major, &minor)) {
            fprintf(stderr, "[CYCLE %d] eglInitialize failed: 0x%x\n", cycle, eglGetError());
            assert(0);
        }
        printf("[CYCLE %d] eglInitialize returned: %d.%d\n", cycle, major, minor); fflush(stdout);

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
        printf("[CYCLE %d] Calling eglChooseConfig...\n", cycle); fflush(stdout);
        assert(eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) == EGL_TRUE && num_configs > 0);
        printf("[CYCLE %d] eglChooseConfig returned config: %p\n", cycle, (void*)config); fflush(stdout);

        EGLint pbuffer_attribs[] = {
            EGL_WIDTH, 32,
            EGL_HEIGHT, 32,
            EGL_NONE
        };
        printf("[CYCLE %d] Calling eglCreatePbufferSurface...\n", cycle); fflush(stdout);
        EGLSurface surface = eglCreatePbufferSurface(dpy, config, pbuffer_attribs);
        printf("[CYCLE %d] eglCreatePbufferSurface returned: %p\n", cycle, (void*)surface); fflush(stdout);
        assert(surface != EGL_NO_SURFACE);

        EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        printf("[CYCLE %d] Calling eglCreateContext...\n", cycle); fflush(stdout);
        EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
        printf("[CYCLE %d] eglCreateContext returned: %p\n", cycle, (void*)ctx); fflush(stdout);
        assert(ctx != EGL_NO_CONTEXT);

        printf("[CYCLE %d] Calling eglMakeCurrent...\n", cycle); fflush(stdout);
        assert(eglMakeCurrent(dpy, surface, surface, ctx) == EGL_TRUE);
        printf("[CYCLE %d] eglMakeCurrent succeeded.\n", cycle); fflush(stdout);

        printf("[CYCLE %d] Rendering & readback...\n", cycle); fflush(stdout);
        glClearColor(0.25f, 0.50f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLubyte pixel[4] = {0, 0, 0, 0};
        glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        assert(abs((int)pixel[0] - 64) <= 1);
        assert(abs((int)pixel[1] - 128) <= 1);
        assert(abs((int)pixel[2] - 191) <= 1);

        printf("[CYCLE %d] Calling eglMakeCurrent(NULL)...\n", cycle); fflush(stdout);
        assert(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE);
        printf("[CYCLE %d] Calling eglDestroyContext...\n", cycle); fflush(stdout);
        assert(eglDestroyContext(dpy, ctx) == EGL_TRUE);
        printf("[CYCLE %d] Calling eglDestroySurface...\n", cycle); fflush(stdout);
        assert(eglDestroySurface(dpy, surface) == EGL_TRUE);
        printf("[CYCLE %d] Calling eglTerminate...\n", cycle); fflush(stdout);
        assert(eglTerminate(dpy) == EGL_TRUE);
        printf("[CYCLE %d] eglTerminate done.\n", cycle); fflush(stdout);

        if (cycle % 2 == 0 || cycle == cycles) {
            printf("[BRIDGE_AUDIT] Cycle %d/%d completed successfully.\n", cycle, cycles);
        }
    }

    printf("[BRIDGE_AUDIT] TEST 5 PASSED: Repeated EGL initialization and teardown verified.\n");
}

int main(int argc, char **argv) {
    printf("=================================================================\n");
    printf("  Milestone 7C Phase A: Libhybris musl/Bionic Bridge Audit Test  \n");
    printf("=================================================================\n");

    test_pthread_key_zero();
    test_pthread_key_destructors();
    test_multithreaded_egl_and_concurrent_rendering();
    test_repeated_egl_lifecycle(10);

    printf("\n>>> ALL PHASE A BRIDGE AUDIT UNIT TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}

/*
 * tests/egl_offscreen_test.c - Milestone 3A: Proprietary EGL Offscreen Validation
 *
 * Minimal standalone AArch64 test harness validating headless/offscreen EGL,
 * OpenGL ES 2.0/3.0 context creation, Pbuffer allocation, shader rendering,
 * hardware job submission, and pixel readback using the proprietary
 * Samsung/ARM Mali-G71 r26p0 driver.
 *
 * Targets /vendor/lib64/egl/libGLES_mali.so on physical target (Samsung Galaxy A30).
 */

typedef unsigned long  size_t;
typedef signed int     int32_t;
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
typedef unsigned char  GLubyte;
#define NULL ((void*)0)

/* Bionic standard prototypes */
extern void __libc_init(void *raw_args, void (*onexit)(void), int (*main)(int, char**, char**), void *params);
extern int printf(const char *fmt, ...);
extern int snprintf(char *str, size_t size, const char *fmt, ...);
extern void *malloc(size_t size);
extern void free(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *dlopen(const char *filename, int flag);
extern char *dlerror(void);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern long read(int fd, void *buf, size_t count);
extern int strcmp(const char *s1, const char *s2);
extern char *strstr(const char *haystack, const char *needle);

#define RTLD_NOW 2
#define O_RDONLY 0

/* EGL Types */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLNativeDisplayType;
typedef void *EGLClientBuffer;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef int32_t EGLint;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_CONTEXT      ((EGLContext)0)
#define EGL_NO_DISPLAY      ((EGLDisplay)0)
#define EGL_NO_SURFACE      ((EGLSurface)0)
#define EGL_FALSE           0
#define EGL_TRUE            1

/* EGL Config Attributes */
#define EGL_BUFFER_SIZE         0x3020
#define EGL_ALPHA_SIZE          0x3021
#define EGL_BLUE_SIZE           0x3022
#define EGL_GREEN_SIZE          0x3023
#define EGL_RED_SIZE            0x3024
#define EGL_DEPTH_SIZE          0x3025
#define EGL_STENCIL_SIZE        0x3026
#define EGL_CONFIG_CAVEAT       0x3027
#define EGL_CONFIG_ID           0x3028
#define EGL_LEVEL               0x3029
#define EGL_MAX_PBUFFER_HEIGHT  0x302A
#define EGL_MAX_PBUFFER_PIXELS  0x302B
#define EGL_MAX_PBUFFER_WIDTH   0x302C
#define EGL_NATIVE_RENDERABLE   0x302D
#define EGL_NATIVE_VISUAL_ID    0x302E
#define EGL_NATIVE_VISUAL_TYPE  0x302F
#define EGL_SAMPLES             0x3031
#define EGL_SAMPLE_BUFFERS      0x3032
#define EGL_SURFACE_TYPE        0x3033
#define EGL_NONE                0x3038
#define EGL_RENDERABLE_TYPE     0x3040

#define EGL_PBUFFER_BIT         0x0001
#define EGL_WINDOW_BIT          0x0004
#define EGL_OPENGL_ES_BIT       0x0001
#define EGL_OPENGL_ES2_BIT      0x0004
#define EGL_OPENGL_ES3_BIT      0x00000040

/* EGL Context and Surface Attributes */
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH                  0x3057
#define EGL_HEIGHT                 0x3056
#define EGL_OPENGL_ES_API          0x30A0

/* EGL Queries */
#define EGL_VENDOR      0x3053
#define EGL_VERSION     0x3054
#define EGL_EXTENSIONS  0x3055
#define EGL_CLIENT_APIS 0x308D

/* EGL Platform Tokens */
#define EGL_PLATFORM_DEVICE_EXT        0x313F
#define EGL_PLATFORM_SURFACELESS_MESA  0x31DC
#define EGL_PLATFORM_ANDROID_KHR       0x3141

/* OpenGL ES Types */
typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef char           GLchar;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_NO_ERROR         0
#define GL_VENDOR           0x1F00
#define GL_RENDERER         0x1F01
#define GL_VERSION          0x1F02
#define GL_EXTENSIONS       0x1F03
#define GL_RGBA             0x1908
#define GL_UNSIGNED_BYTE    0x1401
#define GL_FLOAT            0x1406
#define GL_FALSE            0
#define GL_TRUE             1
#define GL_VERTEX_SHADER    0x8B31
#define GL_FRAGMENT_SHADER  0x8B30
#define GL_COMPILE_STATUS   0x8B81
#define GL_LINK_STATUS      0x8B82
#define GL_TRIANGLES        0x0004

/* EGL Function Pointer Types */
typedef EGLDisplay (*PFNEGLGETDISPLAYPROC)(EGLNativeDisplayType display_id);
typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum platform, void *native_display, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLINITIALIZEPROC)(EGLDisplay dpy, EGLint *major, EGLint *minor);
typedef EGLBoolean (*PFNEGLTERMINATEPROC)(EGLDisplay dpy);
typedef const char *(*PFNEGLQUERYSTRINGPROC)(EGLDisplay dpy, EGLint name);
typedef EGLBoolean (*PFNEGLGETCONFIGSPROC)(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLBoolean (*PFNEGLCHOOSECONFIGPROC)(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLBoolean (*PFNEGLGETCONFIGATTRIBPROC)(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);
typedef EGLSurface (*PFNEGLCREATEPBUFFERSURFACEPROC)(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYSURFACEPROC)(EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean (*PFNEGLBINDAPIPROC)(EGLenum api);
typedef EGLContext (*PFNEGLCREATECONTEXTPROC)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYCONTEXTPROC)(EGLDisplay dpy, EGLContext ctx);
typedef EGLBoolean (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef EGLint     (*PFNEGLGETERRORPROC)(void);
typedef void      *(*PFNEGLGETPROCADDRESSPROC)(const char *procname);

/* GL Function Pointer Types */
typedef const GLubyte *(*PFNGLGETSTRINGPROC)(GLenum name);
typedef void (*PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (*PFNGLCLEARPROC)(GLbitfield mask);
typedef void (*PFNGLFINISHPROC)(void);
typedef void (*PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
typedef GLenum (*PFNGLGETERRORPROC)(void);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (*PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef GLint (*PFNGLGETATTRIBLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);

/* EGL Error string helper */
static const char *egl_error_to_string(EGLint err) {
    switch (err) {
        case 0x3000: return "EGL_SUCCESS";
        case 0x3001: return "EGL_NOT_INITIALIZED";
        case 0x3002: return "EGL_BAD_ACCESS";
        case 0x3003: return "EGL_BAD_ALLOC";
        case 0x3004: return "EGL_BAD_ATTRIBUTE";
        case 0x3005: return "EGL_BAD_CONFIG";
        case 0x3006: return "EGL_BAD_CONTEXT";
        case 0x3007: return "EGL_BAD_CURRENT_SURFACE";
        case 0x3008: return "EGL_BAD_DISPLAY";
        case 0x3009: return "EGL_BAD_MATCH";
        case 0x300A: return "EGL_BAD_NATIVE_PIXMAP";
        case 0x300B: return "EGL_BAD_NATIVE_WINDOW";
        case 0x300C: return "EGL_BAD_PARAMETER";
        case 0x300D: return "EGL_BAD_SURFACE";
        default:     return "UNKNOWN_EGL_ERROR";
    }
}

/* Query Mali interrupts from /proc/interrupts */
static long get_mali_job_interrupts(void) {
    int fd = open("/proc/interrupts", O_RDONLY);
    if (fd < 0) return -1;
    char buf[32768];
    long total = 0;
    while (total < sizeof(buf) - 1) {
        long n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    if (total <= 0) return -1;
    buf[total] = '\0';

    char *line = strstr(buf, "11500000.mali");
    if (!line) return -1;
    
    /* Find beginning of line */
    while (line > buf && *(line - 1) != '\n') line--;
    
    /* Parse first number after line header (e.g. " 57: ") */
    char *p = line;
    while (*p && *p != ':') p++;
    if (!*p) return -1;
    p++; /* skip ':' */

    long sum = 0;
    while (*p && *p != 'G' && *p != '\n') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p >= '0' && *p <= '9') {
            long val = 0;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
            }
            sum += val;
        } else {
            p++;
        }
    }
    return sum;
}

int main(int argc, char **argv, char **envp) {
    printf("================================================================\n");
    printf(" Milestone 3A: Proprietary Mali-G71 EGL Offscreen Validation\n");
    printf("================================================================\n");

    const char *driver_path = "/vendor/lib64/egl/libGLES_mali.so";
    printf("[*] Loading proprietary driver: %s\n", driver_path);
    void *mali_handle = dlopen(driver_path, RTLD_NOW);
    if (!mali_handle) {
        printf("[-] FATAL: dlopen(%s) failed: %s\n", driver_path, dlerror());
        return 1;
    }
    printf("[+] Successfully loaded %s at %p\n", driver_path, mali_handle);

    /* Resolve EGL functions */
    PFNEGLGETPROCADDRESSPROC eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(mali_handle, "eglGetProcAddress");
    PFNEGLGETDISPLAYPROC eglGetDisplay = (PFNEGLGETDISPLAYPROC)dlsym(mali_handle, "eglGetDisplay");
    PFNEGLINITIALIZEPROC eglInitialize = (PFNEGLINITIALIZEPROC)dlsym(mali_handle, "eglInitialize");
    PFNEGLTERMINATEPROC eglTerminate = (PFNEGLTERMINATEPROC)dlsym(mali_handle, "eglTerminate");
    PFNEGLQUERYSTRINGPROC eglQueryString = (PFNEGLQUERYSTRINGPROC)dlsym(mali_handle, "eglQueryString");
    PFNEGLGETCONFIGSPROC eglGetConfigs = (PFNEGLGETCONFIGSPROC)dlsym(mali_handle, "eglGetConfigs");
    PFNEGLCHOOSECONFIGPROC eglChooseConfig = (PFNEGLCHOOSECONFIGPROC)dlsym(mali_handle, "eglChooseConfig");
    PFNEGLGETCONFIGATTRIBPROC eglGetConfigAttrib = (PFNEGLGETCONFIGATTRIBPROC)dlsym(mali_handle, "eglGetConfigAttrib");
    PFNEGLCREATEPBUFFERSURFACEPROC eglCreatePbufferSurface = (PFNEGLCREATEPBUFFERSURFACEPROC)dlsym(mali_handle, "eglCreatePbufferSurface");
    PFNEGLDESTROYSURFACEPROC eglDestroySurface = (PFNEGLDESTROYSURFACEPROC)dlsym(mali_handle, "eglDestroySurface");
    PFNEGLBINDAPIPROC eglBindAPI = (PFNEGLBINDAPIPROC)dlsym(mali_handle, "eglBindAPI");
    PFNEGLCREATECONTEXTPROC eglCreateContext = (PFNEGLCREATECONTEXTPROC)dlsym(mali_handle, "eglCreateContext");
    PFNEGLDESTROYCONTEXTPROC eglDestroyContext = (PFNEGLDESTROYCONTEXTPROC)dlsym(mali_handle, "eglDestroyContext");
    PFNEGLMAKECURRENTPROC eglMakeCurrent = (PFNEGLMAKECURRENTPROC)dlsym(mali_handle, "eglMakeCurrent");
    PFNEGLGETERRORPROC eglGetError = (PFNEGLGETERRORPROC)dlsym(mali_handle, "eglGetError");

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = NULL;
    if (eglGetProcAddress) {
        eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    }

    /* Resolve GLES functions directly from driver */
    PFNGLGETSTRINGPROC glGetString = (PFNGLGETSTRINGPROC)dlsym(mali_handle, "glGetString");
    PFNGLVIEWPORTPROC glViewport = (PFNGLVIEWPORTPROC)dlsym(mali_handle, "glViewport");
    PFNGLCLEARCOLORPROC glClearColor = (PFNGLCLEARCOLORPROC)dlsym(mali_handle, "glClearColor");
    PFNGLCLEARPROC glClear = (PFNGLCLEARPROC)dlsym(mali_handle, "glClear");
    PFNGLFINISHPROC glFinish = (PFNGLFINISHPROC)dlsym(mali_handle, "glFinish");
    PFNGLREADPIXELSPROC glReadPixels = (PFNGLREADPIXELSPROC)dlsym(mali_handle, "glReadPixels");
    PFNGLGETERRORPROC glGetError = (PFNGLGETERRORPROC)dlsym(mali_handle, "glGetError");
    PFNGLCREATESHADERPROC glCreateShader = (PFNGLCREATESHADERPROC)dlsym(mali_handle, "glCreateShader");
    PFNGLSHADERSOURCEPROC glShaderSource = (PFNGLSHADERSOURCEPROC)dlsym(mali_handle, "glShaderSource");
    PFNGLCOMPILESHADERPROC glCompileShader = (PFNGLCOMPILESHADERPROC)dlsym(mali_handle, "glCompileShader");
    PFNGLGETSHADERIVPROC glGetShaderiv = (PFNGLGETSHADERIVPROC)dlsym(mali_handle, "glGetShaderiv");
    PFNGLCREATEPROGRAMPROC glCreateProgram = (PFNGLCREATEPROGRAMPROC)dlsym(mali_handle, "glCreateProgram");
    PFNGLATTACHSHADERPROC glAttachShader = (PFNGLATTACHSHADERPROC)dlsym(mali_handle, "glAttachShader");
    PFNGLLINKPROGRAMPROC glLinkProgram = (PFNGLLINKPROGRAMPROC)dlsym(mali_handle, "glLinkProgram");
    PFNGLGETPROGRAMIVPROC glGetProgramiv = (PFNGLGETPROGRAMIVPROC)dlsym(mali_handle, "glGetProgramiv");
    PFNGLUSEPROGRAMPROC glUseProgram = (PFNGLUSEPROGRAMPROC)dlsym(mali_handle, "glUseProgram");
    PFNGLDRAWARRAYSPROC glDrawArrays = (PFNGLDRAWARRAYSPROC)dlsym(mali_handle, "glDrawArrays");
    PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)dlsym(mali_handle, "glGetAttribLocation");
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)dlsym(mali_handle, "glVertexAttribPointer");
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)dlsym(mali_handle, "glEnableVertexAttribArray");

    /* Step 1: Platform Display probing */
    EGLDisplay dpy = EGL_NO_DISPLAY;

    printf("\n[*] Step 1: Testing Platform Surfaceless / Device extensions...\n");
    if (eglGetPlatformDisplayEXT) {
        printf("[*] Probing EGL_PLATFORM_SURFACELESS_MESA (0x31DC)...\n");
        dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
        if (dpy == EGL_NO_DISPLAY) {
            EGLint err = eglGetError();
            printf("[-] EGL_PLATFORM_SURFACELESS_MESA rejected (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        } else {
            printf("[+] EGL_PLATFORM_SURFACELESS_MESA accepted, dpy = %p\n", dpy);
        }

        if (dpy == EGL_NO_DISPLAY) {
            printf("[*] Probing EGL_PLATFORM_ANDROID_KHR (0x3141)...\n");
            dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANDROID_KHR, EGL_DEFAULT_DISPLAY, NULL);
            if (dpy == EGL_NO_DISPLAY) {
                EGLint err = eglGetError();
                printf("[-] EGL_PLATFORM_ANDROID_KHR rejected (Error 0x%x: %s)\n", err, egl_error_to_string(err));
            } else {
                printf("[+] EGL_PLATFORM_ANDROID_KHR accepted, dpy = %p\n", dpy);
            }
        }
    } else {
        printf("[-] eglGetPlatformDisplayEXT not available\n");
    }

    if (dpy == EGL_NO_DISPLAY) {
        printf("[*] Falling back to standard eglGetDisplay(EGL_DEFAULT_DISPLAY)...\n");
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) {
            EGLint err = eglGetError();
            printf("[-] FATAL: eglGetDisplay(EGL_DEFAULT_DISPLAY) failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
            return 1;
        }
        printf("[+] eglGetDisplay(EGL_DEFAULT_DISPLAY) SUCCESS! dpy = %p\n", dpy);
    }

    /* Step 2: Initialize EGL */
    printf("\n[*] Step 2: Initializing EGL display connection (opening /dev/mali0)...\n");
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        EGLint err = eglGetError();
        printf("[-] FATAL: eglInitialize failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] eglInitialize SUCCESS! EGL Version: %d.%d\n", major, minor);
    printf("    EGL_VENDOR     : %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("    EGL_VERSION    : %s\n", eglQueryString(dpy, EGL_VERSION));
    printf("    EGL_CLIENT_APIS: %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));

    /* Step 3: Enumerate EGL Configs */
    printf("\n[*] Step 3: Enumerating available EGL configurations...\n");
    EGLint total_configs = 0;
    if (!eglGetConfigs(dpy, NULL, 0, &total_configs) || total_configs == 0) {
        EGLint err = eglGetError();
        printf("[-] eglGetConfigs failed to query total count (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] Total EGL configurations available: %d\n", total_configs);

    /* Step 4: Choose Pbuffer EGL Config */
    printf("\n[*] Step 4: Selecting EGL config with Pbuffer + OpenGL ES 2.0/3.0 support...\n");
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
    EGLint num_configs = 0;
    if (!eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
        EGLint err = eglGetError();
        printf("[-] eglChooseConfig failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] Chosen Config %p (matched %d configurations)\n", config, num_configs);

    EGLint r = 0, g = 0, b = 0, a = 0, surface_type = 0;
    eglGetConfigAttrib(dpy, config, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(dpy, config, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(dpy, config, EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(dpy, config, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(dpy, config, EGL_SURFACE_TYPE, &surface_type);
    printf("    Config Attributes: RGBA(%d,%d,%d,%d), Surface Type: 0x%x\n", r, g, b, a, surface_type);

    /* Step 5: Create Pbuffer Surface (Offscreen Framebuffer) */
    const int fb_width = 64;
    const int fb_height = 64;
    printf("\n[*] Step 5: Creating %dx%d Offscreen Pbuffer Surface...\n", fb_width, fb_height);
    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH,  fb_width,
        EGL_HEIGHT, fb_height,
        EGL_NONE
    };

    EGLSurface pbuffer_surface = eglCreatePbufferSurface(dpy, config, pbuffer_attribs);
    if (pbuffer_surface == EGL_NO_SURFACE) {
        EGLint err = eglGetError();
        printf("[-] FATAL: eglCreatePbufferSurface failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] Pbuffer Surface created successfully: %p\n", pbuffer_surface);

    /* Step 6: Create OpenGL ES Context */
    printf("\n[*] Step 6: Creating OpenGL ES 2.0 Context...\n");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        EGLint err = eglGetError();
        printf("[-] eglBindAPI(EGL_OPENGL_ES_API) failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT) {
        EGLint err = eglGetError();
        printf("[-] FATAL: eglCreateContext failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] OpenGL ES Context created successfully: %p\n", context);

    /* Step 7: Make Current */
    printf("\n[*] Step 7: Binding Context to Pbuffer Surface (eglMakeCurrent)...\n");
    if (!eglMakeCurrent(dpy, pbuffer_surface, pbuffer_surface, context)) {
        EGLint err = eglGetError();
        printf("[-] FATAL: eglMakeCurrent failed (Error 0x%x: %s)\n", err, egl_error_to_string(err));
        return 1;
    }
    printf("[+] Context bound and active!\n");

    /* Step 8: Query Active OpenGL ES Hardware Strings */
    printf("\n[*] Step 8: Active Hardware & Driver Identification:\n");
    const char *gl_vendor   = (const char *)glGetString(GL_VENDOR);
    const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
    const char *gl_version  = (const char *)glGetString(GL_VERSION);
    printf("    ===================================================\n");
    printf("    GL_VENDOR  : %s\n", gl_vendor ? gl_vendor : "(null)");
    printf("    GL_RENDERER: %s\n", gl_renderer ? gl_renderer : "(null)");
    printf("    GL_VERSION : %s\n", gl_version ? gl_version : "(null)");
    printf("    ===================================================\n");

    /* Verification check: Confirm physical GPU renderer */
    if (!gl_renderer || !strstr(gl_renderer, "Mali")) {
        printf("[-] ERROR: Expected ARM Mali renderer, got: %s\n", gl_renderer ? gl_renderer : "(null)");
        return 1;
    }
    printf("[+] VERIFIED: Direct physical GPU renderer identified (%s)\n", gl_renderer);

    /* Step 9: Hardware Rendering Execution (Shader Pipeline + Geometry) */
    printf("\n[*] Step 9: Executing Hardware-Accelerated Rendering & Job Submission...\n");
    long irq_before = get_mali_job_interrupts();
    printf("[*] Pre-render Mali Job Interrupts count: %ld\n", irq_before);

    glViewport(0, 0, fb_width, fb_height);

    /* 9A: Clear background to Cornflower Blue: RGBA (100, 149, 237, 255) */
    glClearColor(100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* 9B: Compile Shader Pipeline for In-Center Geometry */
    const char *vshader_src =
        "attribute vec2 a_pos;\n"
        "void main() {\n"
        "   gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";

    const char *fshader_src =
        "precision mediump float;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n" /* Solid Red: RGBA (255, 0, 0, 255) */
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vshader_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fshader_src, NULL);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    /* Triangle centered at origin, covering coordinates (-0.5, -0.5) to (0.5, 0.5) */
    GLfloat tri_verts[] = {
         0.0f,  0.6f,
        -0.6f, -0.6f,
         0.6f, -0.6f
    };

    GLint pos_loc = glGetAttribLocation ? glGetAttribLocation(prog, "a_pos") : 0;
    if (pos_loc < 0) pos_loc = 0;
    if (glVertexAttribPointer) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, tri_verts);
    }
    if (glEnableVertexAttribArray) {
        glEnableVertexAttribArray(pos_loc);
    }

    if (glDrawArrays) {
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glFinish(); /* Flush command stream, submit atom chain to /dev/mali0, and wait */

    long irq_after = get_mali_job_interrupts();
    printf("[*] Post-render Mali Job Interrupts count: %ld\n", irq_after);
    printf("[+] Mali Job IRQ delta: %ld\n", irq_after - irq_before);

    /* Step 10: Dual-Zone Pixel Readback & Color Math Verification */
    printf("\n[*] Step 10: Reading back pixels via glReadPixels...\n");
    uint8_t *pixels = (uint8_t *)malloc(fb_width * fb_height * 4);
    if (!pixels) {
        printf("[-] Failed to allocate CPU pixel readback buffer\n");
        return 1;
    }
    memset(pixels, 0, fb_width * fb_height * 4);

    glReadPixels(0, 0, fb_width, fb_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        printf("[-] glReadPixels returned GL error 0x%x\n", gl_err);
        return 1;
    }

    /* Verification 1: Corner Background Pixel (0, 0) -> Cornflower Blue (100, 149, 237, 255) */
    uint8_t bg_r = pixels[0];
    uint8_t bg_g = pixels[1];
    uint8_t bg_b = pixels[2];
    uint8_t bg_a = pixels[3];

    printf("[+] Pixel (0,0) [Background]: RGBA = (%u, %u, %u, %u)\n", bg_r, bg_g, bg_b, bg_a);
    printf("    Expected Background     : RGBA = (100, 149, 237, 255) [Cornflower Blue]\n");

    /* Verification 2: Center Triangle Pixel (32, 32) -> Triangle Red (255, 0, 0, 255) */
    int center_idx = (32 * fb_width + 32) * 4;
    uint8_t fg_r = pixels[center_idx + 0];
    uint8_t fg_g = pixels[center_idx + 1];
    uint8_t fg_b = pixels[center_idx + 2];
    uint8_t fg_a = pixels[center_idx + 3];

    printf("[+] Pixel (32,32) [Center Triangle]: RGBA = (%u, %u, %u, %u)\n", fg_r, fg_g, fg_b, fg_a);
    printf("    Expected Triangle Color       : RGBA = (255, 0, 0, 255) [Solid Red]\n");

    int bg_ok = (bg_r >= 98 && bg_r <= 102 && bg_g >= 147 && bg_g <= 151 && bg_b >= 235 && bg_b <= 239 && bg_a == 255);
    int fg_ok = (fg_r >= 250 && fg_g <= 5 && fg_b <= 5 && fg_a == 255);

    if (bg_ok && fg_ok) {
        printf("[+] SUCCESS: Background and Geometry pixels match mathematical ground truth!\n");
        printf("[+] CONFIRMED: Hardware GPU 3D vertex & fragment shader execution succeeded on Mali-G71!\n");
    } else {
        printf("[-] MISMATCH: Color readback validation failed (bg_ok=%d, fg_ok=%d)\n", bg_ok, fg_ok);
        return 1;
    }

    /* Step 11: Clean Teardown */
    printf("\n[*] Step 11: Teardown & Context Destruction...\n");
    free(pixels);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, pbuffer_surface);
    eglTerminate(dpy);
    dlclose(mali_handle);
    printf("[+] Clean teardown complete. All EGL tests PASSED!\n");
    printf("================================================================\n");

    return 0;
}

/* Freestanding Bionic Entry Point */
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

#define NULL ((void*)0)
#define RTLD_NOW 2
#define ENOENT 2
#define EINVAL 22

void *dlopen(const char *filename, int flag);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);

struct hw_module_t;

static void mystrcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}
static void mystrcat(char *dst, const char *src) {
    while (*dst) dst++;
    mystrcpy(dst, src);
}

int hw_get_module_by_class(const char *class_id, const char *inst, const struct hw_module_t **module) {
    char path[256];
    void *handle = NULL;
    
    if (inst) {
        mystrcpy(path, "/vendor/lib64/hw/");
        mystrcat(path, class_id);
        mystrcat(path, ".");
        mystrcat(path, inst);
        mystrcat(path, ".so");
        handle = dlopen(path, RTLD_NOW);
    }
    if (!handle) {
        mystrcpy(path, "/vendor/lib64/hw/");
        mystrcat(path, class_id);
        mystrcat(path, ".exynos7904.so");
        handle = dlopen(path, RTLD_NOW);
    }
    if (!handle) {
        mystrcpy(path, "/vendor/lib64/hw/");
        mystrcat(path, class_id);
        mystrcat(path, ".default.so");
        handle = dlopen(path, RTLD_NOW);
    }
    if (!handle) {
        mystrcpy(path, "/system/lib64/hw/");
        mystrcat(path, class_id);
        mystrcat(path, ".default.so");
        handle = dlopen(path, RTLD_NOW);
    }
    if (!handle) {
        return -ENOENT;
    }

    const struct hw_module_t *hmi = (const struct hw_module_t *)dlsym(handle, "HMI");
    if (!hmi) {
        dlclose(handle);
        return -EINVAL;
    }
    *module = hmi;
    return 0;
}

int hw_get_module(const char *id, const struct hw_module_t **module) {
    return hw_get_module_by_class(id, NULL, module);
}

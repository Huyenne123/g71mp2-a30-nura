#define NULL ((void*)0)
typedef unsigned long size_t;

void* malloc(size_t size);
void free(void* ptr);
int close(int fd);
int dup(int oldfd);
void* memcpy(void* dest, const void* src, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);

typedef struct native_handle {
    int version;
    int numFds;
    int numInts;
    int data[0];
} native_handle_t;

native_handle_t* native_handle_create(int numFds, int numInts) {
    native_handle_t* h = (native_handle_t*)malloc(sizeof(native_handle_t) + sizeof(int)*(numFds + numInts));
    if (h) {
        h->version = sizeof(native_handle_t);
        h->numFds = numFds;
        h->numInts = numInts;
    }
    return h;
}

native_handle_t* native_handle_clone(const native_handle_t* handle) {
    if (!handle) return NULL;
    native_handle_t* clone = native_handle_create(handle->numFds, handle->numInts);
    if (!clone) return NULL;
    for (int i = 0; i < handle->numFds; i++) {
        clone->data[i] = dup(handle->data[i]);
    }
    memcpy(&clone->data[handle->numFds], &handle->data[handle->numFds], sizeof(int)*handle->numInts);
    return clone;
}

int native_handle_close(const native_handle_t* handle) {
    if (!handle) return -1;
    for (int i = 0; i < handle->numFds; i++) {
        if (handle->data[i] >= 0) close(handle->data[i]);
    }
    return 0;
}

int native_handle_delete(native_handle_t* handle) {
    if (handle) free(handle);
    return 0;
}

int property_get_int32(const char *key, int default_value) {
    return default_value;
}

int property_get(const char *key, char *value, const char *default_value) {
    if (default_value) {
        strcpy(value, default_value);
        return (int)strlen(default_value);
    }
    value[0] = 0;
    return 0;
}

int property_set(const char *key, const char *value) {
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hybris/gralloc/gralloc.h>

int main() {
    printf("[GRALLOC_TEST] Initializing gralloc...\n");
    fflush(stdout);

    hybris_gralloc_initialize(0);
    printf("[GRALLOC_TEST] hybris_gralloc_initialize completed!\n");
    fflush(stdout);

    buffer_handle_t handle = NULL;
    uint32_t stride = 0;
    int ret = hybris_gralloc_allocate(1080, 2340, 1 /* HAL_PIXEL_FORMAT_RGBA_8888 */, 0x3 /* GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN */, &handle, &stride);
    printf("[GRALLOC_TEST] hybris_gralloc_allocate returned %d, handle=%p, stride=%u\n", ret, (void*)handle, stride);
    fflush(stdout);

    if (handle) {
        printf("[GRALLOC_TEST] handle: numFds=%d, numInts=%d\n", handle->numFds, handle->numInts);
        for (int i = 0; i < handle->numFds; i++) {
            printf("[GRALLOC_TEST] fd[%d]=%d\n", i, handle->data[i]);
        }
        fflush(stdout);

        void *vaddr = NULL;
        ret = hybris_gralloc_lock(handle, 0x3, 0, 0, 1080, 2340, &vaddr);
        printf("[GRALLOC_TEST] hybris_gralloc_lock returned %d, vaddr=%p\n", ret, vaddr);
        fflush(stdout);

        if (vaddr) {
            uint32_t *p = (uint32_t*)vaddr;
            p[0] = 0x11223344;
            printf("[GRALLOC_TEST] Wrote test pattern 0x%08x to mapped buffer\n", p[0]);
            fflush(stdout);
            hybris_gralloc_unlock(handle);
            printf("[GRALLOC_TEST] hybris_gralloc_unlock completed\n");
            fflush(stdout);
        }

        hybris_gralloc_release(handle, 1);
        printf("[GRALLOC_TEST] hybris_gralloc_release completed\n");
        fflush(stdout);
    }

    printf("[GRALLOC_TEST] Test finished successfully! Exiting cleanly with return 0.\n");
    fflush(stdout);
    return 0;
}

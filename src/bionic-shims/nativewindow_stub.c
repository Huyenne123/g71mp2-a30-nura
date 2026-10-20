int AHardwareBuffer_acquire(void* buffer) { return 0; }
int AHardwareBuffer_release(void* buffer) { return 0; }
void* AHardwareBuffer_getNativeHandle(void* buffer) { return (void*)0; }
int AHardwareBuffer_allocate(void* desc, void** outBuffer) { return -1; }
void AHardwareBuffer_describe(void* buffer, void* outDesc) { }

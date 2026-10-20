typedef unsigned long size_t;
typedef __builtin_va_list va_list;

extern "C" {
    void* malloc(size_t size);
    void free(void* ptr);
    size_t strlen(const char* s);
}

namespace android {
class String8 {
    char *mString;
public:
    String8();
    ~String8();
    void clear();
    size_t length() const;
    void appendFormatV(const char* fmt, va_list args);
};

String8::String8() : mString(0) {}
String8::~String8() { if (mString) free(mString); }
void String8::clear() { if (mString) { free(mString); mString = 0; } }
size_t String8::length() const { return mString ? strlen(mString) : 0; }
void String8::appendFormatV(const char* fmt, va_list args) {}
}

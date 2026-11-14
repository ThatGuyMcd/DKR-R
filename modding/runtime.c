/* Small freestanding guest runtime. No host libc calls or imported DLLs. */
typedef unsigned int Size;
void* memset(void* destination, int value, Size count) {
    volatile unsigned char* out = destination;
    for (Size i = 0; i < count; ++i)
        out[i] = (unsigned char)value;
    return destination;
}
void* memcpy(void* destination, const void* source, Size count) {
    volatile unsigned char* out = destination;
    const volatile unsigned char* in = source;
    for (Size i = 0; i < count; ++i)
        out[i] = in[i];
    return destination;
}
void* memmove(void* destination, const void* source, Size count) {
    volatile unsigned char* out = destination;
    const volatile unsigned char* in = source;
    if ((unsigned int)destination < (unsigned int)source)
        for (Size i = 0; i < count; ++i)
            out[i] = in[i];
    else
        while (count) {
            --count;
            out[count] = in[count];
        }
    return destination;
}
int memcmp(const void* left, const void* right, Size count) {
    const unsigned char* a = left;
    const unsigned char* b = right;
    for (Size i = 0; i < count; ++i)
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    return 0;
}

#ifndef DKR_SDK2_JSON_H
#define DKR_SDK2_JSON_H
/* Freestanding bounded JSON writer for copied tools values and reports. It
 * needs no host libc, heap, compiler-runtime division or variadic formatter. */
typedef struct DkrJson {
    char* bytes;
    unsigned size, used;
    int failed;
} DkrJson;
static inline void dkr_json_char(DkrJson* j, char c) {
    if (j->used + 1 >= j->size) {
        j->failed = 1;
        return;
    }
    j->bytes[j->used++] = c;
    j->bytes[j->used] = 0;
}
static inline void dkr_json_raw(DkrJson* j, const char* s) {
    while (*s)
        dkr_json_char(j, *s++);
}
static inline void dkr_json_text(DkrJson* j, const char* s) {
    dkr_json_char(j, '"');
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') {
            dkr_json_char(j, '\\');
            dkr_json_char(j, (char)c);
        } else if (c == '\n')
            dkr_json_raw(j, "\\n");
        else if (c == '\r')
            dkr_json_raw(j, "\\r");
        else if (c == '\t')
            dkr_json_raw(j, "\\t");
        else if (c >= 32)
            dkr_json_char(j, (char)c);
    }
    dkr_json_char(j, '"');
}
static inline void dkr_json_unsigned(DkrJson* j, unsigned n) {
    char digits[10];
    unsigned used = 0;
    do {
        digits[used++] = (char)('0' + n % 10);
        n /= 10;
    } while (n && used < 10);
    while (used)
        dkr_json_char(j, digits[--used]);
}
static inline void dkr_json_integer(DkrJson* j, int n) {
    if (n < 0) {
        dkr_json_char(j, '-');
        dkr_json_unsigned(j, 0U - (unsigned)n);
    } else
        dkr_json_unsigned(j, (unsigned)n);
}
static inline void dkr_json_float(DkrJson* j, float n) {
    if (!(n >= -1e7F && n <= 1e7F)) {
        dkr_json_raw(j, "null");
        return;
    }
    if (n < 0) {
        dkr_json_char(j, '-');
        n = -n;
    }
    unsigned whole = (unsigned)n;
    dkr_json_unsigned(j, whole);
    dkr_json_char(j, '.');
    unsigned part = (unsigned)((n - (float)whole) * 1000.F);
    if (part > 999)
        part = 999;
    dkr_json_char(j, (char)('0' + part / 100));
    dkr_json_char(j, (char)('0' + part / 10 % 10));
    dkr_json_char(j, (char)('0' + part % 10));
}
static inline void dkr_json_key(DkrJson* j, const char* key) {
    dkr_json_text(j, key);
    dkr_json_char(j, ':');
}
#endif

#ifndef DKR_SDK2_JSON_READ_H
#define DKR_SDK2_JSON_READ_H
/* Small, bounded JSON reader for data documents. It never allocates, uses a
 * host parser or accepts executable expressions. The caller owns all tokens.
 * Text is retained by offset; copy/unescape a string before displaying it.
 * Numeric conversion is locale-independent and rejects non-finite results. */
typedef struct DkrJsonToken {
    unsigned begin, end, child, next, type;
} DkrJsonToken;
typedef struct DkrJsonReader {
    const char* bytes;
    unsigned length, at, count, capacity;
    DkrJsonToken* tokens;
    int failed;
} DkrJsonReader;
enum { DKR_JSON_OBJECT = 1, DKR_JSON_ARRAY = 2, DKR_JSON_STRING = 3, DKR_JSON_NUMBER = 4, DKR_JSON_LITERAL = 5 };
static inline void dkr_json_space(DkrJsonReader* r) {
    while (r->at < r->length &&
           (r->bytes[r->at] == ' ' || r->bytes[r->at] == '\n' || r->bytes[r->at] == '\r' || r->bytes[r->at] == '\t'))
        ++r->at;
}
static inline unsigned dkr_json_value(DkrJsonReader* r, unsigned depth) {
    dkr_json_space(r);
    if (depth > 16 || r->at == r->length || r->count == r->capacity) {
        r->failed = 1;
        return 0;
    }
    const unsigned index = r->count++;
    DkrJsonToken* t = &r->tokens[index];
    t->begin = r->at;
    t->child = 0;
    const char first = r->bytes[r->at++];
    if (first == '{' || first == '[') {
        t->type = first == '{' ? DKR_JSON_OBJECT : DKR_JSON_ARRAY;
        t->child = r->count;
        dkr_json_space(r);
        const char close = first == '{' ? '}' : ']';
        if (r->at < r->length && r->bytes[r->at] == close)
            ++r->at;
        else
            while (!r->failed) {
                if (first == '{') {
                    const unsigned key = dkr_json_value(r, depth + 1);
                    if (r->failed || r->tokens[key].type != DKR_JSON_STRING) {
                        r->failed = 1;
                        break;
                    }
                    dkr_json_space(r);
                    if (r->at == r->length || r->bytes[r->at++] != ':') {
                        r->failed = 1;
                        break;
                    }
                }
                dkr_json_value(r, depth + 1);
                dkr_json_space(r);
                if (r->at == r->length) {
                    r->failed = 1;
                    break;
                }
                const char next = r->bytes[r->at++];
                if (next == close)
                    break;
                if (next != ',') {
                    r->failed = 1;
                    break;
                }
            }
    } else if (first == '"') {
        t->type = DKR_JSON_STRING;
        int closed = 0;
        while (r->at < r->length) {
            const unsigned char c = (unsigned char)r->bytes[r->at++];
            if (c == '"') {
                closed = 1;
                break;
            }
            if (c < 32) {
                r->failed = 1;
                break;
            }
            if (c == '\\') {
                if (r->at == r->length) {
                    r->failed = 1;
                    break;
                }
                const char escaped = r->bytes[r->at++];
                if (escaped == 'u')
                    for (unsigned i = 0; i < 4; ++i) {
                        if (r->at == r->length) {
                            r->failed = 1;
                            break;
                        }
                        const char h = r->bytes[r->at++];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')))
                            r->failed = 1;
                    }
                else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' && escaped != 'f' &&
                         escaped != 'n' && escaped != 'r' && escaped != 't')
                    r->failed = 1;
            }
        }
        if (!closed)
            r->failed = 1;
    } else if (first == '-' || (first >= '0' && first <= '9')) {
        t->type = DKR_JSON_NUMBER;
        r->at = t->begin;
        if (first == '-')
            ++r->at;
        if (r->at == r->length) {
            r->failed = 1;
            return index;
        }
        if (r->bytes[r->at] == '0')
            ++r->at;
        else if (r->bytes[r->at] >= '1' && r->bytes[r->at] <= '9')
            while (r->at < r->length && r->bytes[r->at] >= '0' && r->bytes[r->at] <= '9')
                ++r->at;
        else {
            r->failed = 1;
            return index;
        }
        if (r->at < r->length && r->bytes[r->at] == '.') {
            const unsigned begin = ++r->at;
            while (r->at < r->length && r->bytes[r->at] >= '0' && r->bytes[r->at] <= '9')
                ++r->at;
            if (begin == r->at)
                r->failed = 1;
        }
        if (r->at < r->length && (r->bytes[r->at] == 'e' || r->bytes[r->at] == 'E')) {
            ++r->at;
            if (r->at < r->length && (r->bytes[r->at] == '+' || r->bytes[r->at] == '-'))
                ++r->at;
            const unsigned begin = r->at;
            while (r->at < r->length && r->bytes[r->at] >= '0' && r->bytes[r->at] <= '9')
                ++r->at;
            if (begin == r->at)
                r->failed = 1;
        }
    } else {
        t->type = DKR_JSON_LITERAL;
        const char* expected = first == 't' ? "rue" : first == 'f' ? "alse" : first == 'n' ? "ull" : "!";
        for (unsigned i = 0; expected[i]; ++i)
            if (r->at == r->length || r->bytes[r->at++] != expected[i]) {
                r->failed = 1;
                break;
            }
    }
    t->end = r->at;
    t->next = r->count;
    return index;
}
static inline int dkr_json_parse(const char* bytes, unsigned length, DkrJsonToken* tokens, unsigned capacity) {
    DkrJsonReader r = {bytes, length, 0, 0, capacity, tokens, 0};
    dkr_json_value(&r, 0);
    dkr_json_space(&r);
    return r.failed || r.at != length ? -1 : (int)r.count;
}
static inline int dkr_json_equal(const char* bytes, const DkrJsonToken* token, const char* key) {
    if (token->type != DKR_JSON_STRING)
        return 0;
    unsigned i = 0;
    for (; key[i] && token->begin + 1 + i < token->end - 1; ++i)
        if (bytes[token->begin + 1 + i] != key[i])
            return 0;
    return !key[i] && token->begin + 1 + i == token->end - 1;
}
static inline int dkr_json_member(const char* bytes, const DkrJsonToken* tokens, unsigned object, const char* key) {
    if (tokens[object].type != DKR_JSON_OBJECT)
        return -1;
    for (unsigned i = tokens[object].child; i < tokens[object].next;) {
        const unsigned value = i + 1;
        if (dkr_json_equal(bytes, &tokens[i], key))
            return (int)value;
        i = tokens[value].next;
    }
    return -1;
}
static inline int dkr_json_u32(const char* bytes, const DkrJsonToken* token, unsigned* value) {
    if (token->type != DKR_JSON_NUMBER || token->begin == token->end)
        return 0;
    unsigned result = 0;
    for (unsigned i = token->begin; i < token->end; ++i) {
        const unsigned c = (unsigned char)bytes[i] - '0';
        if (c > 9 || result > (0xffffffffU - c) / 10)
            return 0;
        result = result * 10 + c;
    }
    *value = result;
    return 1;
}
static inline int dkr_json_real(const char* bytes, const DkrJsonToken* token, float* value) {
    if (token->type != DKR_JSON_NUMBER)
        return 0;
    unsigned i = token->begin;
    float result = 0, scale = 1;
    int negative = 0, exponent = 0, exponent_negative = 0;
    if (i < token->end && bytes[i] == '-') {
        negative = 1;
        ++i;
    }
    unsigned digits = 0;
    while (i < token->end && bytes[i] >= '0' && bytes[i] <= '9') {
        result = result * 10 + (bytes[i++] - '0');
        ++digits;
    }
    if (!digits)
        return 0;
    if (i < token->end && bytes[i] == '.') {
        ++i;
        unsigned decimal = 0;
        while (i < token->end && bytes[i] >= '0' && bytes[i] <= '9') {
            scale *= 0.1f;
            result += (bytes[i++] - '0') * scale;
            ++decimal;
        }
        if (!decimal)
            return 0;
    }
    if (i < token->end && (bytes[i] == 'e' || bytes[i] == 'E')) {
        ++i;
        if (i < token->end && (bytes[i] == '-' || bytes[i] == '+'))
            exponent_negative = bytes[i++] == '-';
        unsigned n = 0;
        while (i < token->end && bytes[i] >= '0' && bytes[i] <= '9') {
            exponent = exponent * 10 + bytes[i++] - '0';
            if (exponent > 38)
                return 0;
            ++n;
        }
        if (!n)
            return 0;
    }
    if (i != token->end)
        return 0;
    for (int n = 0; n < exponent; ++n)
        result *= exponent_negative ? 0.1f : 10.0f;
    if (!(result >= 0 && result <= 3.4e38f))
        return 0;
    *value = negative ? -result : result;
    return 1;
}
#endif

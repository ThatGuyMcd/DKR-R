/* Published vectors: RFC 1321 (MD5), RFC 6234 (SHA), RFC 2202 and
 * RFC 4231 (HMAC cases 1 and 6). Exercise the actual dependency header's
 * indirect callbacks, including streamed updates and reset after final.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <picohash.h>

typedef struct {
    const char* name;
    void (*init)(picohash_ctx_t*);
    const char* empty;
    const char* abc;
    const char* hmac_short;
    const char* hmac_long;
    size_t short_key_length;
    size_t long_key_length;
} hash_case;

static const hash_case cases[] = {
    {"md5", picohash_init_md5, "d41d8cd98f00b204e9800998ecf8427e", "900150983cd24fb0d6963f7d28e17f72",
     "9294727a3638bb1c13f48ef8158bfc9d", "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd", 16, 80},
    {"sha1", picohash_init_sha1, "da39a3ee5e6b4b0d3255bfef95601890afd80709", "a9993e364706816aba3e25717850c26c9cd0d89d",
     "b617318655057264e28bc0b6fb378c8ef146be00", "aa4ae5e15272d00e95705637ce8a3b55ed402112", 20, 80},
    {"sha224", picohash_init_sha224, "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f",
     "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
     "896fb1128abbdf196832107cd49df33f47b4b1169912ba4f53684b22",
     "95e9a0db962095adaebe9b2d6f0dbce2d499f112f2d2b7273fa6870e", 20, 131},
    {"sha256", picohash_init_sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
     "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
     "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 20, 131},
};

static void expect_digest(picohash_ctx_t* ctx, const char* expected) {
    unsigned char digest[PICOHASH_MAX_DIGEST_LENGTH];
    char actual[PICOHASH_MAX_DIGEST_LENGTH * 2 + 1];
    picohash_final(ctx, digest);
    for (size_t i = 0; i < ctx->digest_length; ++i)
        snprintf(actual + i * 2, 3, "%02x", digest[i]);
    assert(strlen(expected) == ctx->digest_length * 2);
    if (strcmp(actual, expected)) {
        fprintf(stderr, "Expected %s, got %s\n", expected, actual);
        assert(!"Published hash vector mismatch");
    }
}

static void check_hash(const hash_case* test) {
    picohash_ctx_t ctx;
    test->init(&ctx);
    picohash_update(&ctx, "", 0);
    expect_digest(&ctx, test->empty);
    picohash_reset(&ctx);
    picohash_update(&ctx, "a", 1);
    picohash_update(&ctx, "bc", 2);
    expect_digest(&ctx, test->abc);
    picohash_reset(&ctx);
    picohash_update(&ctx, "abc", 3);
    expect_digest(&ctx, test->abc);
}

static void check_hmac(const hash_case* test, unsigned char key_byte, size_t key_length, const char* message,
                       const char* expected) {
    unsigned char key[131];
    assert(key_length <= sizeof(key));
    memset(key, key_byte, key_length);
    picohash_ctx_t ctx;
    picohash_init_hmac(&ctx, test->init, key, key_length);
    const size_t length = strlen(message);
    picohash_update(&ctx, message, 2);
    picohash_update(&ctx, message + 2, length - 2);
    expect_digest(&ctx, expected);
    /* Reset must restore the inner keyed hash after outer finalization. */
    picohash_reset(&ctx);
    picohash_update(&ctx, message, length);
    expect_digest(&ctx, expected);
}

int main(void) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const hash_case* test = &cases[i];
        check_hash(test);
        check_hmac(test, 0x0b, test->short_key_length, "Hi There", test->hmac_short);
        check_hmac(test, 0xaa, test->long_key_length, "Test Using Larger Than Block-Size Key - Hash Key First",
                   test->hmac_long);
        printf("%s callback and HMAC vectors passed\n", test->name);
    }
    return 0;
}

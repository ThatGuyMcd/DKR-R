// The pinned Mbed TLS must be built thread safe. libdatachannel calls psa_crypto_init() from
// every TLS and DTLS transport it creates, on its own worker threads, and Mbed TLS documents
// that as unsafe without MBEDTLS_THREADING_C (mbedtls_config.h: "The PSA subsystem has an
// implicit shared context"). Two first handshakes in one process used to corrupt the heap
// (DKRWebSocketIdle, exit 0xc0000374 / 0xc0000005).
//
// On Windows the mutexes come from mbedtls_threading/threading_alt.h and are installed as the
// library defaults by patches/mbedtls/0001-*.patch. Without that, every mutex lock fails and
// psa_crypto_init() cannot succeed, so this test also pins the patch.
#include <mbedtls/build_info.h>
#include <psa/crypto.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#if !defined(MBEDTLS_THREADING_C)
#error "MBEDTLS_THREADING_C must be enabled by mbedtls_dkrr_user_config.h"
#endif
#if defined(_WIN32) && !defined(MBEDTLS_THREADING_ALT)
#error "Windows uses MBEDTLS_THREADING_ALT with the SRWLOCK mutexes"
#endif
#if !defined(_WIN32) && !defined(MBEDTLS_THREADING_PTHREAD)
#error "Every other platform uses MBEDTLS_THREADING_PTHREAD"
#endif

int main() {
    constexpr unsigned thread_count = 16;
    std::atomic<bool> go{false};
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < thread_count; ++i) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
            }
            // All threads hit the first-use initialisation at the same moment.
            if (psa_crypto_init() != PSA_SUCCESS) {
                ++failures;
                return;
            }
            unsigned char bytes[32]{};
            for (int round = 0; round < 64; ++round)
                if (psa_generate_random(bytes, sizeof(bytes)) != PSA_SUCCESS) {
                    ++failures;
                    return;
                }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& thread : threads)
        thread.join();
    if (failures.load() != 0) {
        std::fprintf(stderr, "%u of %u threads failed psa_crypto_init or psa_generate_random\n", failures.load(),
                     thread_count);
        return 1;
    }
    std::puts("Mbed TLS PSA initialisation is thread safe");
    return 0;
}

#pragma once

// WebRTC data channels use DTLS-SRTP key export even when DKR-R carries no
// media tracks. The upstream Mbed TLS LTS profile leaves this extension off
// by default, so enable precisely the protocol facility libdatachannel needs.
#define MBEDTLS_SSL_DTLS_SRTP

// Threading. libdatachannel runs TLS (wss signalling) and DTLS (data channel)
// handshakes on its own worker threads, and every one of them calls
// psa_crypto_init() and then TLS 1.3 / PSA operations. Mbed TLS documents that
// as requiring MBEDTLS_THREADING_C ("The PSA subsystem has an implicit shared
// context", mbedtls_config.h); without it two first handshakes in one process
// race on PSA's global state and corrupt the heap.
#define MBEDTLS_THREADING_C
#if defined(_WIN32)
// Mbed TLS 3.6 ships only a pthread backend. On Windows the mutexes are the
// SRWLOCK ones in mbedtls_threading/threading_alt.h, installed as the library's
// default function pointers by patches/mbedtls/0001-*.patch, so no binary has to
// call mbedtls_threading_set_alt() before its first TLS use.
#define MBEDTLS_THREADING_ALT
#define DKRR_MBEDTLS_THREADING_WIN32
#else
#define MBEDTLS_THREADING_PTHREAD
#endif

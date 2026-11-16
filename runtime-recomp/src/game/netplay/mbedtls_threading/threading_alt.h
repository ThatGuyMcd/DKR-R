#ifndef DKRR_MBEDTLS_THREADING_ALT_H
#define DKRR_MBEDTLS_THREADING_ALT_H

/* Included by mbedtls/threading.h when MBEDTLS_THREADING_ALT is set (Windows only,
 * see mbedtls_dkrr_user_config.h). An SRWLOCK is the Windows counterpart of the
 * statically initialised pthread mutex: an all-zero SRWLOCK is a valid unlocked
 * lock, so the library's global mutexes work before and without
 * mbedtls_threading_init-style registration. It is exclusive and not recursive,
 * like the pthread default, and needs no destruction. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef struct mbedtls_threading_mutex_t {
    SRWLOCK srw;
} mbedtls_threading_mutex_t;

static inline void dkrr_mbedtls_mutex_init(mbedtls_threading_mutex_t* mutex) {
    InitializeSRWLock(&mutex->srw);
}

static inline void dkrr_mbedtls_mutex_free(mbedtls_threading_mutex_t* mutex) {
    (void)mutex;
}

static inline int dkrr_mbedtls_mutex_lock(mbedtls_threading_mutex_t* mutex) {
    if (mutex == NULL) {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    AcquireSRWLockExclusive(&mutex->srw);
    return 0;
}

static inline int dkrr_mbedtls_mutex_unlock(mbedtls_threading_mutex_t* mutex) {
    if (mutex == NULL) {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    ReleaseSRWLockExclusive(&mutex->srw);
    return 0;
}

#endif

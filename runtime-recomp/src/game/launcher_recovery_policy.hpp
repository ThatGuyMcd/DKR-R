#pragma once

namespace dkr::runtime {
// A Windows game surface retired after an error can keep presenting its old
// frame even while the launcher draws. Reuse the proven fresh-window recovery
// path for content/save errors, without changing successful legacy returns.
constexpr bool launcher_surface_recovery(bool owned_failed, bool returned_owned,
                                         bool local_failure) {
#if defined(_WIN32)
    return owned_failed || returned_owned || local_failure;
#else
    return owned_failed;
#endif
}
}

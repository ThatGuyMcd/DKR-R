#pragma once
// What a build tells its peers about how it computes. Rollback needs every
// peer to produce the same bits from the same inputs. Peers are matched on a
// named contract a build can check about itself, not on the CPU it was built
// for, so an architecture nobody has listed races the others as long as it
// honours the contract. docs/CROSS-ARCHITECTURE.md has the evidence.
#include <bit>
#include <cfloat>
#include <limits>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

// strict-ieee754-v1: IEEE 754 binary32/binary64, little-endian, and every
// intermediate result evaluated in its own type (no x87 extended precision,
// no fast-math). The build flags keep contraction off (-ffp-contract=off,
// /fp:strict), which a translation unit cannot check about itself.
inline constexpr bool kStrictIeee754 = std::numeric_limits<float>::is_iec559 &&
                                       std::numeric_limits<double>::is_iec559 &&
                                       std::endian::native == std::endian::little
#if defined(FLT_EVAL_METHOD)
                                       && FLT_EVAL_METHOD == 0
#endif
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
                                       && false
#endif
    ;

// For logs and the lobby. "x86_64" and "arm64" are also part of the hash
// builds without contracts agree on, so those two spellings never change.
inline constexpr std::string_view build_architecture() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
    return "ppc64le";
#elif defined(__loongarch64)
    return "loongarch64";
#elif defined(__wasm64__)
    return "wasm64";
#elif defined(__wasm32__)
    return "wasm32";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}

// A build that cannot vouch for the contract names its own architecture, so
// it only ever meets builds for that architecture.
inline std::string floating_point_contract(bool strict = kStrictIeee754,
                                           std::string_view architecture = build_architecture()) {
    return strict ? std::string("strict-ieee754-v1") : "unverified-" + std::string(architecture);
}

} // namespace dkr::runtime::netplay

#include "magic_code_vm.hpp"
#include <recomp.h>
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <optional>
#include <cstdio>

#include "magic_code_execution_policy.hpp"

namespace dkr::mods::code {
namespace {
[[noreturn]] void fault(const char* message) {
    throw Error(std::string("Custom Magic Code: ") + message);
}
[[noreturn]] void memory_fault(const char* message, std::uint32_t address, std::size_t bytes, bool write,
                               bool instruction) {
    char details[112];
    std::snprintf(details, sizeof(details), " [%s address=0x%08X bytes=%zu]",
                  instruction ? "execute"
                  : write     ? "write"
                              : "read",
                  address, bytes);
    throw Error(std::string("Custom Magic Code: ") + message + details);
}
std::uint32_t physical(std::uint32_t a) {
    if ((a & 0xe0000000U) != 0x80000000U)
        fault("address is outside guest RDRAM");
    return a & 0x1fffffffU;
}
std::uint64_t signed32(std::uint32_t value) {
    return std::uint64_t(std::int64_t(std::int32_t(value)));
}
std::uint64_t mask(unsigned bytes) {
    return bytes == 8 ? ~std::uint64_t{} : (std::uint64_t{1} << (bytes * 8)) - 1;
}
// Avoid signed shifts/overflow and host-dependent NaN payloads in guest state.
template <class U> U arithmetic_right(U value, unsigned shift) {
    if (!shift)
        return value;
    return value >> shift | ((value >> (sizeof(U) * 8 - 1)) ? ~U{} << (sizeof(U) * 8 - shift) : U{});
}
template <class F> F canonical(F v) {
    return std::isnan(v) ? std::numeric_limits<F>::quiet_NaN() : v;
}
std::uint32_t fword(const Registers& c, unsigned r) {
    return std::uint32_t(c.floating[r & ~1U] >> ((r & 1U) * 32));
}
void fword(Registers& c, unsigned r, std::uint32_t value) {
    const auto shift = (r & 1U) * 32;
    auto& pair = c.floating[r & ~1U];
    pair = (pair & ~(0xffffffffULL << shift)) | std::uint64_t(value) << shift;
}
struct Rounding {
    int original = std::fegetround();
    explicit Rounding(unsigned mode) {
        constexpr int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
        if (std::fesetround(modes[mode & 3]))
            fault("host cannot establish the guest rounding mode");
    }
    ~Rounding() {
        std::fesetround(original);
    }
};
template <class F> void floating_op(Registers& c, unsigned op, unsigned fd, unsigned fs, unsigned ft) {
    auto get = [&](unsigned i) -> F {
        if constexpr (sizeof(F) == 4)
            return std::bit_cast<float>(fword(c, i));
        else {
            if (i & 1)
                fault("unaligned double-precision register");
            return std::bit_cast<double>(c.floating[i]);
        }
    };
    auto set = [&](F v) {
        v = canonical(v);
        if constexpr (sizeof(F) == 4)
            fword(c, fd, std::bit_cast<std::uint32_t>(v));
        else {
            if (fd & 1)
                fault("unaligned double-precision destination");
            c.floating[fd] = std::bit_cast<std::uint64_t>(v);
        }
    };
    const F a = get(fs);
    if (op >= 0x30) {
        const F b = get(ft);
        const bool unordered = std::isnan(a) || std::isnan(b);
        const bool condition = ((op & 1) && unordered) || ((op & 2) && a == b) || ((op & 4) && a < b);
        c.fcsr = (c.fcsr & ~0x800000U) | (condition ? 0x800000U : 0U);
        return;
    }
    if ((op >= 8 && op <= 15) || op == 36 || op == 37) {
        const auto mode = op == 36 || op == 37 ? c.fcsr & 3U : op & 3U;
        if ((op == 37 || (op >= 8 && op <= 11)) && (fd & 1))
            fault("unaligned integer floating destination");
        Rounding rounding(mode);
        // nearbyint honours the explicit rounding environment, unlike a cast.
        const double value = std::nearbyint(double(a));
        const bool wide = op == 37 || (op >= 8 && op <= 11);
        if (wide)
            c.floating[fd] = !std::isfinite(value) || value < -0x1p63 || value >= 0x1p63
                                 ? 0x8000000000000000ULL
                                 : std::uint64_t(std::int64_t(value));
        else
            fword(c, fd,
                  !std::isfinite(value) || value < -0x1p31 || value >= 0x1p31 ? 0x80000000U
                                                                              : std::uint32_t(std::int32_t(value)));
        return;
    }
    Rounding rounding(c.fcsr);
    switch (op) {
    case 0:
        set(a + get(ft));
        break;
    case 1:
        set(a - get(ft));
        break;
    case 2:
        set(a * get(ft));
        break;
    case 3:
        set(a / get(ft));
        break;
    case 4:
        set(std::sqrt(a));
        break;
    case 5:
        set(std::fabs(a));
        break;
    case 6:
        set(a);
        break;
    case 7:
        set(-a);
        break;
    case 32:
        fword(c, fd, std::bit_cast<std::uint32_t>(canonical(float(a))));
        break;
    case 33:
        if (fd & 1)
            fault("unaligned double-precision destination");
        c.floating[fd] = std::bit_cast<std::uint64_t>(canonical(double(a)));
        break;
    default:
        fault("unsupported floating-point instruction");
    }
}
} // namespace
Memory::Memory(std::span<std::uint8_t> ram, std::vector<Region> regions) : ram_(ram), regions_(std::move(regions)) {
    if (ram.size() % 4)
        fault("invalid RDRAM extent");
    for (auto& r : regions_) {
        const auto off = physical(r.address);
        if (!r.size || off > ram.size() || r.size > ram.size() - off || r.size > 0x20000000U - off)
            fault("invalid memory permission range");
        r.address = off;
    }
    std::sort(regions_.begin(), regions_.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    for (std::size_t i = 1; i < regions_.size(); ++i)
        if (regions_[i].address < regions_[i - 1].address + regions_[i - 1].size)
            fault("overlapping guest memory permissions");
}
std::size_t Memory::checked(std::uint32_t a, unsigned size, bool write, bool instruction) const {
    if (size != 1 && size != 2 && size != 4 && size != 8)
        fault("invalid memory access width");
    if (a & (size - 1))
        memory_fault("unaligned guest memory access", a, size, write, instruction);
    return range(a, size, write, instruction);
}
std::size_t Memory::range(std::uint32_t a, std::size_t size, bool write, bool instruction) const {
    const auto off = physical(a);
    if (off > ram_.size() || size > ram_.size() - off)
        memory_fault("guest memory access exceeds RDRAM", a, size, write, instruction);
    auto& cached = instruction ? last_code_ : last_data_;
    const auto contains = [&](const Region& r) {
        return off >= r.address && off - r.address <= r.size && size <= r.size - (off - r.address);
    };
    if (cached >= regions_.size() || !contains(regions_[cached])) {
        auto it = std::upper_bound(regions_.begin(), regions_.end(), off,
                                   [](auto a, const Region& r) { return a < r.address; });
        if (it == regions_.begin())
            memory_fault("access before the first permitted memory range", a, size, write, instruction);
        --it;
        cached = std::size_t(it - regions_.begin());
    }
    const auto& r = regions_[cached];
    if (contains(r) && (!write || r.writable) && (!instruction || r.executable))
        return off;
    memory_fault(write ? "write to an unowned or read-only memory range" : "read or execution outside permitted memory",
                 a, size, write, instruction);
}
std::uint64_t Memory::load(std::uint32_t a, unsigned size, bool instruction) const {
    const auto off = checked(a, size, false, instruction);
    std::uint64_t result = 0;
    const auto& region = regions_[instruction ? last_code_ : last_data_];
    if (region.immutable) {
        for (unsigned i = 0; i < size; ++i)
            result = result << 8 | region.immutable[off - region.address + i];
        return result;
    }
    for (unsigned i = 0; i < size; ++i)
        result = result << 8 | ram_[(off + i) ^ 3U];
    return result;
}
void Memory::store(std::uint32_t a, unsigned size, std::uint64_t value) {
    const auto off = checked(a, size, true, false);
    for (unsigned i = 0; i < size; ++i)
        ram_[(off + size - 1 - i) ^ 3U] = std::uint8_t(value >> (i * 8));
}
void Memory::read(std::uint32_t a, std::span<std::uint8_t> bytes) const {
    if (bytes.empty())
        return;
    const auto offset = range(a, bytes.size(), false, false);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = ram_[(offset + i) ^ 3U];
}
void Memory::write(std::uint32_t a, View bytes) {
    if (bytes.empty())
        return;
    const auto offset = range(a, bytes.size(), true, false);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        ram_[(offset + i) ^ 3U] = bytes[i];
}
void Memory::fill(std::uint32_t a, std::size_t size, std::uint8_t value) {
    if (!size)
        return;
    const auto offset = range(a, size, true, false);
    if (((offset | size) & 3U) == 0)
        std::fill_n(ram_.begin() + offset, size, value);
    else
        for (std::size_t i = 0; i < size; ++i)
            ram_[(offset + i) ^ 3U] = value;
}
bool Memory::executable(std::uint32_t a) const {
    if ((a & 0xe0000003U) != 0x80000000U)
        return false;
    const auto off = a & 0x1fffffffU;
    auto it =
        std::upper_bound(regions_.begin(), regions_.end(), off, [](auto a, const Region& r) { return a < r.address; });
    if (it == regions_.begin())
        return false;
    --it;
    return it->executable && off - it->address <= it->size && 4 <= it->size - (off - it->address);
}
void validate_instructions(View bytes) {
    if (bytes.empty() || bytes.size() % 4)
        fault("invalid function instruction extent");
    for (std::size_t at = 0; at < bytes.size(); at += 4) {
        const auto instruction = be32(bytes, at);
        const unsigned op = instruction >> 26, rs = (instruction >> 21) & 31, rt = (instruction >> 16) & 31,
                       rd = (instruction >> 11) & 31, sa = (instruction >> 6) & 31, fn = instruction & 63;
        if (op == 0) {
            switch (fn) {
            case 0:
            case 2:
            case 3:
            case 4:
            case 6:
            case 7:
            case 8:
            case 9:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
            case 20:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
            case 48:
            case 49:
            case 50:
            case 51:
            case 52:
            case 54:
            case 56:
            case 58:
            case 59:
            case 60:
            case 62:
            case 63:
                break;
            default:
                fault("package contains an unsupported or privileged SPECIAL instruction");
            }
        } else if (op == 1) {
            if (rt != 0 && rt != 1 && rt != 2 && rt != 3 && rt != 16 && rt != 17 && rt != 18 && rt != 19)
                fault("package contains an unsupported REGIMM instruction");
        } else if (op == 17) {
            if (rs == 0 || rs == 4)
                continue;
            if (rs == 1 || rs == 5) {
                if (rd & 1)
                    fault("package requires the unsupported FR=1 ABI");
                continue;
            }
            if (rs == 2) {
                if (rd != 0 && rd != 31)
                    fault("unsupported floating control register");
                continue;
            }
            if (rs == 6) {
                if (rd != 31)
                    fault("unsupported floating control register");
                continue;
            }
            if (rs == 8) {
                if (rt > 3)
                    fault("unsupported floating condition code");
                continue;
            }
            if (rs == 20 || rs == 21) {
                if ((fn != 32 && fn != 33) || (rs == 21 && (rd & 1)) || (fn == 33 && (sa & 1)))
                    fault("invalid floating conversion");
                continue;
            }
            if (rs != 16 && rs != 17)
                fault("unsupported floating instruction format");
            if (fn > 15 && fn != 32 && fn != 33 && fn != 36 && fn != 37 && fn < 48)
                fault("unsupported floating instruction");
            if (rs == 17 && (rd & 1))
                fault("package requires the unsupported FR=1 ABI");
        } else {
            switch (op) {
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 20:
            case 21:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:
            case 40:
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 49:
            case 53:
            case 55:
            case 57:
            case 61:
            case 63:
                break;
            default:
                fault("package contains an unsupported, atomic or privileged opcode");
            }
        }
    }
}
void execute(Memory& memory, Registers& c, std::uint32_t entry, Budget& budget, const Gateway& gateway) {
    if (++budget.depth > 64) {
        --budget.depth;
        fault("maximum callback depth exceeded");
    }
    struct Depth {
        unsigned& n;
        ~Depth() {
            --n;
        }
    } depth{budget.depth};
    // Preserve the caller's return link. Each invocation retires before a
    // simulation checkpoint; suspension in the middle of native code is forbidden.
    constexpr std::uint32_t Done = 0xfffffffcU;
    const auto caller_ra = c.integer[31];
    c.integer[31] = signed32(Done);
    struct Link {
        Registers& c;
        std::uint64_t ra;
        ~Link() {
            c.integer[31] = ra;
            c.integer[0] = 0;
        }
    } link{c, caller_ra};
    c.integer[0] = 0;
    std::uint32_t pc = entry;
    std::optional<std::uint32_t> pending;
    try {
        while (pc != Done) {
            if (!budget.instructions)
                fault("instruction budget exceeded");
            --budget.instructions;
            execution::instruction();
            const auto instruction = std::uint32_t(memory.load(pc, 4, true));
            const unsigned op = instruction >> 26, rs = (instruction >> 21) & 31, rt = (instruction >> 16) & 31,
                           rd = (instruction >> 11) & 31, sa = (instruction >> 6) & 31, fn = instruction & 63;
            const auto immediate = std::uint16_t(instruction);
            const auto signed_immediate = std::int32_t(std::int16_t(immediate));
            const auto a = c.integer[rs], b = c.integer[rt];
            const auto address = std::uint32_t(a + std::uint64_t(std::int64_t(signed_immediate)));
            const auto prior = pending;
            pending.reset();
            bool annul = false;
            auto branch = [&](bool yes, bool likely = false) {
                if (prior)
                    fault("control transfer in a branch delay slot");
                if (yes)
                    pending = pc + 4 + std::uint32_t(signed_immediate * 4);
                else if (likely)
                    annul = true;
            };
            auto jump = [&](std::uint32_t destination) {
                if (prior)
                    fault("control transfer in a branch delay slot");
                pending = destination;
            };
            auto set32 = [&](unsigned r, std::uint32_t value) { c.integer[r] = signed32(value); };
            auto partial_load = [&](unsigned width, bool left) {
                const auto offset = address & (width - 1), shift = (left ? offset : width - 1 - offset) * 8;
                const auto word = memory.load(address & ~(width - 1), width);
                const auto bits = mask(width);
                const auto value =
                    left ? (b & ~(bits << shift)) | (word << shift) : (b & ~(bits >> shift)) | (word >> shift);
                if (width == 4)
                    set32(rt, std::uint32_t(value));
                else
                    c.integer[rt] = value;
            };
            auto partial_store = [&](unsigned width, bool left) {
                // Only touched bytes are read/written; no write permission is
                // inferred from a neighbouring aligned word.
                const auto offset = address & (width - 1), begin = address & ~(width - 1);
                if (left)
                    for (unsigned i = offset; i < width; ++i)
                        memory.store(begin + i, 1, b >> ((width - 1 - (i - offset)) * 8));
                else
                    for (unsigned i = 0; i <= offset; ++i)
                        memory.store(begin + i, 1, b >> ((offset - i) * 8));
            };
            switch (op) {
            case 0:
                switch (fn) {
                case 0:
                    set32(rd, std::uint32_t(b) << sa);
                    break;
                case 2:
                    set32(rd, std::uint32_t(b) >> sa);
                    break;
                case 3:
                    set32(rd, arithmetic_right(std::uint32_t(b), sa));
                    break;
                case 4:
                    set32(rd, std::uint32_t(b) << (a & 31));
                    break;
                case 6:
                    set32(rd, std::uint32_t(b) >> (a & 31));
                    break;
                case 7:
                    set32(rd, arithmetic_right(std::uint32_t(b), unsigned(a & 31)));
                    break;
                case 8:
                    jump(std::uint32_t(a));
                    break;
                case 9:
                    c.integer[rd] = signed32(pc + 8);
                    jump(std::uint32_t(a));
                    break;
                case 15:
                    break; // single-owner memory has no outstanding guest stores
                case 16:
                    c.integer[rd] = c.hi;
                    break;
                case 17:
                    c.hi = a;
                    break;
                case 18:
                    c.integer[rd] = c.lo;
                    break;
                case 19:
                    c.lo = a;
                    break;
                case 20:
                    c.integer[rd] = b << (a & 63);
                    break;
                case 22:
                    c.integer[rd] = b >> (a & 63);
                    break;
                case 23:
                    c.integer[rd] = arithmetic_right(b, unsigned(a & 63));
                    break;
                case 24: {
                    const auto v = std::int64_t(std::int32_t(a)) * std::int64_t(std::int32_t(b));
                    c.lo = signed32(std::uint32_t(v));
                    c.hi = signed32(std::uint32_t(std::uint64_t(v) >> 32));
                    break;
                }
                case 25: {
                    const auto v = std::uint64_t(std::uint32_t(a)) * std::uint64_t(std::uint32_t(b));
                    c.lo = signed32(std::uint32_t(v));
                    c.hi = signed32(std::uint32_t(v >> 32));
                    break;
                }
                case 26: {
                    const auto x = std::int32_t(a), y = std::int32_t(b);
                    if (!y) {
                        c.lo = signed32(x < 0 ? 1 : ~0U);
                        c.hi = signed32(std::uint32_t(x));
                    } else if (x == std::numeric_limits<std::int32_t>::min() && y == -1) {
                        c.lo = signed32(std::uint32_t(x));
                        c.hi = 0;
                    } else {
                        c.lo = signed32(std::uint32_t(x / y));
                        c.hi = signed32(std::uint32_t(x % y));
                    }
                    break;
                }
                case 27:
                    if (!std::uint32_t(b)) {
                        c.lo = ~std::uint64_t{};
                        c.hi = signed32(std::uint32_t(a));
                    } else {
                        c.lo = signed32(std::uint32_t(a) / std::uint32_t(b));
                        c.hi = signed32(std::uint32_t(a) % std::uint32_t(b));
                    }
                    break;
                case 28: {
                    std::int64_t lo, hi;
                    DMULT(std::int64_t(a), std::int64_t(b), &lo, &hi);
                    c.lo = std::uint64_t(lo);
                    c.hi = std::uint64_t(hi);
                    break;
                }
                case 29:
                    DMULTU(a, b, &c.lo, &c.hi);
                    break;
                case 30: {
                    const auto x = std::int64_t(a), y = std::int64_t(b);
                    if (!y) {
                        c.lo = x < 0 ? 1 : ~std::uint64_t{};
                        c.hi = a;
                    } else if (x == std::numeric_limits<std::int64_t>::min() && y == -1) {
                        c.lo = a;
                        c.hi = 0;
                    } else {
                        c.lo = std::uint64_t(x / y);
                        c.hi = std::uint64_t(x % y);
                    }
                    break;
                }
                case 31:
                    if (!b) {
                        c.lo = ~std::uint64_t{};
                        c.hi = a;
                    } else {
                        c.lo = a / b;
                        c.hi = a % b;
                    }
                    break;
                case 32:
                case 33:
                    set32(rd, std::uint32_t(a) + std::uint32_t(b));
                    break;
                case 34:
                case 35:
                    set32(rd, std::uint32_t(a) - std::uint32_t(b));
                    break;
                case 36:
                    c.integer[rd] = a & b;
                    break;
                case 37:
                    c.integer[rd] = a | b;
                    break;
                case 38:
                    c.integer[rd] = a ^ b;
                    break;
                case 39:
                    c.integer[rd] = ~(a | b);
                    break;
                case 42:
                    c.integer[rd] = std::int64_t(a) < std::int64_t(b);
                    break;
                case 43:
                    c.integer[rd] = a < b;
                    break;
                case 44:
                case 45:
                    c.integer[rd] = a + b;
                    break;
                case 46:
                case 47:
                    c.integer[rd] = a - b;
                    break;
                case 48:
                    if (std::int64_t(a) >= std::int64_t(b))
                        fault("guest trap");
                    break;
                case 49:
                    if (a >= b)
                        fault("guest trap");
                    break;
                case 50:
                    if (std::int64_t(a) < std::int64_t(b))
                        fault("guest trap");
                    break;
                case 51:
                    if (a < b)
                        fault("guest trap");
                    break;
                case 52:
                    if (a == b)
                        fault("guest trap");
                    break;
                case 54:
                    if (a != b)
                        fault("guest trap");
                    break;
                case 56:
                    c.integer[rd] = b << sa;
                    break;
                case 58:
                    c.integer[rd] = b >> sa;
                    break;
                case 59:
                    c.integer[rd] = arithmetic_right(b, sa);
                    break;
                case 60:
                    c.integer[rd] = b << (sa + 32);
                    break;
                case 62:
                    c.integer[rd] = b >> (sa + 32);
                    break;
                case 63:
                    c.integer[rd] = arithmetic_right(b, sa + 32);
                    break;
                default:
                    fault("unsupported or privileged integer instruction");
                }
                break;
            case 1:
                if (rt == 0 || rt == 2 || rt == 16 || rt == 18) {
                    if (rt & 16)
                        c.integer[31] = signed32(pc + 8);
                    branch(std::int64_t(a) < 0, rt & 2);
                } else if (rt == 1 || rt == 3 || rt == 17 || rt == 19) {
                    if (rt & 16)
                        c.integer[31] = signed32(pc + 8);
                    branch(std::int64_t(a) >= 0, rt & 2);
                } else
                    fault("unsupported REGIMM instruction");
                break;
            case 2:
                jump(((pc + 4) & 0xf0000000U) | ((instruction & 0x3ffffffU) << 2));
                break;
            case 3:
                c.integer[31] = signed32(pc + 8);
                jump(((pc + 4) & 0xf0000000U) | ((instruction & 0x3ffffffU) << 2));
                break;
            case 4:
            case 20:
                branch(a == b, op == 20);
                break;
            case 5:
            case 21:
                branch(a != b, op == 21);
                break;
            case 6:
            case 22:
                branch(std::int64_t(a) <= 0, op == 22);
                break;
            case 7:
            case 23:
                branch(std::int64_t(a) > 0, op == 23);
                break;
            case 8:
            case 9:
                set32(rt, std::uint32_t(a) + std::uint32_t(signed_immediate));
                break;
            case 10:
                c.integer[rt] = std::int64_t(a) < signed_immediate;
                break;
            case 11:
                c.integer[rt] = a < std::uint64_t(std::int64_t(signed_immediate));
                break;
            case 12:
                c.integer[rt] = a & immediate;
                break;
            case 13:
                c.integer[rt] = a | immediate;
                break;
            case 14:
                c.integer[rt] = a ^ immediate;
                break;
            case 15:
                set32(rt, std::uint32_t(immediate) << 16);
                break;
            case 17:
                if (rs == 0)
                    set32(rt, fword(c, rd));
                else if (rs == 1) {
                    if (rd & 1)
                        fault("unaligned floating register");
                    c.integer[rt] = c.floating[rd];
                } else if (rs == 2) {
                    if (rd == 31)
                        set32(rt, c.fcsr);
                    else if (rd == 0)
                        set32(rt, 0xa00);
                    else
                        fault("unsupported floating control register");
                } else if (rs == 4)
                    fword(c, rd, std::uint32_t(b));
                else if (rs == 5) {
                    if (rd & 1)
                        fault("unaligned floating register");
                    c.floating[rd] = b;
                } else if (rs == 6) {
                    if (rd != 31)
                        fault("unsupported floating control register");
                    c.fcsr = std::uint32_t(b) & 0x0183ffffU;
                } else if (rs == 8) {
                    if (rt > 3)
                        fault("unsupported floating branch condition");
                    branch(bool(c.fcsr & 0x800000U) == bool(rt & 1), rt & 2);
                } else if (rs == 16)
                    floating_op<float>(c, fn, sa, rd, rt);
                else if (rs == 17)
                    floating_op<double>(c, fn, sa, rd, rt);
                else if (rs == 20 || rs == 21) {
                    if (rs == 21 && (rd & 1))
                        fault("unaligned integer floating register");
                    Rounding rounding(c.fcsr);
                    const auto v = rs == 20 ? std::int64_t(std::int32_t(fword(c, rd))) : std::int64_t(c.floating[rd]);
                    if (fn == 32)
                        fword(c, sa, std::bit_cast<std::uint32_t>(float(v)));
                    else if (fn == 33) {
                        if (sa & 1)
                            fault("unaligned floating destination");
                        c.floating[sa] = std::bit_cast<std::uint64_t>(double(v));
                    } else
                        fault("unsupported integer floating conversion");
                } else
                    fault("unsupported COP1 instruction");
                break;
            case 24:
            case 25:
                c.integer[rt] = a + std::uint64_t(std::int64_t(signed_immediate));
                break;
            case 26:
                partial_load(8, true);
                break;
            case 27:
                partial_load(8, false);
                break;
            case 32:
                c.integer[rt] = std::uint64_t(std::int64_t(std::int8_t(memory.load(address, 1))));
                break;
            case 33:
                c.integer[rt] = std::uint64_t(std::int64_t(std::int16_t(memory.load(address, 2))));
                break;
            case 34:
                partial_load(4, true);
                break;
            case 35:
                set32(rt, std::uint32_t(memory.load(address, 4)));
                break;
            case 36:
                c.integer[rt] = memory.load(address, 1);
                break;
            case 37:
                c.integer[rt] = memory.load(address, 2);
                break;
            case 38:
                partial_load(4, false);
                break;
            case 39:
                c.integer[rt] = memory.load(address, 4);
                break;
            case 40:
                memory.store(address, 1, b);
                break;
            case 41:
                memory.store(address, 2, b);
                break;
            case 42:
                partial_store(4, true);
                break;
            case 43:
                memory.store(address, 4, b);
                break;
            case 44:
                partial_store(8, true);
                break;
            case 45:
                partial_store(8, false);
                break;
            case 46:
                partial_store(4, false);
                break;
            case 49:
                fword(c, rt, std::uint32_t(memory.load(address, 4)));
                break;
            case 53:
                if (rt & 1)
                    fault("unaligned floating register");
                c.floating[rt] = memory.load(address, 8);
                break;
            case 55:
                c.integer[rt] = memory.load(address, 8);
                break;
            case 57:
                memory.store(address, 4, fword(c, rt));
                break;
            case 61:
                if (rt & 1)
                    fault("unaligned floating register");
                memory.store(address, 8, c.floating[rt]);
                break;
            case 63:
                memory.store(address, 8, b);
                break;
            default:
                fault("unsupported, atomic or privileged instruction");
            }
            c.integer[0] = 0;
            pc += annul ? 8 : 4;
            if (prior) {
                pc = *prior;
                if (pc != Done && !memory.executable(pc)) {
                    if (!gateway)
                        fault("unlinked function call");
                    gateway(pc, c, budget);
                    pc = std::uint32_t(c.integer[31]);
                    c.integer[0] = 0;
                    if (pc == Done)
                        break;
                    if (!memory.executable(pc))
                        fault("native call returned outside mod code");
                }
            }
        }
    } catch (const Error& e) {
        char context[32];
        std::snprintf(context, sizeof(context), " [pc=0x%08X]", pc);
        throw Error(std::string(e.what()) + context);
    }
}
} // namespace dkr::mods::code

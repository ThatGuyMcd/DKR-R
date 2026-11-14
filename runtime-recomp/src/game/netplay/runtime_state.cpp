#include "runtime_state.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace dkr::runtime::netplay {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{'D', 'K', 'R', 'S', 'T', 'A', 'T', 'E'};
constexpr std::uint32_t kFormatVersion = 1U;

struct SnapshotHeader {
    std::array<std::uint8_t, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t frame = 0;
    std::uint32_t memory_bytes = 0;
    std::uint32_t context_count = 0;
};

static_assert(sizeof(SnapshotHeader) == 24U);

SerializableContext encode(const recomp_context& source) {
    SerializableContext result{};
    const auto* gprs = &source.r0;
    const auto* fprs = &source.f0;
    for (std::size_t index = 0; index < result.gpr.size(); ++index) {
        result.gpr[index] = gprs[index];
        result.fpr[index] = fprs[index].u64;
    }
    result.hi = source.hi;
    result.lo = source.lo;
    result.status = source.status_reg;
    result.mips3_float_mode = source.mips3_float_mode;
    return result;
}

void decode(const SerializableContext& source, recomp_context& destination) {
    auto* gprs = &destination.r0;
    auto* fprs = &destination.f0;
    for (std::size_t index = 0; index < source.gpr.size(); ++index) {
        gprs[index] = source.gpr[index];
        fprs[index].u64 = source.fpr[index];
    }
    destination.hi = source.hi;
    destination.lo = source.lo;
    destination.status_reg = source.status;
    destination.mips3_float_mode = source.mips3_float_mode;
    repair_float_register_pointer(destination);
}

} // namespace

RuntimeState::RuntimeState(std::size_t memory_bytes) : memory_bytes_(memory_bytes) {}

bool RuntimeState::register_context(recomp_context* context) {
    if (context == nullptr)
        return false;
    std::scoped_lock lock(mutex_);
    if (std::find(contexts_.begin(), contexts_.end(), context) != contexts_.end()) {
        return true;
    }
    const auto empty = std::find(contexts_.begin(), contexts_.end(), nullptr);
    if (empty == contexts_.end())
        return false;
    *empty = context;
    return true;
}

void RuntimeState::unregister_context(recomp_context* context) {
    std::scoped_lock lock(mutex_);
    const auto found = std::find(contexts_.begin(), contexts_.end(), context);
    if (found != contexts_.end())
        *found = nullptr;
}

void RuntimeState::reset() {
    std::scoped_lock lock(mutex_);
    contexts_.fill(nullptr);
}

std::size_t RuntimeState::snapshot_size() const {
    return sizeof(SnapshotHeader) + memory_bytes_ + kMaximumTrackedContexts * sizeof(SerializableContext);
}

std::size_t RuntimeState::active_context_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(contexts_.begin(), contexts_.end(), [](const auto* value) { return value != nullptr; }));
}

bool RuntimeState::capture(std::uint8_t* rdram, std::uint32_t frame, std::span<std::uint8_t> destination) const {
    if (rdram == nullptr || destination.size() != snapshot_size() ||
        memory_bytes_ > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto context_count = static_cast<std::uint32_t>(
        std::count_if(contexts_.begin(), contexts_.end(), [](const auto* value) { return value != nullptr; }));
    SnapshotHeader header{kMagic, kFormatVersion, frame, static_cast<std::uint32_t>(memory_bytes_), context_count};
    std::size_t cursor = 0;
    std::memcpy(destination.data() + cursor, &header, sizeof(header));
    cursor += sizeof(header);
    std::memcpy(destination.data() + cursor, rdram, memory_bytes_);
    cursor += memory_bytes_;
    for (const recomp_context* context : contexts_) {
        const SerializableContext encoded = context != nullptr ? encode(*context) : SerializableContext{};
        std::memcpy(destination.data() + cursor, &encoded, sizeof(encoded));
        cursor += sizeof(encoded);
    }
    return true;
}

bool RuntimeState::restore(std::uint8_t* rdram, std::span<const std::uint8_t> source, std::uint32_t& frame) {
    if (rdram == nullptr || source.size() != snapshot_size())
        return false;
    SnapshotHeader header{};
    std::memcpy(&header, source.data(), sizeof(header));
    if (header.magic != kMagic || header.version != kFormatVersion || header.memory_bytes != memory_bytes_) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const std::size_t active = static_cast<std::size_t>(
        std::count_if(contexts_.begin(), contexts_.end(), [](const auto* value) { return value != nullptr; }));
    if (header.context_count != active)
        return false;

    std::size_t cursor = sizeof(header);
    std::memcpy(rdram, source.data() + cursor, memory_bytes_);
    cursor += memory_bytes_;
    for (recomp_context* context : contexts_) {
        SerializableContext encoded{};
        std::memcpy(&encoded, source.data() + cursor, sizeof(encoded));
        cursor += sizeof(encoded);
        if (context != nullptr)
            decode(encoded, *context);
    }
    frame = header.frame;
    return true;
}

void repair_float_register_pointer(recomp_context& context) {
    context.f_odd = context.mips3_float_mode != 0 ? &context.f1.u32l : &context.f0.u32h;
}

} // namespace dkr::runtime::netplay

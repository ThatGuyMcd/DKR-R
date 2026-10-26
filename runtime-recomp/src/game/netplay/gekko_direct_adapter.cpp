#include "gekko_direct_adapter.hpp"

#include <mutex>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

struct ReceiveSlot {
    std::uint8_t source = 0U;
    GekkoNetResult result{};
};
struct SharedState {
    std::mutex mutex;
    GekkoDirectAdapter* active = nullptr;
    std::vector<RollbackPacket> packets;
    std::vector<ReceiveSlot> slots;
    std::vector<GekkoNetResult*> results;
};
// Constructed on first use and intentionally never destroyed: adapters owned
// by other translation units' statics (runtime_netplay.cpp's g_rollback)
// deactivate during exit, after this file's statics would already be gone.
SharedState& shared_state() {
    static auto* instance = new SharedState;
    return *instance;
}

// Gekko consumes every receive result synchronously before asking the adapter
// for another batch. Keep the batch in reusable adapter-owned storage and make
// its matching release callback a no-op. The previous bridge allocated a
// result, address and payload for every rollback datagram, then copied the
// payload only for Gekko to free all three allocations immediately. Under
// replay bursts that allocator churn competed directly with the authored game
// thread and showed up as audio/video hitches.
void adapter_free(void*) {}

void adapter_send(GekkoNetAddress* address, const char* data, int length) {
    if (address == nullptr || address->data == nullptr || address->size != 1U || data == nullptr || length <= 0) {
        return;
    }
    auto& shared = shared_state();
    std::scoped_lock lock(shared.mutex);
    if (shared.active == nullptr || !shared.active->active())
        return;
    const auto target = *static_cast<const std::uint8_t*>(address->data);
    shared.active->session()->send_rollback_packet(
        target, {reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(length)});
}

GekkoNetResult** adapter_receive(int* length) {
    if (length == nullptr)
        return nullptr;
    *length = 0;
    auto& shared = shared_state();
    std::scoped_lock lock(shared.mutex);
    shared.results.clear();
    if (shared.active == nullptr || !shared.active->active())
        return nullptr;

    shared.active->session()->take_rollback_packets(shared.packets);
    shared.slots.resize(shared.packets.size());
    shared.results.resize(shared.packets.size());
    for (std::size_t index = 0U; index < shared.packets.size(); ++index) {
        RollbackPacket& packet = shared.packets[index];
        ReceiveSlot& slot = shared.slots[index];
        slot.source = packet.source_slot;
        slot.result = {};
        slot.result.addr.data = &slot.source;
        slot.result.addr.size = 1U;
        slot.result.data = packet.bytes.empty() ? nullptr : packet.bytes.data();
        slot.result.data_len = static_cast<unsigned int>(packet.bytes.size());
        shared.results[index] = &slot.result;
    }
    *length = static_cast<int>(shared.results.size());
    return shared.results.empty() ? nullptr : shared.results.data();
}

GekkoNetAdapter g_adapter{adapter_send, adapter_receive, adapter_free};

} // namespace

GekkoDirectAdapter::~GekkoDirectAdapter() {
    deactivate();
}

bool GekkoDirectAdapter::activate(DirectSession& session) {
    auto& shared = shared_state();
    std::scoped_lock lock(shared.mutex);
    if (shared.active != nullptr && shared.active != this)
        return false;
    session_ = &session;
    shared.active = this;
    return true;
}

void GekkoDirectAdapter::deactivate() {
    auto& shared = shared_state();
    std::scoped_lock lock(shared.mutex);
    if (shared.active == this) {
        shared.active = nullptr;
        shared.packets.clear();
        shared.slots.clear();
        shared.results.clear();
    }
    session_ = nullptr;
}

bool GekkoDirectAdapter::active() const {
    return session_ != nullptr;
}
GekkoNetAdapter* GekkoDirectAdapter::adapter() {
    return &g_adapter;
}

} // namespace dkr::runtime::netplay

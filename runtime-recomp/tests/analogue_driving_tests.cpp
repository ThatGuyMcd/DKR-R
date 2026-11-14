#include "analog_driving/native.hpp"
#include "analog_driving/poll.hpp"
#if DKR_ANALOGUE_INPUT_CODEC
#include "netplay/experimental_owner_inputs.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/netplay_protocol.hpp"
#include "netplay/replay_recorder.hpp"
#endif
#include "online_stability/live_inputs.hpp"
#include "look_behind/policy.hpp"
#define DKR_PRESENTATION_HOST 1
#include "dkr/presentation.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
using namespace dkr::runtime;
#if DKR_ANALOGUE_INPUT_CODEC
namespace net = dkr::runtime::netplay;
namespace ex = net::experimental;
#endif
void sampling() {
    driving::Profile p;
    p.enabled = true;
    driving::configure(0, p);
    for (unsigned n : {0U, 64U, 128U, 192U, 255U}) {
        const float raw = .03F + .97F * float(n) / 255;
        auto v = driving::poll(0, raw, raw, 0, 0);
        assert(v.pedals.accelerator == n && v.pedals.brake == n);
    }
    assert(driving::poll(0, .5F, .25F, 1, 0).pedals.accelerator == 255);
    assert(driving::poll(0, .5F, .25F, 0, 0, false).pedals.accelerator == 0);
    p.accelerator.inverted = true;
    driving::configure(0, p);
    assert(driving::poll(0, 1, 0, 0, 0).pedals.accelerator == 0);
    assert(driving::poll(0, 0, 0, 0, 0, false).pedals.accelerator == 0);
    assert(driving::poll(0, 1, 0, 1, 0, false).pedals.accelerator == 255);
    p.enabled = false;
    driving::configure(1, p);
    assert(!driving::poll(1, 1, 1, 1, 1).pedals.flags);
    driving::publish(0, {255, 0, 1});
    {
        driving::InputScope scope({driving::Pedals{64, 128, 1}, {}, {}, {}});
        driving::begin_frame(true);
        driving::publish(0, {});
        assert(driving::frame.inputs[0] == driving::Pedals(64, 128, 1));
    }
}
void native_effort() {
    std::vector<std::uint8_t> ram(0x800000);
    constexpr unsigned racer = 0x80010000, b = 0x80001000, player = 0x80001004, blocked = 0x80001008;
    const auto word = [&](unsigned a, unsigned n) { std::memcpy(ram.data() + a - 0x80000000, &n, 4); };
    const auto byte = [&](unsigned a, unsigned n) { ram[(a - 0x80000000) ^ 3] = std::uint8_t(n); };
    const auto real = [&](unsigned a, float n) { word(a, std::bit_cast<unsigned>(n)); };
    const auto read = [&](unsigned a) {
        float f;
        std::memcpy(&f, ram.data() + a - 0x80000000, 4);
        return f;
    };
    word(b, 0xC000);
    word(player, 0);
    byte(blocked, 0);
    driving::InputScope scope({driving::Pedals{128, 64, 1}, {}, {}, {}});
    driving::frame.symbols = {b, player, blocked};
    driving::frame.requests[0] = {{128, 64, 1}, true};
    for (unsigned vehicle = 0; vehicle < 3; ++vehicle) {
        byte(racer + 0x1D6, vehicle);
        real(racer + 0xB4, .9F);
        real(racer + 0xB8, 1.F);
        if (!vehicle)
            driving::apply(ram.data(), 2, racer, 0);
        else {
            driving::apply(ram.data(), 0, racer, std::bit_cast<unsigned>(.9F));
            driving::apply(ram.data(), 1, racer, std::bit_cast<unsigned>(1.F));
        }
        assert(std::abs(read(racer + 0xB4) - 128.F / 255) < .00001F);
        assert(std::abs(read(racer + 0xB8) - 64.F / 255 * (vehicle ? 1.2F : 1.F)) < .00001F);
    }
    byte(racer + 0x1D6, 0);
    for (unsigned n : {0U, 255U}) {
        driving::frame.requests[0] = {{std::uint8_t(n), std::uint8_t(n), 1}, true};
        real(racer + 0xB4, .8F);
        real(racer + 0xB8, -.01F);
        driving::apply(ram.data(), 2, racer, 0);
        assert(read(racer + 0xB4) == .8F && read(racer + 0xB8) == -.01F);
    }
    driving::frame.requests[0] = {{128, 128, 1}, true};
    byte(blocked, 1);
    real(racer + 0xB4, 1);
    driving::apply(ram.data(), 2, racer, 0);
    assert(read(racer + 0xB4) == 1);
    byte(blocked, 0);
    word(b, 0);
    driving::apply(ram.data(), 2, racer, 0);
    assert(read(racer + 0xB4) == 1);
    word(b, 0xC000);
    word(racer, 0xFFFF0000);
    driving::apply(ram.data(), 2, racer, 0);
    assert(read(racer + 0xB4) == 1);
}
#if DKR_ANALOGUE_INPUT_CODEC
void wire() {
    std::string error;
    for (unsigned v : {0U, 64U, 128U, 192U, 255U}) {
        net::PackedInput p{0xC000, -80, 80, std::uint8_t(v), std::uint8_t(255 - v), 1};
        assert(net::input_from_bits(net::packed_input_bits(p)) == p);
        net::protocol::InputBatch batch;
        batch.epoch = 1;
        batch.player_slot = 1;
        batch.inputs = {p};
        auto b = net::protocol::encode_input_batch(batch);
        net::protocol::InputBatch copy;
        assert(net::protocol::decode_input_batch(b, copy, error) && copy.inputs[0] == p);
        for (unsigned n = 0; n < b.size(); ++n)
            assert(!net::protocol::decode_input_batch(std::span(b).first(n), copy, error));
        b[16] = 2;
        assert(!net::protocol::decode_input_batch(b, copy, error));
        ex::OwnerInputPacket o;
        o.epoch = 1;
        o.players = 2;
        o.owner = 1;
        o.count = 1;
        o.inputs[0] = p;
        auto packet = ex::encode_owner_inputs(o);
        auto r = ex::decode_owner_inputs(packet);
        assert(r && r->inputs[0] == p);
        packet.pop_back();
        assert(!ex::decode_owner_inputs(packet));
        net::protocol::FrameCommitPayload c;
        c.epoch = 1;
        c.occupied_mask = 3;
        c.inputs[1] = p;
        const auto hash = net::protocol::frame_commit_hash(1, c);
        ++c.inputs[1].brake;
        assert(net::protocol::frame_commit_hash(1, c) != hash);
    }
    dkr::online_stability::LiveInputs l;
    l.controls = l.epoch = l.lease = 1;
    l.count = 1;
    l.inputs[0] = {0xC000, 0, 0, 64, 192, 1};
    auto b = dkr::online_stability::encode_live_inputs(l);
    dkr::online_stability::LiveInputs c;
    assert(dkr::online_stability::decode_live_inputs(b, c) && c.inputs == l.inputs);
}
struct Model final : ex::Simulation {
    std::int64_t effort = 0;
    ex::SimulationContract contract() const override {
        return {77, sizeof(effort), ex::kRequiredStateDomains, true, true, true};
    }
    bool capture(std::span<std::uint8_t> b, std::string&) override {
        std::memcpy(b.data(), &effort, sizeof(effort));
        return true;
    }
    bool restore(std::span<const std::uint8_t> b, std::string&) override {
        std::memcpy(&effort, b.data(), sizeof(effort));
        return true;
    }
    bool tick(std::uint32_t, const net::FrameInputs& p, ex::TickOutput&, std::string&) override {
        effort += p[1].accelerator - p[1].brake;
        return true;
    }
    bool commit(std::uint64_t, std::uint32_t, std::span<const std::uint8_t>, bool, std::string&) override {
        return true;
    }
};
void replay() {
    Model m;
    ex::Driver d(m);
    std::string e;
    assert(d.start({91, 2, 20}, e));
    std::int64_t expected = 0;
    for (unsigned f = 0; f < 12; ++f) {
        assert(d.receive(91, 0, f, {}) == ex::InputResult::Accepted);
        assert(d.step() == ex::Step::Advanced);
        net::PackedInput actual{0xC000, 0, 0, std::uint8_t(f * 13), std::uint8_t(f * 3), 1};
        expected += f * 10;
        assert(d.receive(91, 1, f, actual) == ex::InputResult::Accepted);
        for (unsigned n = 0; n < 40 && d.correcting(); ++n)
            assert(d.step(false) != ex::Step::Failed);
        d.step(false);
    }
    assert(m.effort == expected && d.statistics().rollbacks > 0 && d.statistics().confirmed_frames == 12);
}
void recorded_file() {
    const auto directory = std::filesystem::temp_directory_path() / "dkr-beta3-analogue-recording";
    net::ReplayRecorder recorder;
    recorder.configure(directory);
    net::Room room;
    room.room_id = "analogue-test";
    room.rules.record_replay = true;
    room.players[0].occupied = room.players[1].occupied = true;
    recorder.begin(room);
    net::FrameInputs inputs{};
    inputs[1] = {0xC000, 0, 0, 64, 192, 1};
    recorder.record(5, inputs);
    std::string error;
    assert(recorder.finalize(error));
    std::ifstream stream(recorder.last_path(), std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
    assert(bytes.size() == 32 + 4 + sizeof(inputs) && bytes[20] == 2);
    assert(bytes[48] == 64 && bytes[49] == 192 && bytes[50] == 1);
    stream.close();
    std::filesystem::remove(recorder.last_path());
    std::filesystem::remove(directory);
}
#endif
void rear_view() {
    DkrViewSnapshot s{};
    s.flags = DKR_VIEW_DRIVING | DKR_VIEW_ONLINE;
    s.view = 2;
    s.vehicle = 1;
    s.racer_id = 42;
    s.racer_generation = 7;
    s.position[0] = 10;
    s.position[1] = 20;
    s.position[2] = 30;
    s.original_position[0] = 10;
    s.original_position[1] = 55;
    s.original_position[2] = 100;
    s.original_fov = 1;
    s.original_near = 10;
    s.original_far = 15000;
    DkrViewPose p{};
    assert(look_behind::pose(s, p) && p.view == 2 && p.racer_id == 42 && p.position[2] == -40 && p.position[1] == 55);
    s.flags |= DKR_VIEW_SCRIPTED;
    assert(!look_behind::pose(s, p));
    s.flags = DKR_VIEW_DRIVING;
    look_behind::enabled = false;
    assert(!look_behind::pose(s, p));
    look_behind::enabled = true;
}
int main() {
    sampling();
    native_effort();
    rear_view();
#if DKR_ANALOGUE_INPUT_CODEC
    wire();
    replay();
    recorded_file();
    std::cout << "Extended production input transport, recording and analogue-only rollback passed.\n";
#endif
    std::cout << "Analogue calibration, native restrictions and local online rear-view pose passed.\n";
}

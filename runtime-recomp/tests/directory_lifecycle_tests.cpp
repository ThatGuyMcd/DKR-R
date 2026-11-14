#include "netplay/direct_session.hpp"
#include "owner_state.hpp"
#include "online_directory.cpp"
#include "online_stability/live_delay.hpp"

#include <iostream>
#include <stdexcept>
#include <thread>

namespace dkr::runtime::netplay {
struct DirectSessionTestAccess {
    static void prepare(DirectSession& session, bool host) {
        {
            std::scoped_lock lock(session.mutex_);
            session.worker_stop_.store(true, std::memory_order_release);
        }
        session.state_changed_.notify_all();
        if (session.network_worker_.joinable())
            session.network_worker_.join();
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = host;
        session.state_ = host ? ConnectionState::Hosting : ConnectionState::Lobby;
    }

    static void fail(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        session.fail_locked("Synthetic terminal failure");
    }

    static void publish_member(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        session.method_ = ConnectionMethod::QuickJoin;
        session.invite_ = "ABCDE";
        session.sender_id_ = 18446744073709551614ULL;
        std::string error;
        if (!session.transport_->open(0, error))
            throw std::runtime_error(error);
        session.last_client_request_ = std::chrono::steady_clock::now();
        session.pump_locked();
    }
    static void status_pair(DirectSession& host, DirectSession& guest) {
        prepare(host, true);
        prepare(guest, false);
        std::scoped_lock lock(host.mutex_, guest.mutex_);
        std::string error;
        if (!host.transport_->open(0, error) || !guest.transport_->open(0, error))
            throw std::runtime_error(error);
        PeerAddress host_address, guest_address;
        if (!DatagramSocket::resolve("127.0.0.1", host.transport_->local_port(), host_address, error) ||
            !DatagramSocket::resolve("127.0.0.1", guest.transport_->local_port(), guest_address, error))
            throw std::runtime_error(error);
        const auto key = secure::generate_key();
        host.match_id_ = guest.match_id_ = 1234;
        host.sender_id_ = 1;
        guest.sender_id_ = 2;
        guest.host_sender_id_ = 1;
        guest.host_address_ = host_address;
        guest.key_ = key;
        guest.local_slot_ = 1;
        host.local_slot_ = 0;
        host.room_view_.name = guest.room_view_.name = "Status test";
        auto& peer = host.peers_[0];
        peer.active = true;
        peer.sender_id = 2;
        peer.address = guest_address;
        peer.slot = 1;
        peer.key = key;
        const auto now = std::chrono::steady_clock::now();
        guest.last_admission_response_ = guest.last_client_request_ = now;
        auto& pending = host.pending_joins_[0];
        pending.active = true;
        pending.sender_id = 3;
        pending.display_name = "Joining racer";
        pending.first_seen = pending.last_seen = now;
        pending.mods_approved = true;
        // Retain the synthetic pending row without sending it an admission ACK.
        pending.last_acknowledgement = now + std::chrono::seconds(5);
    }
    static void pump_status(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        session.pump_locked();
    }
    static void dynamic_pair(DirectSession& host, DirectSession& guest) {
        status_pair(host, guest);
        std::scoped_lock lock(host.mutex_, guest.mutex_);
        host.room_view_.rules.automatic_input_delay = guest.room_view_.rules.automatic_input_delay = true;
    }
};
} // namespace dkr::runtime::netplay

namespace {
namespace online = dkr::online;
using dkr::runtime::netplay::DirectSession;
using dkr::runtime::netplay::DirectSessionTestAccess;
unsigned checks = 0;

void check(bool value, const char* reason) {
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}

auto publish_host(DirectSession& session, bool legacy = false) {
    auto owner = online::native::owner(&session);
    owner->epoch = "closed-epoch";
    online::native::Admission admission;
    admission.capability[0] = 17;
    owner->admissions.push_back(admission);
    online::publish(&session, {{legacy ? "legacyHost" : "host", true}, {"epoch", owner->epoch}});
    auto bridge = online::bridge(&session);
    bridge->commands.push_back({{"type", "intent"}});
    bridge->responses.push_back({{"type", "grant"}});
    return owner;
}

void close_and_rehost() {
    DirectSession host;
    DirectSessionTestAccess::prepare(host, true);
    online::visibility(true);
    const auto account = online::view();
    auto retired = publish_host(host);
    check(online::hosting_mode(&host) == 2, "fixture publishes a hosted room");
    host.disconnect("You closed the lobby.");
    check(online::hosting_mode(&host) == 0, "close must retire hosting without destroying the launcher");
    check(online::commands(&host).empty(), "closed lobby cannot retain admission commands");
    check(online::bridge(&host)->responses.empty(), "closed lobby cannot retain grants");
    check(retired->admissions.empty(), "retired admissions are revoked");
    check(online::native::owner(&host)->epoch.empty(), "replacement lobby receives fresh owner state");
    check(online::view()->enabled == account->enabled && online::view()->signed_in == account->signed_in &&
              online::view()->connection_generation == account->connection_generation && online::view()->public_desired,
          "lobby close preserves account connection and visibility preference");
    for (int iteration = 0; iteration < 3; ++iteration) {
        DirectSessionTestAccess::prepare(host, true);
        publish_host(host, iteration == 1);
        host.disconnect("You closed the lobby.");
        check(online::hosting_mode(&host) == 0, "repeated public/private rehosting retires each bridge");
    }
    host.disconnect("Already closed");
    check(online::hosting_mode(&host) == 0, "repeated close is harmless");
    online::visibility(false);
}

void leave_is_scoped_to_the_session() {
    DirectSession host, guest;
    DirectSessionTestAccess::prepare(host, true);
    DirectSessionTestAccess::prepare(guest, false);
    publish_host(host);
    online::publish(&guest, {{"legacyHost", true}});
    guest.disconnect("You left the online lobby.");
    check(online::hosting_mode(&guest) == 0, "leaving clears the departing session bridge");
    check(online::hosting_mode(&host) == 2, "leaving does not retire another session's hosted room");
}

void terminal_failure_retires_the_room() {
    DirectSession host;
    DirectSessionTestAccess::prepare(host, true);
    auto retired = publish_host(host);
    DirectSessionTestAccess::fail(host);
    check(online::hosting_mode(&host) == 0, "terminal transport failure retires the room");
    check(retired->admissions.empty(), "terminal failure revokes admission capabilities");
    host.disconnect("Clear failed session");
    check(online::hosting_mode(&host) == 0, "failure followed by close remains retired");
}

void client_publishes_social_membership() {
    DirectSession guest;
    DirectSessionTestAccess::prepare(guest, false);
    DirectSessionTestAccess::publish_member(guest);
    const auto state = online::bridge(&guest)->state;
    check(state.value("socialMember", false), "admitted client publishes social membership");
    check(state.value("code", std::string{}) == "ABCDE", "client publishes the joined lobby code");
    check(state.value("sender", std::string{}) == "18446744073709551614",
          "client preserves its full native sender identity");
}
void clients_receive_joining_progress() {
    DirectSession host, guest;
    DirectSessionTestAccess::status_pair(host, guest);
    // The first host pump establishes its room incarnation. The following
    // pumps send the metadata through real authenticated loopback UDP.
    DirectSessionTestAccess::pump_status(host);
    DirectSessionTestAccess::pump_status(host);
    auto bridge = dkr::online_stability::control(&host);
    bridge->mods({{3, 0, 50, 100, {}, dkr::online_stability::Stage::Downloading}});
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
    for (unsigned i = 0; i < 8; ++i) {
        DirectSessionTestAccess::pump_status(host);
        DirectSessionTestAccess::pump_status(guest);
    }
    const auto rows = dkr::online_stability::control(&guest)->joins();
    check(rows.size() == 1, "admitted client receives host joining snapshot");
    check(rows[0].name == "Joining racer" && rows[0].stage == dkr::online_stability::Stage::Downloading,
          "client sees canonical download stage and joining name");
    check(rows[0].received == 50 && rows[0].total == 100, "client receives exact bounded progress counters");
    guest.disconnect("Leave status test");
    check(dkr::online_stability::control(&guest)->joins().empty(), "leaving immediately clears joining rows");
    host.disconnect("Close status test");
    check(dkr::online_stability::control(&host)->joins().empty(), "closing immediately clears host progress");
}
void dynamic_delay_uses_the_authenticated_extension_dispatcher() {
    namespace stability = dkr::online_stability;
    DirectSession host, guest;
    DirectSessionTestAccess::dynamic_pair(host, guest);
    DirectSessionTestAccess::pump_status(host);
    DirectSessionTestAccess::pump_status(guest);
    auto host_bridge = stability::control(&host), guest_bridge = stability::control(&guest);
    stability::LiveDelay a(host_bridge, 4, 12), b(guest_bridge, 4, 12);
    a.local_owner(0);
    b.local_owner(1);
    const auto start = stability::Clock::now();
    for (unsigned ms = 0; ms < 12000; ms += 20) {
        const auto frame = ms * 30 / 1000;
        const auto now = start + std::chrono::milliseconds(ms);
        a.arrival(1, frame);
        b.arrival(0, frame);
        a.pulse(91, 1, frame, false, false, now);
        b.pulse(91, 1, frame, false, false, now);
        // These are real encrypted UDP records through DirectSession's normal
        // extension dispatcher, not a direct call between Control fixtures.
        DirectSessionTestAccess::pump_status(host);
        DirectSessionTestAccess::pump_status(guest);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(a.negotiated() && b.negotiated(), "actual authenticated extension dispatch negotiates Dynamic Input Delay");
    check(a.lead() == 0 && b.lead() == 0,
          "actual authenticated targets reduce both local-link owners to zero added delay");
    host.disconnect("Close dynamic dispatch test");
    guest.disconnect("Leave dynamic dispatch test");
}
} // namespace

void bootstrap_keys_survive_room_retirement() {
    DirectSession guest;
    DirectSessionTestAccess::prepare(guest, false);
    const auto expected = online::secure::generate_key_pair();
    const online::secure::Key admission{9};
    {
        auto& state = online::state();
        std::lock_guard lock(state.mutex);
        state.join_key = expected;
        state.bootstrap = {{"code", "ABCD1"}, {"admission", online::secure::encode_key(admission)}};
    }
    online::Bootstrap bootstrap;
    check(online::take_bootstrap(&guest, bootstrap), "directory bootstrap is transferred to the native session");
    // Production join_friend_invite resets its previous session before it
    // consumes this freshly authenticated key. Exercise that exact handoff.
    guest.disconnect("Prepare the next authenticated join");
    check(online::bypass_code_resolution(&guest, admission),
          "room cleanup must preserve the next join's bootstrap key");
    const auto used = online::client_key(&guest);
    check(used.public_key == expected.public_key && used.secret == expected.secret,
          "native join receives the directory-bound key pair");
    check(!online::bypass_code_resolution(&guest, admission), "bootstrap key is consumed once");
    check(online::hosting_mode(&guest) == 0, "bootstrap preservation does not leave a hosted room");
}

int main() try {
    bootstrap_keys_survive_room_retirement();
    close_and_rehost();
    leave_is_scoped_to_the_session();
    terminal_failure_retires_the_room();
    client_publishes_social_membership();
    clients_receive_joining_progress();
    dynamic_delay_uses_the_authenticated_extension_dispatcher();
    std::cout << checks << " directory lifecycle checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

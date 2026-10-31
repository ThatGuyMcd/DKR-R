#include "netplay/direct_session.hpp"
#include "owner_state.hpp"
#include "online_directory.cpp"

#include <iostream>
#include <stdexcept>

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
    std::cout << checks << " directory lifecycle checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

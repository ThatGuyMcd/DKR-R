#include "netplay/session_chat/chat.hpp"
#include "netplay/session_chat/feed_policy.hpp"
#include "netplay/session_chat/input_policy.hpp"
#include "netplay/session_chat/settings.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
using namespace dkr::chat;
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void check(bool ok, const char* reason) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "chat: %s\n", reason);
        std::abort();
    }
}
struct Room {
    Clock::time_point now = Clock::time_point{} + 10s;
    std::array<Channel, 4> nodes;
    bool drop_submit = false, drop_delivery = false, duplicate = false, disabled = false;
    unsigned packets = 0;
    Room() {
        configure();
    }
    void configure(Id room = 90) {
        for (Id id = 1; id <= 3; ++id) {
            Context c{room, id, 1, true, !disabled, {{1, "Host"}, {2, "Racer"}, {3, "Spectator"}}};
            nodes[id].configure(c, now);
        }
    }
    void run(std::chrono::milliseconds duration) {
        for (auto end = now + duration; now < end;) {
            now += 20ms;
            for (Id id = 1; id <= 3; ++id)
                for (const auto& p : nodes[id].outgoing(now)) {
                    Wire w;
                    check(decode(p.bytes, w), "all emitted packets have a valid bounded codec");
                    ++packets;
                    if (w.kind == Kind::Submit && drop_submit) {
                        drop_submit = false;
                        continue;
                    }
                    if (w.kind == Kind::Delivery && p.target == 3 && drop_delivery) {
                        drop_delivery = false;
                        continue;
                    }
                    nodes[p.target].receive(id, p.bytes, now);
                    if (duplicate)
                        nodes[p.target].receive(id, p.bytes, now);
                }
        }
    }
};
} // namespace
int main() {
    {
        View v;
        const auto now = Clock::time_point{} + 20s;
        std::set<Id> muted;
        check(recent_feed(v, muted, now).empty(), "an idle race has no chat window");
        for (Id id = 1; id <= 5; ++id)
            v.messages.push_back({id, id, id, 0, "Racer", "Hello", Delivery::Sent, now - 1s});
        auto feed = recent_feed(v, muted, now);
        check(feed.size() == 3 && feed.front()->id == 3 && feed.back()->id == 5,
              "race feed keeps the last three messages in conversation order");
        muted = {3, 4, 5};
        feed = recent_feed(v, muted, now);
        check(feed.size() == 2 && feed[0]->id == 1 && feed[1]->id == 2,
              "muted messages do not displace earlier visible messages");
        check(recent_feed(v, {}, now + 7s).empty(),
              "the whole passive overlay disappears when messages reach eight seconds");
        check(recent_feed(v, {1, 2, 3, 4, 5}, now).empty(), "muted-only traffic cannot display an empty chat window");
        check(recent_feed(v, {}, now - 2s).empty(), "future receipt timestamps cannot pin the feed open");
    }
    check(valid_text("Hello") && valid_text("Caf\xC3\xA9 \xF0\x9F\x91\x8D"), "UTF-8 plain text accepted");
    for (const auto& bad :
         {std::string{}, std::string(257, 'x'), std::string("bad\nline"), std::string("\xc0\xaf"),
          std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe2\x80\xaehidden")})
        check(!valid_text(bad), "invalid, oversized and directional-control text rejected");
    Wire message{Kind::Delivery, 90, 5, 7, 2, 1700000000, false, "Racer", "Hello"};
    const auto encoded = encode(message);
    Wire decoded;
    check(decode(encoded, decoded) && decoded.text == "Hello" && decoded.author == 2, "delivery codec round trip");
    for (std::size_t size = 0; size < encoded.size(); ++size)
        check(!decode(std::span(encoded).first(size), decoded), "truncation cannot publish partial text");
    auto trailing = encoded;
    trailing.push_back(0);
    check(!decode(trailing, decoded), "trailing bytes rejected");
    check(peer_id("18446744073709551615") == UINT64_MAX && !peer_id("18446744073709551616") && !peer_id("x2"),
          "peer identities parse without overflow");
    {
        Room r;
        r.run(500ms);
        std::string error;
        r.drop_submit = r.drop_delivery = r.duplicate = true;
        check(r.nodes[2].send("  Hello everyone!  ", error, r.now), "client can queue a message");
        check(r.nodes[2].view(r.now).messages.back().delivery == Delivery::Waiting, "client sees pending delivery");
        r.run(4s);
        for (Id id = 1; id <= 3; ++id) {
            const auto v = r.nodes[id].view(r.now);
            check(v.messages.size() == 1 && v.messages[0].text == "Hello everyone!" && v.messages[0].name == "Racer" &&
                      v.messages[0].delivery == Delivery::Sent,
                  "host, racer and spectator receive one canonical message despite losses/duplicates");
        }
        check(r.nodes[3].send("Watching this race", error, r.now), "spectator can speak");
        r.run(1s);
        check(r.nodes[1].send("Ready when you are", error, r.now), "host can speak");
        r.run(1s);
        for (Id id = 1; id <= 3; ++id)
            check(r.nodes[id].view(r.now).messages.size() == 3, "same conversation reaches every admitted participant");
        check(r.nodes[2].view(r.now, 0).messages.empty() && r.nodes[2].view(r.now, 0).known,
              "status snapshots do not copy message history");
        check(r.nodes[2].view(r.now, 1).messages.size() == 1 && r.nodes[2].view(r.now, 1).messages[0].name == "Host",
              "feed snapshots select the newest messages");
        auto wrong = message;
        wrong.room = 91;
        check(!r.nodes[2].receive(1, encode(wrong), r.now), "old room cannot inject chat");
        check(!r.nodes[2].receive(3, encoded, r.now) && !r.nodes[1].receive(2, encoded, r.now),
              "clients cannot impersonate host delivery");
        auto submit = message;
        submit.kind = Kind::Submit;
        check(!r.nodes[1].receive(99, encode(submit), r.now), "unadmitted identity cannot chat");
        r.configure(91);
        for (Id id = 1; id <= 3; ++id)
            check(r.nodes[id].view(r.now).messages.empty(), "new room clears chat history and pending packets");
    }
    {
        Room r;
        r.run(500ms);
        std::string error;
        check(r.nodes[2].send("Retry me", error, r.now), "timeout fixture queued");
        // Only this client's service runs: the host never receives the submit.
        for (unsigned n = 0; n < 12; ++n) {
            r.now += 1s;
            r.nodes[2].outgoing(r.now);
        }
        check(r.nodes[2].view(r.now).messages.back().delivery == Delivery::Failed,
              "unconfirmed messages visibly fail within a bounded time");
        const auto request = r.nodes[2].view(r.now).messages.back().request;
        check(r.nodes[2].retry(request, error, r.now), "failed message can retry with its original identity");
        r.run(2s);
        for (Id id = 1; id <= 3; ++id)
            check(r.nodes[id].view(r.now).messages.size() == 1, "manual retry does not duplicate messages");
    }
    {
        Room r;
        r.run(500ms);
        r.disabled = true;
        r.configure();
        r.run(500ms);
        std::string error;
        for (Id id = 1; id <= 3; ++id)
            check(!r.nodes[id].send("Hello", error, r.now), "host can disable chat for the room");
        r.disabled = false;
        r.configure();
        r.run(500ms);
        check(r.nodes[3].send("Back online", error, r.now), "chat can be enabled live");
    }
    {
        Room r;
        r.run(500ms);
        std::string error;
        unsigned accepted = 0;
        for (unsigned n = 0; n < 1000; ++n)
            accepted += r.nodes[1].send("Spam", error, r.now);
        check(accepted <= 3, "sender flood cannot create an unbounded broadcast backlog");
        unsigned sent = 0;
        for (unsigned n = 0; n < 1000; ++n)
            sent += r.nodes[1].outgoing(r.now).size();
        check(sent <= 2, "per-pump chat bandwidth is bounded");
        for (unsigned n = 0; n < 130; ++n) {
            r.run(1100ms);
            r.nodes[1].send("Bounded history", error, r.now);
        }
        r.run(2s);
        for (Id id = 1; id <= 3; ++id)
            check(r.nodes[id].view(r.now).messages.size() <= MaximumHistory, "room history stays bounded");
    }
    {
        typing = false;
        open_requested = false;
        suppress_open_text = false;
        check(input_action(InputKind::KeyDown, 23, false, false, 23, false) == InputAction::Pass,
              "offline shortcut does not consume driving keys");
        check(input_action(InputKind::KeyDown, 23, false, true, 23, false) == InputAction::Consume &&
                  captures_game_input(),
              "online chat opens and blocks game input immediately");
        check(input_action(InputKind::Text, 0, false, false, 23, false) == InputAction::Consume,
              "opening shortcut is not typed into chat");
        check(input_action(InputKind::Text, 0, false, false, 23, false) == InputAction::Forward,
              "typing reaches the UI");
        check(input_action(InputKind::KeyDown, 41, false, false, 23, true) == InputAction::Consume && !typing.load(),
              "Escape closes without triggering game pause");
    }
    {
        int owner = 0;
        const auto old = channel(&owner);
        Context c{44, 1, 1, true, true, {{1, "Host"}}};
        old->configure(c);
        reset(&owner);
        check(!old->view().active && channel(&owner) != old, "retired session cannot keep a live chat channel");
        reset(&owner);
    }
    {
        shortcut_key = 23;
        load_setting("online_chat_key", 41);
        load_setting("online_chat_key", 58);
        load_setting("online_chat_key", 68);
        check(shortcut_key == 23, "platform shortcuts cannot make chat impossible to open");
        load_setting("online_chat_key", 24);
        check(shortcut_key == 24, "chat key can be customized");
        std::ostringstream saved;
        save_settings(saved);
        check(saved.str().find("online_chat_key=24") != std::string::npos,
              "chat preferences persist with launcher settings");
        shortcut_key = 23;
    }
    std::printf("Session chat checks=%u\n", checks);
    return 0;
}

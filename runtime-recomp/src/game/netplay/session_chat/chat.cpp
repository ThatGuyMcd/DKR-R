#include "chat.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <limits>
#include <set>

namespace dkr::chat {
namespace {
constexpr std::array<std::uint8_t, 6> magic{'D', 'K', 'R', 'X', 1, 4};
constexpr std::size_t MaximumPending = 128;
void put(Bytes& b, Id n, unsigned width = 8) {
    for (unsigned i = 0; i < width; ++i)
        b.push_back(std::uint8_t(n >> (8 * i)));
}
void text(Bytes& b, std::string_view value) {
    put(b, value.size(), 2);
    b.insert(b.end(), value.begin(), value.end());
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 6;
    bool ok = true;
    Id number(unsigned width = 8) {
        if (width > bytes.size() - at) {
            ok = false;
            return 0;
        }
        Id n = 0;
        for (unsigned i = 0; i < width; ++i)
            n |= Id(bytes[at++]) << (8 * i);
        return n;
    }
    std::string text(std::size_t limit) {
        const auto n = number(2);
        if (!ok || n > limit || n > bytes.size() - at) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(bytes.data() + at), std::size_t(n));
        at += std::size_t(n);
        return value;
    }
    bool end() const {
        return ok && at == bytes.size();
    }
};
Wire delivery(Id room, const Message& m) {
    Wire w;
    w.kind = Kind::Delivery;
    w.room = room;
    w.id = m.id;
    w.request = m.request;
    w.author = m.author;
    w.seconds = m.seconds;
    w.name = m.name;
    w.text = m.text;
    return w;
}
struct Registry {
    std::mutex mutex;
    std::map<const void*, std::shared_ptr<Channel>> sessions;
};
Registry& registry() {
    static auto* const value = new Registry;
    return *value;
}
} // namespace
bool valid_text(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum)
        return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        unsigned cp = first, count = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            cp = first & 31;
            count = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            cp = first & 15;
            count = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            cp = first & 7;
            count = 3;
            minimum = 0x10000;
        } else if (first >= 0x80)
            return false;
        if (count > value.size() - i)
            return false;
        for (unsigned n = 0; n < count; ++n) {
            const auto c = static_cast<unsigned char>(value[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || (cp >= 127 && cp <= 159) ||
            (cp >= 0x2028 && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069))
            return false;
    }
    return true;
}
Id peer_id(std::string_view text) {
    Id id = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9' || id > (UINT64_MAX - (c - '0')) / 10)
            return 0;
        id = id * 10 + c - '0';
    }
    return id;
}
Bytes encode(const Wire& w) {
    if (!w.room)
        return {};
    Bytes b(magic.begin(), magic.end());
    put(b, unsigned(w.kind), 1);
    put(b, w.room);
    switch (w.kind) {
    case Kind::Policy:
        if (!w.id)
            return {};
        put(b, w.id);
        put(b, w.enabled, 1);
        break;
    case Kind::Submit:
        if (!w.request || !valid_text(w.text))
            return {};
        put(b, w.request);
        text(b, w.text);
        break;
    case Kind::Delivery:
        if (!w.id || !w.request || !w.author || !valid_text(w.name, 24) || !valid_text(w.text))
            return {};
        put(b, w.id);
        put(b, w.request);
        put(b, w.author);
        put(b, w.seconds);
        text(b, w.name);
        text(b, w.text);
        break;
    case Kind::Receipt:
        if (!w.id)
            return {};
        put(b, w.id);
        break;
    case Kind::Reject:
        if (!w.request || !valid_text(w.text, 80))
            return {};
        put(b, w.request);
        text(b, w.text);
        break;
    default:
        return {};
    }
    return b;
}
bool decode(std::span<const std::uint8_t> b, Wire& output) {
    if (b.size() < 15 || b.size() > 360 || !std::equal(magic.begin(), magic.end(), b.begin()))
        return false;
    Reader r{b};
    Wire w;
    w.kind = Kind(r.number(1));
    w.room = r.number();
    switch (w.kind) {
    case Kind::Policy: {
        w.id = r.number();
        const auto enabled = r.number(1);
        if (enabled > 1)
            return false;
        w.enabled = enabled != 0;
        break;
    }
    case Kind::Submit:
        w.request = r.number();
        w.text = r.text(MaximumText);
        break;
    case Kind::Delivery:
        w.id = r.number();
        w.request = r.number();
        w.author = r.number();
        w.seconds = r.number();
        w.name = r.text(24);
        w.text = r.text(MaximumText);
        break;
    case Kind::Receipt:
        w.id = r.number();
        break;
    case Kind::Reject:
        w.request = r.number();
        w.text = r.text(80);
        break;
    default:
        return false;
    }
    if (!r.end() || encode(w).empty())
        return false;
    output = std::move(w);
    return true;
}
bool Channel::Replay::accept(Id id) {
    if (!id)
        return false;
    if (id > top) {
        const auto shift = id - top;
        bits = shift >= 64 ? 0 : bits << shift;
        top = id;
    }
    const auto distance = top - id;
    if (distance >= 64 || (bits & (Id(1) << distance)))
        return false;
    bits |= Id(1) << distance;
    return true;
}
bool Channel::Bucket::take(double capacity, double per_second, Clock::time_point now) {
    if (!at.time_since_epoch().count())
        tokens = capacity;
    else
        tokens =
            std::min(capacity, tokens + std::max(0., std::chrono::duration<double>(now - at).count()) * per_second);
    at = now;
    if (tokens < 1.)
        return false;
    tokens -= 1.;
    return true;
}
const Member* Channel::member(Id id) const {
    for (const auto& m : context_.members)
        if (m.id == id)
            return &m;
    return nullptr;
}
void Channel::configure(Context next, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (next.members.size() > MaximumMembers)
        next.members.resize(MaximumMembers);
    if (!next.active || !next.room || !next.local || !next.host)
        next = {};
    const bool changed = next.room != context_.room || next.local != context_.local || next.host != context_.host;
    const bool roster = next.members != context_.members;
    if (changed) {
        history_.clear();
        pending_.clear();
        immediate_.clear();
        requests_.clear();
        authors_.clear();
        ingress_.clear();
        room_rate_ = {};
        delivered_ = {};
        known_ = enabled_ = false;
        next_request_ = next_message_ = policy_revision_ = remote_policy_ = 0;
        policy_at_ = send_at_ = {};
        began_ = now;
    }
    const bool policy = next.allow != context_.allow;
    context_ = std::move(next);
    if (context_.active && context_.local == context_.host) {
        known_ = true;
        enabled_ = context_.allow;
        if (changed || policy || !policy_revision_) {
            ++policy_revision_;
            policy_at_ = {};
        }
        if (roster)
            policy_at_ = {};
    }
    std::erase_if(pending_,
                  [&](const auto& row) { return context_.local == context_.host && !member(row.first.first); });
    std::erase_if(immediate_,
                  [&](const auto& row) { return context_.local == context_.host && !member(std::get<0>(row.first)); });
    if (roster) {
        std::erase_if(requests_, [&](const auto& row) { return !member(row.first); });
        std::erase_if(authors_, [&](const auto& row) { return !member(row.first); });
        std::erase_if(ingress_, [&](const auto& row) { return !member(row.first); });
    }
}
void Channel::append(Message m) {
    history_.push_back(std::move(m));
    while (history_.size() > MaximumHistory)
        history_.pop_front();
}
void Channel::queue(Id target, Wire w, Clock::time_point now) {
    const auto id = w.kind == Kind::Submit ? w.request : w.id;
    const auto key = std::pair{target, id};
    if (pending_.contains(key))
        return;
    if (pending_.size() < MaximumPending)
        pending_.emplace(key, Pending{std::move(w), now, now + std::chrono::seconds(10), 0});
}
void Channel::immediate(Id target, Wire w) {
    const Id id = w.kind == Kind::Receipt ? w.id : w.kind == Kind::Reject ? w.request : w.id;
    const auto key = std::tuple{target, id, w.kind};
    if (immediate_.size() < 64 || immediate_.contains(key))
        immediate_[key] = std::move(w);
}
bool Channel::accept(Id source, Id request, const std::string& text, std::string& error, Clock::time_point now) {
    if (!enabled_) {
        error = "The host has disabled lobby chat.";
        return false;
    }
    const auto* author = member(source);
    if (!author || !valid_text(author->name, 24)) {
        error = "Chat is still connecting.";
        return false;
    }
    const auto copies = context_.members.size() > 0 ? context_.members.size() - 1 : 0;
    if (pending_.size() + copies > MaximumPending) {
        error = "Chat is busy. Please try again shortly.";
        return false;
    }
    if (!authors_[source].take(3, 1, now) || !room_rate_.take(6, 3, now)) {
        error = "Please wait a moment before sending another message.";
        return false;
    }
    if (next_message_ == UINT64_MAX) {
        error = "Please create a new lobby to continue chatting.";
        return false;
    }
    Message m{++next_message_, request, source, Id(std::time(nullptr)), author->name, text, Delivery::Sent, now};
    append(m);
    const auto wire = delivery(context_.room, m);
    for (const auto& peer : context_.members)
        if (peer.id != context_.local)
            queue(peer.id, wire, now);
    return true;
}
bool Channel::send(std::string text, std::string& error, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    error.clear();
    const auto first = text.find_first_not_of(" \t\r\n"), last = text.find_last_not_of(" \t\r\n");
    text = first == text.npos ? std::string{} : text.substr(first, last - first + 1);
    if (!valid_text(text)) {
        error = "Use a single-line message of up to 256 bytes.";
        return false;
    }
    if (!context_.active || !known_) {
        error = "Chat is still connecting.";
        return false;
    }
    if (!enabled_) {
        error = "The host has disabled lobby chat.";
        return false;
    }
    if (next_request_ == UINT64_MAX) {
        error = "Please create a new lobby to continue chatting.";
        return false;
    }
    if (context_.local == context_.host)
        return accept(context_.local, ++next_request_, text, error, now);
    if (pending_.size() >= 8) {
        error = "Please wait for your earlier messages to finish sending.";
        return false;
    }
    const auto request = ++next_request_;
    const auto* author = member(context_.local);
    append({0, request, context_.local, 0, author ? author->name : "You", text, Delivery::Waiting, now});
    Wire w;
    w.kind = Kind::Submit;
    w.room = context_.room;
    w.request = request;
    w.text = std::move(text);
    queue(context_.host, std::move(w), now);
    return true;
}
void Channel::fail(Id request) {
    for (auto& m : history_)
        if (!m.id && m.author == context_.local && m.request == request)
            m.delivery = Delivery::Failed;
}
bool Channel::retry(Id request, std::string& error, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    error.clear();
    if (!context_.active || !known_ || !enabled_ || pending_.size() >= 8) {
        error = "Chat is not ready to resend this message.";
        return false;
    }
    for (auto& m : history_)
        if (m.author == context_.local && m.request == request && m.delivery == Delivery::Failed) {
            m.delivery = Delivery::Waiting;
            Wire w;
            w.kind = Kind::Submit;
            w.room = context_.room;
            w.request = request;
            w.text = m.text;
            queue(context_.host, std::move(w), now);
            return true;
        }
    error = "This message is no longer available to resend.";
    return false;
}
bool Channel::receive(Id source, std::span<const std::uint8_t> bytes, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!context_.active || !source || source == context_.local ||
        (context_.local == context_.host ? !member(source) : source != context_.host) ||
        !ingress_[source].take(48, 24, now))
        return false;
    Wire w;
    if (!decode(bytes, w) || w.room != context_.room)
        return false;
    const bool host = context_.local == context_.host;
    if (w.kind == Kind::Policy && !host) {
        if (w.id <= remote_policy_)
            return false;
        remote_policy_ = w.id;
        known_ = true;
        enabled_ = w.enabled;
        if (!enabled_) {
            for (const auto& [key, p] : pending_)
                if (p.wire.kind == Kind::Submit)
                    fail(p.wire.request);
            pending_.clear();
        }
        return true;
    }
    if (w.kind == Kind::Submit && host) {
        for (const auto& m : history_)
            if (m.author == source && m.request == w.request && m.id) {
                if (authors_[source].take(3, 1, now))
                    queue(source, delivery(context_.room, m), now);
                return true;
            }
        std::string error;
        auto seen = requests_[source];
        if (!seen.accept(w.request))
            error = "This message has expired.";
        else if (accept(source, w.request, w.text, error, now)) {
            requests_[source] = seen;
            return true;
        }
        Wire reject;
        reject.kind = Kind::Reject;
        reject.room = context_.room;
        reject.request = w.request;
        reject.text = std::move(error);
        immediate(source, std::move(reject));
        return true;
    }
    if (w.kind == Kind::Receipt && host) {
        pending_.erase({source, w.id});
        return true;
    }
    if (w.kind == Kind::Reject && !host) {
        const auto p = pending_.find({context_.host, w.request});
        if (p == pending_.end())
            return false;
        fail(w.request);
        pending_.erase(p);
        return true;
    }
    if (w.kind == Kind::Delivery && !host) {
        Wire ack;
        ack.kind = Kind::Receipt;
        ack.room = context_.room;
        ack.id = w.id;
        immediate(source, std::move(ack));
        if (!delivered_.accept(w.id))
            return true;
        Message m{w.id, w.request, w.author, w.seconds, w.name, w.text, Delivery::Sent, now};
        bool replaced = false;
        if (w.author == context_.local) {
            pending_.erase({context_.host, w.request});
            for (auto& old : history_)
                if (!old.id && old.author == w.author && old.request == w.request) {
                    old = m;
                    replaced = true;
                    break;
                }
        }
        if (!replaced)
            append(std::move(m));
        return true;
    }
    return false;
}
void Channel::service(Clock::time_point now) {
    for (auto p = pending_.begin(); p != pending_.end();)
        if (now >= p->second.expires) {
            if (p->second.wire.kind == Kind::Submit)
                fail(p->second.wire.request);
            p = pending_.erase(p);
        } else
            ++p;
    if (context_.local == context_.host && now >= policy_at_) {
        Wire w;
        w.kind = Kind::Policy;
        w.room = context_.room;
        w.id = policy_revision_;
        w.enabled = enabled_;
        for (const auto& m : context_.members)
            if (m.id != context_.local)
                immediate(m.id, w);
        policy_at_ = now + std::chrono::seconds(2);
    }
}
std::vector<Packet> Channel::outgoing(Clock::time_point now) {
    std::lock_guard lock(mutex_);
    std::vector<Packet> result;
    if (!context_.active)
        return result;
    service(now);
    if (now < send_at_)
        return result;
    send_at_ = now + std::chrono::milliseconds(50);
    for (auto p = immediate_.begin(); p != immediate_.end() && result.size() < 2;) {
        result.push_back({std::get<0>(p->first), encode(p->second)});
        p = immediate_.erase(p);
    }
    for (auto& [key, p] : pending_)
        if (result.size() < 2 && now >= p.next && p.attempts < 6) {
            result.push_back({key.first, encode(p.wire)});
            ++p.attempts;
            p.next = now + std::chrono::milliseconds(1000);
        }
    return result;
}
View Channel::view(Clock::time_point now, std::size_t maximum) const {
    std::lock_guard lock(mutex_);
    View v{context_.room,
           context_.local,
           context_.active,
           context_.local == context_.host,
           known_,
           enabled_,
           context_.members,
           {},
           {}};
    const auto count = std::min(maximum, history_.size());
    v.messages.assign(history_.end() - std::ptrdiff_t(count), history_.end());
    if (v.active && !v.known)
        v.status = now - began_ > std::chrono::seconds(6) ? "Chat is not available in this lobby."
                                                          : "Connecting lobby chat...";
    else if (v.active && !v.enabled)
        v.status = "The host has disabled lobby chat.";
    return v;
}
std::shared_ptr<Channel> channel(const void* key) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    auto& p = r.sessions[key];
    if (!p)
        p = std::make_shared<Channel>();
    return p;
}
void reset(const void* key) {
    auto& r = registry();
    std::shared_ptr<Channel> p;
    {
        std::lock_guard lock(r.mutex);
        const auto i = r.sessions.find(key);
        if (i == r.sessions.end())
            return;
        p = std::move(i->second);
        r.sessions.erase(i);
    }
    p->configure({});
}
} // namespace dkr::chat

#pragma once
// The decisions DirectSession's owner thread makes for session chat, as pure
// functions over plain data: who is in the room (build_context) and which of
// the channel's packets go to whom (route). The session fills the structs under
// its locks, calls these, and performs the sends itself.
#include "netplay/session_chat/chat.hpp"

#include <string>
#include <vector>

namespace dkr::chat {

struct RosterSnapshot {
    // How the session learns its members.
    enum class Source {
        Party,       // a party lobby: the party roster
        HostLobby,   // classic lobby, this device hosts: its connected peers
        ClientLobby, // classic lobby, joined: the relay's live roster plus the room's slot list
    };
    struct Peer {
        Id id = 0;
        bool active = false;
        unsigned slot = 0;
    };
    struct Slot {
        bool occupied = false;
        std::string peer_id; // the text form chat::peer_id() parses
        std::string name;
    };

    Source source = Source::ClientLobby;
    Id room = 0, local = 0;
    // The host's sender id as a client knows it (0 until learned). Ignored when is_host.
    Id host = 0;
    bool is_host = false;
    // The connection is hosting, in the lobby, loading or running.
    bool connected = false;
    // The host's chat switch (chat::host_enabled).
    bool allowed = true;
    std::string local_name;
    std::vector<Member> party; // Source::Party: every party member, this device included
    std::vector<Peer> peers;   // Source::HostLobby
    std::vector<Slot> slots;   // the room's player list (names for peers, ids for clients)
    std::vector<Member> relay; // Source::ClientLobby: relay lobby members (endpoint, name)
};

Context build_context(const RosterSnapshot&);

struct RouteTable {
    bool is_host = false;
    // Client: the host's sender id, the only recipient. Ignored for a host.
    Id host = 0;
    // Host: the ids that can be reached right now (party peers, or active classic peers).
    std::vector<Id> reachable;
};
struct Send {
    Id target = 0;
    Bytes bytes;
};

// The packets the session will send, in their order. A host sends to reachable
// members only; a client sends to the host only. Anything else is dropped.
std::vector<Send> route(std::vector<Packet> packets, const RouteTable&);

} // namespace dkr::chat

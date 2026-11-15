#pragma once

#include "revision_symbols.hpp"
#include "rom_revision.hpp"

#include <atomic>
#include <cstdint>

// These are decomp symbol addresses, not guessed offsets. The selected ROM
// chooses one immutable address table inside the shared frontend/runtime,
// without compiling a second renderer, window, input stack, or UI for another
// retail revision. Read it through current().
namespace dkr::runtime::revision_addresses {

// AddressTable and the immutable kUsV77 / kUsV80 tables are generated from
// runtime-recomp/symbols/revision_addresses.toml (scripts/gen_guest_symbols.py).

// Threading contract.
//
// select() is a startup step. The shipped app calls it once, from
// RegisterGame on the main thread, before the CPU payload's sections are
// registered and before any guest, render or audio thread exists. The
// experimental race frontend and the host tests call it on their own single
// thread before starting anything that reads. Nothing switches revision while
// the game runs.
//
// current() goes through a single atomic pointer to an immutable table, so a
// reader sees either the whole old table or the whole new one, never a
// mixture. select() stores the revision first and publishes the pointer last,
// with release ordering, so a thread that acquires the new pointer also sees
// the new revision.
inline std::atomic<const AddressTable*> gCurrentTable{&kUsV77};
inline std::atomic<rom::Revision> gSelectedRevision{rom::Revision::UsV77};

inline const AddressTable& table_for(const rom::Revision revision) {
    return revision == rom::Revision::UsV80 ? kUsV80 : kUsV77;
}

// The table for the selected revision. See the threading contract above.
inline const AddressTable& current() {
    return *gCurrentTable.load(std::memory_order_acquire);
}

inline bool select(const rom::Revision revision) {
    if (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80) {
        return false;
    }

    const AddressTable& table = table_for(revision);
    gSelectedRevision.store(revision, std::memory_order_release);
    gCurrentTable.store(&table, std::memory_order_release);
    return true;
}

inline rom::Revision selected_revision() {
    return gSelectedRevision.load(std::memory_order_acquire);
}

} // namespace dkr::runtime::revision_addresses

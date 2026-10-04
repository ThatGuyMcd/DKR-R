#include "custom_music_sequence.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace dkr::runtime::custom_music {
namespace {

constexpr std::size_t kHeaderBytes = 68;
constexpr std::size_t kTracks = 16;
// Work bounds, as in the addon: a valid song needs nowhere near them.
constexpr std::uint32_t kMaxEvents = 400000U;
constexpr std::uint32_t kMaxEventsPerTick = 4096U;
constexpr std::uint64_t kMaxTicks = 1ULL << 30;

constexpr std::uint8_t kMeta = 0xFF;
constexpr std::uint8_t kMetaTempo = 0x51;
constexpr std::uint8_t kMetaEnd = 0x2F;
constexpr std::uint8_t kMetaLoopStart = 0x2E;
constexpr std::uint8_t kMetaLoopEnd = 0x2D;
constexpr std::uint8_t kBlock = 0xFE;
constexpr std::uint8_t kInfinite = 0xFF;

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kControl = 0xB0;
constexpr std::uint8_t kProgram = 0xC0;
constexpr std::uint8_t kChannelPressure = 0xD0;

struct Failure {
    std::string code;
    std::string message;
};

[[noreturn]] void fail(std::string code, std::string message) {
    throw Failure{std::move(code), std::move(message)};
}

std::uint32_t be32(std::span<const std::uint8_t> data, std::size_t offset) {
    return (std::uint32_t{data[offset]} << 24) | (std::uint32_t{data[offset + 1]} << 16) |
           (std::uint32_t{data[offset + 2]} << 8) | std::uint32_t{data[offset + 3]};
}

// __getTrackByte and __readVarLen for one track, with the bounds the original
// does not check.
struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t track = 0;
    std::size_t position = 0;
    std::size_t backup = 0;
    std::size_t backup_left = 0;

    [[noreturn]] void fail_track(const char* code, const std::string& what) const {
        fail(code, "Track " + std::to_string(track + 1) + " " + what);
    }

    std::uint8_t raw(std::size_t at) const {
        if (at >= data.size()) {
            fail_track("past-end", "runs past the end of the sequence.");
        }
        return data[at];
    }

    std::uint8_t byte() {
        if (backup_left != 0U) {
            const std::uint8_t value = raw(backup);
            ++backup;
            --backup_left;
            return value;
        }
        const std::size_t code = position;
        const std::uint8_t value = raw(position++);
        if (value != kBlock) {
            return value;
        }
        const std::uint8_t following = raw(position++);
        if (following == kBlock) {
            return kBlock;  // FE FE is a literal FE.
        }
        const std::size_t distance = (std::size_t{following} << 8) | raw(position);
        const std::size_t length = raw(position + 1);
        position += 2;
        // cseq.c reads `length` bytes from the source with no checks; a zero
        // length wraps curBULen to 0xFFFFFFFF.
        if (length == 0U) {
            fail_track("backref-empty", "has a back reference of length zero.");
        }
        if (position < distance + 4U || position - (distance + 4U) < kHeaderBytes ||
            position - (distance + 4U) + length > code) {
            fail_track("backref-range", "has a back reference outside the bytes before it.");
        }
        const std::size_t source = position - (distance + 4U);
        backup = source + 1U;
        backup_left = length - 1U;
        return raw(source);
    }

    std::uint32_t var_len() {
        std::uint32_t value = byte();
        if ((value & 0x80U) == 0U) {
            return value;
        }
        value &= 0x7FU;
        for (int i = 0; i < 3; ++i) {
            const std::uint8_t more = byte();
            value = (value << 7) | (more & 0x7FU);
            if ((more & 0x80U) == 0U) {
                return value;
            }
        }
        fail_track("varlen", "has a variable-length number longer than four bytes.");
    }
};

enum class Kind { Midi, Tempo, End, LoopStart, LoopEnd };

struct Event {
    Kind kind = Kind::Midi;
    std::uint8_t status = 0;
    std::uint8_t byte1 = 0;
    std::uint8_t byte2 = 0;
    std::uint32_t duration = 0;
    std::uint32_t tempo = 0;
    std::uint8_t loop_count = 0;
    std::uint8_t loop_current = 0;
    std::int64_t loop_target = 0;
};

std::string hex2(unsigned value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    return std::string{"0x"} + digits[(value >> 4) & 0xFU] + digits[value & 0xFU];
}

// __alCSeqGetTrackEvent: one event; updates the running status.
Event read_event(Reader& reader, std::uint8_t& last_status) {
    Event event;
    const std::uint8_t status = reader.byte();
    if (status == kMeta) {
        const std::uint8_t kind = reader.byte();
        if (kind == kMetaTempo) {
            event.kind = Kind::Tempo;
            event.tempo = std::uint32_t{reader.byte()} << 16;
            event.tempo |= std::uint32_t{reader.byte()} << 8;
            event.tempo |= reader.byte();
            if (event.tempo == 0U) {
                reader.fail_track("tempo-zero", "sets a tempo of zero microseconds per beat.");
            }
            last_status = 0;
            return event;
        }
        if (kind == kMetaEnd) {
            event.kind = Kind::End;
            return event;
        }
        if (kind == kMetaLoopStart) {
            reader.byte();
            reader.byte();
            event.kind = Kind::LoopStart;
            last_status = 0;
            return event;
        }
        if (kind == kMetaLoopEnd) {
            // The handler reads its six bytes straight from curLoc, past any
            // back reference in progress, and jumps relative to them.
            if (reader.backup_left != 0U) {
                reader.fail_track("loop-in-backref", "ends a loop inside a back reference.");
            }
            const std::size_t at = reader.position;
            if (at + 6U > reader.data.size()) {
                reader.fail_track("past-end", "runs past the end of the sequence.");
            }
            event.kind = Kind::LoopEnd;
            event.loop_count = reader.data[at];
            event.loop_current = reader.data[at + 1U];
            event.loop_target = static_cast<std::int64_t>(at + 6U) -
                                static_cast<std::int64_t>(be32(reader.data, at + 2U));
            reader.position = at + 6U;
            last_status = 0;
            return event;
        }
        // Release builds leave event->type uninitialised here.
        reader.fail_track("meta-unknown", "has meta event " + hex2(kind) +
                                              ", which DKR's player does not understand.");
    }
    if ((status & 0x80U) != 0U) {
        if (status >= 0xF0U) {
            reader.fail_track("system-message", "has system message " + hex2(status) +
                                                    "; compact sequences carry only channel "
                                                    "messages and meta events.");
        }
        event.status = status;
        event.byte1 = reader.byte();
        last_status = status;
    } else {
        if (last_status == 0U) {
            reader.fail_track("running-status",
                              "uses running status with no status before it (after a meta "
                              "event or at a loop start every event must carry its own).");
        }
        event.status = last_status;
        event.byte1 = status;
    }
    const std::uint8_t kind = event.status & 0xF0U;
    if (kind != kProgram && kind != kChannelPressure) {
        event.byte2 = reader.byte();
        if (kind == kNoteOn) {
            event.duration = reader.var_len();
        }
    }
    if (event.byte1 > 0x7FU || event.byte2 > 0x7FU) {
        reader.fail_track("data-byte", "has a data byte above 0x7F in a " + hex2(event.status) +
                                           " message.");
    }
    return event;
}

struct Loop {
    std::uint64_t start_tick = 0;
    std::uint64_t end_tick = 0;
    bool infinite = false;
};

// A track read once, front to back, as if every loop had finished: where each
// event starts and at which tick, which is what loop targets must hit.
struct TrackScan {
    std::map<std::size_t, std::uint64_t> boundaries;
    std::vector<Loop> loops;
};

TrackScan scan_track(std::span<const std::uint8_t> data, std::size_t track, std::size_t start) {
    TrackScan scan;
    Reader reader{data, track, start};
    std::vector<std::pair<std::uint64_t, Event>> loop_ends;
    std::uint64_t tick = 0;
    std::uint8_t last_status = 0;
    std::uint32_t events = 0;
    for (;;) {
        if (reader.backup_left == 0U) {
            scan.boundaries.emplace(reader.position, tick);
        }
        tick += reader.var_len();
        if (tick > kMaxTicks) {
            reader.fail_track("too-long", "is longer than any song could be.");
        }
        const Event event = read_event(reader, last_status);
        if (++events > kMaxEvents) {
            reader.fail_track("too-many-events", "has more events than a song could hold.");
        }
        if (event.kind == Kind::End) {
            break;
        }
        if (event.kind == Kind::LoopEnd) {
            loop_ends.emplace_back(tick, event);
            if (event.loop_current == kInfinite) {
                break;  // never passed: what follows is unreachable
            }
        }
    }
    for (const auto& [end_tick, event] : loop_ends) {
        const auto found = event.loop_target < 0
                               ? scan.boundaries.end()
                               : scan.boundaries.find(static_cast<std::size_t>(event.loop_target));
        if (found == scan.boundaries.end()) {
            reader.fail_track("loop-target",
                              "loops back to a byte that is not the start of an event.");
        }
        if (found->second >= end_tick) {
            // A loop over no ticks would replay forever within one frame.
            reader.fail_track("loop-direction", "loops forward or over no time; a loop must "
                                                "jump back across at least one tick.");
        }
        scan.loops.push_back({found->second, end_tick, event.loop_current == kInfinite});
    }
    return scan;
}

struct TrackState {
    Reader reader;
    std::uint64_t next_tick = 0;
    std::uint8_t last_status = 0;
    bool done = false;
};

void check_midi(const Event& event, std::uint64_t tick, std::size_t track,
                SequenceReport& report, const std::array<bool, 128>* programs) {
    const std::uint8_t kind = event.status & 0xF0U;
    const unsigned channel = event.status & 0x0FU;
    const std::string where = "Track " + std::to_string(track + 1) + ", tick " +
                              std::to_string(tick);
    if (kind == kNoteOn) {
        if (event.byte2 == 0U) {
            fail("velocity-zero", where + ": a note-on with velocity zero; compact sequences "
                                          "give every note a duration instead.");
        }
        if (event.duration == 0U) {
            fail("duration-zero", where + ": a note with no duration never ends.");
        }
        report.channel_mask = static_cast<std::uint16_t>(report.channel_mask | (1U << channel));
        ++report.notes;
    } else if (kind == kNoteOff) {
        fail("note-off", where + ": an explicit note-off; compact sequences give every note a "
                                 "duration instead.");
    } else if (kind == kProgram) {
        // __setInstChanState dereferences the slot: a null one crashes.
        if (programs != nullptr && !(*programs)[event.byte1]) {
            fail("program-missing", where + ": program " + std::to_string(event.byte1) +
                                        " does not exist in DKR's instrument bank.");
        }
    } else if (kind == kControl) {
        if ((event.byte1 == 0x6AU || event.byte1 == 0x6CU) && event.byte2 > 15U) {
            fail("channel-switch", where + ": a channel switch names channel " +
                                       std::to_string(event.byte2 + 1U) + ".");
        }
    }
}

SequenceReport check(std::span<const std::uint8_t> data, std::uint32_t capacity,
                     const std::array<bool, 128>* programs) {
    SequenceReport report;
    report.size = static_cast<std::uint32_t>(std::min<std::size_t>(data.size(), UINT32_MAX));
    if (data.size() < kHeaderBytes) {
        fail("too-short", "Too short to be a DKR sequence (" + std::to_string(data.size()) +
                              " bytes).");
    }
    if (data.size() > capacity) {
        fail("too-large", "The sequence is " + std::to_string(data.size()) +
                              " bytes; DKR's music buffer holds " + std::to_string(capacity) +
                              ".");
    }
    report.division = be32(data, 64);
    if (report.division == 0U || report.division > 0x7FFFU) {
        fail("division", "The time division (" + std::to_string(report.division) +
                             " ticks per beat) is not usable.");
    }
    std::array<std::size_t, kTracks> offsets{};
    for (std::size_t track = 0; track < kTracks; ++track) {
        offsets[track] = be32(data, track * 4U);
        if (offsets[track] != 0U) {
            report.track_mask = static_cast<std::uint16_t>(report.track_mask | (1U << track));
        }
    }
    if (report.track_mask == 0U) {
        fail("no-tracks", "The sequence has no tracks.");
    }
    for (std::size_t track = 0; track < kTracks; ++track) {
        if (offsets[track] != 0U && (offsets[track] < kHeaderBytes || offsets[track] >= data.size())) {
            fail("track-offset", "Track " + std::to_string(track + 1) +
                                     " starts outside the sequence.");
        }
    }

    // Follow the first pass and one full repeat of the longest endless loop;
    // a song that ends is followed to its end.
    std::optional<std::uint64_t> horizon;
    for (std::size_t track = 0; track < kTracks; ++track) {
        if (offsets[track] == 0U) {
            continue;
        }
        for (const Loop& loop : scan_track(data, track, offsets[track]).loops) {
            if (loop.infinite) {
                report.loops_forever = true;
                const std::uint64_t until = loop.end_tick + (loop.end_tick - loop.start_tick);
                horizon = std::max(horizon.value_or(0U), until);
            }
        }
    }

    // alCSeqNextEvent over a private copy: loop counters are written back.
    std::vector<std::uint8_t> buffer(data.begin(), data.end());
    const std::span<const std::uint8_t> view{buffer};
    std::array<std::optional<TrackState>, kTracks> tracks;
    for (std::size_t track = 0; track < kTracks; ++track) {
        if (offsets[track] != 0U) {
            TrackState state{Reader{view, track, offsets[track]}};
            state.next_tick = state.reader.var_len();
            tracks[track] = state;
        }
    }
    std::uint32_t events = 0;
    std::uint32_t same_tick = 0;
    std::uint64_t tick = 0;
    for (;;) {
        std::size_t next = kTracks;
        for (std::size_t track = 0; track < kTracks; ++track) {
            if (tracks[track] && !tracks[track]->done &&
                (next == kTracks || tracks[track]->next_tick < tracks[next]->next_tick)) {
                next = track;
            }
        }
        if (next == kTracks) {
            break;
        }
        TrackState& state = *tracks[next];
        if (horizon && state.next_tick >= *horizon) {
            break;
        }
        same_tick = state.next_tick == tick ? same_tick + 1U : 0U;
        tick = state.next_tick;
        if (same_tick > kMaxEventsPerTick) {
            fail("zero-time", "Track " + std::to_string(next + 1) +
                                  " produces events forever without time passing.");
        }
        if (++events > kMaxEvents) {
            fail("too-many-events", "The song has more events than the validator will follow.");
        }
        Reader& reader = state.reader;
        const Event event = read_event(reader, state.last_status);
        if (event.kind == Kind::End) {
            state.done = true;
            continue;
        }
        if (event.kind == Kind::Tempo) {
            if (tick == 0U && report.initial_tempo == 0U) {
                report.initial_tempo = event.tempo;
            }
        } else if (event.kind == Kind::LoopEnd) {
            const std::size_t at = reader.position - 6U;
            if (event.loop_current == 0U) {
                buffer[at + 1U] = event.loop_count;
            } else {
                if (event.loop_current != kInfinite) {
                    buffer[at + 1U] = static_cast<std::uint8_t>(event.loop_current - 1U);
                }
                reader.position = static_cast<std::size_t>(event.loop_target);
                reader.backup_left = 0;
            }
        } else if (event.kind == Kind::Midi) {
            check_midi(event, tick, next, report, programs);
        }
        state.next_tick = tick + reader.var_len();
    }
    if (report.notes == 0U) {
        fail("no-notes", "The sequence plays no notes.");
    }
    return report;
}

} // namespace

SequenceCheck validate_sequence(std::span<const std::uint8_t> data, std::uint32_t capacity,
                                const std::array<bool, 128>* programs) {
    SequenceCheck result;
    try {
        result.report = check(data, capacity, programs);
        result.code = "ok";
    } catch (const Failure& failure) {
        result.code = failure.code;
        result.message = failure.message;
    }
    return result;
}

} // namespace dkr::runtime::custom_music

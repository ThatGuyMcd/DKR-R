#include "startup_flow.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace dkr::runtime::startup;
    assert(!skip_logos());
    for (bool requested : {false, true}) {
        for (bool online : {false, true}) {
            for (bool direct : {false, true}) {
                prepare_launch(requested, online, direct);
                const bool expected = requested && !online && !direct;
                assert(skip_logos() == expected);
                assert(consume_title_skip() == expected);
                assert(!skip_logos());
                assert(!consume_title_skip()); // returning to the title never re-skips
            }
        }
    }
    prepare_launch(true, false, false);
    prepare_launch(false, true, false); // next online launch cancels stale request
    assert(!consume_title_skip());
    assert(parse_skip_intro("1"));
    assert(parse_skip_intro("1\r"));
    assert(parse_destination("1\r") == Destination::CharacterSelect);
    assert(parse_destination("0") == Destination::StartMenu);
    assert(parse_destination("2") == Destination::StartMenu);
    character_select_pending = true;
    prepare_launch(true, true, false, Destination::CharacterSelect);
    assert(!consume_character_select());
    for (const auto text : {"", "0", "true", "2", "-1", "1junk"})
        assert(!parse_skip_intro(text));
    std::puts("Offline title skip, settings parsing, one-shot and online isolation passed.");
}

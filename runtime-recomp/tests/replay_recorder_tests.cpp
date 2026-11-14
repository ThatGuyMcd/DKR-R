#include "replay_recorder.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    using namespace dkr::runtime::netplay;
    const auto directory = std::filesystem::temp_directory_path() / "dkr-r-replay-recorder-test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    Room room{};
    room.room_id = "test-room";
    room.rules.record_replay = true;
    room.players[0].occupied = true;
    room.players[1].occupied = true;
    room.manifest.release_version = "test";
    ReplayRecorder recorder;
    recorder.configure(directory);
    recorder.begin(room);
    FrameInputs inputs{};
    inputs[0] = {0x8000U, 10, -4};
    inputs[1] = {0x4000U, -9, 3};
    recorder.record(0U, inputs);
    recorder.record(1U, inputs);
    std::string error;
    auto detached = recorder.detach();
    assert(!recorder.active() && detached.active());
    recorder.begin(room); // a new session cannot mutate the detached recording
    assert(detached.finalize(error));
    assert(std::filesystem::is_regular_file(detached.last_path()));
    assert(std::filesystem::file_size(detached.last_path()) > 32U);
    std::filesystem::remove_all(directory, cleanup_error);
    std::cout << "replay recorder tests passed\n";
}

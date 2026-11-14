#include "file_picker/protocol.hpp"
#include "file_picker/native_arguments.hpp"
#include "updates/update_process.hpp"
#include <fstream>
#include <thread>
#include <cstdlib>
int main(int argc, char** argv) {
    using namespace dkr::file_picker;
    auto values = native_arguments(argc, argv);
    std::vector<char*> arguments;
    for (auto& value : values)
        arguments.push_back(value.data());
    argc = arguments.size();
    argv = arguments.data();
    if (argc != 4)
        return 2;
    const auto root = std::filesystem::u8path(argv[2]);
    std::ifstream in(root / "request.json");
    Json request;
    in >> request;
    validate(request);
    const auto mode = request["name"].get<std::string>();
    if (mode == "crash") {
#ifdef _WIN32
        // A shell fault uses an exception exit status, not an ordinary code.
        TerminateProcess(GetCurrentProcess(), 0xC0000094);
#endif
        return 139;
    }
    if (mode == "timeout") {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (mode == "malformed") {
        dkr::updates::durable_write(root / "result.json", "bad JSON");
        return 0;
    }
    Json result{{"schema", 1},
                {"status", mode == "cancel" ? "cancel" : "okay"},
                {"path", mode == "cancel" ? "" : "/tmp/My Games/été 日本語.z64"}};
    if (const auto* loader = std::getenv("LD_LIBRARY_PATH"))
        result["loader"] = loader;
    dkr::updates::durable_write(root / "result.json", result.dump());
    return 0;
}

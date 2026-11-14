// macOS: the import worker sandbox must confine writes to its transaction
// directory and deny network, fork and exec, and a worker must exit with its
// parent. Each check runs in a forked child.
#include "legacy_mod_process.hpp"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
bool can_write(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    return out && (out << "x") && out.flush();
}
} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkr-mod-sandbox-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto inside = root / "transaction";
    std::filesystem::create_directories(inside);

    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        try {
            dkr::mods::sandbox_import_worker(inside);
        } catch (...) {
            _exit(10);
        }
        if (!can_write(inside / "staged.bin"))
            _exit(11);
        if (can_write(root / "escaped.bin"))
            _exit(12);
        const int network = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in loopback{};
        loopback.sin_family = AF_INET;
        loopback.sin_port = htons(9);
        loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // Seatbelt may refuse the socket or the connect; either way with EPERM.
        if (network < 0) {
            if (errno != EPERM)
                _exit(16);
        } else {
            if (connect(network, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == 0)
                _exit(13);
            if (errno != EPERM)
                _exit(14);
        }
        const pid_t grandchild = fork();
        if (grandchild == 0)
            _exit(0);
        if (grandchild > 0 || errno != EPERM)
            _exit(17);
        char* arguments[] = {const_cast<char*>("/usr/bin/true"), nullptr};
        execv(arguments[0], arguments);
        _exit(errno == EPERM ? 0 : 15);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 100;
    std::filesystem::remove_all(root);
    if (code != 0)
        std::fprintf(stderr, "sandbox check failed: %d\n", code);
    assert(code == 0);
    std::puts("[test][mod-sandbox] writes confined, network, fork and exec denied");

    // Parent-death watch: the intermediate parent exits only after its
    // constrained worker reports the watch armed; the worker must then be
    // gone within a few seconds.
    int pid_pipe[2]{}, ready_pipe[2]{};
    assert(pipe(pid_pipe) == 0 && pipe(ready_pipe) == 0);
    const pid_t intermediate = fork();
    assert(intermediate >= 0);
    if (intermediate == 0) {
        close(pid_pipe[0]);
        const pid_t worker = fork();
        if (worker == 0) {
            close(ready_pipe[0]);
            try {
                dkr::mods::constrain_import_worker();
            } catch (...) {
                _exit(10);
            }
            const char armed = 1;
            if (write(ready_pipe[1], &armed, 1) != 1)
                _exit(10);
            for (;;)
                pause();
        }
        close(ready_pipe[1]);
        char armed = 0;
        if (worker < 0 || read(ready_pipe[0], &armed, 1) != 1)
            _exit(12);
        if (write(pid_pipe[1], &worker, sizeof(worker)) != sizeof(worker))
            _exit(11);
        _exit(0);
    }
    close(pid_pipe[1]);
    close(ready_pipe[0]);
    close(ready_pipe[1]);
    pid_t worker = 0;
    const bool got_pid = read(pid_pipe[0], &worker, sizeof(worker)) == sizeof(worker);
    assert(waitpid(intermediate, &status, 0) == intermediate);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !got_pid)
        std::fprintf(stderr, "parent-death setup failed: %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : 100);
    assert(got_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    bool exited = false;
    for (int attempt = 0; attempt < 200 && !exited; ++attempt) {
        exited = kill(worker, 0) != 0 && errno == ESRCH;
        if (!exited)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (!exited) {
        kill(worker, SIGKILL);
        std::fprintf(stderr, "worker outlived its parent\n");
    }
    assert(exited);
    std::puts("[test][mod-sandbox] worker exits with its parent");
    return 0;
}

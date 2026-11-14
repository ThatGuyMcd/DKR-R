#include "crash_reports/reporting.hpp"
#include "updates/update_core.hpp"
#include "updates/update_process.hpp"
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <json/json.hpp>
#include <miniz/miniz.h>

using namespace dkr::crash_reports;
namespace fs = std::filesystem;
using Json = nlohmann::json;
static std::string entry(const Pending& report, const char* name) {
    mz_zip_archive zip{};
    assert(mz_zip_reader_init_mem(&zip, report.bytes.data(), report.bytes.size(), 0));
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
    const std::string result = data ? std::string(static_cast<char*>(data), size) : std::string{};
    mz_free(data);
    mz_zip_reader_end(&zip);
    return result;
}
static void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    assert(out.good());
}
int main() {
    // The temp dir can sit behind a link (macOS /var, a Windows 8.3 or junction profile path);
    // callers hand the API a canonical profile, so the fixture does too.
    const auto id = dkr::updates::unique_id();
    const auto root = fs::canonical(fs::temp_directory_path()) / ("dkr-crash-report-test-" + id);
    const auto alias = fs::temp_directory_path() / ("dkr-crash-report-alias-" + id);
    fs::create_directories(root / "logs");
    fs::create_directory(root / "crash-dumps");
    try {
        const auto now = now_ms();
        const Consent enabled{1, false, now - 10000};
        const auto reportRoot = root / "crash-reports";
        Queue queue(root, "1.0.14-beta.3", {"ARealPerson", "PrivateMachine"});
        assert(read_consent(reportRoot).choice == -1);
        const auto crash =
            std::string(
                "[boot] DKR-R 1.0.14 persistent runtime log\n[boot][binary] id=0123456789abcdef0123456789abcdef01234567\n[boot][crash] exception=0xC0000005 rva=0x1234\n") +
            "[boot][crash] minidump=C:\\Users\\ARealPerson\\DKR-R-crash-20261103-120000.dmp status=written\n" +
            "[debug] /home/ARealPerson/My Games/ROM.z64\n" + "[online] token=topSecret12345\n[chat] Private message\n" +
            "[debug] ARealPerson PrivateMachine 192.168.1.8 fe80::1234 alice@example.com\n" +
            "[debug] https://example.com/?key=secret\n[boot][crash] #1 racer_update+0x20\n" +
            "/tmp/.mount_DKR-R/ARealPerson/DKR-R(+0x123abc) [0x7fff12345678]\n" +
            "/home/ARealPerson/My Games/DKR-R(racer_update+0x456def) [0x7fff456789ab]\n";
        write(root / "logs/runtime-previous.log", crash);
        write(root / "logs/runtime.log", "[boot] fresh launch\n");
        write(root / "crash-dumps/DKR-R-crash-20261103-120000.dmp", "MDMP synthetic process memory");
        queue.discover({}, now);
        assert(queue.count() == 0);
        queue.discover({0, false, now}, now);
        assert(queue.count() == 0);
        queue.discover({1, false, now + 10000}, now);
        assert(queue.count() == 0); // no retroactive upload
        write_consent(reportRoot, enabled);
        assert(read_consent(reportRoot).choice == 1 && !read_consent(reportRoot).dumps);
        queue.discover(enabled, now);
        assert(queue.count() == 1);
        auto job = queue.due(now);
        assert(job.id.size() == 32 && !job.bytes.empty());
        const auto log = entry(job, "runtime-previous.log");
        for (const auto* privateValue : {"ARealPerson", "PrivateMachine", "192.168.1.8", "fe80::1234", "alice@",
                                         "topSecret", "Private message", "example.com", "C:\\Users"})
            assert(log.find(privateValue) == std::string::npos);
        assert(log.find("racer_update") != std::string::npos && log.find("0xC0000005") != std::string::npos);
        assert(log.find("legacy-frame rva=0x123abc pc=0x7fff12345678") != std::string::npos);
        assert(log.find("legacy-frame rva=0x456def pc=0x7fff456789ab") != std::string::npos);
        const auto manifest = Json::parse(entry(job, "manifest.json"));
        assert(manifest["version"] == "1.0.14" && manifest["collectorVersion"] == "1.0.14-beta.3");
        assert(manifest["producerBinaryId"] == "0123456789abcdef0123456789abcdef01234567");
        assert(entry(job, "crash.dmp").empty());
        assert(!Json::parse(entry(job, "manifest.json"))["rawDump"].get<bool>());
        const auto frozen = job.bytes;
        write(root / "logs/runtime.log", "[boot] another log\n");
        queue.discover(enabled, now);
        assert(queue.count() == 1 && queue.due(now).bytes == frozen);
        queue.delivered(job.id, {}, now);
        assert(queue.due(now + 29999).id.empty());
        assert(queue.due(now + 30000).bytes == frozen); // offline retry retains same snapshot
        Queue restarted(root, "1.0.14-beta.3");
        assert(restarted.count() == 1 && restarted.due(now + 30000).id == job.id);
        restarted.delivered(job.id, {429, 180, false}, now + 30000);
        assert(restarted.due(now + 209999).id.empty());
        assert(restarted.due(now + 210000).id == job.id);
        restarted.delivered(job.id, {200, 0, false}, now + 210000);
        assert(restarted.count() == 1); // an unconfirmed 200 is not an ack
        restarted.delivered(job.id, {200, 0, true}, now + 240000);
        restarted.discover(enabled, now + 240000);
        assert(restarted.count() == 0); // receipt survives restart
        write(root / "logs/runtime-previous.log", crash + "[boot][crash] second crash\n");
        restarted.discover({1, true, enabled.since}, now);
        auto raw = restarted.due(now);
        assert(!entry(raw, "crash.dmp").empty());
        restarted.reconcile(enabled, now);
        assert(entry(restarted.due(now), "crash.dmp").empty()); // revoking dumps strips unsent memory
        restarted.delivered(raw.id, {400, 0, false}, now);
        assert(restarted.blocked() && restarted.due(now + RetentionMs).id.empty());
        restarted.reconcile({0, false, now}, now);
        assert(restarted.count() == 0);
        assert(fs::exists(root / "crash-dumps/DKR-R-crash-20261103-120000.dmp"));
        write_consent(reportRoot, {0, false, now});
        assert(read_consent(reportRoot).choice == 0);
        assert(read_consent(reportRoot).choice == 0); // declined never prompts again
        for (unsigned i = 0; i < MaxQueue + 2; ++i) {
            write(root / "logs/runtime-previous.log", crash + std::to_string(i));
            restarted.discover(enabled, now);
        }
        assert(restarted.count() == MaxQueue);
        restarted.reconcile(enabled, now + RetentionMs + 1);
        assert(restarted.count() == 0);
        // A link to a different profile's dump is not collected.
        const auto linked = root / "crash-dumps/DKR-R-crash-20261103-120000.dmp";
        fs::remove(linked);
        std::error_code link_error;
        fs::create_symlink(root / "logs/runtime.log", linked, link_error);
        if (!link_error) {
            write(root / "logs/runtime-previous.log", crash + "link case");
            restarted.discover({1, true, enabled.since}, now);
            assert(entry(restarted.due(now), "crash.dmp").empty());
        }
        // A link above the profile is resolved once and tolerated.
        std::error_code alias_error;
        fs::create_directory_symlink(root, alias, alias_error);
        if (!alias_error) {
            assert(Queue(alias, "1.0.14-beta.3").count() == restarted.count());
            fs::remove(alias);
        }
        // A link inside the diagnostic tree is still refused.
        fs::create_directories(root / "other");
        fs::create_directory_symlink(reportRoot, root / "other/crash-reports", link_error);
        if (!link_error) {
            bool refused = false;
            try {
                Queue other(root / "other", "1.0.14-beta.3");
            } catch (const std::runtime_error&) {
                refused = true;
            }
            assert(refused);
        }
        // Startup/disable state persists through the same service API as UI.
        restarted.reconcile({0, false, now}, now); // synthetic reports never leave the test
        write(root / "logs/runtime-previous.log", "[boot] clean synthetic session\n");
        start(root, "1.0.14-beta.3");
        assert(view().consent.choice == 0);
        assert(choose(true, false));
        assert(view().consent.choice == 1);
        gameplay(true);
        assert(choose(false, false));
        assert(view().consent.choice == 0);
        gameplay(false);
        for (unsigned i = 0; i < 25; ++i) {
            assert(choose(true, false));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            assert(choose(false, false));
            assert(read_consent(reportRoot).choice == 0);
        }
        shutdown();
        assert(read_consent(reportRoot).choice == 0);
    } catch (...) {
        shutdown();
        std::error_code ignored;
        fs::remove(alias, ignored);
        fs::remove_all(root);
        throw;
    }
    // The absolute root is a fresh, uniquely named test directory above.
    fs::remove_all(root);
}

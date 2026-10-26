#include "update_install.hpp"
#include "update_process.hpp"
#include "update_trust.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <json/json.hpp>
#include <set>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace dkr::updates {
namespace {
using Json = nlohmann::json;
Json parse(const std::filesystem::path& file) {
    auto bytes = read_text(file);
    return Json::parse(bytes.begin(), bytes.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 32)
            throw std::runtime_error("Update request is too deeply nested.");
        return true;
    });
}
std::filesystem::path health(const std::filesystem::path& data, const std::string& token) {
    unhex(token, 16);
    return data / "updates" / ("health-" + token + ".json");
}
void set_health_environment(const std::string& token, const std::filesystem::path& data) {
#ifdef _WIN32
    auto wide_token = std::wstring(token.begin(), token.end());
    SetEnvironmentVariableW(L"DKR_UPDATE_TOKEN", token.empty() ? nullptr : wide_token.c_str());
    SetEnvironmentVariableW(L"DKR_UPDATE_DATA", token.empty() ? nullptr : data.c_str());
#else
    if (token.empty()) {
        unsetenv("DKR_UPDATE_TOKEN");
        unsetenv("DKR_UPDATE_DATA");
    } else {
        setenv("DKR_UPDATE_TOKEN", token.c_str(), 1);
        setenv("DKR_UPDATE_DATA", data.c_str(), 1);
    }
#endif
}
std::filesystem::path bundle(const std::filesystem::path& executable) {
    auto root = executable.parent_path().parent_path().parent_path();
    if (root.extension() != ".app")
        throw std::runtime_error("This macOS build is not an application bundle.");
    return root;
}
std::string kind() {
#ifdef _WIN32
    return "zip";
#elif defined(__APPLE__)
    return "bundle";
#else
    return "appimage";
#endif
}
std::filesystem::path actual_target() {
#ifdef _WIN32
    return application_directory();
#elif defined(__APPLE__)
    return bundle(application_executable());
#else
    return application_executable();
#endif
}
void ordinary(const std::filesystem::path& path) {
    (void)hash_file(path);
}
void restore_single(const std::filesystem::path& target, const std::filesystem::path& backup, bool directory) {
    if (!directory)
        ordinary(backup);
    else
        require_private_tree(backup);
    if (directory) {
        auto failed = target;
        failed += ".failed-" + unique_id();
        if (std::filesystem::exists(target))
            std::filesystem::rename(target, failed);
        std::filesystem::rename(backup, target);
    } else {
        auto restore = target;
        restore += ".restore-" + unique_id();
        std::filesystem::copy_file(backup, restore);
        replace_file(restore, target);
    }
}
} // namespace
std::string installation_unavailable(const Configuration& config) {
#if defined(__ANDROID__)
    (void)config;
    return {}; // The Android adapter uses the system installer.
#elif defined(DKR_IOS)
    (void)config;
    return "iOS updates must be installed through your existing sideloading/distribution route. "
           "Open the release page.";
#else
    try {
        auto target = actual_target();
        if (target == target.root_path())
            return "The application location is not safe for in-place updates.";
        ordinary(config.helper);
#ifdef _WIN32
        (void)installed_files(target);
        ordinary(target / "DKR-R.exe");
#elif !defined(__APPLE__)
        auto* image = std::getenv("APPIMAGE");
        if (!image || !*image)
            return "This is not an AppImage installation. Update through your package manager or "
                   "install the downloaded release manually.";
        ordinary(target);
#endif
        return {};
    } catch (const std::exception&) {
        return "This build/location has no qualified updater helper or ownership receipt. Open the "
               "release page and update manually.";
    }
#endif
}
void prepare_install(const Configuration& config, const Manifest& manifest, const std::filesystem::path& package,
                     std::stop_token stop) {
#if defined(__ANDROID__) || defined(DKR_IOS)
    (void)config;
    (void)manifest;
    (void)package;
    (void)stop;
    throw std::runtime_error("Use the platform installer for this package.");
#else
    const auto* artifact = manifest.find(config.platform);
    if (!artifact)
        throw std::runtime_error("Update does not support this platform.");
    if (hash_file(package, kMaxPackageBytes) != artifact->sha256 ||
        std::filesystem::file_size(package) != artifact->bytes)
        throw std::runtime_error("The downloaded update changed before installation.");
    auto target = actual_target();
    auto root = kind() == "zip" ? target : target.parent_path();
    require_private_tree(root);
    auto id = unique_id();
    auto job = config.data / "updates" / ("handoff-" + id);
    std::filesystem::create_directory(job);
    require_private_tree(job);
    auto stage = root / (".dkr-update-stage-" + id);
    if (artifact->kind == "zip")
        extract_verified(package, *artifact, stage);
    else {
        copy_file_durable(package, stage);
        if (hash_file(stage) != artifact->sha256)
            throw std::runtime_error("Could not verify the staged AppImage.");
#ifndef _WIN32
        std::filesystem::permissions(stage,
                                     std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::replace);
#endif
    }
    if (stop.stop_requested())
        throw std::runtime_error(
            "Update installation preparation cancelled. The working application was not modified.");
    auto helper = job / config.helper.filename();
    copy_file_durable(config.helper, helper);
#ifndef _WIN32
    std::filesystem::permissions(helper, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
#endif
    auto manifest_bytes = read_text(config.data / "updates/manifest.json"),
         signature = read_text(config.data / "updates/manifest.sig", 128);
    auto verified = verify_manifest(manifest_bytes, signature, kUpdatePublicKey);
    if (verified.version != manifest.version)
        throw std::runtime_error("Update metadata changed during staging.");
    durable_write(job / "manifest.json", manifest_bytes);
    durable_write(job / "manifest.sig", signature);
    auto executable = kind() == "zip" ? target / "DKR-R.exe" : application_executable();
    auto request = Json{{"schema", 1},
                        {"id", id},
                        {"kind", kind()},
                        {"platform", config.platform},
                        {"parent", process_id()},
                        {"target", path_utf8(target)},
                        {"stage", path_utf8(stage)},
                        {"data", path_utf8(config.data)},
                        {"executable", path_utf8(executable)},
                        {"arguments", config.arguments}};
    durable_write(job / "request.json", request.dump());
    durable_write(config.data / "updates/last-install.json", Json{{"id", id}}.dump());
    Child child(helper, {"--apply", path_utf8(job / "request.json")}, job);
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < end) {
        if (std::filesystem::exists(job / "ready"))
            return;
        if (child.wait(0) >= 0)
            throw std::runtime_error("Update helper could not accept the handoff; the current app remains open.");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("Update helper did not acknowledge the handoff; no installed files were changed.");
#endif
}
void acknowledge_updated_launcher(const Configuration& config) {
    auto* token = std::getenv("DKR_UPDATE_TOKEN");
    auto* data = std::getenv("DKR_UPDATE_DATA");
    if (!token || !data)
        return;
    try {
        auto supplied = std::filesystem::canonical(std::filesystem::u8path(data));
        if (supplied != std::filesystem::canonical(config.data))
            throw std::runtime_error("The updated launcher selected a different data folder.");
        auto id = std::string(token);
        unhex(id, 16);
        durable_write(health(config.data, id),
                      Json{{"schema", 1}, {"id", id}, {"version", config.version}, {"ready", true}}.dump());
        set_health_environment({}, {});
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[updates][health] %s\n", e.what());
    }
}
int updater_main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
        std::puts("DKR-R updater protocol 1");
        return 0;
    }
    const bool recover_only = argc == 3 && std::string_view(argv[1]) == "--recover";
    if (argc != 3 || (!recover_only && std::string_view(argv[1]) != "--apply"))
        return 2;
    std::filesystem::path job;
    std::filesystem::path target;
    std::string package_kind;
    bool successor_started = false;
    bool parent_closed = false;
    std::filesystem::path single_backup;
    std::filesystem::path restart_executable, restart_root;
    std::vector<std::string> restart_arguments;
    try {
        auto request_path = std::filesystem::canonical(std::filesystem::u8path(argv[2]));
        job = request_path.parent_path();
        require_private_tree(job);
        auto r = parse(request_path);
        if (r.at("schema") != 1)
            throw std::runtime_error("Unsupported update request.");
        auto id = r.at("id").get<std::string>();
        unhex(id, 16);
        auto data = std::filesystem::canonical(std::filesystem::u8path(r.at("data").get<std::string>()));
        if (job != data / "updates" / ("handoff-" + id))
            throw std::runtime_error("Update request is outside its private handoff directory.");
        auto manifest =
            verify_manifest(read_text(job / "manifest.json"), read_text(job / "manifest.sig", 128), kUpdatePublicKey);
        if (r.at("platform") != platform())
            throw std::runtime_error("Update helper architecture does not match.");
        auto* artifact = manifest.find(platform());
        if (!artifact)
            throw std::runtime_error("No update package for this helper.");
        target = std::filesystem::weakly_canonical(std::filesystem::u8path(r.at("target").get<std::string>()));
        if (target == target.root_path())
            throw std::runtime_error("Refusing to update a filesystem root.");
        package_kind = r.at("kind").get<std::string>();
        if (package_kind != kind())
            throw std::runtime_error("Unexpected update installation kind.");
        auto root = package_kind == "zip" ? target : target.parent_path();
        if (package_kind == "bundle" && target.extension() != ".app")
            throw std::runtime_error("Update recovery is restricted to the exact application bundle.");
        if (recover_only) {
            InstallLock recovery(root, 5000);
            if (package_kind == "zip") {
                recover_files(target);
                archive_transaction(target);
            } else {
                auto record = parse(job / "single-journal.json");
                auto backup = root / (".dkr-update-backup-" + id);
                if (record.at("target") != path_utf8(target) || record.at("backup") != path_utf8(backup))
                    throw std::runtime_error("Recovery record does not match its exact application.");
                if (package_kind == "appimage" && hash_file(backup) != record.at("old_hash").get<std::string>())
                    throw std::runtime_error("The recovery backup changed; nothing was restored.");
                restore_single(target, backup, package_kind == "bundle");
            }
            durable_write(job / "result.json", Json{{"state", "recovered"}}.dump());
            return 0;
        }
        auto stage = std::filesystem::u8path(r.at("stage").get<std::string>());
        if (stage != root / (".dkr-update-stage-" + id))
            throw std::runtime_error("Update staging is not beside its exact target.");
        auto executable = std::filesystem::u8path(r.at("executable").get<std::string>());
        if (package_kind == "zip" && executable != target / "DKR-R.exe")
            throw std::runtime_error("Unexpected Windows launcher path.");
        if (package_kind == "appimage" && executable != target)
            throw std::runtime_error("Unexpected AppImage launcher path.");
        if (package_kind == "bundle" && executable != target / "Contents/MacOS/DKR-R")
            throw std::runtime_error("Unexpected application bundle launcher path.");
        std::vector<std::string> arguments = r.at("arguments").get<std::vector<std::string>>();
        if (arguments.size() > 128)
            throw std::runtime_error("Too many relaunch arguments.");
        for (auto& arg : arguments)
            if (arg.size() > 32768 || arg.find('\0') != arg.npos)
                throw std::runtime_error("Invalid relaunch argument.");
        restart_executable = executable;
        restart_root = root;
        restart_arguments = arguments;
        wait_for_parent(r.at("parent").get<std::uint64_t>(), job / "ready");
        parent_closed = true;
        std::filesystem::path backup;
        {
            InstallLock lock(root, 5000);
            if (package_kind == "zip") {
                if (pending_files(target)) {
                    recover_files(target);
                    archive_transaction(target);
                }
                auto current = parse(target / "assets/updates/install.json");
                if (Version::parse(manifest.version)
                        .compare(Version::parse(current.at("version").get<std::string>())) <= 0)
                    throw std::runtime_error("Refusing an automatic downgrade or same-version replacement.");
                install_files(target, stage, *artifact);
                authorize_updated_launch(target, id, data, manifest.version);
            } else {
                backup = root / (".dkr-update-backup-" + id);
                single_backup = backup;
                if (std::filesystem::exists(backup))
                    throw std::runtime_error("Backup destination already exists.");
                if (package_kind == "appimage") {
                    if (std::filesystem::file_size(stage) != artifact->bytes || hash_file(stage) != artifact->sha256)
                        throw std::runtime_error("Staged AppImage failed verification.");
                    copy_file_durable(target, backup);
                    durable_write(job / "single-journal.json", Json{{"schema", 1},
                                                                    {"state", "prepared"},
                                                                    {"target", path_utf8(target)},
                                                                    {"backup", path_utf8(backup)},
                                                                    {"old_hash", hash_file(backup)},
                                                                    {"new_hash", artifact->sha256}}
                                                                   .dump());
                    replace_file(stage, target);
                } else {
                    require_private_tree(stage);
                    if (target.extension() != ".app")
                        throw std::runtime_error("Unsafe bundle target.");
                    for (auto& file : artifact->files)
                        if (file.link.empty() && hash_file(stage / std::filesystem::u8path(file.path)) != file.sha256)
                            throw std::runtime_error("Staged bundle failed verification.");
                    durable_write(job / "single-journal.json", Json{{"schema", 1},
                                                                    {"state", "prepared"},
                                                                    {"target", path_utf8(target)},
                                                                    {"backup", path_utf8(backup)}}
                                                                   .dump());
                    std::filesystem::rename(target, backup);
                    std::filesystem::rename(stage, target);
                }
            }
        }
        set_health_environment(id, data);
        Child next(executable, arguments, root, true);
        successor_started = true;
        set_health_environment({}, {});
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        bool healthy = false;
        while (std::chrono::steady_clock::now() < end) {
            if (std::filesystem::exists(health(data, id))) {
                auto proof = parse(health(data, id));
                healthy = proof.value("ready", false) && proof.value("id", std::string{}) == id &&
                          proof.value("version", std::string{}) == manifest.version;
                break;
            }
            if (const int code = next.wait(0); code >= 0) {
                // A deliberate clean close is not a startup failure. Do not
                // undo the update or reopen an app the user has just closed.
                if (code == 0)
                    healthy = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (healthy) {
            if (package_kind == "zip") {
                commit_files(target);
                archive_transaction(target);
            }
            durable_write(job / "result.json", Json{{"state", "complete"}, {"version", manifest.version}}.dump());
            return 0;
        }
        // Never overwrite a still-running failed successor. The bootstrap can
        // recover a Windows transaction on its next normal launch instead.
        if (next.wait(0) < 0)
            throw std::runtime_error("The new launcher did not acknowledge readiness. Close it "
                                     "before recovery; the verified backup was retained.");
        {
            InstallLock lock(root, 5000);
            if (package_kind == "zip") {
                recover_files(target);
                archive_transaction(target);
            } else
                restore_single(target, backup, package_kind == "bundle");
        }
        durable_write(
            job / "result.json",
            Json{{"state", "recovered"}, {"error", "Updated launcher did not start; previous application restored."}}
                .dump());
        Child previous(executable, arguments, root);
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[updates][installer] %s\n", error.what());
        if (!successor_started && !target.empty() && package_kind == "zip") {
            try {
                InstallLock recovery(target, 5000);
                if (pending_files(target)) {
                    recover_files(target);
                    archive_transaction(target);
                }
            } catch (const std::exception& recovery_error) {
                std::fprintf(stderr, "[updates][recovery] %s\n", recovery_error.what());
            }
        }
        if (!successor_started && !single_backup.empty() && std::filesystem::exists(single_backup)) {
            try {
                InstallLock recovery(target.parent_path(), 5000);
                restore_single(target, single_backup, package_kind == "bundle");
            } catch (const std::exception& recovery_error) {
                std::fprintf(stderr, "[updates][recovery] %s\n", recovery_error.what());
            }
        }
        try {
            if (!job.empty())
                durable_write(job / "result.json", Json{{"state", "failed"}, {"error", error.what()}}.dump());
        } catch (...) {
        }
        if (parent_closed && !successor_started && !restart_executable.empty()) {
            try {
                Child previous(restart_executable, restart_arguments, restart_root);
            } catch (...) {
            }
        }
        return 1;
    }
}
} // namespace dkr::updates

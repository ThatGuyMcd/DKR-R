#pragma once
#ifndef _WIN32
#include <dlfcn.h>
#include <condition_variable>
#include <chrono>
#include <string>
#include <thread>

// Optional system Secret Service. No refresh credential is written to disk.
// Dynamically loaded so AppImage also runs on desktops without a keyring.
namespace dkr::online::keyring {
struct Api {
    void* library = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
    using New = void* (*)(const char*, int, ...);
    using Store = int (*)(const void*, const char*, const char*, const char*, void*, void**, ...);
    using Lookup = char* (*)(const void*, void*, void**, ...);
    using Clear = int (*)(const void*, void*, void**, ...);
    New schema = library ? reinterpret_cast<New>(dlsym(library, "secret_schema_new")) : nullptr;
    Store store = library ? reinterpret_cast<Store>(dlsym(library, "secret_password_store_sync")) : nullptr;
    Lookup lookup = library ? reinterpret_cast<Lookup>(dlsym(library, "secret_password_lookup_sync")) : nullptr;
    Clear clear = library ? reinterpret_cast<Clear>(dlsym(library, "secret_password_clear_sync")) : nullptr;
    void (*unref)(void*) = library ? reinterpret_cast<void (*)(void*)>(dlsym(library, "secret_schema_unref")) : nullptr;
    void (*free_password)(char*) = library ? reinterpret_cast<void (*)(char*)>(dlsym(library, "secret_password_free"))
                                           : nullptr;
    void (*free_error)(void*) = library ? reinterpret_cast<void (*)(void*)>(dlsym(library, "g_error_free")) : nullptr;
    void* (*cancel_new)() = library ? reinterpret_cast<void* (*)()>(dlsym(library, "g_cancellable_new")) : nullptr;
    void (*cancel)(void*) = library ? reinterpret_cast<void (*)(void*)>(dlsym(library, "g_cancellable_cancel"))
                                    : nullptr;
    void (*object_unref)(void*) = library ? reinterpret_cast<void (*)(void*)>(dlsym(library, "g_object_unref"))
                                          : nullptr;
    bool available() const {
        return schema && store && lookup && clear && unref && free_password && free_error && cancel_new && cancel &&
               object_unref;
    }
};
inline Api& api() {
    static auto* p = new Api;
    return *p;
}
inline bool available() {
    return api().available();
}
struct Request {
    Api& a = api();
    void* schema = nullptr;
    void* cancellable = nullptr;
    std::jthread timer;
    Request() {
        if (!a.available())
            return;
        schema = a.schema("com.positron.DKRR.Launcher", 0, "profile", 0, nullptr);
        cancellable = a.cancel_new();
        if (schema && cancellable)
            timer = std::jthread([this](std::stop_token stop) {
                std::mutex mutex;
                std::condition_variable_any cv;
                std::unique_lock lock(mutex);
                cv.wait_for(lock, stop, std::chrono::seconds(8), [] { return false; });
                if (!stop.stop_requested())
                    a.cancel(cancellable);
            });
    }
    ~Request() {
        timer.request_stop();
        if (timer.joinable())
            timer.join();
        if (schema)
            a.unref(schema);
        if (cancellable)
            a.object_unref(cancellable);
    }
    bool ready() const {
        return schema && cancellable;
    }
};
inline bool store(const std::string& profile, const std::string& value) {
    Request r;
    if (!r.ready())
        return false;
    void* error = nullptr;
    bool ok = r.a.store(r.schema, nullptr, "DKR-R Discord sign-in", value.c_str(), r.cancellable, &error, "profile",
                        profile.c_str(), nullptr) != 0;
    if (error)
        r.a.free_error(error);
    return ok;
}
inline std::string load(const std::string& profile) {
    Request r;
    if (!r.ready())
        return {};
    void* error = nullptr;
    char* password = r.a.lookup(r.schema, r.cancellable, &error, "profile", profile.c_str(), nullptr);
    std::string value = password ? password : "";
    if (password)
        r.a.free_password(password);
    if (error)
        r.a.free_error(error);
    return value;
}
inline void clear(const std::string& profile) {
    Request r;
    if (!r.ready())
        return;
    void* error = nullptr;
    r.a.clear(r.schema, r.cancellable, &error, "profile", profile.c_str(), nullptr);
    if (error)
        r.a.free_error(error);
}
} // namespace dkr::online::keyring
#endif

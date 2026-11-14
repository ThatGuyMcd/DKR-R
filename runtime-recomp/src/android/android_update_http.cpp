#include "../game/updates/update_http.hpp"
#include <SDL_system.h>
#include <jni.h>
#include <stdexcept>

namespace dkr::updates {
std::string native_os_version() {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env)
        throw std::runtime_error("Android version is unavailable.");
    auto type = env->FindClass("android/os/Build$VERSION");
    auto field = type ? env->GetStaticFieldID(type, "SDK_INT", "I") : nullptr;
    if (!field) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (type)
            env->DeleteLocalRef(type);
        throw std::runtime_error("Android version is unavailable.");
    }
    auto value = env->GetStaticIntField(type, field);
    env->DeleteLocalRef(type);
    return std::to_string(value);
}
HttpResponse https_get(const HttpRequest& request, std::stop_token stop, const Progress& progress) {
    if (!allowed_https_url(request.url))
        throw std::runtime_error("Update URL is not an approved HTTPS endpoint.");
    HttpSink sink(request, stop, progress);
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        throw std::runtime_error("Android update networking is unavailable.");
    auto type = env->GetObjectClass(activity);
    auto method =
        env->GetMethodID(type, "requestUpdateHttp", "(Ljava/lang/String;Ljava/lang/String;JJ)Ljava/lang/String;");
    auto url = env->NewStringUTF(request.url.c_str()), etag = env->NewStringUTF(request.etag.c_str());
    auto result = method
                      ? static_cast<jstring>(env->CallObjectMethod(activity, method, url, etag, jlong(request.offset),
                                                                   jlong(reinterpret_cast<std::uintptr_t>(&sink))))
                      : nullptr;
    std::string error;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        error = "Android update connection failed.";
    }
    if (result) {
        auto* text = env->GetStringUTFChars(result, nullptr);
        if (text) {
            error = text;
            env->ReleaseStringUTFChars(result, text);
        }
        env->DeleteLocalRef(result);
    }
    env->DeleteLocalRef(url);
    env->DeleteLocalRef(etag);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (!error.empty())
        throw std::runtime_error(sink.error.empty() ? error : sink.error);
    sink.finish();
    return sink.response;
}
void android_install_update(const std::filesystem::path& package, const std::string& version, const std::string& sha) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        throw std::runtime_error("Android installer is unavailable.");
    auto type = env->GetObjectClass(activity);
    auto method = env->GetMethodID(type, "installVerifiedUpdate",
                                   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    auto path = package.u8string();
    auto name = env->NewStringUTF(reinterpret_cast<const char*>(path.c_str())),
         release = env->NewStringUTF(version.c_str()), digest = env->NewStringUTF(sha.c_str());
    auto result =
        method ? static_cast<jstring>(env->CallObjectMethod(activity, method, name, release, digest)) : nullptr;
    std::string error;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        error = "Android could not open its package installer.";
    }
    if (result) {
        auto* text = env->GetStringUTFChars(result, nullptr);
        if (text) {
            error = text;
            env->ReleaseStringUTFChars(result, text);
        }
        env->DeleteLocalRef(result);
    } else if (error.empty())
        error = "This APK lacks the platform update bridge.";
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(release);
    env->DeleteLocalRef(digest);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (!error.empty())
        throw std::runtime_error(error);
}
} // namespace dkr::updates
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrUpdates_cancelled(JNIEnv*, jclass,
                                                                                          jlong pointer) {
    return reinterpret_cast<dkr::updates::HttpSink*>(std::uintptr_t(pointer))->cancelled();
}
extern "C" JNIEXPORT void JNICALL Java_io_github_thatguymcd_dkrr_DkrUpdates_status(JNIEnv*, jclass, jlong pointer,
                                                                                   jint code) {
    reinterpret_cast<dkr::updates::HttpSink*>(std::uintptr_t(pointer))->response.status = unsigned(code);
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrUpdates_header(JNIEnv* env, jclass,
                                                                                       jlong pointer, jstring name,
                                                                                       jstring value) {
    auto& sink = *reinterpret_cast<dkr::updates::HttpSink*>(std::uintptr_t(pointer));
    auto* a = env->GetStringUTFChars(name, nullptr);
    auto* b = env->GetStringUTFChars(value, nullptr);
    bool ok = a && b && sink.header(a, b);
    if (a)
        env->ReleaseStringUTFChars(name, a);
    if (b)
        env->ReleaseStringUTFChars(value, b);
    return ok;
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrUpdates_bytes(JNIEnv* env, jclass,
                                                                                      jlong pointer, jbyteArray data,
                                                                                      jint count) {
    if (count < 0 || count > 65536 || count > env->GetArrayLength(data))
        return false;
    auto* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes)
        return false;
    bool ok = reinterpret_cast<dkr::updates::HttpSink*>(std::uintptr_t(pointer))
                  ->write(std::span(reinterpret_cast<const std::uint8_t*>(bytes), std::size_t(count)));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    return ok;
}

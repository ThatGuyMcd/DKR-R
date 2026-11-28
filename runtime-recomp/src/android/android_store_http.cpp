#include "../game/mod_store/mobile_http.hpp"

#include <SDL_system.h>
#include <jni.h>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>

namespace {
using dkr::store::MobileHttpReceiver;

struct Transfer {
    MobileHttpReceiver& receiver;
    std::exception_ptr error;
};

Transfer& transfer(jlong context) {
    return *reinterpret_cast<Transfer*>(static_cast<std::uintptr_t>(context));
}

std::string text(JNIEnv* env, jstring value) {
    const auto* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars)
        throw std::runtime_error("The library returned an invalid header.");
    struct Release {
        JNIEnv* env;
        jstring value;
        const char* chars;
        ~Release() {
            env->ReleaseStringUTFChars(value, chars);
        }
    } release{env, value, chars};
    return chars;
}
} // namespace

void dkr::store::mobile_http(std::string_view url, std::string_view method, std::string_view body,
                             const HttpHeaders& headers, unsigned timeout_ms, MobileHttpReceiver& receiver) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || env->PushLocalFrame(16) < 0) {
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        throw std::runtime_error("Android library networking is unavailable.");
    }
    struct PopFrame {
        JNIEnv* env;
        ~PopFrame() {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->PopLocalFrame(nullptr);
        }
    } frame{env};
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity || body.size() > std::numeric_limits<jsize>::max())
        throw std::runtime_error("Android library networking is unavailable.");
    auto type = env->GetObjectClass(activity);
    auto request = type ? env->GetMethodID(type, "requestStoreHttp",
                                           "(Ljava/lang/String;Ljava/lang/String;[B[Ljava/lang/String;IJ)Z")
                        : nullptr;
    if (!request)
        throw std::runtime_error("Android library networking is unavailable.");

    auto make_string = [&](std::string_view value) {
        auto result = env->NewStringUTF(std::string(value).c_str());
        if (!result)
            throw std::runtime_error("Android could not prepare the library request.");
        return result;
    };
    auto endpoint = make_string(url);
    auto operation = make_string(method);
    auto bytes = env->NewByteArray(static_cast<jsize>(body.size()));
    if (!bytes)
        throw std::runtime_error("Android could not prepare the library request body.");
    if (!body.empty())
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(body.size()), reinterpret_cast<const jbyte*>(body.data()));
    auto string_type = env->FindClass("java/lang/String");
    auto fields =
        string_type ? env->NewObjectArray(static_cast<jsize>(2 * headers.size()), string_type, nullptr) : nullptr;
    if (!fields)
        throw std::runtime_error("Android could not prepare the library request headers.");
    jsize index = 0;
    for (const auto& [name, value] : headers) {
        auto key = make_string(name);
        auto field = make_string(value);
        env->SetObjectArrayElement(fields, index++, key);
        env->SetObjectArrayElement(fields, index++, field);
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(field);
    }
    Transfer context{receiver, {}};
    const auto ok =
        env->CallBooleanMethod(activity, request, endpoint, operation, bytes, fields, static_cast<jint>(timeout_ms),
                               static_cast<jlong>(reinterpret_cast<std::uintptr_t>(&context)));
    if (context.error)
        std::rethrow_exception(context.error);
    if (!ok || env->ExceptionCheck() || receiver.stop.stop_requested())
        throw std::runtime_error("The secure library request was interrupted.");
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrStoreHttp_cancelled(JNIEnv*, jclass,
                                                                                            jlong context) {
    return transfer(context).receiver.stop.stop_requested();
}
extern "C" JNIEXPORT void JNICALL Java_io_github_thatguymcd_dkrr_DkrStoreHttp_status(JNIEnv*, jclass, jlong context,
                                                                                     jint status) {
    transfer(context).receiver.status(static_cast<unsigned>(status));
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrStoreHttp_header(JNIEnv* env, jclass,
                                                                                         jlong context, jstring name,
                                                                                         jstring value) {
    auto& state = transfer(context);
    try {
        state.receiver.header(text(env, name), text(env, value));
        return true;
    } catch (...) {
        state.error = std::current_exception();
        return false;
    }
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_thatguymcd_dkrr_DkrStoreHttp_bytes(JNIEnv* env, jclass,
                                                                                        jlong context,
                                                                                        jbyteArray buffer, jint count) {
    auto& state = transfer(context);
    auto* bytes = env->GetByteArrayElements(buffer, nullptr);
    if (!bytes)
        return false;
    try {
        state.receiver.write(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(count));
    } catch (...) {
        state.error = std::current_exception();
    }
    env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    return !state.error;
}

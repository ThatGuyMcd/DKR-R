#include <jni.h>
#include <unistd.h>
#include <cstdlib>
#include <SDL_system.h>
#include <SDL_log.h>
#include "android_platform.hpp"
#include "../game/performance_trace.hpp"
#include "../game/render_metrics.hpp"
#include "../game/graphics_health.hpp"
#include "../game/android_surface_state.hpp"
#include "../game/android_memory_budget.hpp"
#include <android/native_window_jni.h>
#include "ultramodern/config.hpp"

namespace {
void show_native_message(const char* method_name, const char* message) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = type ? env->GetMethodID(type, method_name, "(Ljava/lang/String;)V") : nullptr;
    jstring text = env->NewStringUTF(message);
    if (method && text) env->CallVoidMethod(activity, method, text);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (text) env->DeleteLocalRef(text);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
}

void dkr::runtime::android::report_graphics_failure(const char* message) {
    show_native_message("showGraphicsFailure", message);
}
void dkr::runtime::android::report_task_stall(const char* message) {
    show_native_message("showTaskStall", message);
}
void dkr::runtime::android::performance_tools() {
    show_native_message("showPerformanceTools", "");
}
const std::string& dkr::runtime::android::build_identity() {
    static const std::string identity = [] {
        std::string value = "Android build identity unavailable";
        auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return value;
        jclass type = env->GetObjectClass(activity);
        auto method = type ? env->GetMethodID(type, "getBuildIdentity", "()Ljava/lang/String;") : nullptr;
        auto result = method ? static_cast<jstring>(env->CallObjectMethod(activity, method)) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
        if (result) {
            const char* text = env->GetStringUTFChars(result, nullptr);
            if (text) { value = text; env->ReleaseStringUTFChars(result, text); }
            env->DeleteLocalRef(result);
        }
        if (type) env->DeleteLocalRef(type);
        env->DeleteLocalRef(activity);
        return value;
    }();
    return identity;
}

namespace {
dkr::runtime::performance_trace::Snapshot capture_baseline{};
dkr::runtime::render_metrics::Snapshot render_baseline{};
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_memoryPressure(JNIEnv*, jclass, jint level) {
    dkr::runtime::android_memory::trim(level);
    std::fprintf(stderr, "[Android] Memory pressure level=%d unused texture cache ceiling=%llu MiB\n", level,
        static_cast<unsigned long long>(dkr::runtime::android_memory::unused_cache_limit.load() / (1024 * 1024)));
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_publishSurface(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    dkr::runtime::android_surface::publish(window);
    if (window) ANativeWindow_release(window);
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_beginPerformanceCapture(JNIEnv*, jclass) {
    capture_baseline = dkr::runtime::performance_trace::snapshot();
    render_baseline = dkr::runtime::render_metrics::snapshot();
    dkr::runtime::performance_capture::start();
}
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_endPerformanceCapture(JNIEnv* env, jclass) {
    auto text = dkr::runtime::performance_capture::finish();
    text += dkr::runtime::performance_trace::describe_delta(capture_baseline);
    text += dkr::runtime::render_metrics::describe_delta(render_baseline);
    const auto config = ultramodern::renderer::get_graphics_config();
    std::ostringstream settings;
    settings << "Graphics configuration at end (enum values): resolution=" << unsigned(config.res_option)
        << " supersampling=" << config.ds_option << " precision=" << unsigned(config.hpfb_option)
        << " MSAA=" << unsigned(config.msaa_option) << " refresh-mode=" << unsigned(config.rr_option)
        << " refresh-target=" << config.rr_manual_value << "\n"
        << "First graphics failure (stage/result packed): " << dkr::runtime::graphics_health::failure.load() << "\n";
    text += settings.str();
    return env->NewStringUTF(text.c_str());
}

void dkr::runtime::android::export_diagnostics() {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = type ? env->GetMethodID(type, "exportDiagnostics", "()V") : nullptr;
    if (method) env->CallVoidMethod(activity, method);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot open Android diagnostic export");
    }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

// Called after loading SDL and DKR-R, before SDL_main. Application files and
// installed helpers stay in Android-owned directories, never shared storage.
extern "C" JNIEXPORT void JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_prepareNative(
    JNIEnv* env, jclass, jstring files, jstring libraries) {
    const char* directory = env->GetStringUTFChars(files, nullptr);
    const char* native_libs = env->GetStringUTFChars(libraries, nullptr);
    if (directory) chdir(directory);
    if (native_libs) setenv("DKR_ANDROID_NATIVE_LIBS", native_libs, 1);
    if (directory) env->ReleaseStringUTFChars(files, directory);
    if (native_libs) env->ReleaseStringUTFChars(libraries, native_libs);
}

// Android Storage Access Framework adapter for the existing NFD call sites.
// Runs on SDL's native thread; the Activity remains free to process the picker.
#include <nfd.h>
#include <SDL_system.h>
#include <jni.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace {
std::mutex mutex;
std::condition_variable completed;
std::string result_path, error_text;
bool ready = false;
jlong serial = 0;
bool exporting = false;

nfdresult_t pick(nfdu8char_t** output, bool save, const char* default_name) {
    if (!output) return NFD_ERROR;
    *output = nullptr;
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) { error_text = "Android activity is unavailable."; return NFD_ERROR; }
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(cls, "pickDocument", "(JZLjava/lang/String;)V");
    if (!method || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
        error_text = "Android document picker is unavailable."; return NFD_ERROR;
    }
    std::unique_lock lock(mutex);
    const jlong request = ++serial;
    ready = false; exporting = false; result_path.clear(); error_text.clear();
    jstring name = env->NewStringUTF(default_name ? default_name : "DKR-R-export.bin");
    env->CallVoidMethod(activity, method, request, static_cast<jboolean>(save), name);
    env->DeleteLocalRef(name); env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
    if (env->ExceptionCheck()) {
        env->ExceptionClear(); error_text = "Could not open the Android picker."; return NFD_ERROR;
    }
    if (!completed.wait_for(lock, std::chrono::minutes(5), [] { return ready; })) {
        ++serial; error_text = "Document selection timed out. Please try again."; return NFD_ERROR;
    }
    if (!error_text.empty()) return NFD_ERROR;
    if (result_path.empty()) return NFD_CANCEL;
    *output = static_cast<char*>(std::malloc(result_path.size() + 1));
    if (!*output) { error_text = "Not enough memory for the selected path."; return NFD_ERROR; }
    std::memcpy(*output, result_path.c_str(), result_path.size() + 1);
    exporting = save;
    return NFD_OKAY;
}
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_thatguymcd_dkrr_DkrActivity_documentResult(
    JNIEnv* env, jclass, jlong request, jstring path, jstring error) {
    std::lock_guard lock(mutex);
    if (request != serial) return;
    auto read = [env](jstring value) {
        if (!value) return std::string{};
        const char* text = env->GetStringUTFChars(value, nullptr);
        std::string copy = text ? text : "";
        if (text) env->ReleaseStringUTFChars(value, text);
        return copy;
    };
    result_path = read(path); error_text = read(error); ready = true;
    completed.notify_all();
}

extern "C" {
nfdresult_t NFD_Init() { return NFD_OKAY; }
void NFD_Quit() {
    if (!exporting) return;
    exporting = false;
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(cls, "finishExport", "(Ljava/lang/String;)V");
    if (method) {
        jstring path = env->NewStringUTF(result_path.c_str());
        env->CallVoidMethod(activity, method, path);
        env->DeleteLocalRef(path);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
}
const char* NFD_GetError() { return error_text.c_str(); }
void NFD_ClearError() { error_text.clear(); }
void NFD_FreePathU8(nfdu8char_t* path) { std::free(path); }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t** out, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) {
    return pick(out, false, nullptr);
}
nfdresult_t NFD_SaveDialogU8(nfdu8char_t** out, const nfdu8filteritem_t*, nfdfiltersize_t,
                            const nfdu8char_t*, const nfdu8char_t* name) {
    return pick(out, true, name);
}
nfdresult_t NFD_PickFolderU8(nfdu8char_t** out, const nfdu8char_t*) {
    if (out) *out = nullptr;
    error_text = "Folder import is not available in this Android preview. Import a ZIP instead.";
    return NFD_ERROR;
}
// On Android both native and UTF-8 NFD paths use char strings.
void NFD_FreePathN(nfdnchar_t* path) { NFD_FreePathU8(path); }
nfdresult_t NFD_OpenDialogN(nfdnchar_t** out, const nfdnfilteritem_t* filters,
                          nfdfiltersize_t count, const nfdnchar_t* initial) {
    return NFD_OpenDialogU8(out, filters, count, initial);
}
nfdresult_t NFD_SaveDialogN(nfdnchar_t** out, const nfdnfilteritem_t* filters,
                          nfdfiltersize_t count, const nfdnchar_t* initial, const nfdnchar_t* name) {
    return NFD_SaveDialogU8(out, filters, count, initial, name);
}
nfdresult_t NFD_PickFolderN(nfdnchar_t** out, const nfdnchar_t* initial) {
    return NFD_PickFolderU8(out, initial);
}
}

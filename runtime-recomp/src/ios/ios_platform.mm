#include "ios_platform.hpp"

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#include <SDL.h>

#include <atomic>
#include <mutex>
#include <string>
#include <system_error>

namespace dkr::runtime::ios {
namespace {

std::filesystem::path documents_path() {
    @autoreleasepool {
        NSArray<NSURL*>* urls = [NSFileManager.defaultManager
            URLsForDirectory:NSDocumentDirectory
                   inDomains:NSUserDomainMask];
        NSURL* url = urls.firstObject;
        if (const char* path = url.fileSystemRepresentation) return path;
        NSString* home = NSHomeDirectory();
        if (home.length == 0) return {};
        return std::filesystem::path(home.UTF8String) / "Documents";
    }
}

void on_main(void (^work)(void)) {
    if ([NSThread isMainThread]) work();
    else dispatch_sync(dispatch_get_main_queue(), work);
}

std::filesystem::path unique_destination(const std::filesystem::path& directory,
                                         const std::filesystem::path& name) {
    std::error_code ec;
    std::filesystem::path candidate = directory / name;
    if (!std::filesystem::exists(candidate, ec) && !ec) return candidate;
    const std::string stem = name.stem().string();
    const std::string extension = name.extension().string();
    for (int suffix = 1; suffix < 10000; ++suffix) {
        candidate = directory / (stem + "-" + std::to_string(suffix) + extension);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec) && !ec) return candidate;
    }
    return directory / (stem + "-overflow" + extension);
}

}  // namespace

std::filesystem::path documents_directory() {
    return documents_path();
}

void import_inbox() {
    std::error_code ec;
    const std::filesystem::path docs = documents_path();
    const std::filesystem::path inbox = docs / "Inbox";
    if (docs.empty() || !std::filesystem::is_directory(inbox, ec)) return;
    // Open In drops files in Inbox and the system may delete them if they stay.
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(inbox, ec)) {
        if (ec) break;
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec) || entry_ec) continue;
        const std::filesystem::path dest = unique_destination(docs, entry.path().filename());
        entry_ec.clear();
        std::filesystem::rename(entry.path(), dest, entry_ec);
        if (!entry_ec) continue;
        entry_ec.clear();
        std::filesystem::copy_file(entry.path(), dest, entry_ec);
        if (!entry_ec) std::filesystem::remove(entry.path(), entry_ec);
    }
}

void import_external_file(const char* utf8_path) {
    import_inbox();
    if (utf8_path == nullptr || utf8_path[0] == '\0') return;
    std::error_code ec;
    const std::filesystem::path source(utf8_path);
    if (!std::filesystem::is_regular_file(source, ec) || ec) return;
    const std::filesystem::path docs = documents_path();
    if (docs.empty()) return;
    const std::filesystem::path relative = std::filesystem::relative(source, docs, ec);
    if (!ec && !relative.empty() && *relative.begin() != "..") return;
    const std::filesystem::path dest = unique_destination(docs, source.filename());
    std::filesystem::copy_file(source, dest, ec);
}

void prepare_storage() {
    std::error_code ec;
    const std::filesystem::path docs = documents_path();
    if (!docs.empty()) std::filesystem::create_directories(docs / "DKR-R", ec);
    import_inbox();
}

void configure_session() {
    // Hints have to land before SDL_Init. Playback ignores the silent switch.
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    on_main(^{
        UIApplication.sharedApplication.idleTimerDisabled = YES;
        AVAudioSession* session = AVAudioSession.sharedInstance;
        [session setCategory:AVAudioSessionCategoryPlayback
                        mode:AVAudioSessionModeDefault
                     options:0
                       error:nil];
        [session setActive:YES error:nil];
    });
}

const char* build_identity() {
    return "arm64 " DKR_RELEASE_VERSION;
}

}  // namespace dkr::runtime::ios

namespace {

bool category_is(UIContentSizeCategory category, UIContentSizeCategory expected) {
    return category != nil && [category isEqualToString:expected];
}

float text_scale_for(UIContentSizeCategory category) {
    if (category == nil || category_is(category, UIContentSizeCategoryUnspecified) ||
        category_is(category, UIContentSizeCategoryExtraSmall) ||
        category_is(category, UIContentSizeCategorySmall) ||
        category_is(category, UIContentSizeCategoryMedium) ||
        category_is(category, UIContentSizeCategoryLarge))
        return 1.0f;
    if (category_is(category, UIContentSizeCategoryExtraLarge)) return 1.15f;
    if (category_is(category, UIContentSizeCategoryExtraExtraLarge)) return 1.3f;
    if (category_is(category, UIContentSizeCategoryExtraExtraExtraLarge)) return 1.45f;
    return 1.5f;
}

UIWindow* key_window() {
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene* window_scene = (UIWindowScene*)scene;
        for (UIWindow* window in window_scene.windows) {
            if (window.isKeyWindow) return window;
        }
        if (window_scene.windows.firstObject) return window_scene.windows.firstObject;
    }
    // Before UIKit connects the scene, fall back to the application windows.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray<UIWindow*>* windows = UIApplication.sharedApplication.windows;
#pragma clang diagnostic pop
    for (UIWindow* window in windows) {
        if (window.isKeyWindow) return window;
    }
    return windows.lastObject;
}

void fill_metrics(float* scale, float* text_scale, float insets[4]) {
    UIWindow* window = key_window();
    // SDL UIKit reports window and input coordinates in points. Its ImGui
    // backend already supplies the Retina framebuffer scale separately.
    *scale = 1.0f;
    *text_scale = text_scale_for(window.traitCollection.preferredContentSizeCategory);
    const UIEdgeInsets safe = window ? window.safeAreaInsets : UIEdgeInsetsZero;
    insets[0] = static_cast<float>(safe.left);
    insets[1] = static_cast<float>(safe.top);
    insets[2] = static_cast<float>(safe.right);
    insets[3] = static_cast<float>(safe.bottom);
}

}  // namespace

namespace {
struct UiMetrics {
    float scale = 1.0f;
    float text_scale = 1.0f;
    float insets[4]{};
};
std::mutex metrics_mutex;
UiMetrics cached_metrics;
std::atomic<bool> metrics_refresh_pending{false};

void refresh_metrics() {
    UiMetrics fresh;
    fill_metrics(&fresh.scale, &fresh.text_scale, fresh.insets);
    std::lock_guard lock(metrics_mutex);
    cached_metrics = fresh;
}
}  // namespace

extern "C" void dkr_ios_ui_metrics(float* scale, float* text_scale, float insets[4]) {
    if (!scale || !text_scale || !insets) return;
    if ([NSThread isMainThread]) refresh_metrics();
    else if (!metrics_refresh_pending.exchange(true)) {
        // The render thread holds ImGui's frame mutex, which the SDL main
        // thread also acquires to deliver events. Never wait for that thread.
        // Coalesce refreshes so a backgrounded app cannot accumulate work.
        dispatch_async(dispatch_get_main_queue(), ^{
            refresh_metrics();
            metrics_refresh_pending.store(false);
        });
    }
    std::lock_guard lock(metrics_mutex);
    *scale = cached_metrics.scale;
    *text_scale = cached_metrics.text_scale;
    for (int i = 0; i < 4; ++i) insets[i] = cached_metrics.insets[i];
}


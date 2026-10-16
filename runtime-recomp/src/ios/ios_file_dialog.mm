// Documents picker for the existing NFD call sites. Open In and the Files
// app already land ROMs in Documents; this is the in-app browse path.
#include <nfd.h>

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
std::mutex state_mutex;
std::string error_text;

void set_error(std::string message) {
    std::lock_guard lock(state_mutex);
    error_text = std::move(message);
}


struct OpenState {
    bool done = false;
    bool cancelled = false;
    std::string path;
    std::string error;
};

NSString* sanitize_name(NSString* name, NSString* fallback) {
    if (name.length == 0 || [name isEqualToString:@"."] || [name isEqualToString:@".."])
        return fallback;
    NSCharacterSet* bad = [NSCharacterSet characterSetWithCharactersInString:@"/\\:\0"];
    NSString* cleaned = [[name componentsSeparatedByCharactersInSet:bad] componentsJoinedByString:@"_"];
    return cleaned.length ? cleaned : fallback;
}

NSURL* documents_url() {
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                 inDomains:NSUserDomainMask].firstObject;
}

std::string posix_path(NSURL* url) {
    const char* path = url.fileSystemRepresentation;
    return path ? path : std::string{};
}

nfdresult_t alloc_path(nfdu8char_t** out, const std::string& path) {
    if (!out) {
        set_error("Missing output path.");
        return NFD_ERROR;
    }
    *out = nullptr;
    if (path.empty()) {
        set_error("The selected path is empty.");
        return NFD_ERROR;
    }
    *out = static_cast<char*>(std::malloc(path.size() + 1));
    if (!*out) {
        set_error("Not enough memory for the selected path.");
        return NFD_ERROR;
    }
    std::memcpy(*out, path.c_str(), path.size() + 1);
    return NFD_OKAY;
}

NSString* copy_into_documents(NSURL* url, std::string* error) {
    if (!url) {
        if (error) *error = "No file was selected.";
        return nil;
    }
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    NSURL* docs = documents_url();
    if (!docs) {
        if (scoped) [url stopAccessingSecurityScopedResource];
        if (error) *error = "Documents is unavailable.";
        return nil;
    }
    NSString* name = sanitize_name(url.lastPathComponent, @"ROM.z64");
    NSFileManager* files = NSFileManager.defaultManager;
    NSURL* dest = [docs URLByAppendingPathComponent:name isDirectory:NO];
    for (int suffix = 1; [files fileExistsAtPath:dest.path]; ++suffix) {
        if (suffix > 9999) {
            if (scoped) [url stopAccessingSecurityScopedResource];
            if (error) *error = "Documents already has too many copies of that file.";
            return nil;
        }
        NSString* stem = name.stringByDeletingPathExtension;
        NSString* ext = name.pathExtension;
        NSString* next = ext.length
            ? [NSString stringWithFormat:@"%@-%d.%@", stem, suffix, ext]
            : [NSString stringWithFormat:@"%@-%d", stem, suffix];
        dest = [docs URLByAppendingPathComponent:next isDirectory:NO];
    }
    NSError* copy_error = nil;
    const BOOL copied = [files copyItemAtURL:url toURL:dest error:&copy_error];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!copied) {
        if (error) {
            *error = copy_error.localizedDescription.UTF8String
                ? copy_error.localizedDescription.UTF8String
                : "Could not copy the file into Documents.";
        }
        return nil;
    }
    return dest.path;
}

}  // namespace

@interface DkrOpenDelegate : NSObject <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
@property(nonatomic, assign) OpenState* state;
@end

@implementation DkrOpenDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)picker;
    if (!self.state) return;
    std::string failure;
    NSString* copied = copy_into_documents(urls.firstObject, &failure);
    if (copied.length) self.state->path = copied.UTF8String;
    else self.state->error = failure.empty() ? "Could not copy the file into Documents." : failure;
    self.state->done = true;
}
- (void)presentationControllerDidDismiss:(UIPresentationController*)presentationController {
    (void)presentationController;
    if (!self.state) return;
    self.state->cancelled = true;
    self.state->done = true;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker {
    (void)picker;
    if (!self.state) return;
    self.state->cancelled = true;
    self.state->done = true;
}
@end

namespace {

UIViewController* presenter_for(UIWindow* window) {
    UIViewController* presenter = window.rootViewController;
    while (presenter.presentedViewController) presenter = presenter.presentedViewController;
    return presenter;
}

UIWindow* key_window() {
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene* window_scene = (UIWindowScene*)scene;
        for (UIWindow* window in window_scene.windows) {
            if (window.isKeyWindow) return window;
        }
        if (UIWindow* first = window_scene.windows.firstObject) return first;
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

nfdresult_t open_on_main(nfdu8char_t** out) {
    UIWindow* window = key_window();
    UIViewController* presenter = presenter_for(window);
    if (!presenter) {
        set_error("The document picker needs the DKR-R window.");
        return NFD_ERROR;
    }
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (NSString* identifier in @[
             @"io.github.thatguymcd.dkrr.z64",
             @"io.github.thatguymcd.dkrr.n64",
             @"io.github.thatguymcd.dkrr.v64",
         ]) {
        if (UTType* type = [UTType typeWithIdentifier:identifier]) [types addObject:type];
    }
    [types addObject:UTTypeZIP];
    [types addObject:UTTypeData];
    OpenState state;
    DkrOpenDelegate* delegate = [DkrOpenDelegate new];
    delegate.state = &state;
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types asCopy:YES];
    picker.delegate = delegate;
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;
    [presenter presentViewController:picker animated:YES completion:nil];
    picker.presentationController.delegate = delegate;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (!state.done) {
        if (std::chrono::steady_clock::now() > deadline) {
            picker.presentationController.delegate = nil;
            picker.delegate = nil;
            delegate.state = nullptr;
            [presenter dismissViewControllerAnimated:NO completion:nil];
            set_error("Document selection timed out. Please try again.");
            return NFD_ERROR;
        }
        @autoreleasepool {
            [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                                   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            [NSRunLoop.currentRunLoop runMode:UITrackingRunLoopMode
                                   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    picker.presentationController.delegate = nil;
    picker.delegate = nil;
    delegate.state = nullptr;
    if (state.cancelled) return NFD_CANCEL;
    if (state.path.empty()) {
        set_error(state.error.empty() ? "Could not copy the file into Documents." : state.error);
        return NFD_ERROR;
    }
    return alloc_path(out, state.path);
}

nfdresult_t save_on_main(nfdu8char_t** out, const char* name) {
    NSURL* docs = documents_url();
    if (!docs) {
        set_error("Documents is unavailable.");
        return NFD_ERROR;
    }
    NSString* wanted = name ? [NSString stringWithUTF8String:name] : nil;
    NSString* file = sanitize_name(wanted, @"DKR-R-export.bin");
    NSFileManager* files = NSFileManager.defaultManager;
    NSURL* dest = [docs URLByAppendingPathComponent:file isDirectory:NO];
    for (int suffix = 1; [files fileExistsAtPath:dest.path]; ++suffix) {
        if (suffix > 9999) {
            set_error("Documents already has too many copies of that export.");
            return NFD_ERROR;
        }
        NSString* stem = file.stringByDeletingPathExtension;
        NSString* ext = file.pathExtension;
        NSString* next = ext.length
            ? [NSString stringWithFormat:@"%@-%d.%@", stem, suffix, ext]
            : [NSString stringWithFormat:@"%@-%d", stem, suffix];
        dest = [docs URLByAppendingPathComponent:next isDirectory:NO];
    }
    return alloc_path(out, posix_path(dest));
}

template <typename Fn>
nfdresult_t on_main(Fn&& fn) {
    if ([NSThread isMainThread]) return fn();
    __block nfdresult_t result = NFD_ERROR;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = fn();
    });
    return result;
}

}  // namespace

extern "C" {

nfdresult_t NFD_Init(void) { return NFD_OKAY; }

void NFD_Quit(void) {}

const char* NFD_GetError(void) {
    // Keep the returned pointer stable if another dialog changes the error.
    thread_local std::string snapshot;
    std::lock_guard lock(state_mutex);
    snapshot = error_text;
    return snapshot.empty() ? nullptr : snapshot.c_str();
}

void NFD_ClearError(void) { set_error({}); }

void NFD_FreePathU8(nfdu8char_t* path) { std::free(path); }
void NFD_FreePathN(nfdnchar_t* path) { NFD_FreePathU8(path); }

nfdresult_t NFD_OpenDialogU8(nfdu8char_t** out, const nfdu8filteritem_t*, nfdfiltersize_t,
                            const nfdu8char_t*) {
    set_error({});
    if (!out) {
        set_error("Missing output path.");
        return NFD_ERROR;
    }
    *out = nullptr;
    return on_main([&] { return open_on_main(out); });
}

nfdresult_t NFD_OpenDialogN(nfdnchar_t** out, const nfdnfilteritem_t* filters, nfdfiltersize_t count,
                           const nfdnchar_t* initial) {
    return NFD_OpenDialogU8(out, filters, count, initial);
}

nfdresult_t NFD_SaveDialogU8(nfdu8char_t** out, const nfdu8filteritem_t*, nfdfiltersize_t,
                            const nfdu8char_t*, const nfdu8char_t* name) {
    set_error({});
    if (!out) {
        set_error("Missing output path.");
        return NFD_ERROR;
    }
    *out = nullptr;
    return on_main([&] { return save_on_main(out, name); });
}

nfdresult_t NFD_SaveDialogN(nfdnchar_t** out, const nfdnfilteritem_t* filters, nfdfiltersize_t count,
                           const nfdnchar_t* initial, const nfdnchar_t* name) {
    return NFD_SaveDialogU8(out, filters, count, initial, name);
}

nfdresult_t NFD_PickFolderU8(nfdu8char_t** out, const nfdu8char_t*) {
    set_error({});
    if (!out) {
        set_error("Missing output path.");
        return NFD_ERROR;
    }
    *out = nullptr;
    return on_main([&] {
        const std::string docs = posix_path(documents_url());
        if (docs.empty()) {
            set_error("Documents is unavailable.");
            return NFD_ERROR;
        }
        return alloc_path(out, docs);
    });
}

nfdresult_t NFD_PickFolderN(nfdnchar_t** out, const nfdnchar_t* initial) {
    return NFD_PickFolderU8(out, initial);
}

}

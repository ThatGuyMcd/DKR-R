#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <cstdio>

static Uint32 fullscreen_event_type() {
    static const Uint32 type = SDL_RegisterEvents(1);
    return type;
}

extern "C" void dkr_set_window_fullscreen(Uint32 window_id, bool fullscreen) {
    // Same queue as the Mac path. The iPhone app stays fullscreen either way.
    SDL_Event event{};
    event.type = fullscreen_event_type();
    event.user.windowID = window_id;
    event.user.code = fullscreen ? 1 : 0;
    if (event.type == Uint32(-1) || SDL_PushEvent(&event) != 1)
        std::fprintf(stderr, "[window] cannot queue fullscreen change: %s\n", SDL_GetError());
}

extern "C" bool dkr_handle_fullscreen_event(const SDL_Event* event) {
    if (event == nullptr || event->type != fullscreen_event_type()) return false;
    SDL_Window* window = SDL_GetWindowFromID(event->user.windowID);
    if (window && SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        std::fprintf(stderr, "[window] fullscreen change failed: %s\n", SDL_GetError());
    return true;
}

static UIView* view_with_layer(UIView* view, CALayer* layer) {
    if (view.layer == layer) return view;
    for (UIView* subview in view.subviews) {
        if (UIView* found = view_with_layer(subview, layer)) return found;
    }
    return nil;
}

static bool show_metal_layer(UIWindow* window, CALayer* layer) {
    if (window == nil || layer == nil) return false;
    UIView* root = window.rootViewController.view ?: window;
    UIView* match = view_with_layer(root, layer);
    if (match == nil) match = view_with_layer(window, layer);
    if (match == nil) return false;
    match.hidden = NO;
    UIView* view = match;
    while (view.superview != nil) {
        [view.superview bringSubviewToFront:view];
        if (view.superview == window) break;
        view = view.superview;
    }
    return true;
}

extern "C" bool dkr_show_metal_layer(void* window_ptr, void* layer_ptr) {
    if (window_ptr == nullptr || layer_ptr == nullptr) return false;
    UIWindow* window = (__bridge UIWindow*)window_ptr;
    CALayer* layer = (__bridge CALayer*)layer_ptr;
    if ([NSThread isMainThread]) return show_metal_layer(window, layer);
    __block bool shown = false;
    dispatch_sync(dispatch_get_main_queue(), ^{
        shown = show_metal_layer(window, layer);
    });
    return shown;
}

@interface DkrMetalHostView : UIView
@end

@implementation DkrMetalHostView
+ (Class)layerClass {
    return [CAMetalLayer class];
}
@end

extern "C" void* dkr_create_metal_layer(void* window_ptr) {
    if (window_ptr == nullptr) return nullptr;
    __block void* retained = nullptr;
    auto create = ^{
        UIWindow* window = (__bridge UIWindow*)window_ptr;
        UIView* content = window.rootViewController.view ?: window;
        DkrMetalHostView* host = [[DkrMetalHostView alloc] initWithFrame:content.bounds];
        host.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        host.backgroundColor = UIColor.clearColor;
        // SDL owns touch delivery in the underlying UIKit view.
        host.userInteractionEnabled = NO;
        CAMetalLayer* metal = (CAMetalLayer*)host.layer;
        // Match the swapchain and SDL_GetWindowSizeInPixels, both of which use
        // the screen's native scale (it differs from scale under Display Zoom).
        UIScreen* screen = window.windowScene.screen ?: window.screen;
        metal.contentsScale = screen != nil ? screen.nativeScale
            : (content.contentScaleFactor > 0 ? content.contentScaleFactor : 2.0);
        metal.frame = host.bounds;
        [content addSubview:host];
        retained = (__bridge_retained void*)metal;
    };
    if ([NSThread isMainThread]) create();
    else dispatch_sync(dispatch_get_main_queue(), create);
    return retained;
}

extern "C" void dkr_destroy_metal_layer(void* layer_ptr) {
    if (layer_ptr == nullptr) return;
    auto destroy = ^{
        CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)layer_ptr;
        UIView* host = nil;
        // The layer's delegate is the hosting view under UIKit.
        if ([layer.delegate isKindOfClass:[UIView class]]) host = (UIView*)layer.delegate;
        [host removeFromSuperview];
        [layer removeFromSuperlayer];
    };
    if ([NSThread isMainThread]) destroy();
    else dispatch_sync(dispatch_get_main_queue(), destroy);
}

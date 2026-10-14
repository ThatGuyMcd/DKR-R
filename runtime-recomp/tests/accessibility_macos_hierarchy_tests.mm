#include "runtime_accessibility.hpp"
#include "accessibility_observation.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstring>

using namespace dkr::runtime::accessibility;

// Unlike the handled-event sentinel below, this receiver uses NSView's real
// child traversal. It reproduces an ancestor redispatching into the AX bridge.
@interface TraversingInputView : NSView
@property(nonatomic) NSUInteger equivalents;
@end
@implementation TraversingInputView
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    self.equivalents++;
    return [super performKeyEquivalent:event];
}
@end

@interface RecordingInputView : NSView <NSTextInputClient>
@property(nonatomic) NSUInteger downs, ups, flags, equivalents, commands, inserts, marks, unmarks;
@property(nonatomic, strong) NSEvent* lastEvent;
@property(nonatomic, strong) id inserted, marked;
@property(nonatomic) NSRange replacement, selected;
@end
@implementation RecordingInputView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent*)event { self.downs++; self.lastEvent = event; }
- (void)keyUp:(NSEvent*)event { self.ups++; self.lastEvent = event; }
- (void)flagsChanged:(NSEvent*)event { self.flags++; self.lastEvent = event; }
- (BOOL)performKeyEquivalent:(NSEvent*)event { self.equivalents++; self.lastEvent = event; return YES; }
- (void)doCommandBySelector:(SEL)selector { (void)selector; self.commands++; }
- (void)insertText:(id)value replacementRange:(NSRange)range { self.inserts++; self.inserted = value; self.replacement = range; }
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)range {
    self.marks++; self.marked = value; self.selected = selected; self.replacement = range;
}
- (void)unmarkText { self.unmarks++; self.marked = nil; }
- (NSRange)selectedRange { return NSMakeRange(2, 1); }
- (NSRange)markedRange { return NSMakeRange(1, 3); }
- (BOOL)hasMarkedText { return self.marked != nil; }
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[NSUnderlineStyleAttributeName]; }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual) *actual = range;
    return [[NSAttributedString alloc] initWithString:@"IME"];
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual) *actual = range;
    return NSMakeRect(11, 22, 33, 44);
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point { (void)point; return 7; }
@end

namespace {
// Traverse only the public AX children, never the implementation NSView tree.
id find_identifier(id root, NSString* identifier) {
    NSMutableArray* queue = [NSMutableArray arrayWithObject:root];
    NSMutableSet* seen = [NSMutableSet set];
    for (NSUInteger cursor = 0; cursor < queue.count && cursor < 512; ++cursor) {
        id element = queue[cursor];
        NSValue* identity = [NSValue valueWithPointer:(__bridge const void*)element];
        if ([seen containsObject:identity]) continue;
        [seen addObject:identity];
        if ([element respondsToSelector:@selector(accessibilityIdentifier)] &&
            [[element accessibilityIdentifier] isEqual:identifier]) return element;
        if ([element respondsToSelector:@selector(accessibilityChildren)]) {
            for (id child in [element accessibilityChildren]) {
                if (queue.count >= 512) break;
                [queue addObject:child];
            }
        }
    }
    return nil;
}

void print_focus(const char* owner, id element) {
    NSString* identifier = [element respondsToSelector:@selector(accessibilityIdentifier)]
        ? [element accessibilityIdentifier] : nil;
    NSString* role = [element respondsToSelector:@selector(accessibilityRole)]
        ? [element accessibilityRole] : nil;
    std::printf("%s focus class=%s identifier=%s role=%s\n", owner,
        element ? NSStringFromClass([element class]).UTF8String : "nil",
        identifier ? identifier.UTF8String : "nil", role ? role.UTF8String : "nil");
}

id focus_of(id owner) {
    return [owner respondsToSelector:@selector(accessibilityFocusedUIElement)]
        ? [owner accessibilityFocusedUIElement] : nil;
}

void frame(SDL_Window* window) {
    begin_frame("launcher/0", "Launcher");
    add_button("play", "PLAY", {20, 30, 120, 44});
    add_button("save", "SAVES", {20, 90, 120, 44});
    end_frame();
    pump_native(window);
    SDL_PumpEvents();
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
}

bool check_sdl_pointer(SDL_Window* window, NSWindow* native_window, NSView* root) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    NSView* target = [native_window.contentView hitTest:NSMakePoint(40, 40)];
    if (!target || target == root || [root hitTest:NSMakePoint(40, 40)]) return false;
    for (const auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
        NSEvent* mouse = [NSEvent mouseEventWithType:type location:NSMakePoint(40, 40)
            modifierFlags:0 timestamp:1 windowNumber:native_window.windowNumber context:nil
            eventNumber:1 clickCount:1 pressure:1];
        [native_window sendEvent:mouse];
    }
    CGEventRef cg_scroll = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, 3);
    NSEvent* wheel = [NSEvent eventWithCGEvent:cg_scroll];
    // AppKit sends wheel events to the hit-tested view, independently of the
    // first responder. Use the real SDL view chosen above, not a sentinel.
    [target scrollWheel:wheel];
    CFRelease(cg_scroll);
    unsigned downs = 0, ups = 0, wheels = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.windowID == SDL_GetWindowID(window)) ++downs;
        if (event.type == SDL_MOUSEBUTTONUP && event.button.windowID == SDL_GetWindowID(window)) ++ups;
        if (event.type == SDL_MOUSEWHEEL && event.wheel.windowID == SDL_GetWindowID(window) && event.wheel.y != 0) ++wheels;
    }
    std::printf("Native hit-test bypasses bridge=yes SDL pointer down=%u up=%u wheel=%u\n", downs, ups, wheels);
    return downs == 1 && ups == 1 && wheels == 1;
}

NSEvent* key_event(NSWindow* window, NSEventType type) {
    return [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:0 timestamp:1
        windowNumber:window.windowNumber context:nil characters:@"a" charactersIgnoringModifiers:@"a"
        isARepeat:NO keyCode:0];
}

// Exercise overlapping native key lifetimes through NSApplication's SDL
// event path. This proves the bridge's chord event contract, not a complete
// gameplay poll or guest N64 sample.
bool check_sdl_control_a_chord(SDL_Window* window, NSWindow* native_window) {
    const Uint32 window_id = SDL_GetWindowID(window);
    const auto control_event = [&](bool pressed) {
        return [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
            modifierFlags:pressed ? NSEventModifierFlagControl : 0 timestamp:pressed ? 4 : 7
            windowNumber:native_window.windowNumber context:nil characters:@""
            charactersIgnoringModifiers:@"" isARepeat:NO keyCode:59];
    };
    const auto a_event = [&](NSEventType type) {
        return [NSEvent keyEventWithType:type location:NSZeroPoint
            modifierFlags:NSEventModifierFlagControl timestamp:type == NSEventTypeKeyDown ? 5 : 6
            windowNumber:native_window.windowNumber context:nil characters:@"\x01"
            charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
    };
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    [NSApp sendEvent:control_event(true)];
    [NSApp sendEvent:a_event(NSEventTypeKeyDown)];
    unsigned control_downs = 0, a_downs = 0, premature_ups = 0;
    bool down_order = true;
    while (SDL_PollEvent(&event)) {
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;
        if (event.key.windowID != window_id) continue;
        if (event.key.keysym.scancode == SDL_SCANCODE_LCTRL) {
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) ++control_downs;
            else ++premature_ups;
        } else if (event.key.keysym.scancode == SDL_SCANCODE_A) {
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                ++a_downs;
                down_order &= control_downs == 1 && (event.key.keysym.mod & KMOD_CTRL) != 0;
            } else ++premature_ups;
        }
    }
    const bool control_held = (SDL_GetModState() & KMOD_CTRL) != 0;
    // A releases first while Ctrl remains held, then the flags event releases
    // Ctrl. Both native paths must yield exactly one SDL key-up.
    [NSApp sendEvent:a_event(NSEventTypeKeyUp)];
    unsigned a_ups = 0, unexpected = 0;
    bool up_has_control = true;
    while (SDL_PollEvent(&event)) {
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;
        if (event.key.windowID != window_id) continue;
        if (event.type == SDL_KEYUP && event.key.keysym.scancode == SDL_SCANCODE_A) {
            ++a_ups;
            up_has_control &= (event.key.keysym.mod & KMOD_CTRL) != 0;
        } else if (event.key.keysym.scancode == SDL_SCANCODE_A ||
                   event.key.keysym.scancode == SDL_SCANCODE_LCTRL) ++unexpected;
    }
    const bool control_survived_a_release = (SDL_GetModState() & KMOD_CTRL) != 0;
    [NSApp sendEvent:control_event(false)];
    unsigned control_ups = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;
        if (event.key.windowID != window_id) continue;
        if (event.type == SDL_KEYUP && event.key.keysym.scancode == SDL_SCANCODE_LCTRL) ++control_ups;
        else if (event.key.keysym.scancode == SDL_SCANCODE_A ||
                 event.key.keysym.scancode == SDL_SCANCODE_LCTRL) ++unexpected;
    }
    const bool released = (SDL_GetModState() & KMOD_CTRL) == 0;
    const bool passed = control_downs == 1 && a_downs == 1 && premature_ups == 0 &&
        down_order && control_held && a_ups == 1 && up_has_control &&
        control_survived_a_release && control_ups == 1 && unexpected == 0 && released;
    std::printf("Real SDL overlapping Ctrl+A chord=%s ctrl-down=%u a-down=%u a-up=%u ctrl-up=%u held-between-releases=%s released=%s\n",
        passed ? "yes" : "no", control_downs, a_downs, a_ups, control_ups,
        control_survived_a_release ? "yes" : "no", released ? "yes" : "no");
    return passed;
}

bool check_sdl_input(SDL_Window* window, NSWindow* native_window, id root, bool text_input_active = true) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    // SDL's Cocoa application handles keys/modifiers in NSApplication.sendEvent,
    // before AppKit routes them to the focused native responder.
    [NSApp sendEvent:key_event(native_window, NSEventTypeKeyDown)];
    [NSApp sendEvent:key_event(native_window, NSEventTypeKeyUp)];
    NSEvent* control_down = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
        modifierFlags:NSEventModifierFlagControl timestamp:2 windowNumber:native_window.windowNumber context:nil
        characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:59];
    NSEvent* control_up = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
        modifierFlags:0 timestamp:3 windowNumber:native_window.windowNumber context:nil
        characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:59];
    [NSApp sendEvent:control_down]; [NSApp sendEvent:control_up];
    if (text_input_active) [root insertText:@"z" replacementRange:NSMakeRange(NSNotFound, 0)];
    unsigned downs = 0, ups = 0, texts = 0, modifier_downs = 0, modifier_ups = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN && event.key.windowID == SDL_GetWindowID(window) && event.key.keysym.sym == SDLK_a) ++downs;
        if (event.type == SDL_KEYUP && event.key.windowID == SDL_GetWindowID(window) && event.key.keysym.sym == SDLK_a) ++ups;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LCTRL) ++modifier_downs;
        if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_LCTRL) ++modifier_ups;
        if (event.type == SDL_TEXTINPUT && event.text.windowID == SDL_GetWindowID(window) && std::strcmp(event.text.text, "z") == 0) ++texts;
    }
    const bool modifiers_released = (SDL_GetModState() & KMOD_CTRL) == 0;
    std::printf("Real SDL input a-down=%u a-up=%u text-z=%u ctrl-down=%u ctrl-up=%u modifiers-released=%s\n",
        downs, ups, texts, modifier_downs, modifier_ups, modifiers_released ? "yes" : "no");
    const bool chord = check_sdl_control_a_chord(window, native_window);
    return downs == 1 && ups == 1 && texts == unsigned(text_input_active) &&
        modifier_downs == 1 && modifier_ups == 1 && modifiers_released && chord;
}

bool check_input_forwarding(NSWindow* window, id root, RecordingInputView* receiver) {
    NSEvent* down = key_event(window, NSEventTypeKeyDown);
    NSEvent* up = key_event(window, NSEventTypeKeyUp);
    NSEvent* flags = key_event(window, NSEventTypeFlagsChanged);
    // Route keyboard through the actual window's first responder.
    [window sendEvent:down];
    bool passed = receiver.downs == 1 && receiver.lastEvent == down;
    [window sendEvent:up];
    passed &= receiver.ups == 1 && receiver.lastEvent == up;
    [window sendEvent:flags];
    passed &= receiver.flags == 1 && receiver.lastEvent == flags;
    passed &= [root performKeyEquivalent:down] && receiver.equivalents == 1;
    [root doCommandBySelector:@selector(moveLeft:)];
    passed &= receiver.commands == 1;
    NSRange replacement = NSMakeRange(4, 2), selected = NSMakeRange(1, 1), actual;
    [root insertText:@"insert" replacementRange:replacement];
    passed &= receiver.inserts == 1 && [receiver.inserted isEqual:@"insert"] && NSEqualRanges(receiver.replacement, replacement);
    [root setMarkedText:@"composing" selectedRange:selected replacementRange:replacement];
    passed &= receiver.marks == 1 && [receiver.marked isEqual:@"composing"] &&
        NSEqualRanges(receiver.selected, selected) && NSEqualRanges(receiver.replacement, replacement) && [root hasMarkedText];
    passed &= NSEqualRanges([root selectedRange], NSMakeRange(2, 1));
    passed &= NSEqualRanges([root markedRange], NSMakeRange(1, 3));
    passed &= [[root validAttributesForMarkedText] isEqual:@[NSUnderlineStyleAttributeName]];
    NSAttributedString* substring = [root attributedSubstringForProposedRange:selected actualRange:&actual];
    passed &= [substring.string isEqual:@"IME"] && NSEqualRanges(actual, selected);
    passed &= NSEqualRects([root firstRectForCharacterRange:selected actualRange:&actual], NSMakeRect(11, 22, 33, 44)) &&
        NSEqualRanges(actual, selected);
    passed &= [root characterIndexForPoint:NSMakePoint(5, 6)] == 7;
    passed &= [root inputContext] == receiver.inputContext;
    [root unmarkText];
    passed &= receiver.unmarks == 1 && ![root hasMarkedText];
    std::printf("Native keyboard/flags/edit commands and IME forwarding=%s (down=%lu up=%lu flags=%lu)\n",
        passed ? "yes" : "no", (unsigned long)receiver.downs, (unsigned long)receiver.ups, (unsigned long)receiver.flags);
    return passed;
}
}

int main() {
    @autoreleasepool {
        if (![NSThread isMainThread] || SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "Cannot initialize native hierarchy test: %s\n", SDL_GetError());
            return 2;
        }
        SDL_Window* window = SDL_CreateWindow("Accessible hierarchy test", 40, 60, 640, 480,
            SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) { std::fprintf(stderr, "No window: %s\n", SDL_GetError()); SDL_Quit(); return 2; }
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) { SDL_DestroyWindow(window); SDL_Quit(); return 2; }
        NSWindow* native_window = info.info.cocoa.window;
        NSView* original_content = native_window.contentView;
        NSResponder* initial_responder = native_window.firstResponder;
        [native_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        SDL_PumpEvents();
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        // Start SDL text input before attaching the bridge. Cocoa SDL installs
        // its translator as first responder; the bridge must preserve that IME
        // receiver rather than an earlier responder from before activation.
        SDL_StartTextInput();
        NSResponder* original_responder = native_window.firstResponder;
        frame(window);
        id root = find_identifier(native_window, @"dkr-host-accessibility");
        id play = find_identifier(native_window, @"play");
        id save = find_identifier(native_window, @"save");
        std::printf("Public window AX discovery root=%s play=%s save=%s\n",
            root ? "yes" : "no", play ? "yes" : "no", save ? "yes" : "no");
        id window_focus = focus_of(native_window);
        id app_focus = focus_of(NSApp);
        print_focus("NSWindow", window_focus); print_focus("NSApplication", app_focus);
        const bool initial_focus_reachable = play && (window_focus == play || app_focus == play);
        bool changed_focus_reachable = false;
        if (save) {
            [save setAccessibilityFocused:YES];
            frame(window);
            window_focus = focus_of(native_window);
            app_focus = focus_of(NSApp);
            print_focus("NSWindow after AX focus", window_focus);
            print_focus("NSApplication after AX focus", app_focus);
            changed_focus_reachable = window_focus == save || app_focus == save;
        }
        // A native picker may return SDL's saved responder without changing
        // the semantic selection. The next pump must reconcile ownership even
        // when the last focused AX identifier is still "save".
        [native_window makeFirstResponder:original_responder];
        frame(window);
        const bool same_target_reacquired = native_window.firstResponder == root &&
            focus_of(native_window) == save && focus_of(NSApp) == save;
        std::printf("Saved SDL responder return with unchanged AX target reacquired=%s\n",
            same_target_reacquired ? "yes" : "no");
        // Actual native-picker Cancel can return NSWindow itself instead of
        // the saved SDL responder. Exercise the unchanged ROM modal snapshot
        // through public window/application focus, not just bridge metadata.
        auto picker = host_tree().snapshot();
        picker.scope = "modal/Select Diddy Kong Racing ROM";
        picker.label = "Select Diddy Kong Racing ROM";
        picker.modal = true;
        picker.nodes.resize(1);
        picker.nodes[0].id = "rom-system-picker";
        picker.nodes[0].label = "SYSTEM FILE PICKER";
        picker.nodes[0].focused = true;
        native::update(window, picker);
        id system_picker = find_identifier(native_window, @"rom-system-picker");
        [native_window makeFirstResponder:nil];
        const bool picker_parent_default = native_window.firstResponder == native_window;
        native::update(window, picker);
        const bool picker_focus_restored = picker_parent_default && system_picker &&
            native_window.firstResponder == root && focus_of(native_window) == system_picker &&
            focus_of(NSApp) == system_picker &&
            [[system_picker accessibilityLabel] isEqual:@"SYSTEM FILE PICKER"] &&
            [[root accessibilitySubrole] isEqual:NSAccessibilityDialogSubrole];
        const bool picker_input_forwarded = check_sdl_input(window, native_window, root);
        frame(window);
        const bool picker_cancel_launcher_focus = native_window.firstResponder == root &&
            focus_of(native_window) == find_identifier(native_window, @"save");
        std::printf("Native picker parent-default return=%s public modal focus=%s input=%s launcher return=%s\n",
            picker_parent_default ? "yes" : "no", picker_focus_restored ? "yes" : "no",
            picker_input_forwarded ? "yes" : "no", picker_cancel_launcher_focus ? "yes" : "no");
        const bool preserved_surface = native_window.contentView == original_content;
        const bool root_owns_focus = native_window.firstResponder == root;
        const bool sdl_input = root && check_sdl_input(window, native_window, root);
        std::printf("Content view preserved=%s accessibility root owns native focus=%s\n",
            preserved_surface ? "yes" : "no", root_owns_focus ? "yes" : "no");
        NSView* bridge = (NSView*)root;
        id saved_receiver = [bridge valueForKey:@"eventReceiver"];
        TraversingInputView* ancestor = [[TraversingInputView alloc] initWithFrame:original_content.bounds];
        [original_content addSubview:ancestor];
        [bridge removeFromSuperview];
        [ancestor addSubview:bridge];
        [bridge setValue:ancestor forKey:@"eventReceiver"];
        NSEvent* equivalent = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:native_window.windowNumber
            context:nil characters:@"x" charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
        const bool ancestor_cycle_safe = ![bridge performKeyEquivalent:equivalent] && ancestor.equivalents == 1;
        // A second unrelated dispatch must also work: the guard is per-call.
        const bool ancestor_cycle_reset = ![bridge performKeyEquivalent:equivalent] && ancestor.equivalents == 2;
        [bridge setValue:saved_receiver forKey:@"eventReceiver"];
        [bridge removeFromSuperview];
        [original_content addSubview:bridge];
        [ancestor removeFromSuperview];
        [native_window makeFirstResponder:bridge];
        std::printf("Ancestor key-equivalent redispatch cycle guarded=%s subsequent dispatch=%s\n",
            ancestor_cycle_safe ? "yes" : "no", ancestor_cycle_reset ? "yes" : "no");
        if (!ancestor_cycle_safe || !ancestor_cycle_reset) return 1;
        if (!check_sdl_pointer(window, native_window, bridge)) return 1;
        Node text_node;
        text_node.id = "public-player-name"; text_node.label = "Player name";
        text_node.role = Role::TextField; text_node.value = "Original";
        text_node.text_capacity = 32;
        const auto text_frame = [&]() {
            begin_frame("public-text-entry", "Player setup");
            auto action = add_control(text_node);
            add_text("public-readonly", "Readonly status");
            end_frame(); pump_native(window);
            return action;
        };
        bool public_text_contract = !text_frame();
        id public_text = find_identifier(native_window, @"public-player-name");
        id public_readonly = find_identifier(native_window, @"public-readonly");
        public_text_contract &= public_text && public_readonly &&
            [[public_text accessibilityRole] isEqual:NSAccessibilityTextFieldRole] &&
            [[public_text accessibilityLabel] isEqual:@"Player name"] &&
            [[public_text accessibilityValue] isEqual:@"Original"] &&
            [public_text isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)] &&
            ![public_readonly isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)] &&
            ![public_readonly isAccessibilitySelectorAllowed:@selector(setAccessibilityLabel:)];
        [public_text setAccessibilityFocused:YES];
        public_text_contract &= !text_frame() && focus_of(native_window) == public_text && focus_of(NSApp) == public_text;
        [public_text setAccessibilityValue:@"Renée"];
        auto text_action = text_frame();
        public_text_contract &= text_action && text_action->kind == ActionKind::SetText && text_action->text == "Renée";
        if (text_action) text_node.value = text_action->text;
        public_text_contract &= !text_frame() && [[public_text accessibilityValue] isEqual:@"Renée"];
        const bool text_sdl_input = check_sdl_input(window, native_window, bridge);
        std::printf("Public text-field label/value/focus/queued edit/readonly contract=%s SDL input=%s\n",
            public_text_contract ? "yes" : "no", text_sdl_input ? "yes" : "no");
        // Opt-in production game summaries must reach public AppKit focus,
        // while forwarding the same SDL input and keeping text readonly.
        Observation game;
        game.kind = StateKind::TitleMenu; game.menu_name = "Title"; game.selection_label = "START";
        const auto game_snapshot = [&](NativeFocusPolicy policy) {
            return native_snapshot({}, game, std::nullopt, Clock::now(), 0, policy);
        };
        native::update(window, game_snapshot(NativeFocusPolicy::RegisteredControls));
        const bool ordinary_game_focus = native_window.firstResponder == original_responder && focus_of(root) == nil;
        native::update(window, game_snapshot(NativeFocusPolicy::GameSummary));
        id summary = find_identifier(native_window, @"game-status");
        const bool summary_focus = summary && native_window.firstResponder == root &&
            focus_of(native_window) == summary && focus_of(NSApp) == summary &&
            [[summary accessibilityRole] isEqual:NSAccessibilityStaticTextRole] &&
            [[summary accessibilityValue] isEqual:[NSString stringWithUTF8String:describe_observation(game).c_str()]] &&
            ![summary accessibilityPerformPress];
        const bool summary_sdl_input = check_sdl_input(window, native_window, bridge);
        game.selection_label = "OPTIONS";
        native::update(window, game_snapshot(NativeFocusPolicy::GameSummary));
        const bool summary_stable = find_identifier(native_window, @"game-status") == summary &&
            focus_of(native_window) == summary && focus_of(NSApp) == summary;
        frame(window);
        const bool host_roundtrip = native_window.firstResponder == root && focus_of(native_window) != summary;
        native::update(window, game_snapshot(NativeFocusPolicy::GameSummary));
        const bool game_return = native_window.firstResponder == root &&
            focus_of(native_window) == find_identifier(native_window, @"game-status");
        native::update(window, game_snapshot(NativeFocusPolicy::RegisteredControls));
        const bool summary_off_restored = native_window.firstResponder == original_responder && focus_of(root) == nil;
        std::printf("Game summary public focus=%s readonly/default focus=%s forwarding=%s stable=%s host roundtrip=%s off restoration=%s\n",
            summary_focus ? "yes" : "no", ordinary_game_focus ? "yes" : "no", summary_sdl_input ? "yes" : "no",
            summary_stable ? "yes" : "no", host_roundtrip && game_return ? "yes" : "no", summary_off_restored ? "yes" : "no");
        detach_native();
        const bool removed = !find_identifier(native_window, @"dkr-host-accessibility");
        const bool restored = native_window.firstResponder == original_responder;
        SDL_StopTextInput();
        [native_window makeFirstResponder:initial_responder];

        // A native picker can retire SDL's temporary text-input view. Keep a
        // detached receiver alive deliberately, proving we fall back only to
        // the original attached SDL content view on a key-parent return.
        RecordingInputView* retired = [[RecordingInputView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
        [original_content addSubview:retired];
        [native_window makeFirstResponder:retired];
        frame(window);
        id retirement_root = find_identifier(native_window, @"dkr-host-accessibility");
        [native_window makeFirstResponder:nil];
        [retired removeFromSuperview];
        frame(window);
        const bool retired_receiver_recovered = retirement_root &&
            native_window.firstResponder == retirement_root &&
            [retirement_root valueForKey:@"eventReceiver"] == original_content &&
            [retirement_root valueForKey:@"previousResponder"] == original_content &&
            focus_of(native_window) == find_identifier(native_window, @"play") &&
            // The retired text client no longer provides IME. Verify the
            // ordinary keyboard contract without claiming text composition.
            check_sdl_input(window, native_window, retirement_root, false);
        detach_native();
        const bool fallback_restored = native_window.firstResponder == original_content;
        std::printf("Retired native text receiver recovery=%s original SDL fallback restored=%s\n",
            retired_receiver_recovered ? "yes" : "no", fallback_restored ? "yes" : "no");

        // A native recording receiver provides a deterministic check of the
        // bridge contract, without modifying SDL's content view or OS settings.
        RecordingInputView* receiver = [[RecordingInputView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
        [original_content addSubview:receiver];
        [native_window makeFirstResponder:receiver];
        frame(window);
        root = find_identifier(native_window, @"dkr-host-accessibility");
        const bool forwarding = root && native_window.firstResponder == root && check_input_forwarding(native_window, root, receiver);
        detach_native();
        const bool receiver_restored = native_window.firstResponder == receiver;

        // Another real native text field owns input; a new AX focus request
        // must not take its field editor or alter it when the bridge detaches.
        frame(window);
        root = find_identifier(native_window, @"dkr-host-accessibility");
        NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(180, 30, 160, 28)];
        [original_content addSubview:field];
        [native_window makeFirstResponder:field];
        NSResponder* editor = native_window.firstResponder;
        const bool field_owns_input = field.currentEditor && editor == field.currentEditor;
        native::update(window, game_snapshot(NativeFocusPolicy::GameSummary));
        const bool summary_editor_preserved = field_owns_input && native_window.firstResponder == editor &&
            !native::input_focused(window);
        frame(window);
        NSString* focus_id = [[root accessibilityFocusedUIElement] accessibilityIdentifier];
        id next = find_identifier(native_window, [focus_id isEqual:@"play"] ? @"save" : @"play");
        if (next) [next setAccessibilityFocused:YES];
        frame(window);
        const bool native_input_preserved = field_owns_input && editor != root && native_window.firstResponder == editor;
        // Once the editor ends and SDL is restored, semantic focus remains on
        // the requested button. Reacquire it without a new focus request.
        [native_window makeFirstResponder:receiver];
        frame(window);
        const bool editor_return_reacquired = native_window.firstResponder == root &&
            focus_of(native_window) == next && focus_of(NSApp) == next;
        std::printf("Native editor return with unchanged AX target reacquired=%s\n",
            editor_return_reacquired ? "yes" : "no");
        // Also retain the teardown guard: a different active editor keeps its
        // responder when the accessibility bridge detaches.
        [native_window makeFirstResponder:field];
        editor = native_window.firstResponder;
        const bool editor_active_at_detach = field.currentEditor && editor == field.currentEditor;
        detach_native();
        const bool native_input_preserved_on_detach = editor_active_at_detach && native_window.firstResponder == editor;
        [native_window makeFirstResponder:initial_responder];
        [field removeFromSuperview]; [receiver removeFromSuperview];
        std::printf("Original SDL responder restored=%s recording receiver restored=%s other native input preserved=%s\n",
            restored ? "yes" : "no", receiver_restored ? "yes" : "no",
            native_input_preserved && native_input_preserved_on_detach ? "yes" : "no");
        SDL_DestroyWindow(window); SDL_Quit();
        const bool passed = public_text_contract && text_sdl_input && root && play && save && initial_focus_reachable &&
            changed_focus_reachable && same_target_reacquired && picker_focus_restored && picker_input_forwarded &&
            picker_cancel_launcher_focus && preserved_surface && root_owns_focus && sdl_input && removed && restored &&
            retired_receiver_recovered && fallback_restored &&
            forwarding && receiver_restored && native_input_preserved && editor_return_reacquired && native_input_preserved_on_detach &&
            ordinary_game_focus && summary_focus && summary_sdl_input && summary_stable && host_roundtrip && game_return &&
            summary_off_restored && summary_editor_preserved;
        std::printf("%s: public window AX hierarchy and window/application focused element\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }
}

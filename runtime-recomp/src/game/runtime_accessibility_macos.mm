#include "runtime_accessibility.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace dkr::runtime::accessibility;

@class DkrAccessibilityView;
@interface DkrAccessibilityElement : NSAccessibilityElement {
@public
    Action request;
    Node metadata;
    BOOL focused;
}
@property(nonatomic, strong) id displayValue;
@end

@implementation DkrAccessibilityElement
- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector {
    const bool interactive = self.accessibilityEnabled && metadata.role != Role::StaticText;
    if (selector == @selector(setAccessibilityFocused:)) return interactive;
    if (selector == @selector(accessibilityPerformPress))
        return interactive && metadata.role != Role::Slider && metadata.role != Role::TextField;
    if (selector == @selector(accessibilityPerformIncrement) || selector == @selector(accessibilityPerformDecrement))
        return interactive && (metadata.role == Role::Slider || metadata.role == Role::Choice);
    if (selector == @selector(setAccessibilityValue:))
        return interactive && (metadata.role == Role::Checkbox || metadata.role == Role::Slider || metadata.role == Role::Choice || metadata.role == Role::TextField);
    if (selector == @selector(setAccessibilityExpanded:)) return interactive && metadata.role == Role::Disclosure;
    return [super isAccessibilitySelectorAllowed:selector];
}
- (BOOL)accessibilityPerformPress {
    if (!self.accessibilityEnabled || metadata.role == Role::StaticText || metadata.role == Role::Slider || metadata.role == Role::TextField) return NO;
    Action action = request;
    action.kind = ActionKind::Press;
    return host_tree().enqueue(std::move(action)) ? YES : NO;
}
- (BOOL)isAccessibilityFocused { return focused; }
- (void)setAccessibilityFocused:(BOOL)value {
    if (!value || !self.accessibilityEnabled || metadata.role == Role::StaticText) return;
    Action action = request;
    action.kind = ActionKind::Focus;
    host_tree().enqueue(std::move(action));
}
- (id)accessibilityValue { return self.displayValue; }
- (void)setAccessibilityValue:(id)value {
    if (!self.accessibilityEnabled) return;
    Action action = request;
    if (metadata.role == Role::Checkbox) {
        if ([value respondsToSelector:@selector(boolValue)] && [value boolValue] != (metadata.number != 0))
            [self accessibilityPerformPress];
        return;
    }
    if (metadata.role == Role::TextField) {
        if (![value isKindOfClass:NSString.class]) return;
        NSData* utf8 = [(NSString*)value dataUsingEncoding:NSUTF8StringEncoding];
        if (!utf8 || utf8.length > kMaxTextBytes || utf8.length > metadata.text_capacity) return;
        action.kind = ActionKind::SetText;
        if (utf8.length) action.text.assign(static_cast<const char*>(utf8.bytes), utf8.length);
        else action.text.clear();
        if (!valid_text_value(action.text, metadata.text_capacity)) return;
        host_tree().enqueue(std::move(action));
        return;
    }
    action.kind = ActionKind::SetValue;
    if (metadata.role == Role::Slider && [value respondsToSelector:@selector(doubleValue)]) {
        action.value = [value doubleValue];
    } else if (metadata.role == Role::Choice) {
        if ([value isKindOfClass:NSString.class]) {
            const std::string label([(NSString*)value UTF8String]);
            const auto match = std::find(metadata.choices.begin(), metadata.choices.end(), label);
            if (match == metadata.choices.end()) return;
            action.value = std::distance(metadata.choices.begin(), match);
        } else if ([value respondsToSelector:@selector(doubleValue)]) action.value = [value doubleValue];
        else return;
    } else return;
    host_tree().enqueue(std::move(action));
}
- (BOOL)accessibilityPerformIncrement {
    Action action = request; action.kind = ActionKind::Increment;
    return host_tree().enqueue(std::move(action));
}
- (BOOL)accessibilityPerformDecrement {
    Action action = request; action.kind = ActionKind::Decrement;
    return host_tree().enqueue(std::move(action));
}
- (BOOL)isAccessibilityExpanded { return metadata.role == Role::Disclosure && metadata.selected; }
- (void)setAccessibilityExpanded:(BOOL)value {
    if (metadata.role == Role::Disclosure && value != metadata.selected) [self accessibilityPerformPress];
}
@end

// A non-drawing, non-hit-testing view vends semantics alongside SDL's views.
// It does not replace SDL's contentView, responder or Metal surface.
@interface DkrAccessibilityView : NSView <NSTextInputClient>
@property(nonatomic, strong) NSArray<DkrAccessibilityElement*>* elements;
@property(nonatomic) BOOL modal;
@property(nonatomic, weak) NSResponder* previousResponder;
@property(nonatomic, weak) NSResponder* eventReceiver;
@property(nonatomic, weak) NSView* fallbackReceiver;
@property(nonatomic) BOOL forwardingKeyEquivalent;
@end
@implementation DkrAccessibilityView
- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)resignFirstResponder { return YES; }
- (void)keyDown:(NSEvent*)event { [self.eventReceiver keyDown:event]; }
- (void)keyUp:(NSEvent*)event { [self.eventReceiver keyUp:event]; }
- (void)flagsChanged:(NSEvent*)event { [self.eventReceiver flagsChanged:event]; }
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // NSView searches its children for key equivalents. SDL's content view
    // can be our receiver and ancestor, so that search visits this bridge
    // again. Decline the nested visit; AppKit can continue its normal search
    // and keyDown still reaches SDL through the existing forwarding path.
    if (self.forwardingKeyEquivalent || self.eventReceiver == self) return NO;
    self.forwardingKeyEquivalent = YES;
    @try {
        return [self.eventReceiver performKeyEquivalent:event];
    } @finally {
        self.forwardingKeyEquivalent = NO;
    }
}
- (NSTextInputContext*)inputContext {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:@selector(inputContext)] ? [receiver inputContext] : nil;
}
// Keep SDL's text input/IME client, composition and coordinate conversion.
// NSResponder's inherited editing methods would otherwise swallow input.
- (void)insertText:(id)value replacementRange:(NSRange)range {
    id receiver = self.eventReceiver;
    if ([receiver respondsToSelector:_cmd]) [receiver insertText:value replacementRange:range];
}
- (void)insertText:(id)value {
    [self insertText:value replacementRange:NSMakeRange(NSNotFound, 0)];
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)range {
    id receiver = self.eventReceiver;
    if ([receiver respondsToSelector:_cmd]) [receiver setMarkedText:value selectedRange:selected replacementRange:range];
}
- (void)unmarkText {
    id receiver = self.eventReceiver;
    if ([receiver respondsToSelector:_cmd]) [receiver unmarkText];
}
- (NSRange)selectedRange {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver selectedRange] : NSMakeRange(NSNotFound, 0);
}
- (NSRange)markedRange {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver markedRange] : NSMakeRange(NSNotFound, 0);
}
- (BOOL)hasMarkedText {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] && [receiver hasMarkedText];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver attributedSubstringForProposedRange:range actualRange:actual] : nil;
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver validAttributesForMarkedText] : @[];
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver firstRectForCharacterRange:range actualRange:actual] : NSZeroRect;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    id receiver = self.eventReceiver;
    return [receiver respondsToSelector:_cmd] ? [receiver characterIndexForPoint:point] : NSNotFound;
}
- (void)doCommandBySelector:(SEL)selector { [self.eventReceiver doCommandBySelector:selector]; }
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSAccessibilitySubrole)accessibilitySubrole { return self.modal ? NSAccessibilityDialogSubrole : nil; }
- (BOOL)isAccessibilityModal { return self.modal; }
- (NSArray*)accessibilityChildren { return self.elements ?: @[]; }
- (NSArray*)accessibilityChildrenInNavigationOrder { return self.accessibilityChildren; }
- (id)accessibilityFocusedUIElement {
    for (DkrAccessibilityElement* element in self.elements) if (element->focused) return element;
    return nil;
}
- (void)setAccessibilityFocused:(BOOL)value {
    if (value) [self.accessibilityFocusedUIElement setAccessibilityFocused:YES];
}
- (id)accessibilityHitTest:(NSPoint)point {
    for (DkrAccessibilityElement* element in self.elements)
        if (NSPointInRect(point, element.accessibilityFrame)) return element;
    return self;
}
@end

namespace dkr::runtime::accessibility::native {
namespace {
DkrAccessibilityView* root_view;
__weak NSWindow* native_window;
Uint32 window_id = 0;
NSMutableDictionary<NSString*, DkrAccessibilityElement*>* element_cache;
std::string scope;
std::string last_focus;
std::uint64_t generation = 0;
std::optional<Snapshot> last_snapshot;
NSRect last_screen_frame = NSZeroRect;

bool same_snapshot(const Snapshot& a, const Snapshot& b) {
    if (a.generation != b.generation || a.scope != b.scope || a.label != b.label ||
        a.modal != b.modal || a.nodes.size() != b.nodes.size()) return false;
    return std::equal(a.nodes.begin(), a.nodes.end(), b.nodes.begin(), [](const Node& x, const Node& y) {
        return x.id == y.id && x.label == y.label && x.role == y.role && x.enabled == y.enabled &&
            x.selected == y.selected && x.focused == y.focused && x.value == y.value && x.help == y.help &&
            x.number == y.number && x.minimum == y.minimum && x.maximum == y.maximum && x.step == y.step &&
            x.choices == y.choices && x.choice_enabled == y.choice_enabled && x.text_capacity == y.text_capacity &&
            x.bounds.x == y.bounds.x && x.bounds.y == y.bounds.y &&
            x.bounds.width == y.bounds.width && x.bounds.height == y.bounds.height;
    });
}
id display_observer;
__weak NSResponder* traced_responder;
std::string traced_scope;
bool traced_key_window = false;

NSString* text(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size()
                                encoding:NSUTF8StringEncoding] ?: @"";
}
NSAccessibilityRole native_role(Role role) {
    switch (role) {
    case Role::Button: return NSAccessibilityButtonRole;
    case Role::StaticText: return NSAccessibilityStaticTextRole;
    case Role::TextField: return NSAccessibilityTextFieldRole;
    case Role::Checkbox: return NSAccessibilityCheckBoxRole;
    case Role::Slider: return NSAccessibilitySliderRole;
    case Role::Choice: return NSAccessibilityPopUpButtonRole;
    case Role::Disclosure: return NSAccessibilityDisclosureTriangleRole;
    case Role::MenuItem: return NSAccessibilityMenuItemRole;
    }
    return NSAccessibilityUnknownRole;
}
void read_preferences() {
    NSWorkspace* workspace = NSWorkspace.sharedWorkspace;
    set_display_preferences({workspace.accessibilityDisplayShouldIncreaseContrast != NO,
        workspace.accessibilityDisplayShouldReduceTransparency != NO,
        workspace.accessibilityDisplayShouldReduceMotion != NO,
        workspace.accessibilityDisplayShouldDifferentiateWithoutColor != NO});
}
NSRect screen_bounds(Bounds bounds) {
    // ImGui/SDL dimensions are logical window points. NSView's flipped origin
    // plus AppKit conversion handles the screen origin and Retina scale.
    NSRect local = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    local = NSIntersectionRect(local, root_view.bounds);
    const NSRect window_rect = [root_view convertRect:local toView:nil];
    return [native_window convertRectToScreen:window_rect];
}
bool parent_has_default_focus() {
    return native_window.firstResponder == native_window &&
        native_window.isKeyWindow && NSApp.keyWindow == native_window &&
        native_window.attachedSheet == nil;
}
bool can_acquire_focus() {
    NSResponder* saved = root_view.previousResponder;
    NSResponder* current = native_window.firstResponder;
    if (!saved || saved == root_view) return false;
    if ([saved isKindOfClass:NSView.class] && [(NSView*)saved window] != native_window) return false;
    if ([saved isKindOfClass:NSWindow.class] && saved != native_window) return false;
    // Native file panels/editors keep their own responder. Recover only the
    // saved SDL responder, our own view, or the key parent's default below.
    // NSOpenPanel can return the parent window's default responder rather
    // than SDL's saved view. Recover the semantic target only once that
    // parent is key again; sheets and native editors retain their input.
    return current == root_view || (current == native_window
        ? parent_has_default_focus() : current == saved);
}
void trace_focus(const Snapshot& snapshot, const std::string& focus) {
    const char* enabled = std::getenv("DKR_ACCESSIBILITY_TRACE");
    if (!enabled || std::strcmp(enabled, "1") != 0) return;
    NSResponder* current = native_window.firstResponder;
    const bool key = native_window.isKeyWindow;
    if (current == traced_responder && snapshot.scope == traced_scope && key == traced_key_window) return;
    id window_focus = native_window.accessibilityFocusedUIElement;
    id semantic_focus = root_view.accessibilityFocusedUIElement;
    const auto class_name = [](id object) { return object ? NSStringFromClass([object class]).UTF8String : "nil"; };
    std::fprintf(stderr,
        "[a11y][focus] scope=%s key=%d current=%s saved=%s receiver=%s root-owns=%d target=%s local-window-focus=%s child-window=%s child-top=%s\n",
        snapshot.scope.c_str(), int(key), class_name(current), class_name(root_view.previousResponder),
        class_name(root_view.eventReceiver), int(current == root_view), focus.c_str(), class_name(window_focus),
        class_name([semantic_focus accessibilityWindow]), class_name([semantic_focus accessibilityTopLevelUIElement]));
    traced_responder = current; traced_scope = snapshot.scope; traced_key_window = key;
}
}

void detach() {
    if (![NSThread isMainThread]) return;
    if (root_view) {
        if (native_window.firstResponder == root_view)
            [native_window makeFirstResponder:root_view.previousResponder ?: native_window.contentView];
        root_view.elements = @[];
        NSAccessibilityPostNotification(root_view, NSAccessibilityUIElementDestroyedNotification);
        [root_view removeFromSuperview];
    }
    if (display_observer) {
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:display_observer];
        display_observer = nil;
    }
    root_view = nil; native_window = nil; element_cache = nil;
    window_id = 0; generation = 0; scope.clear(); last_focus.clear();
    last_snapshot.reset(); last_screen_frame = NSZeroRect;
    traced_responder = nil; traced_scope.clear(); traced_key_window = false;
}

void update(SDL_Window* window, const Snapshot& snapshot) {
    if (![NSThread isMainThread] || !window) return;
    @autoreleasepool {
        const Uint32 current_id = SDL_GetWindowID(window);
        if (!root_view || window_id != current_id) {
            detach();
            SDL_SysWMinfo info{};
            SDL_VERSION(&info.version);
            if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) return;
            native_window = info.info.cocoa.window;
            if (!native_window.contentView) return;
            window_id = current_id;
            root_view = [[DkrAccessibilityView alloc] initWithFrame:native_window.contentView.bounds];
            root_view.previousResponder = native_window.firstResponder;
            root_view.fallbackReceiver = native_window.contentView;
            root_view.eventReceiver = native_window.firstResponder != native_window
                ? native_window.firstResponder : native_window.contentView;
            if (!root_view.eventReceiver) root_view.eventReceiver = native_window.contentView;
            root_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            root_view.accessibilityIdentifier = @"dkr-host-accessibility";
            element_cache = [NSMutableDictionary dictionary];
            [native_window.contentView addSubview:root_view];
            read_preferences();
            display_observer = [NSWorkspace.sharedWorkspace.notificationCenter
                addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification) {
                    (void)notification;
                    read_preferences();
                }];
        }
        if (!NSEqualRects(root_view.frame, native_window.contentView.bounds))
            root_view.frame = native_window.contentView.bounds;
        // SDL can retire its temporary IME responder while this view owns
        // focus, or detach it across a native picker. Use the original SDL
        // content view only on that safe return path, never a native editor.
        const bool retired_receiver = !root_view.eventReceiver ||
            ([root_view.eventReceiver isKindOfClass:NSView.class] &&
             [(NSView*)root_view.eventReceiver window] != native_window);
        if (((native_window.firstResponder == root_view && !root_view.eventReceiver) ||
             (parent_has_default_focus() && retired_receiver)) &&
            root_view.fallbackReceiver.window == native_window) {
            root_view.eventReceiver = root_view.fallbackReceiver;
            root_view.previousResponder = root_view.fallbackReceiver;
        }
        // Local IDs can repeat across scopes. Keep the prior object alive so
        // rebuilding a modal with the same ID still reports its new AX focus.
        id previous_focused_element = root_view.accessibilityFocusedUIElement;
        const NSRect screen_frame = [native_window convertRectToScreen:[root_view convertRect:root_view.bounds toView:nil]];
        const bool needs_update = !last_snapshot || !same_snapshot(*last_snapshot, snapshot) ||
            !NSEqualRects(screen_frame, last_screen_frame);
        std::string focus = last_focus;
        if (needs_update) {
            root_view.accessibilityLabel = text(snapshot.label);
            root_view.modal = snapshot.modal;
            root_view.accessibilityHelp = snapshot.modal ? @"Modal dialog. Background controls are unavailable." : nil;
            // Game observations retain their scope and generation while rows
            // appear, expire or change order. Notify AX clients when the
            // actual child structure changes, independently of action identity.
            const bool changed = generation != snapshot.generation || scope != snapshot.scope ||
                !last_snapshot || last_snapshot->nodes.size() != snapshot.nodes.size() ||
                !std::equal(last_snapshot->nodes.begin(), last_snapshot->nodes.end(), snapshot.nodes.begin(),
                    [](const Node& a, const Node& b) { return a.id == b.id; });
            if (scope != snapshot.scope) [element_cache removeAllObjects];
            NSMutableArray<DkrAccessibilityElement*>* children = [NSMutableArray array];
            NSMutableArray<DkrAccessibilityElement*>* changed_values = [NSMutableArray array];
            NSMutableSet<NSString*>* present = [NSMutableSet set];
            focus.clear();
            bool geometry_changed = false;
            for (const auto& node : snapshot.nodes) {
                NSString* identifier = text(node.id);
                DkrAccessibilityElement* element = element_cache[identifier];
                const bool existed = element != nil;
                if (!element) {
                    element = [[DkrAccessibilityElement alloc] init];
                    element_cache[identifier] = element;
                }
                element.accessibilityIdentifier = identifier;
                element.accessibilityParent = root_view;
                element.accessibilityRole = native_role(node.role);
                element.accessibilityHelp = node.help.empty() ? nil : text(node.help);
                // Static text uses its value once; button names use their label.
                id previous_value = element.accessibilityValue;
                element.accessibilityLabel = node.role == Role::StaticText ? nil : text(node.label);
                element.displayValue = node.role == Role::StaticText ? text(node.label) :
                    (node.role == Role::TextField ? text(node.value) :
                    (node.role == Role::Checkbox || node.role == Role::Slider ? @(node.number) :
                     (!node.value.empty() ? text(node.value) : (node.selected ? @"Current page" : nil))));
                element.accessibilityMinValue = @(node.minimum);
                element.accessibilityMaxValue = @(node.maximum);
                element.accessibilityValueDescription = node.role == Role::TextField || node.value.empty() ? nil : text(node.value);
                if (existed && previous_value != element.accessibilityValue &&
                    ![previous_value isEqual:element.accessibilityValue]) [changed_values addObject:element];
                element.accessibilitySelected = node.selected;
                element.accessibilityEnabled = node.enabled;
                const NSRect next_frame = snapshot.scope == "game-status"
                    ? [native_window convertRectToScreen:[root_view convertRect:root_view.bounds toView:nil]]
                    : screen_bounds(node.bounds);
                // Notify clients that cache geometry, including when only the
                // window moved. Compare effective frames after clipping.
                if (existed && !NSEqualRects(element.accessibilityFrame, next_frame))
                    geometry_changed = true;
                element.accessibilityFrame = next_frame;
                element->focused = node.focused;
                element->metadata = node;
                element->request = {snapshot.generation, snapshot.scope, node.id, ActionKind::Press};
                [children addObject:element];
                [present addObject:identifier];
                if (node.focused) focus = node.id;
            }
            for (NSString* key in element_cache.allKeys)
                if (![present containsObject:key]) [element_cache removeObjectForKey:key];
            root_view.elements = children;
            scope = snapshot.scope; generation = snapshot.generation;
            if (changed || geometry_changed)
                NSAccessibilityPostNotification(root_view, NSAccessibilityLayoutChangedNotification);
            for (DkrAccessibilityElement* element in changed_values)
                NSAccessibilityPostNotification(element, NSAccessibilityValueChangedNotification);
            last_snapshot = snapshot;
            last_screen_frame = screen_frame;
        }
        // Native responder ownership can change with identical semantics after
        // a file picker/editor closes. Never skip this reconciliation.
        id focused_element = root_view.accessibilityFocusedUIElement;
        const bool focus_changed = focused_element != previous_focused_element;
        bool reacquired = false;
        // Reconcile native ownership separately from semantic identity. A
        // native picker may return the same SDL responder and the same button.
        if (focused_element && can_acquire_focus()) {
            if (native_window.firstResponder != root_view)
                reacquired = [native_window makeFirstResponder:root_view];
            if (focus_changed || reacquired)
                NSAccessibilityPostNotification(focused_element, NSAccessibilityFocusedUIElementChangedNotification);
        } else if (!focused_element && native_window.firstResponder == root_view) {
            [native_window makeFirstResponder:root_view.previousResponder ?: native_window.contentView];
        }
        last_focus = std::move(focus);
        trace_focus(snapshot, last_focus);
    }
}

bool screen_reader_active() {
    return [NSThread isMainThread] && NSWorkspace.sharedWorkspace.voiceOverEnabled;
}
// Game speech/demand uses the same native-input ownership boundary as focus.
// A field editor in this key window still owns input, as does an attached sheet.
bool input_focused(SDL_Window* window) {
    return [NSThread isMainThread] && window && SDL_GetWindowID(window) == window_id &&
        (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0 && native_window.isKeyWindow &&
        NSApp.keyWindow == native_window && native_window.attachedSheet == nil && can_acquire_focus();
}
void announce(const std::string& message) {
    if (![NSThread isMainThread] || message.empty() || !root_view || !native_window.isKeyWindow ||
        NSApp.keyWindow != native_window || native_window.attachedSheet != nil || !can_acquire_focus()) return;
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification,
        @{NSAccessibilityAnnouncementKey: text(message),
          NSAccessibilityPriorityKey: @(NSAccessibilityPriorityMedium)});
}
} // namespace dkr::runtime::accessibility::native

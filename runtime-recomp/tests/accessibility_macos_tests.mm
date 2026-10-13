#include "runtime_accessibility.hpp"
#include "accessibility_observation.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#import <Cocoa/Cocoa.h>
#include <cassert>
#include <cstdio>

using namespace dkr::runtime::accessibility;

// Capture the adapter's real notification call without depending on a running
// screen reader or observing another process's accessibility notifications.
static __weak id notification_root;
static NSUInteger layout_notifications;
static NSUInteger focus_notifications;
static __weak id notified_focus;
static NSUInteger announcement_notifications;
void NSAccessibilityPostNotification(id element, NSAccessibilityNotificationName notification) {
    if (element == notification_root && [notification isEqual:NSAccessibilityLayoutChangedNotification])
        ++layout_notifications;
    if ([notification isEqual:NSAccessibilityFocusedUIElementChangedNotification]) {
        ++focus_notifications;
        notified_focus = element;
    }
}

// Match AppKit's generic parameter exactly. A plain NSDictionary* declares a
// C++ overload here, leaving the adapter bound to AppKit instead of this capture.
void NSAccessibilityPostNotificationWithUserInfo(id element, NSAccessibilityNotificationName notification,
                                               NSDictionary<NSAccessibilityNotificationUserInfoKey, id>* user_info) {
    (void)element; (void)user_info;
    if ([notification isEqual:NSAccessibilityAnnouncementRequestedNotification]) ++announcement_notifications;
}

int main() {
    @autoreleasepool {
        assert([NSThread isMainThread]);
        assert(SDL_Init(SDL_INIT_VIDEO) == 0);
        SDL_Window* window = SDL_CreateWindow("Accessible host test", 40, 60, 640, 480,
            SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
        assert(window);
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        assert(SDL_GetWindowWMInfo(window, &info));
        NSWindow* native = info.info.cocoa.window;
        NSView* original = native.contentView;
        [native makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        SDL_PumpEvents();
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        begin_frame("launcher/0", "Launcher");
        add_button("play", "PLAY", {20, 30, 120, 44});
        add_button("start", "START", {20, 90, 120, 44}, false);
        end_frame();
        pump_native(window);
        NSView* root = native.contentView.subviews.lastObject;
        assert(root != original && native.contentView == original);
        notification_root = root;
        assert([root.accessibilityIdentifier isEqual:@"dkr-host-accessibility"]);
        NSArray* children = root.accessibilityChildren;
        assert(children.count == 2);
        id play = children[0], start = children[1];
        assert([[play accessibilityRole] isEqual:NSAccessibilityButtonRole]);
        assert([[play accessibilityLabel] isEqual:@"PLAY"]);
        assert([play isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert(![start isAccessibilityEnabled] && ![start accessibilityPerformPress]);
        assert(![start isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        NSArray* unchanged_children = root.accessibilityChildren;
        id unchanged_label = [play accessibilityLabel];
        for (int i = 0; i < 20; ++i) pump_native(window);
        assert(root.accessibilityChildren == unchanged_children);
        assert([play accessibilityLabel] == unchanged_label);
        // Help is not part of the shared action-generation identity. The
        // native adapter still updates it without requiring a generation bump.
        auto changed_help = host_tree().snapshot();
        changed_help.nodes[0].help = "Launch the selected Game Pak";
        native::update(window, changed_help);
        assert([[play accessibilityHelp] isEqual:@"Launch the selected Game Pak"]);
        assert(root.accessibilityChildren != unchanged_children);
        native::update(window, host_tree().snapshot());

        NSRect expected = [native convertRectToScreen:[root convertRect:NSMakeRect(20, 30, 120, 44) toView:nil]];
        assert(NSEqualRects([play accessibilityFrame], expected));
        // Geometry can change without changing semantic action identity.
        // Clients caching AX frames must receive one refresh notification.
        auto moved_control = host_tree().snapshot();
        auto geometry_notifications = layout_notifications;
        moved_control.nodes[0].bounds.x += 15;
        native::update(window, moved_control);
        assert(root.accessibilityChildren[0] == play);
        assert(!NSEqualRects([play accessibilityFrame], expected));
        assert(layout_notifications == ++geometry_notifications);
        native::update(window, moved_control);
        assert(layout_notifications == geometry_notifications);
        const NSRect before_window_move = [play accessibilityFrame];
        const NSPoint original_origin = native.frame.origin;
        [native setFrameOrigin:NSMakePoint(original_origin.x + 25, original_origin.y + 20)];
        native::update(window, moved_control);
        assert(root.accessibilityChildren[0] == play);
        assert(!NSEqualRects([play accessibilityFrame], before_window_move));
        assert(layout_notifications == ++geometry_notifications);
        native::update(window, moved_control);
        assert(layout_notifications == geometry_notifications);
        [native setFrameOrigin:original_origin];
        native::update(window, host_tree().snapshot());
        assert(NSEqualRects([play accessibilityFrame], expected));
        assert([play accessibilityPerformPress]);
        [play setAccessibilityFocused:YES];
        begin_frame("launcher/0", "Launcher");
        assert(add_button("play", "PLAY", {20, 30, 120, 44}));
        add_button("start", "START", {20, 90, 120, 44}, false);
        end_frame(); pump_native(window);
        assert(root.accessibilityChildren[0] == play); // stable objects across redraw
        assert([play isAccessibilityFocused]);
        assert(root.accessibilityFocusedUIElement == play);

        begin_frame("modal/quit", "Quit?", true);
        add_button("cancel", "CANCEL", {40, 60, 120, 44});
        end_frame(); pump_native(window);
        assert(root.accessibilityChildren.count == 1);
        assert([[root accessibilitySubrole] isEqual:NSAccessibilityDialogSubrole]);
        assert([root isAccessibilityModal]);
        assert(![play accessibilityPerformPress]); // retained stale native element
        id cancel = root.accessibilityChildren[0];
        assert([cancel isAccessibilityFocused] && [cancel accessibilityPerformPress]);
        begin_frame("modal/quit", "Quit?", true);
        assert(add_button("cancel", "CANCEL", {40, 60, 120, 44}));
        end_frame(); pump_native(window);
        begin_frame("launcher/0", "Launcher");
        add_button("play", "PLAY", {20, 30, 120, 44});
        end_frame(); pump_native(window);
        assert(![root isAccessibilityModal]);
        assert([root.accessibilityChildren[0] isAccessibilityFocused]);
        Node check;
        check.id = "automatic"; check.label = "Automatic announcements";
        check.role = Role::Checkbox; check.number = 1; check.value = "On";
        check.help = "Announce menu selection changes";
        Node slider;
        slider.id = "interval"; slider.label = "Position announcement interval";
        slider.role = Role::Slider; slider.number = 1500; slider.minimum = 500; slider.maximum = 10000;
        slider.value = "1500 milliseconds";
        Node choice;
        choice.id = "mode"; choice.label = "Mode"; choice.role = Role::Choice;
        choice.maximum = 2; choice.choices = {"Original", "Disabled", "Modern"};
        choice.choice_enabled = {true, false, true}; choice.value = "Original";
        Node disclosure;
        disclosure.id = "advanced"; disclosure.label = "Advanced";
        disclosure.role = Role::Disclosure; disclosure.selected = true;
        begin_frame("settings", "Settings");
        add_control(check); add_control(slider); add_control(choice); add_control(disclosure);
        end_frame(); pump_native(window);
        id checkbox = root.accessibilityChildren[0], range = root.accessibilityChildren[1];
        id options = root.accessibilityChildren[2], section = root.accessibilityChildren[3];
        for (id control in @[checkbox, range, options, section])
            assert([control isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert([[checkbox accessibilityRole] isEqual:NSAccessibilityCheckBoxRole]);
        assert([[checkbox accessibilityValue] boolValue]);
        assert([[checkbox accessibilityHelp] isEqual:@"Announce menu selection changes"]);
        assert([[range accessibilityRole] isEqual:NSAccessibilitySliderRole]);
        assert([[range accessibilityValue] intValue] == 1500);
        assert([[range accessibilityValueDescription] isEqual:@"1500 milliseconds"]);
        assert(![range isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
        assert([range isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        assert([[options accessibilityRole] isEqual:NSAccessibilityPopUpButtonRole]);
        assert([[section accessibilityRole] isEqual:NSAccessibilityDisclosureTriangleRole]);
        assert([section isAccessibilityExpanded]);
        [options setAccessibilityValue:@"Disabled"]; // rejected by portable queue
        [range setAccessibilityValue:@2000];
        begin_frame("settings", "Settings");
        assert(!add_control(check));
        auto changed = add_control(slider);
        assert(changed && changed->kind == ActionKind::SetValue && changed->value == 2000);
        assert(!add_control(choice)); add_control(disclosure);
        end_frame(); pump_native(window);
        slider.enabled = false;
        begin_frame("settings", "Settings");
        add_control(check); add_control(slider); add_control(choice); add_control(disclosure);
        end_frame(); pump_native(window);
        assert(![range isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        assert(![range isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert(![range accessibilityPerformIncrement]);
        assert(![play accessibilityPerformPress]);

        // Standard public text-field semantics use snapshot values and queued
        // edits; native callbacks never create or alter an AppKit field editor.
        Node name;
        name.id = "player-name"; name.label = "Player name";
        name.role = Role::TextField; name.value = "Original"; name.text_capacity = 32;
        const auto text_frame = [&]() {
            begin_frame("text-entry", "Text entry");
            auto action = add_control(name);
            add_text("readonly", "Readonly status");
            end_frame(); pump_native(window);
            return action;
        };
        assert(!text_frame());
        id text_field = root.accessibilityChildren[0], readonly = root.accessibilityChildren[1];
        assert([[text_field accessibilityRole] isEqual:NSAccessibilityTextFieldRole]);
        assert([[text_field accessibilityLabel] isEqual:@"Player name"]);
        assert([[text_field accessibilityValue] isEqual:@"Original"]);
        assert([text_field isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        assert([text_field isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert(![text_field isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
        assert(![text_field accessibilityPerformPress]);
        // Owner-set metadata properties are not client-settable AX attributes.
        assert(![readonly isAccessibilitySelectorAllowed:@selector(setAccessibilityLabel:)]);
        assert(![readonly isAccessibilitySelectorAllowed:@selector(setAccessibilityValueDescription:)]);
        assert(![readonly isAccessibilitySelectorAllowed:@selector(setAccessibilityEnabled:)]);
        assert(![text_field isAccessibilitySelectorAllowed:@selector(setAccessibilityLabel:)]);
        [text_field setAccessibilityFocused:YES];
        assert(!text_frame());
        assert(root.accessibilityFocusedUIElement == text_field);
        assert(native.accessibilityFocusedUIElement == text_field);
        assert(NSApp.accessibilityFocusedUIElement == text_field);
        [text_field setAccessibilityValue:@"Renée"];
        assert([[text_field accessibilityValue] isEqual:@"Original"]); // no optimistic mutation
        auto edit = text_frame();
        assert(edit && edit->kind == ActionKind::SetText && edit->text == "Renée");
        name.value = edit->text;
        assert(!text_frame());
        assert([[text_field accessibilityValue] isEqual:@"Renée"]);
        [text_field setAccessibilityValue:@123];
        [text_field setAccessibilityValue:@"line\nbreak"];
        [text_field setAccessibilityValue:[@"x" stringByPaddingToLength:33 withString:@"x" startingAtIndex:0]];
        assert(!text_frame());
        [text_field setAccessibilityValue:@""];
        edit = text_frame();
        assert(edit && edit->kind == ActionKind::SetText && edit->text.empty());
        name.enabled = false;
        assert(!text_frame());
        assert(![text_field isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        [text_field setAccessibilityValue:@"Disabled"];
        assert(!text_frame());
        name.enabled = true;
        assert(!text_frame());
        [text_field setAccessibilityValue:@"Stale edit"];
        begin_frame("text-entry-replaced", "Replacement");
        assert(!add_control(name));
        end_frame(); pump_native(window);
        [text_field setAccessibilityValue:@"Stale element"];
        begin_frame("text-entry-replaced", "Replacement");
        assert(!add_control(name));
        end_frame(); pump_native(window);

        begin_frame("offscreen-settings", "Settings");
        add_button("visible", "Visible setting", {20, 30, 120, 44});
        Node offscreen;
        offscreen.id = "offscreen"; offscreen.label = "Offscreen setting";
        offscreen.role = Role::Checkbox; offscreen.bounds = {20, 900, 120, 44};
        add_control(offscreen);
        end_frame(); pump_native(window);
        assert(root.accessibilityChildren.count == 2);
        id offscreen_element = root.accessibilityChildren[1];
        assert([[offscreen_element accessibilityLabel] isEqual:@"Offscreen setting"]);
        [offscreen_element setAccessibilityFocused:YES];
        begin_frame("offscreen-settings", "Settings");
        add_button("visible", "Visible setting", {20, 30, 120, 44});
        add_control(offscreen);
        assert(host_tree().take_focus_request("offscreen"));
        end_frame(); pump_native(window);
        assert([offscreen_element isAccessibilityFocused]);

        // Exercise the production composer rather than a handcrafted focus
        // flag. The policy changes keyboard AX focus, never static-text actions.
        Observation game;
        game.kind = StateKind::TitleMenu; game.menu_name = "Title"; game.selection_label = "START";
        const auto compose = [&](NativeFocusPolicy policy) {
            return native_snapshot({}, game, std::nullopt, Clock::now(), 0, policy);
        };
        NSResponder* sdl_responder = [root valueForKey:@"previousResponder"];
        native::update(window, compose(NativeFocusPolicy::RegisteredControls));
        assert(native.firstResponder == sdl_responder && root.accessibilityFocusedUIElement == nil);
        native::update(window, compose(NativeFocusPolicy::GameSummary));
        id summary = root.accessibilityChildren[0];
        assert(native.firstResponder == root && root.accessibilityFocusedUIElement == summary);
        assert([[summary accessibilityIdentifier] isEqual:@"game-status"]);
        assert([[summary accessibilityRole] isEqual:NSAccessibilityStaticTextRole]);
        assert([[summary accessibilityValue] isEqual:[NSString stringWithUTF8String:describe_observation(game).c_str()]]);
        // Readonly text may be the programmatic keyboard summary while never
        // advertising a focus/value setter or an action to assistive apps.
        assert([summary isAccessibilitySelectorAllowed:@selector(accessibilityValue)]);
        assert(![summary isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert(![summary isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        assert(![summary isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
        assert(![summary isAccessibilitySelectorAllowed:@selector(accessibilityPerformIncrement)]);
        assert(![summary isAccessibilitySelectorAllowed:@selector(accessibilityPerformDecrement)]);
        assert(![summary accessibilityPerformPress]);
        const auto focused_once = focus_notifications;
        for (int i = 0; i < 20; ++i) native::update(window, compose(NativeFocusPolicy::GameSummary));
        game.selection_label = "OPTIONS";
        native::update(window, compose(NativeFocusPolicy::GameSummary));
        assert(root.accessibilityChildren[0] == summary && root.accessibilityFocusedUIElement == summary);
        assert(focus_notifications == focused_once);
        native::update(window, compose(NativeFocusPolicy::RegisteredControls));
        assert(native.firstResponder == sdl_responder && root.accessibilityFocusedUIElement == nil);

        // Decoded menus share generation zero and the game-status scope even
        // when their rows appear, reorder or expire. Retain native identities
        // and focus while notifying exactly once for each structural change.
        Snapshot observed;
        observed.scope = "game-status"; observed.label = "Game status";
        Node status; status.id = "status"; status.label = "Title"; status.role = Role::StaticText;
        status.focused = true;
        observed.nodes = {status};
        native::update(window, observed);
        id status_element = root.accessibilityChildren[0];
        auto notifications = layout_notifications;
        Node row; row.id = "menu/0"; row.label = "START"; row.role = Role::StaticText;
        observed.nodes.push_back(row);
        native::update(window, observed);
        assert(layout_notifications == ++notifications);
        assert(root.accessibilityChildren[0] == status_element);
        assert(root.accessibilityFocusedUIElement == status_element);
        id row_element = root.accessibilityChildren[1];
        assert([row_element isAccessibilitySelectorAllowed:@selector(accessibilityValue)]);
        assert(![row_element isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)]);
        assert(![row_element isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
        for (int i = 0; i < 20; ++i) native::update(window, observed);
        assert(layout_notifications == notifications);
        observed.nodes[1].label = "OPTIONS";
        native::update(window, observed);
        assert(layout_notifications == notifications); // value-only change
        assert(root.accessibilityChildren[1] == row_element);
        std::swap(observed.nodes[0], observed.nodes[1]);
        native::update(window, observed);
        assert(layout_notifications == ++notifications);
        assert(root.accessibilityChildren[0] == row_element);
        assert(root.accessibilityChildren[1] == status_element);
        assert(root.accessibilityFocusedUIElement == status_element);
        observed.nodes.erase(observed.nodes.begin());
        native::update(window, observed);
        assert(layout_notifications == ++notifications);
        assert(root.accessibilityChildren.count == 1 && root.accessibilityChildren[0] == status_element);
        native::update(window, observed);
        assert(layout_notifications == notifications);

        // A modal may reuse the focused control's local ID. The AX object is
        // replaced on scope change, so notify clients of that new focus even
        // when AppKit's responder remains the same bridge view.
        Snapshot launcher;
        launcher.scope = "focus-regression/launcher"; launcher.label = "Launcher";
        Node button;
        button.id = "primary"; button.label = "Open ROM browser";
        button.role = Role::Button; button.focused = true;
        button.bounds = {20, 30, 120, 44};
        launcher.nodes = {button};
        native::update(window, launcher);
        assert(native.firstResponder == root);
        id previous_button = root.accessibilityFocusedUIElement;
        assert([[previous_button accessibilityLabel] isEqual:@"Open ROM browser"]);
        auto modal = launcher;
        modal.scope = "focus-regression/modal"; modal.label = "Select ROM"; modal.modal = true;
        modal.nodes[0].label = "Choose ROM file";
        const auto previous_focus_notifications = focus_notifications;
        native::update(window, modal);
        id modal_button = root.accessibilityFocusedUIElement;
        assert(modal_button != previous_button);
        assert([[modal_button accessibilityLabel] isEqual:@"Choose ROM file"]);
        assert([[root accessibilityLabel] isEqual:@"Select ROM"]);
        assert([root isAccessibilityModal]);
        assert([[root accessibilitySubrole] isEqual:NSAccessibilityDialogSubrole]);
        assert(root.accessibilityChildren.count == 1 && root.accessibilityChildren[0] == modal_button);
        assert(focus_notifications == previous_focus_notifications + 1);
        assert(notified_focus == modal_button);
        native::update(window, modal);
        assert(focus_notifications == previous_focus_notifications + 1);

        // A native panel may return to NSWindow's default responder with an
        // identical modal snapshot. Reacquire once and notify the same named
        // control, without regenerating children or posting every frame.
        SDL_ShowWindow(window);
        SDL_PumpEvents();
        [native makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        assert(native.isKeyWindow && NSApp.keyWindow == native);
        assert(native::input_focused(window));
        [native makeFirstResponder:nil];
        assert(native.firstResponder == native);
        assert(native::input_focused(window));
        const auto before_parent_return = focus_notifications;
        native::update(window, modal);
        assert(native.firstResponder == root);
        assert(root.accessibilityFocusedUIElement == modal_button);
        assert(focus_notifications == before_parent_return + 1 && notified_focus == modal_button);
        native::update(window, modal);
        assert(focus_notifications == before_parent_return + 1);

        // The same default responder must not be acquired while a different
        // native window is key (the situation while the picker is open).
        NSWindow* panel = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 100, 100)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [panel makeKeyAndOrderFront:nil];
        [native makeFirstResponder:nil];
        native::update(window, modal);
        assert(native.firstResponder == native);
        assert(!native::input_focused(window));
        const auto background_announcements = announcement_notifications;
        native::announce("Background speech must be suppressed");
        assert(announcement_notifications == background_announcements);
        assert(focus_notifications == before_parent_return + 1);
        [panel orderOut:nil];
        [native makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        SDL_PumpEvents();
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        assert(native.isKeyWindow && NSApp.keyWindow == native);
        [native makeFirstResponder:nil];
        native::update(window, modal);
        assert(native.firstResponder == root && notified_focus == modal_button);

        // Speech shares the responder guard: a key parent with the default
        // responder is valid, while a native editor or sheet owns its input.
        const auto speech_count = announcement_notifications;
        native::announce("Foreground speech");
        assert(announcement_notifications == speech_count + 1);
        NSTextField* native_field = [[NSTextField alloc] initWithFrame:NSMakeRect(180, 30, 160, 28)];
        [original addSubview:native_field];
        [native makeFirstResponder:native_field];
        NSResponder* field_editor = native.firstResponder;
        assert(native_field.currentEditor && field_editor == native_field.currentEditor);
        assert(!native::input_focused(window));
        native::announce("Editor speech must be suppressed");
        native::update(window, modal);
        assert(native.firstResponder == field_editor);
        assert(announcement_notifications == speech_count + 1);
        [native makeFirstResponder:root];
        [native_field removeFromSuperview];
        assert(native::input_focused(window));
        NSWindow* sheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 100, 100)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [native beginSheet:sheet completionHandler:nil];
        assert(native.attachedSheet == sheet && !native::input_focused(window));
        native::announce("Sheet speech must be suppressed");
        assert(announcement_notifications == speech_count + 1);
        [native endSheet:sheet];
        [sheet orderOut:nil];
        [native makeKeyAndOrderFront:nil];
        SDL_PumpEvents();
        [native makeFirstResponder:nil];
        assert(native::input_focused(window));
        native::announce("Default responder foreground speech");
        assert(announcement_notifications == speech_count + 2);
        native::update(window, modal);
        assert(native.firstResponder == root && native::input_focused(window));

        SDL_SetWindowSize(window, 800, 600);
        SDL_PumpEvents(); pump_native(window);
        assert(NSEqualRects(root.frame, original.bounds));
        const auto prefs = display_preferences();
        assert(prefs.reduce_motion == NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion);
        detach_native();
        assert(![native.contentView.subviews containsObject:root]);
        assert(![cancel accessibilityPerformPress]);
        SDL_DestroyWindow(window); SDL_Quit();
        std::puts("PASS: macOS accessibility roles, actions, focus, bounds, modal and teardown");
    }
}

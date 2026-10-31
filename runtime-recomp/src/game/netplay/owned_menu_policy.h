#pragma once
// Full-scene no-owner notifications; custom owners always get first refusal.
// Unknown service calls remain blocked by the native dispatch boundary.
static inline int dkr_menu_notification(unsigned event) {
    return event <= 23 || (event >= 100 && event <= 105);
}

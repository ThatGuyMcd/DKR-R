#pragma once
/* Exact full-scene notifications supported by the native no-owner path.
 * Owners always get first refusal. Unknown service calls remain blocked. */
static int dkr_menu_notification(unsigned event) {
    return event<=23 || (event>=100 && event<=105);
}

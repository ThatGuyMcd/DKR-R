#pragma once
/* Exact full-scene notifications supported by the native no-owner path.
 * Owners always get first refusal. Unknown service calls remain blocked. */
static int dkr_menu_notification(unsigned event) {
    /* 24/25 bracket trackmenu_input even when no code/mod owner exists. */
    return event<=25 || (event>=100 && event<=105);
}

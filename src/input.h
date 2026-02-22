/* src/input.h - Keyboard Input Handling */
#ifndef INPUT_H
#define INPUT_H

#include "data.h"
#include <stdbool.h>
#include <wayland-client.h>

/* Callback for Alt release event (set by main.c) */
typedef void (*alt_release_callback_t)(void);
extern alt_release_callback_t on_alt_release;

/* Callback for Escape key - hide without switching (set by main.c) */
extern alt_release_callback_t on_escape;

/* Reset Alt state (call when switcher shows to avoid stale detection) */
void input_reset_alt_state(void);

/* Set the modifier key by name (e.g., "Alt", "Super", "Control") */
void input_set_modifier(const char *name);

/* Get keyboard listener for Wayland seat */
const struct wl_keyboard_listener *get_keyboard_listener(void);

/* Cleanup input resources */
void input_cleanup(void);

#endif /* INPUT_H */

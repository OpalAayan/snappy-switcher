/* src/input.h - Keyboard Input Handling */
#ifndef INPUT_H
#define INPUT_H

#include "data.h"
#include <stdbool.h>
#include <wayland-client.h>

/* Dismiss trigger classification */
typedef enum {
    DISMISS_TYPE_MODIFIER,  /* Standard XKB modifier (Alt, Ctrl, Shift, Super) */
    DISMISS_TYPE_KEYCODE,   /* Regular key tracked via key press/release events */
} DismissType;

/* Callback for Alt release event (set by main.c) */
typedef void (*alt_release_callback_t)(void);
extern alt_release_callback_t on_alt_release;

/* Callback for Escape key - hide without switching (set by main.c) */
extern alt_release_callback_t on_escape;

/* Configure which modifier release dismisses the switcher (e.g. "alt", "super") */
void input_set_dismiss_modifier(const char *mod);

/* Set toggle mode: when true, modifier release will NOT dismiss the switcher.
 * Used when the switcher is opened via CMD_TOGGLE (no modifier held). */
void input_set_toggle_mode(bool enabled);

/* Reset modifier state (call when switcher shows to avoid stale detection) */
void input_reset_alt_state(void);

/* Get keyboard listener for Wayland seat */
const struct wl_keyboard_listener *get_keyboard_listener(void);

/* Get pointer listener for Wayland seat (hover to select, click to switch) */
const struct wl_pointer_listener *get_pointer_listener(void);

/* Cleanup input resources */
void input_cleanup(void);

#endif /* INPUT_H */

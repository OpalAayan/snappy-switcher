/* src/input.c - Keyboard Input Implementation */
#define _POSIX_C_SOURCE 200809L

#include "input.h"
#include "hyprland.h"
#include "render.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#define LOG(fmt, ...) fprintf(stderr, "[Input] " fmt "\n", ##__VA_ARGS__)

static struct xkb_context *xkb_ctx = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_st = NULL;

static const char *modifier_name = XKB_MOD_NAME_ALT; // Default to "Alt"
static bool alt_pressed = false;
static bool ignore_first_release = true;

alt_release_callback_t on_alt_release = NULL;
alt_release_callback_t on_escape = NULL;
static AppState *app_state = NULL;

void input_reset_alt_state(void) {
  alt_pressed = true;
  ignore_first_release = true;
  LOG("Alt state reset");
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int fd, uint32_t size) {
  (void)keyboard;
  (void)data;
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }

  char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return;
  }

  if (!xkb_ctx)
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_ctx) {
    munmap(map, size);
    close(fd);
    return;
  }

  if (xkb_st) {
    xkb_state_unref(xkb_st);
    xkb_st = NULL;
  }
  if (xkb_keymap) {
    xkb_keymap_unref(xkb_keymap);
    xkb_keymap = NULL;
  }

  xkb_keymap = xkb_keymap_new_from_string(
      xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map, size);
  close(fd);

  if (xkb_keymap)
    xkb_st = xkb_state_new(xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys) {
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)surface;
  (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface) {
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state_w) {
  (void)keyboard;
  (void)serial;
  (void)time;
  app_state = (AppState *)data;

  if (!xkb_st || !app_state || state_w != WL_KEYBOARD_KEY_STATE_PRESSED)
    return;

  xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_st, key + 8);

  switch (sym) {
  case XKB_KEY_Tab:
  case XKB_KEY_ISO_Left_Tab:
    if (app_state->count > 0) {
      if (sym == XKB_KEY_ISO_Left_Tab) {
        app_state->selected_index--;
        if (app_state->selected_index < 0)
          app_state->selected_index = app_state->count - 1;
      } else {
        app_state->selected_index++;
        if (app_state->selected_index >= app_state->count)
          app_state->selected_index = 0;
      }
      render_ui(app_state, app_state->width, app_state->height);
    }
    break;

  case XKB_KEY_Escape:
    if (on_escape)
      on_escape();
    else if (on_alt_release) {
      app_state->selected_index = 0;
      on_alt_release();
    }
    break;

  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter:
    if (app_state->count > 0 && app_state->selected_index >= 0 &&
        app_state->selected_index < app_state->count) {
      switch_to_window(app_state->windows[app_state->selected_index].address);
      if (on_alt_release)
        on_alt_release();
    }
    break;
  }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t depressed,
                               uint32_t latched, uint32_t locked,
                               uint32_t group) {
  (void)keyboard;
  (void)serial;
  app_state = (AppState *)data;

  if (!xkb_st)
    return;
  xkb_state_update_mask(xkb_st, depressed, latched, locked, 0, 0, group);

  bool alt_now = xkb_state_mod_name_is_active(xkb_st, modifier_name,
                                              XKB_STATE_MODS_EFFECTIVE);

  if (ignore_first_release) {
    ignore_first_release = false;
    alt_pressed = alt_now;
    return;
  }

  if (alt_pressed && !alt_now) {
    if (on_alt_release)
      on_alt_release();
  }
  alt_pressed = alt_now;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay) {
  (void)data;
  (void)keyboard;
  (void)rate;
  (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

const struct wl_keyboard_listener *get_keyboard_listener(void) {
  return &keyboard_listener;
}

// Allow setting the modifier key by name (e.g., "Alt", "Super", "Control")
void input_set_modifier(const char *name) {
  if (name && *name)
    modifier_name = name;
  else
    modifier_name = XKB_MOD_NAME_ALT;
}

void input_cleanup(void) {
  if (xkb_st)
    xkb_state_unref(xkb_st);
  if (xkb_keymap)
    xkb_keymap_unref(xkb_keymap);
  if (xkb_ctx)
    xkb_context_unref(xkb_ctx);
}

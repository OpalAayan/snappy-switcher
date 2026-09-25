/* src/main.c - Snappy Switcher Daemon (v4.0) */
#define _POSIX_C_SOURCE 200809L

#include "backend.h"
#include "config.h"
#include "icons.h"
#include "input.h"
#include "render.h"
#include "socket.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr_backend.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define LOG(fmt, ...) fprintf(stderr, "[Daemon] " fmt "\n", ##__VA_ARGS__)

/* Retry limits for robust startup */
#define WAYLAND_RETRY_MAX 25
#define WAYLAND_RETRY_MS 200
#define PROTOCOL_RETRY_MAX 50
#define PROTOCOL_RETRY_MS 100

/* Ruthless Takeover Protocol */
#define TAKEOVER_TIMEOUT_MS 1000
#define TAKEOVER_POLL_MS 100

/* Global State */
struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_shm *shm = NULL;
struct zwlr_layer_shell_v1 *layer_shell = NULL;
struct wl_surface *surface = NULL;
struct zwlr_layer_surface_v1 *layer_surface = NULL;
struct wl_output *output = NULL;
struct wl_seat *seat = NULL;

int output_scale = 1;
struct wl_keyboard *keyboard = NULL;
struct wl_pointer *pointer = NULL;

static bool running = true;
static bool visible = false;
static bool is_configured = false;

static AppState app_state;
static Config *config = NULL;
static int socket_fd = -1;

static Backend *backend = NULL;

/* Startup Race Condition Fix */
// static bool first_show_done = false;

/* Signal Handling */
static volatile sig_atomic_t should_quit = 0;
static volatile sig_atomic_t caught_signal = 0;

/* Safety timeout: auto-dismiss the switcher if it gets stuck (e.g. keyboard
 * focus never granted due to a competing exclusive overlay like a lock screen,
 * or modifier release events lost due to compositor quirks).
 * The timer resets on every user navigation action (Tab, arrows). */
#define SAFETY_TIMEOUT_SEC 10
static struct timespec watchdog_timestamp;
static bool watchdog_active = false;

static void signal_handler(int sig) {
  caught_signal = sig;
  should_quit = 1;
}

/* Helper: Polite Sleep */
static void sleep_ms(int ms) {
  struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/* --- Wayland Events --- */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *layer_surf,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  (void)data;
  if (w > 0 && h > 0) {
    app_state.width = w;
    app_state.height = h;
  }
  zwlr_layer_surface_v1_ack_configure(layer_surf, serial);
  is_configured = true;

  if (visible) {
    app_state.needs_render = true;
  }
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *layer_surf) {
  (void)data;
  (void)layer_surf;
  LOG("Layer surface closed by compositor");
  should_quit = 1;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void seat_capabilities(void *data, struct wl_seat *wl_seat,
                              uint32_t caps) {
  (void)wl_seat;
  AppState *state = (AppState *)data;
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, get_keyboard_listener(), state);
    LOG("Keyboard listener attached");
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard) {
    /* The seat lost its keyboard (suspend/resume, VT switch, input device
     * hotplug). The wl_keyboard is now inert: releasing it and clearing the
     * pointer is what lets the branch above re-acquire one when the capability
     * comes back. Without this the daemon never receives wl_keyboard.enter
     * again and the switcher opens but ignores every key. */
    wl_keyboard_release(keyboard);
    keyboard = NULL;
    LOG("Keyboard listener released (seat lost keyboard capability)");
  }

  /* Pointer is optional: hover/click on cards is an extra way to pick a
   * window alongside the keyboard. Same acquire/release dance as above. */
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, get_pointer_listener(), state);
    LOG("Pointer listener attached");
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
    wl_pointer_release(pointer);
    pointer = NULL;
    LOG("Pointer listener released (seat lost pointer capability)");
  }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
  (void)data;
  (void)wl_seat;
  (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* --- wl_output listener (HiDPI scale tracking) --- */
static void output_geometry(void *data, struct wl_output *wl_output, int32_t x,
                            int32_t y, int32_t pw, int32_t ph, int32_t subpx,
                            const char *make, const char *model,
                            int32_t transform) {
  (void)data;
  (void)wl_output;
  (void)x;
  (void)y;
  (void)pw;
  (void)ph;
  (void)subpx;
  (void)make;
  (void)model;
  (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                        int32_t w, int32_t h, int32_t refresh) {
  (void)data;
  (void)wl_output;
  (void)flags;
  (void)w;
  (void)h;
  (void)refresh;
}

static void output_done(void *data, struct wl_output *wl_output) {
  (void)data;
  (void)wl_output;
}

static void output_scale_event(void *data, struct wl_output *wl_output,
                               int32_t factor) {
  (void)data;
  (void)wl_output;
  if (factor >= 1) {
    output_scale = factor;
    LOG("Output scale: %d", output_scale);
  }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale_event,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
  (void)version;
  AppState *state = (AppState *)data;

  if (strcmp(interface, wl_compositor_interface.name) == 0)
    compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  else if (strcmp(interface, wl_shm_interface.name) == 0)
    shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
    layer_shell =
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
  else if (strcmp(interface, wl_seat_interface.name) == 0) {
    seat = wl_registry_bind(registry, name, &wl_seat_interface, 4);
    wl_seat_add_listener(seat, &seat_listener, state);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    output = wl_registry_bind(registry, name, &wl_output_interface, 3);
    wl_output_add_listener(output, &output_listener, NULL);
  }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
  (void)data;
  (void)registry;
  (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_global_remove};

/* --- Logic --- */

static void destroy_panel(void) {
  if (layer_surface) {
    zwlr_layer_surface_v1_destroy(layer_surface);
    layer_surface = NULL;
  }
  if (surface) {
    wl_surface_destroy(surface);
    surface = NULL;
  }
  visible = false;
  is_configured = false;
  LOG("Panel destroyed");
}

static void create_panel(void) {
  if (surface) {
    LOG("Panel already exists");
    return;
  }

  surface = wl_compositor_create_surface(compositor);
  if (!surface) {
    LOG("Failed to create surface");
    return;
  }

  layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "snappy-switcher");
  if (!layer_surface) {
    LOG("Failed to create layer surface");
    wl_surface_destroy(surface);
    surface = NULL;
    return;
  }

  zwlr_layer_surface_v1_set_size(layer_surface, 1, 1); // 最小初始尺寸 -Minimum initial size
  zwlr_layer_surface_v1_set_anchor(layer_surface, 0);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener,
                                     NULL);

  wl_surface_set_buffer_scale(surface, output_scale);
  wl_surface_commit(surface);
  wl_display_roundtrip(display);

  LOG("Panel created");
}

static void hide_switcher(void) {
  if (!visible)
    return;

  visible = false;
  is_configured = false;
  watchdog_active = false;
  input_set_toggle_mode(
      false); /* Reset toggle state to avoid leaking into next session */

  /* Clear any error banner from this session */
  free(app_state.error_message);
  app_state.error_message = NULL;

  /* Reset workspace filter so it doesn't leak into the next session */
  app_state.filter_workspace = false;

  /* Release exclusive keyboard focus before hiding/destroying the surface.
   * The interactivity change is double-buffered (takes effect on commit).
   * This ensures the compositor cleanly restores keyboard focus and
   * re-synchronizes the seat modifier state on the next focused window,
   * preventing stuck modifier keys in pointer-locked apps (games). */
  if (layer_surface && surface) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(surface);
    wl_display_flush(display);
  }

  if (config && config->follow_monitor) {
    destroy_panel();
  } else {
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
    wl_display_flush(display);
    LOG("Panel hidden (not destroyed)");
  }
}

static void show_switcher(bool is_linear) {
  LOG("Showing switcher...");

  if (config && config->follow_monitor && !surface) {
    create_panel();
    if (!surface) {
      LOG("Failed to create panel");
      return;
    }
  } else if (visible) {
    return;
  }

  input_reset_alt_state();

  /* Preserve filter_workspace across state reset — it was set by
   * handle_command() before we were called, and app_state_init()
   * would clobber it back to false. */
  bool ws_filter = app_state.filter_workspace;

  app_state_free(&app_state);
  app_state_init(&app_state);

  app_state.filter_workspace = ws_filter;

  if (!backend) {
    LOG("Error: Backend not initialized");
    return;
  }

  if (backend->get_windows(&app_state, config, is_linear) < 0) {
    LOG("Failed to update window list");
    return;
  }

  if (is_linear) {
    /* Linear mode: find where the active window landed in the
     * deterministic sort order and select relative to it. */
    int active_idx = 0;
    for (int i = 0; i < app_state.count; i++) {
      if (app_state.windows[i].is_active) {
        active_idx = i;
        break;
      }
    }
    if (config && config->sticky_mode) {
      app_state.selected_index = active_idx;
    } else {
      app_state.selected_index =
          (app_state.count > 1) ? (active_idx + 1) % app_state.count : 0;
    }
  } else {
    /* MRU mode: active window is always index 0 */
    if (config && config->sticky_mode) {
      app_state.selected_index = 0;
    } else {
      app_state.selected_index = (app_state.count > 1) ? 1 : 0;
    }
  }

  calculate_dimensions(&app_state, &app_state.width, &app_state.height);
  int max_cols = config ? config->max_cols : 5;
  app_state.cols = (app_state.count < max_cols) ? app_state.count : max_cols;
  zwlr_layer_surface_v1_set_size(layer_surface, app_state.width,
                                 app_state.height);
  /* EXCLUSIVE keyboard interactivity: the compositor MUST give keyboard focus
   * to this overlay-layer surface regardless of pointer state. This is critical
   * when a fullscreen game has pointer lock active — ON_DEMAND would never
   * grant focus because the pointer is trapped inside the game window.
   * EXCLUSIVE is the correct semantic for transient alt-tab overlays and is
   * what lock screens, launchers (wofi/rofi/bemenu), etc. use. */
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

  visible = true;

  /* Start the safety watchdog timer. If the switcher hasn't been dismissed
   * within SAFETY_TIMEOUT_SEC (timer resets on every navigation keypress),
   * force an auto-dismiss. This catches edge cases like:
   *  - Competing exclusive overlays preventing keyboard focus acquisition
   *  - Modifier release events lost due to compositor quirks
   *  - Multi-seat scenarios where keyboard events go to wrong seat */
  clock_gettime(CLOCK_MONOTONIC, &watchdog_timestamp);
  watchdog_active = true;

  wl_surface_set_buffer_scale(surface, output_scale);
  wl_surface_commit(surface);
  wl_display_flush(display);
}

static void select_and_hide(void) {
  char *address = NULL;
  if (visible && app_state.count > 0 && backend) {
    WindowInfo *win = &app_state.windows[app_state.selected_index];
    address = strdup(win->address);
    LOG("Switching to: %s (using %s backend)", win->title, backend->get_name());
  }
  hide_switcher();
  if (address) {
    backend->activate_window(address);
    free(address);
  }
}

static void handle_command(const char *payload) {
  /* Protocol: CMD:MOD:WORKSPACE_FLAG:SOURCE:SILENT_FLAG:LINEAR_FLAG:PATH
   * Also supports legacy bare commands (e.g. "QUIT" from takeover)
   * and 4/5-field payloads (SILENT_FLAG/LINEAR_FLAG default to "0").
   * `PATH` is an optional flag that is used for --reload-config. Not required
   * to send in every other command. */
  char cmd_buf[32] = {0};
  char mod_buf[64] = {0};
  char source_buf[16] = {0};
  char silent_buf[4] = {0};
  char linear_buf[4] = {0};

  /* NOTE: unknown how big this should be. it is 1024 in `load_config_from()`, so i took that */
  char path_buf[1024] = {0};
  int ws_flag = 0;

  /* Safe parse: use strtok_r with ':' delimiter */
  /* changed 256 -> 2048 because it's next power of 2 w/ the new 1024 path buf included */
  char buf[2048];
  strncpy(buf, payload, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *saveptr = NULL;
  char *tok_cmd = strtok_r(buf, ":", &saveptr);
  char *tok_mod = strtok_r(NULL, ":", &saveptr);
  char *tok_ws = strtok_r(NULL, ":", &saveptr);
  char *tok_src = strtok_r(NULL, ":", &saveptr);
  char *tok_sil = strtok_r(NULL, ":", &saveptr);
  char *tok_lin = strtok_r(NULL, ":", &saveptr);
  char *tok_path = strtok_r(NULL, ":", &saveptr);

  if (!tok_cmd) {
    LOG("Malformed command: '%s'", payload);
    return;
  }

  strncpy(cmd_buf, tok_cmd, sizeof(cmd_buf) - 1);
  if (tok_mod)
    strncpy(mod_buf, tok_mod, sizeof(mod_buf) - 1);
  if (tok_ws)
    ws_flag = atoi(tok_ws);
  if (tok_src)
    strncpy(source_buf, tok_src, sizeof(source_buf) - 1);
  if (tok_sil)
    strncpy(silent_buf, tok_sil, sizeof(silent_buf) - 1);
  if (tok_lin)
    strncpy(linear_buf, tok_lin, sizeof(linear_buf) - 1);
  if (tok_path)
    strncpy(path_buf, tok_path, sizeof(path_buf) - 1);

  bool is_silent = (strcmp(silent_buf, "1") == 0);
  bool is_linear = (strcmp(linear_buf, "1") == 0);

  /* Route commands that don't need modifier/workspace context */
  if (strcmp(cmd_buf, CMD_QUIT) == 0) {
    should_quit = 1;
    return;
  }
  if (strcmp(cmd_buf, CMD_HIDE) == 0) {
    hide_switcher();
    return;
  }
  if (strcmp(cmd_buf, CMD_SELECT) == 0) {
    select_and_hide();
    return;
  }

  /* --- Silent Mode: bypass Wayland UI entirely --- */
  if (is_silent &&
      (strcmp(cmd_buf, CMD_NEXT) == 0 || strcmp(cmd_buf, CMD_PREV) == 0)) {
    /* If the GUI panel is currently open, tear it down cleanly before
     * executing the silent switch.  This prevents a state desync where
     * the visible panel keeps running with stale data while the silent
     * path focuses a different window underneath. */
    if (visible) {
      hide_switcher();
    }

    if (!backend) {
      LOG("Silent mode: backend not initialized");
      return;
    }

    /* Build a temporary state for the window query */
    AppState silent_state;
    app_state_init(&silent_state);
    silent_state.filter_workspace = (ws_flag != 0);

    if (backend->get_windows(&silent_state, config, is_linear) < 0) {
      LOG("Silent mode: failed to fetch window list");
      app_state_free(&silent_state);
      return;
    }

    if (silent_state.count <= 1) {
      LOG("Silent mode: %d window(s), nothing to switch to",
          silent_state.count);
      app_state_free(&silent_state);
      return;
    }

    int dir = (strcmp(cmd_buf, CMD_NEXT) == 0) ? 1 : -1;
    int target;

    if (is_linear) {
      /* Linear mode: find the active window and step relative to it */
      int active_idx = 0;
      for (int i = 0; i < silent_state.count; i++) {
        if (silent_state.windows[i].is_active) {
          active_idx = i;
          break;
        }
      }
      target = (active_idx + dir + silent_state.count) % silent_state.count;
    } else {
      /* MRU mode: active window is always index 0.
       * NEXT = second most recent (index 1),
       * PREV = least recent (last in sorted list) */
      target = (dir == 1) ? 1 : silent_state.count - 1;
    }

    char *address = strdup(silent_state.windows[target].address);
    LOG("Silent mode: focusing window '%s' (index %d/%d)",
        silent_state.windows[target].title, target, silent_state.count);

    app_state_free(&silent_state);

    if (address) {
      backend->activate_window(address);
      free(address);
    }
    return; /* CRITICAL: do NOT fall through to the GUI path */
  }

  /* Determine invocation source and modifier presence */
  bool from_cli = (strcmp(source_buf, "cli") == 0);
  bool has_real_mod = (mod_buf[0] != '\0' && strcmp(mod_buf, "none") != 0);

  /* Apply modifier if provided */
  if (has_real_mod) {
    input_set_dismiss_modifier(mod_buf);
  }

  /* Store workspace flag on app_state for hyprland.c to use */
  app_state.filter_workspace = (ws_flag != 0);
  if (app_state.filter_workspace)
    LOG("Workspace filter: ON");

  /* Route command */
  if (strcmp(cmd_buf, CMD_TOGGLE) == 0) {
    if (visible)
      hide_switcher();
    else {
      input_set_toggle_mode(true);
      show_switcher(is_linear);
    }
    return;
  }

  /* NEXT / PREV navigation */
  if (strcmp(cmd_buf, CMD_NEXT) == 0 || strcmp(cmd_buf, CMD_PREV) == 0) {
    if (!visible) {
      /* CLI without a modifier: use toggle mode (no dismiss-on-release).
       * CLI/bind with a modifier: normal dismiss-on-release. */
      if (from_cli && !has_real_mod) {
        input_set_toggle_mode(true);
      } else {
        input_set_toggle_mode(false);
      }
      show_switcher(is_linear);
    } else {
      int dir = (strcmp(cmd_buf, CMD_NEXT) == 0) ? 1 : -1;
      if (app_state.count > 0) {
        app_state.selected_index =
            (app_state.selected_index + dir + app_state.count) %
            app_state.count;
        app_state.needs_render = true;
      }
    }
    return;
  }

  if (strcmp(cmd_buf, CMD_RELOAD) == 0) {
    LOG("Reloading config...");
    free_config(config);
    icons_cleanup();

    config = load_config_from(path_buf); /* path buf will be NULL if none provided */
    render_set_config(config);
    icons_init(config->icon_theme, config->icon_fallback);
    return;
  }

  LOG("Unknown command: '%s'", cmd_buf);
}

/* Client Mode (CLI) — builds CMD:MOD:WORKSPACE_FLAG:SOURCE:SILENT:LINEAR
 * payload */
static int run_client(int argc, char **argv) {
  const char *prog = argv[0];
  const char *cmd = NULL;
  const char *mod = "none";
  int workspace = 0;
  int silent = 0;
  int linear = 0;

  /* Parse arguments: <command> [--mod <key>] [--workspace] [--silent]
   * [--linear] */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mod") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: --mod requires an argument\n", prog);
        return 1;
      }
      mod = argv[++i];
    } else if (strcmp(argv[i], "--workspace") == 0) {
      workspace = 1;
    } else if (strcmp(argv[i], "--silent") == 0) {
      silent = 1;
    } else if (strcmp(argv[i], "--linear") == 0) {
      linear = 1;
    } else if (strcmp(argv[i], "--daemon") == 0 ||
               strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
      /* Skip daemon-only flags (--config eats next arg too) */
      if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0)
        i++;
    } else if (argv[i][0] != '-' && !cmd) {
      cmd = argv[i];
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "%s: unknown option '%s'\n", prog, argv[i]);
      return 1;
    }
  }

  if (!cmd) {
    fprintf(stderr, "%s: no command specified\n", prog);
    return 1;
  }

  /* Map user command to protocol token */
  const char *proto_cmd = NULL;
  if (strcmp(cmd, "next") == 0)
    proto_cmd = CMD_NEXT;
  else if (strcmp(cmd, "prev") == 0)
    proto_cmd = CMD_PREV;
  else if (strcmp(cmd, "toggle") == 0)
    proto_cmd = CMD_TOGGLE;
  else if (strcmp(cmd, "select") == 0)
    proto_cmd = CMD_SELECT;
  else if (strcmp(cmd, "hide") == 0)
    proto_cmd = CMD_HIDE;
  else if (strcmp(cmd, "quit") == 0)
    proto_cmd = CMD_QUIT;
  else {
    fprintf(stderr, "%s: unknown command '%s'\n", prog, cmd);
    return 1;
  }

  /* Build payload: CMD:MOD:WORKSPACE_FLAG:SOURCE:SILENT:LINEAR */
  char payload[256];
  const char *source = (strcmp(mod, "none") == 0) ? "cli" : "bind";
  snprintf(payload, sizeof(payload), "%s:%s:%d:%s:%d:%d", proto_cmd, mod,
           workspace, source, silent, linear);

  if (!is_daemon_running()) {
    fprintf(stderr,
            "Daemon not running. Start with: snappy-switcher --daemon\n");
    return 1;
  }
  return send_command(payload) == 0 ? 0 : 1;
}

/* Ruthless Takeover: Kill existing zombie daemon before startup */
static int takeover_existing_daemon(void) {
  if (!is_daemon_running()) {
    return 0; /* No existing daemon, proceed normally */
  }

  LOG("Existing daemon detected - initiating takeover...");

  /* Step 1: Send quit command to existing daemon */
  if (send_command(CMD_QUIT) != 0) {
    LOG("Failed to send quit command, socket may be stuck");
  }

  /* Step 2: Poll until old daemon dies or timeout */
  int elapsed = 0;
  while (elapsed < TAKEOVER_TIMEOUT_MS) {
    sleep_ms(TAKEOVER_POLL_MS);
    elapsed += TAKEOVER_POLL_MS;

    if (!is_daemon_running()) {
      LOG("Old daemon terminated gracefully after %dms", elapsed);
      return 0; /* Success - old daemon gone */
    }
  }

  /* Step 3: Force takeover - unlink the socket file */
  LOG("Timeout! Old daemon did not respond - forcing takeover...");
  if (unlink(get_socket_path()) == 0) {
    LOG("Forcefully removed stale socket");
  } else {
    LOG("Warning: Could not unlink socket: %s", strerror(errno));
  }

  /* Small delay to ensure cleanup */
  sleep_ms(50);
  return 0;
}

/* Daemon Mode. config_path: NULL = use default, else load from this file. */
static int run_daemon(const char *config_path) {
  /* Ruthless Takeover: Kill any existing zombie instead of exiting politely */
  if (takeover_existing_daemon() != 0) {
    LOG("Failed to take over from existing daemon");
    return 1;
  }

  /* 1. Signals */
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  /* Ignore signals that shouldn't terminate the daemon */
  struct sigaction sa_ignore;
  sa_ignore.sa_handler = SIG_IGN;
  sigemptyset(&sa_ignore.sa_mask);
  sa_ignore.sa_flags = 0;
  sigaction(SIGHUP, &sa_ignore, NULL);  /* Terminal hangup - ignore */
  sigaction(SIGUSR1, &sa_ignore, NULL); /* User signal 1 - ignore */
  sigaction(SIGUSR2, &sa_ignore, NULL); /* User signal 2 - ignore */
  sigaction(SIGPIPE, &sa_ignore, NULL); /* Broken pipe - ignore */

  /* 2. Config & Resources */
  config = load_config_from(config_path);
  if (!config)
    config = get_default_config();
  render_set_config(config);
  icons_init(config->icon_theme, config->icon_fallback);
  /* dismiss_modifier is now set dynamically per-command via IPC */
  app_state_init(&app_state);

  backend = backend_init();
  if (!backend) {
    LOG("Failed to initialize backend");
    return 1;
  }
  LOG("Using %s backend", backend->get_name());

  /* Callbacks */
  on_alt_release = select_and_hide;
  on_escape = hide_switcher; /* hide without switch */

  /* 3. Wayland Connection */
  for (int i = 0; i < WAYLAND_RETRY_MAX; i++) {
    display = wl_display_connect(NULL);
    if (display)
      break;
    sleep_ms(WAYLAND_RETRY_MS);
  }
  if (!display) {
    LOG("Failed to connect to Wayland");
    backend_cleanup(backend);
    return 1;
  }

  static struct wl_registry *registry = NULL;
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &app_state);

  /* 4. Bind Protocols */
  for (int i = 0; i < PROTOCOL_RETRY_MAX; i++) {
    wl_display_roundtrip(display);
    if (compositor && layer_shell && shm)
      break;
    sleep_ms(PROTOCOL_RETRY_MS);
  }
  if (!compositor || !layer_shell || !shm) {
    LOG("Failed to bind Wayland protocols");
    backend_cleanup(backend);
    if (registry)
      wl_registry_destroy(registry);
    wl_display_disconnect(display);
    free_config(config);
    icons_cleanup();
    app_state_free(&app_state);
    return 1;
  }

  /* 5. Surface Setup */
  surface = wl_compositor_create_surface(compositor);
  layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "snappy-switcher");
  zwlr_layer_surface_v1_set_size(layer_surface, 1,
                                 1); /* Minimal initial size */
  zwlr_layer_surface_v1_set_anchor(layer_surface, 0);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener,
                                     NULL);
  wl_surface_commit(surface);
  wl_display_roundtrip(display);

  /* 6. Socket Server */
  socket_fd = init_server();
  if (socket_fd < 0) {
    backend_cleanup(backend);
    return 1;
  }

  LOG("Daemon Started (PID: %d)", getpid());

  /* Poll array: [0] main compositor, [1] IPC socket, [2] wlr backend display */
  int wlr_fd = wlr_backend_get_fd();
  struct pollfd fds[3];
  fds[0].fd = wl_display_get_fd(display);
  fds[0].events = POLLIN;
  fds[1].fd = socket_fd;
  fds[1].events = POLLIN;
  fds[2].fd = wlr_fd; /* -1 if Hyprland backend — poll() ignores fd < 0 */
  fds[2].events = POLLIN;

  while (running && !should_quit) {
    /* Prepare read: drain any already-queued events first */
    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) < 0) {
        LOG("Fatal: wl_display_dispatch_pending failed — Wayland display "
            "disconnected");
        should_quit = 1;
        break;
      }
    }
    if (should_quit)
      break;

    if (wl_display_flush(display) < 0) {
      /* EAGAIN is normal (outgoing buffer temporarily full), only fatal
       * if the error is something else (EPIPE, ECONNRESET, etc.) */
      if (errno != EAGAIN) {
        LOG("Fatal: wl_display_flush failed (%s) — Wayland display "
            "disconnected",
            strerror(errno));
        wl_display_cancel_read(display);
        break;
      }
    }

    int poll_ret = poll(fds, 3, 100);
    if (poll_ret < 0) {
      if (errno == EINTR) {
        wl_display_cancel_read(display);
        continue;
      }
      LOG("Fatal: poll() error: %s", strerror(errno));
      wl_display_cancel_read(display);
      break;
    }

    /* ---- Compositor disconnect detection ----
     * When the compositor (Hyprland) exits, the Wayland fd fires
     * POLLHUP and/or POLLERR.  We MUST catch this to avoid an
     * infinite spin loop where poll() returns instantly every time. */
    if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      LOG("Fatal: Wayland compositor connection lost (revents=0x%x) — shutting "
          "down",
          fds[0].revents);
      wl_display_cancel_read(display);
      break;
    }

    if (fds[0].revents & POLLIN) {
      if (wl_display_read_events(display) < 0) {
        LOG("Fatal: wl_display_read_events failed — Wayland display "
            "disconnected");
        break;
      }
      if (wl_display_dispatch_pending(display) < 0) {
        LOG("Fatal: wl_display_dispatch_pending failed — Wayland display "
            "disconnected");
        break;
      }
    } else {
      wl_display_cancel_read(display);
    }

    /* Background wlr dispatch: keeps the toplevel manager connection alive */
    if (fds[2].fd >= 0 && (fds[2].revents & POLLIN)) {
      wlr_backend_dispatch();
    }

    if (fds[1].revents & POLLIN) {
      while (1) {
        struct sockaddr_un cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        int client = accept(socket_fd, (struct sockaddr *)&cli_addr, &clilen);
        if (client < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          LOG("Accept error: %s", strerror(errno));
          break;
        }

        /* EINTR-safe IPC read */
        char buffer[256];
        ssize_t n;
        do {
          n = read(client, buffer, sizeof(buffer) - 1);
        } while (n == -1 && errno == EINTR);

        if (n > 0) {
          buffer[n] = '\0';
          if (buffer[n - 1] == '\n')
            buffer[n - 1] = '\0';
          LOG("Received command: %s", buffer);
          handle_command(buffer);
        }
        close(client);
      }
    }

    /* --- Reset safety watchdog on user navigation activity ---
     * needs_render is set by keyboard handlers on Tab/Arrow presses,
     * so it serves as a reliable activity indicator. Reset the timer
     * before the render block consumes the flag. */
    if (visible && watchdog_active && app_state.needs_render) {
      clock_gettime(CLOCK_MONOTONIC, &watchdog_timestamp);
    }

    /* --- Deferred render: one frame per loop iteration --- */
    if (visible && app_state.needs_render && is_configured) {
      app_state.needs_render = false;

      /* If an error was set after show_switcher() sized the panel,
       * resize to the compact error overlay dimensions first.
       * Only call set_size + commit when dimensions actually changed
       * to avoid an infinite set_size → configure → needs_render loop. */
      if (app_state.error_message) {
        uint32_t new_w, new_h;
        calculate_dimensions(&app_state, &new_w, &new_h);

        if (new_w != app_state.width || new_h != app_state.height) {
          app_state.width = new_w;
          app_state.height = new_h;
          zwlr_layer_surface_v1_set_size(layer_surface, app_state.width,
                                         app_state.height);
          wl_surface_commit(surface);
          wl_display_roundtrip(display);
          /* The roundtrip dispatched a configure event which set
           * needs_render = true.  Render on the next iteration
           * with the compositor-confirmed dimensions. */
          continue;
        }
      }

      render_ui(&app_state, app_state.width, app_state.height, output_scale);
    }

    /* --- Safety timeout: auto-dismiss if stuck ---
     * If the switcher has been visible for SAFETY_TIMEOUT_SEC without user
     * activity, something went wrong (focus never granted, modifier events
     * lost, etc.). Force teardown to avoid a permanently stuck overlay.
     * The NONE interactivity set by hide_switcher() ensures the compositor
     * re-synchronizes the seat modifier state on the next focused window,
     * preventing dangling modifier keys in the underlying app. */
    if (visible && watchdog_active) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      double elapsed = (double)(now.tv_sec - watchdog_timestamp.tv_sec) +
                       (double)(now.tv_nsec - watchdog_timestamp.tv_nsec) / 1e9;
      if (elapsed >= (double)SAFETY_TIMEOUT_SEC) {
        LOG("Safety timeout (%.0fs): auto-dismissing stuck switcher", elapsed);
        hide_switcher();
      }
    }
  }

  /* 8. Cleanup */
  if (caught_signal) {
    LOG("Caught signal %d, shutting down...", caught_signal);
  } else {
    LOG("Cleaning up...");
  }

  cleanup_server(socket_fd);
  input_cleanup();
  icons_cleanup();
  render_cleanup_buffers();
  app_state_free(&app_state);
  free_config(config);

  if (backend) {
    backend_cleanup(backend);
    backend = NULL;
  }

  if (layer_surface)
    zwlr_layer_surface_v1_destroy(layer_surface);
  if (surface)
    wl_surface_destroy(surface);
  if (keyboard)
    wl_keyboard_destroy(keyboard);
  if (pointer)
    wl_pointer_destroy(pointer);
  if (output)
    wl_output_destroy(output);
  if (seat)
    wl_seat_destroy(seat);
  if (registry)
    wl_registry_destroy(registry);
  if (display)
    wl_display_disconnect(display);

  return 0;
}

static void print_help(const char *prog) {
  printf("Snappy Switcher v4.5.0 - A modular, Hyprland-driven window switcher "
         "for Wayland\n\n");
  printf("Usage: %s [OPTION] | <command> [--mod <key>] [--workspace] "
         "[--silent] [--linear]\n\n",
         prog);
  printf("Options:\n");
  printf("  --daemon             Start the switcher daemon\n");
  printf("  --config, -c PATH    Use config file (daemon only)\n");
  printf("  --help, -h           Show this help message\n\n");
  /* NOTE: maybe change to 'Force reload config' if hot reloading ever gets implemented? */
  printf("  --reload-config, -r  Reload config\n\n");
  printf("Commands (requires daemon running):\n");
  printf("  next                 Select next window\n");
  printf("  prev                 Select previous window\n");
  printf("  toggle               Toggle the switcher visibility\n");
  printf("  select               Activate the selected window\n");
  printf("  hide                 Hide the switcher\n");
  printf("  quit                 Terminate the daemon\n\n");
  printf("Flags (with next, prev, toggle):\n");
  printf("  --mod <key>          Dismiss key (alt, super, ctrl, shift, space, "
         "etc.)\n");
  printf("  --workspace          Filter windows to current workspace\n");
  printf(
      "  --silent             Instant MRU switch without UI (next/prev only)\n");
  printf("  --linear             Use deterministic workspace/address sort "
         "instead of MRU\n\n");
  printf("Examples:\n");
  printf("  %s --daemon &\n", prog);
  printf("  %s next --mod alt             # Alt+Tab style\n", prog);
  printf("  %s next --mod space           # Space as dismiss key\n", prog);
  printf("  %s next --workspace --mod super  # Super+Tab workspace filter\n",
         prog);
  printf("  %s toggle                     # Toggle without modifier\n", prog);
  printf("  %s next --silent              # Instant switch, no UI\n", prog);
  printf("  %s prev --silent --workspace  # Silent prev on current workspace\n",
         prog);
  printf("  %s next --linear              # Linear cycle through windows\n",
         prog);
  printf("  %s next --silent --linear     # Silent linear cycle\n", prog);
}

int main(int argc, char **argv) {
  /* Ignore SIGPIPE early: client mode must handle broken pipe via errno,
   * not be terminated by signal if daemon crashes during send_command() */
  signal(SIGPIPE, SIG_IGN);

  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s <command> [--mod <key>] [--workspace] [--silent] "
            "[--linear] | --daemon\n",
            argv[0]);
    fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    return 1;
  }

  /* First pass: check for --help and --daemon */
  const char *config_path = NULL;
  bool daemon_mode = false;
  bool reloading_config = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--daemon") == 0) {
      daemon_mode = true;
    }
    if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: --config requires an argument\n", argv[0]);
        return 1;
      }
      config_path = argv[++i];
    }

    if (strcmp(argv[i], "--reload-config") == 0 || strcmp(argv[i], "-r") == 0) {
      reloading_config = true;
    }
  }

  if (daemon_mode)
    return run_daemon(config_path);

  if (reloading_config) {
    /* command structure: CMD:MOD:WORKSPACE_FLAG:SOURCE:SILENT_FLAG:LINEAR_FLAG:PATH */
    if (!config_path) {
      send_command(CMD_RELOAD);
      return 0;
    }

    /* sizeof + 1024 = 1031; maybe make 2048 to match new buffer in `handle_command()`?*/
    char full_command[sizeof(CMD_RELOAD) + 1024] = {0};
    snprintf(full_command, sizeof(full_command),
             "%s:alt:0:cli:0:0:%s",
             CMD_RELOAD, config_path);
    send_command(full_command);

    return 0;
  }

  /* Client mode: pass full argv for run_client to parse */
  return run_client(argc, argv);
}

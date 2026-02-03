/* src/wlr_backend.c - Proper wlr-foreign-toplevel-management backend */
#define _POSIX_C_SOURCE 200809L

#include "wlr_backend.h"
#include "backend.h"
#include "config.h"
#include "data.h"
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define LOG(fmt, ...) fprintf(stderr, "[WLR] " fmt "\n", ##__VA_ARGS__)

typedef struct WindowNode WindowNode;

struct WindowNode {
  struct zwlr_foreign_toplevel_handle_v1 *handle;
  char *title;
  char *app_id;
  char *identifier;
  int state;
  int is_active;
  int is_minimized;
  uint64_t activation_serial; /* window activation serial */
  WindowNode *next;
};

typedef struct {
  struct wl_display *display;
  struct wl_registry *registry;
  struct zwlr_foreign_toplevel_manager_v1 *manager;
  struct wl_seat *seat;
  WindowNode *windows;
  int window_count;
  int initialized;
  int needs_refresh;
  uint64_t activation_counter; /* global activation counter */
} WlrBackendState;

static WlrBackendState backend_state = {0};

// move window to the front of the activation history list
static void move_window_to_front(WindowNode *window) {
  if (!window || !backend_state.windows || window == backend_state.windows) {
    return;
  }

  // remove window from the list
  WindowNode **prev = &backend_state.windows;
  WindowNode *curr = backend_state.windows;

  while (curr) {
    if (curr == window) {
      *prev = curr->next;
      break;
    }
    prev = &curr->next;
    curr = curr->next;
  }

  // insert window to the front of the list
  window->next = backend_state.windows;
  backend_state.windows = window;

  // update activation serial (smaller numbers mean more recently activated)
  backend_state.activation_counter++;
  window->activation_serial = backend_state.activation_counter;
}

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  (void)data;
  LOG("Registry global: %s (name: %u, version: %u)", interface, name, version);

  if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
    backend_state.manager = wl_registry_bind(
        registry, name, &zwlr_foreign_toplevel_manager_v1_interface, 1);
    LOG("Bound foreign toplevel manager");
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    backend_state.seat =
        wl_registry_bind(registry, name, &wl_seat_interface, 1);
    LOG("Bound seat");
  }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  (void)data;
  (void)registry;
  LOG("Registry global remove: %u", name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void
toplevel_handle_title(void *data,
                      struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                      const char *title) {
  WindowNode *window = (WindowNode *)data;
  (void)toplevel;

  if (window->title)
    free(window->title);
  window->title = strdup(title ? title : "");
  LOG("Window title updated: %s", window->title);
}

static void
toplevel_handle_app_id(void *data,
                       struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                       const char *app_id) {
  WindowNode *window = (WindowNode *)data;
  (void)toplevel;

  if (window->app_id)
    free(window->app_id);
  window->app_id = strdup(app_id ? app_id : "");
  LOG("Window app_id updated: %s", window->app_id);
}

static void
toplevel_handle_output_enter(void *data,
                             struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                             struct wl_output *output) {
  (void)data;
  (void)toplevel;
  (void)output;
  LOG("Window entered output");
}

static void
toplevel_handle_output_leave(void *data,
                             struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                             struct wl_output *output) {
  (void)data;
  (void)toplevel;
  (void)output;
  LOG("Window left output");
}

static void
toplevel_handle_state(void *data,
                      struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                      struct wl_array *wl_state) {
  WindowNode *window = (WindowNode *)data;
  (void)toplevel;

  int was_active = window->is_active;

  window->state = 0;
  window->is_active = 0;
  window->is_minimized = 0;

  uint32_t *state;
  wl_array_for_each(state, wl_state) {
    window->state |= (1 << *state);
    if (*state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
      window->is_active = 1;
    }
    if (*state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
      window->is_minimized = 1;
    }
  }

  // if window is active and wasn't before, move it to the front
  if (window->is_active && !was_active) {
    LOG("Window became active: %s", window->title);
    move_window_to_front(window);
  }
}

static void
toplevel_handle_done(void *data,
                     struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
  WindowNode *window = (WindowNode *)data;
  (void)toplevel;

  LOG("Window done: %s (app_id: %s)", window->title, window->app_id);
  backend_state.needs_refresh = 1;
}

static void
toplevel_handle_closed(void *data,
                       struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
  WindowNode *window = (WindowNode *)data;
  (void)toplevel;

  LOG("Window closed: %s", window->title);

  WindowNode **prev = &backend_state.windows;
  WindowNode *curr = backend_state.windows;

  while (curr) {
    if (curr == window) {
      *prev = curr->next;
      backend_state.window_count--;
      break;
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (window->handle) {
    zwlr_foreign_toplevel_handle_v1_destroy(window->handle);
  }
  free(window->title);
  free(window->app_id);
  free(window->identifier);
  free(window);

  backend_state.needs_refresh = 1;
}

static void
toplevel_handle_parent(void *data,
                       struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                       struct zwlr_foreign_toplevel_handle_v1 *parent) {
  (void)data;
  (void)toplevel;
  (void)parent;
  LOG("Window parent updated");
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener =
    {
        .title = toplevel_handle_title,
        .app_id = toplevel_handle_app_id,
        .output_enter = toplevel_handle_output_enter,
        .output_leave = toplevel_handle_output_leave,
        .state = toplevel_handle_state,
        .done = toplevel_handle_done,
        .closed = toplevel_handle_closed,
        .parent = toplevel_handle_parent,
};

static void
manager_handle_toplevel(void *data,
                        struct zwlr_foreign_toplevel_manager_v1 *manager,
                        struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
  (void)data;
  (void)manager;

  LOG("New toplevel window");

  WindowNode *window = malloc(sizeof(WindowNode));
  if (!window) {
    LOG("Failed to allocate window node");
    return;
  }

  memset(window, 0, sizeof(WindowNode));
  window->handle = toplevel;
  window->identifier = malloc(64);
  if (window->identifier) {
    snprintf(window->identifier, 64, "wlr-%p", toplevel);
  }

  // initial activation serial is 0 (0 means never activated)
  window->activation_serial = 0;

  window->next = backend_state.windows;
  backend_state.windows = window;
  backend_state.window_count++;

  zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &toplevel_listener,
                                               window);

  LOG("Added window, total: %d", backend_state.window_count);
}

static void
manager_handle_finished(void *data,
                        struct zwlr_foreign_toplevel_manager_v1 *manager) {
  (void)data;
  (void)manager;

  LOG("Toplevel manager finished");
  backend_state.manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener =
    {
        .toplevel = manager_handle_toplevel,
        .finished = manager_handle_finished,
};

static void cleanup_windows(void) {
  WindowNode *curr = backend_state.windows;
  while (curr) {
    WindowNode *next = curr->next;
    if (curr->handle) {
      zwlr_foreign_toplevel_handle_v1_destroy(curr->handle);
    }
    free(curr->title);
    free(curr->app_id);
    free(curr->identifier);
    free(curr);
    curr = next;
  }
  backend_state.windows = NULL;
  backend_state.window_count = 0;
  backend_state.activation_counter = 0;
}

int wlr_backend_init(void) {
  if (backend_state.initialized) {
    LOG("Already initialized");
    return 0;
  }

  LOG("Initializing WLR backend...");

  backend_state.display = wl_display_connect(NULL);
  if (!backend_state.display) {
    LOG("Failed to connect to Wayland display");
    return -1;
  }

  backend_state.registry = wl_display_get_registry(backend_state.display);
  if (!backend_state.registry) {
    LOG("Failed to get registry");
    wl_display_disconnect(backend_state.display);
    return -1;
  }

  wl_registry_add_listener(backend_state.registry, &registry_listener, NULL);

  LOG("First roundtrip to get globals...");
  wl_display_roundtrip(backend_state.display);

  if (!backend_state.manager) {
    LOG("No foreign toplevel manager found");
    wl_registry_destroy(backend_state.registry);
    wl_display_disconnect(backend_state.display);
    return -1;
  }

  if (!backend_state.seat) {
    LOG("Warning: No seat found, window activation may not work");
  }

  zwlr_foreign_toplevel_manager_v1_add_listener(backend_state.manager,
                                                &manager_listener, NULL);

  LOG("Second roundtrip to get initial windows...");
  wl_display_roundtrip(backend_state.display);

  // set activation serial for initial windows
  int counter = 0;
  WindowNode *curr = backend_state.windows;
  while (curr) {
    if (curr->is_active) {
      // current active window should be in front
      backend_state.activation_counter++;
      curr->activation_serial = backend_state.activation_counter;
      counter++;
    }
    curr = curr->next;
  }

  LOG("WLR backend initialized with %d windows (%d active)",
      backend_state.window_count, counter);
  backend_state.initialized = 1;
  backend_state.needs_refresh = 0;

  return 0;
}

void wlr_backend_cleanup(void) {
  LOG("Cleaning up WLR backend");

  if (backend_state.manager) {
    zwlr_foreign_toplevel_manager_v1_destroy(backend_state.manager);
    backend_state.manager = NULL;
  }

  if (backend_state.seat) {
    wl_seat_destroy(backend_state.seat);
    backend_state.seat = NULL;
  }

  cleanup_windows();

  if (backend_state.registry) {
    wl_registry_destroy(backend_state.registry);
    backend_state.registry = NULL;
  }

  if (backend_state.display) {
    wl_display_disconnect(backend_state.display);
    backend_state.display = NULL;
  }

  backend_state.initialized = 0;
  backend_state.window_count = 0;
  backend_state.needs_refresh = 0;
  backend_state.activation_counter = 0;
}

int wlr_get_windows(AppState *state, Config *config) {
  (void)config;

  if (!backend_state.initialized) {
    LOG("Backend not initialized");
    return -1;
  }

  LOG("Getting windows from WLR backend...");

  app_state_free(state);
  app_state_init(state);

  // process pending events
  while (wl_display_prepare_read(backend_state.display) != 0) {
    wl_display_dispatch_pending(backend_state.display);
  }
  wl_display_flush(backend_state.display);

  struct pollfd pfd = {.fd = wl_display_get_fd(backend_state.display),
                       .events = POLLIN};

  if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
    wl_display_read_events(backend_state.display);
  } else {
    wl_display_cancel_read(backend_state.display);
  }

  wl_display_dispatch_pending(backend_state.display);

  LOG("Found %d windows via WLR protocol", backend_state.window_count);

  if (backend_state.window_count == 0) {
    LOG("No windows found");
    return 0;
  }

  // sort windows by activation order
  // the list is already sorted by activation order (most recently activated
  // first)
  WindowNode *curr = backend_state.windows;
  int index = 0;

  while (curr) {
    WindowInfo info;

    if (curr->is_minimized) {
      curr = curr->next;
      continue;
    }

    info.address = strdup(curr->identifier ? curr->identifier : "");
    info.title = strdup(curr->title ? curr->title : "Untitled");
    info.class_name = strdup(curr->app_id ? curr->app_id : "unknown");
    info.workspace_id = 0;

    // use activation serial as focus_history_id
    // activation serial is larger for more recently activated
    // for windows that have never been activated, give them a very large value
    // to ensure they are at the end
    if (curr->activation_serial == 0) {
      info.focus_history_id = 10000 + index;
    } else {
      // activation serial is smaller for more recently activated (because we
      // want the most recently activated at the front) use the maximum value
      // minus the serial number to achieve this
      info.focus_history_id = 1000 - curr->activation_serial;
    }

    info.is_active = curr->is_active;
    info.is_floating = 0;
    info.group_count = 1;
    info.focus_history_id = curr->is_active ? 0 : info.focus_history_id;

    if (app_state_add(state, &info) < 0) {
      window_info_free(&info);
      LOG("Failed to add window to AppState");
    } else {
      LOG("Added window %d: %s (%s), activation_serial: %lu", index, info.title,
          info.class_name, (unsigned long)curr->activation_serial);
    }

    curr = curr->next;
    index++;
  }

  // sort windows by focus_history_id (smaller numbers mean more recently
  // activated)
  if (state->count > 1) {
    int i, j;
    for (i = 0; i < state->count - 1; i++) {
      for (j = i + 1; j < state->count; j++) {
        if (state->windows[i].focus_history_id >
            state->windows[j].focus_history_id) {
          WindowInfo tmp = state->windows[i];
          state->windows[i] = state->windows[j];
          state->windows[j] = tmp;
        }
      }
    }
  }

  LOG("Successfully processed %d windows", state->count);
  return 0;
}

void wlr_activate_window(const char *identifier) {
  if (!backend_state.initialized || !identifier) {
    LOG("Cannot activate window: not initialized or identifier NULL");
    return;
  }

  LOG("Activating window: %s", identifier);

  WindowNode *curr = backend_state.windows;
  while (curr) {
    if (curr->identifier && strcmp(curr->identifier, identifier) == 0) {
      LOG("Found window to activate: %s", curr->title);

      // update activation history: move window to the front
      move_window_to_front(curr);

      // send activation request
      if (curr->handle && backend_state.seat) {
        LOG("Activating window via WLR protocol: %s", curr->title);
        zwlr_foreign_toplevel_handle_v1_activate(curr->handle,
                                                 backend_state.seat);
        wl_display_flush(backend_state.display);
        LOG("Window activation sent");
      }
      return;
    }
    curr = curr->next;
  }

  LOG("Window not found: %s", identifier);
}

const char *wlr_get_name(void) { return "wlr"; }
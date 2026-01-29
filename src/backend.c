/* src/backend.c - Backend abstraction layer */
#include "backend.h"
#include "hyprland.h"
#include "wlr_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG(fmt, ...) fprintf(stderr, "[Backend] " fmt "\n", ##__VA_ARGS__)

/* Backend implementations */
static Backend backends[] = {{.type = BACKEND_HYPRLAND,
                              .init = hyprland_backend_init,
                              .cleanup = hyprland_backend_cleanup,
                              .get_windows = update_window_list,
                              .activate_window = switch_to_window,
                              .get_name = hyprland_get_name},
                             {.type = BACKEND_WLR,
                              .init = wlr_backend_init,
                              .cleanup = wlr_backend_cleanup,
                              .get_windows = wlr_get_windows,
                              .activate_window = wlr_activate_window,
                              .get_name = wlr_get_name}};

static Backend *current_backend = NULL;

/* Helper function to detect which backend to use */
static BackendType detect_backend(void) {
  /* Check for Hyprland first */
  const char *hyprland_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");

  if (hyprland_sig && xdg_runtime) {
    /* Check if Hyprland socket exists */
    char path[1024];
    snprintf(path, sizeof(path), "%s/hypr/%s/.socket.sock", xdg_runtime,
             hyprland_sig);

    if (access(path, F_OK) == 0) {
      LOG("Detected Hyprland backend");
      return BACKEND_HYPRLAND;
    }
  }

  /* Check for Wayland environment */
  const char *wayland_display = getenv("WAYLAND_DISPLAY");
  const char *xdg_session_type = getenv("XDG_SESSION_TYPE");

  if ((wayland_display && wayland_display[0] != '\0') ||
      (xdg_session_type && strcmp(xdg_session_type, "wayland") == 0)) {
    LOG("Detected Wayland environment, using wlr backend");
    return BACKEND_WLR;
  }

  LOG("No suitable backend detected");
  return BACKEND_UNKNOWN;
}

Backend *backend_init(void) {
  if (current_backend) {
    return current_backend;
  }

  BackendType type = detect_backend();

  for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
    if (backends[i].type == type) {
      current_backend = &backends[i];

      /* Initialize the backend */
      if (current_backend->init && current_backend->init() < 0) {
        LOG("Failed to initialize %s backend", current_backend->get_name());
        current_backend = NULL;
        return NULL;
      }

      LOG("Using %s backend", current_backend->get_name());
      return current_backend;
    }
  }

  LOG("No suitable backend found");
  return NULL;
}

void backend_cleanup(Backend *backend) {
  if (!backend)
    return;

  if (backend->cleanup) {
    backend->cleanup();
  }

  if (backend == current_backend) {
    current_backend = NULL;
  }
}

BackendType backend_get_type(Backend *backend) {
  return backend ? backend->type : BACKEND_UNKNOWN;
}
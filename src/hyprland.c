/* src/hyprland.c - Hyprland IPC and Data Management */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>

#include "hyprland.h"
#include "config.h"
#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>

#define LOG(fmt, ...) fprintf(stderr, "[Hyprland] " fmt "\n", ##__VA_ARGS__)
#define BUFFER_SIZE 65536
#define INITIAL_CAPACITY 32

/* Cached dispatch syntax flag: true = Lua (v0.55+ with hyprland.lua),
 * false = legacy hyprlang (pre-0.55 or 0.55 with hyprland.conf). */
static bool use_lua_dispatch = false;

static char *get_socket_path(void);
static char *hyprland_request(const char *cmd);

/*
 * Probe Hyprland IPC to determine the correct dispatch syntax.
 *
 * Strategy:
 *   1. Request "version" — parse major.minor to check for >= 0.55.
 *   2. If >= 0.55, request "systeminfo" and look for "configProvider: lua".
 *      If the user is on 0.55 but still using hyprland.conf, the provider
 *      will be "hyprlang" and we must keep using legacy dispatch syntax.
 *
 * This runs once during backend init; the result is cached in
 * `use_lua_dispatch`.
 */
static void detect_dispatch_syntax(void) {
  use_lua_dispatch = false;

  /* Step 1: Check Hyprland version */
  char *ver_resp = hyprland_request("version");
  if (!ver_resp) {
    LOG("Failed to query Hyprland version, defaulting to legacy dispatch");
    return;
  }

  int major = -1, minor = -1;
  /* The version response contains a line like "Version: v0.XX.Y" or similar.
   * We scan for the first occurrence of "v<digits>.<digits>". */
  const char *v = strstr(ver_resp, "v");
  while (v) {
    if (sscanf(v, "v%d.%d", &major, &minor) == 2)
      break;
    v = strstr(v + 1, "v");
  }

  free(ver_resp);

  if (major < 0 || minor < 0) {
    LOG("Could not parse Hyprland version, defaulting to legacy dispatch");
    return;
  }

  LOG("Detected Hyprland version: v%d.%d", major, minor);

  /* Pre-0.55 — always legacy */
  if (major == 0 && minor < 55) {
    LOG("Version < 0.55, using legacy dispatch syntax");
    return;
  }

  /* Step 2: Version >= 0.55 — check config provider */
  char *info_resp = hyprland_request("systeminfo");
  if (!info_resp) {
    LOG("Failed to query systeminfo, defaulting to legacy dispatch");
    return;
  }

  if (strstr(info_resp, "configProvider: lua")) {
    use_lua_dispatch = true;
    LOG("Config provider is Lua, using Lua dispatch syntax");
  } else {
    LOG("Config provider is not Lua (likely hyprlang), using legacy dispatch");
  }

  free(info_resp);
}

int hyprland_backend_init(void) {
  char *socket_path = get_socket_path();
  if (!socket_path) {
    LOG("HYPRLAND_INSTANCE_SIGNATURE or XDG_RUNTIME_DIR not set");
    return -1;
  }

  /* Test if socket exists */
  if (access(socket_path, F_OK) != 0) {
    free(socket_path);
    LOG("Hyprland socket not found");
    return -1;
  }

  free(socket_path);

  /* Probe IPC once to determine dispatch syntax */
  detect_dispatch_syntax();

  return 0;
}

void hyprland_backend_cleanup(void) {
  /* Nothing to cleanup for Hyprland backend */
}

const char *hyprland_get_name(void) { return "hyprland"; }

/* --- Memory Management --- */
void app_state_init(AppState *state) {
  state->windows = NULL;
  state->count = 0;
  state->capacity = 0;
  state->selected_index = 0;
  state->width = 200; /* Default safe size */
  state->height = 100;
  state->error_message = NULL;
  state->filter_workspace = false;
}

void window_info_free(WindowInfo *info) {
  if (info) {
    free(info->address);
    free(info->title);
    free(info->class_name);
    free(info->workspace_name);
    memset(info, 0, sizeof(WindowInfo));
  }
}

void app_state_free(AppState *state) {
  if (state) {
    if (state->windows) {
      for (int i = 0; i < state->count; i++) {
        window_info_free(&state->windows[i]);
      }
      free(state->windows);
    }
    state->windows = NULL;
    state->count = 0;
    state->capacity = 0;
    free(state->error_message);
    state->error_message = NULL;
  }
}

static char *safe_strdup(const char *str) {
  char *dup = strdup(str ? str : "");
  if (!dup) {
    /* OOM fallback: return empty string from static storage.
     * Callers must tolerate this — all current callers do. */
    static char empty[] = "";
    return empty;
  }
  return dup;
}

int app_state_add(AppState *state, WindowInfo *info) {
  if (state->count >= state->capacity) {
    int new_cap = state->capacity == 0 ? INITIAL_CAPACITY : state->capacity * 2;
    WindowInfo *new_ptr = realloc(state->windows, new_cap * sizeof(WindowInfo));
    if (!new_ptr)
      return -1;
    state->windows = new_ptr;
    state->capacity = new_cap;
  }
  state->windows[state->count++] = *info;
  return 0;
}

/* --- Sorting (Stable MRU) --- */
static int compare_mru(const void *a, const void *b) {
  const WindowInfo *wa = (const WindowInfo *)a;
  const WindowInfo *wb = (const WindowInfo *)b;

  int diff = wa->focus_history_id - wb->focus_history_id;
  if (diff != 0)
    return diff;

  const char *addr_a = wa->address ? wa->address : "";
  const char *addr_b = wb->address ? wb->address : "";
  return strcmp(addr_a, addr_b);
}

/* --- Sorting (Linear: workspace_id ASC, workspace_name ASC, address ASC) --- */
static int compare_linear(const void *a, const void *b) {
  const WindowInfo *wa = (const WindowInfo *)a;
  const WindowInfo *wb = (const WindowInfo *)b;

  if (wa->workspace_id != wb->workspace_id)
    return wa->workspace_id - wb->workspace_id;

  const char *name_a = wa->workspace_name ? wa->workspace_name : "";
  const char *name_b = wb->workspace_name ? wb->workspace_name : "";
  int name_diff = strcmp(name_a, name_b);
  if (name_diff != 0)
    return name_diff;

  const char *addr_a = wa->address ? wa->address : "";
  const char *addr_b = wb->address ? wb->address : "";
  return strcmp(addr_a, addr_b);
}

/* --- IPC --- */
static char *get_socket_path(void) {
  const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  const char *xdg = getenv("XDG_RUNTIME_DIR");
  if (!sig || !xdg)
    return NULL;

  size_t len = strlen(xdg) + strlen(sig) + 32;
  char *path = malloc(len);
  if (path)
    snprintf(path, len, "%s/hypr/%s/.socket.sock", xdg, sig);
  return path;
}

static char *hyprland_request(const char *cmd) {
  char *path = get_socket_path();
  if (!path)
    return NULL;

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    free(path);
    return NULL;
  }

  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  free(path);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return NULL;
  }

  /* EINTR-safe write */
  size_t cmd_len = strlen(cmd);
  size_t written_total = 0;
  while (written_total < cmd_len) {
    ssize_t w = write(fd, cmd + written_total, cmd_len - written_total);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      close(fd);
      return NULL;
    }
    written_total += (size_t)w;
  }

  size_t capacity = BUFFER_SIZE;
  char *resp = malloc(capacity);
  if (!resp) {
    close(fd);
    return NULL;
  }
  size_t total = 0;
  ssize_t n;

  while ((n = read(fd, resp + total, capacity - total - 1)) != 0) {
    if (n < 0) {
      if (errno == EINTR)
        continue;
      /* Real error */
      free(resp);
      close(fd);
      return NULL;
    }
    total += (size_t)n;
    if (total >= capacity - 1) {
      capacity *= 2;
      char *tmp = realloc(resp, capacity);
      if (!tmp) {
        free(resp);
        close(fd);
        return NULL;
      }
      resp = tmp;
    }
  }
  resp[total] = '\0';
  close(fd);
  return resp;
}

/* --- Active Workspace Query --- */

/* Sentinel: no filtering (all workspaces) */
#define WS_FILTER_NONE -999

/*
 * Query Hyprland for the currently focused workspace ID.
 * Returns the integer workspace ID, or WS_FILTER_NONE on failure.
 */
static int get_active_workspace_id(void) {
  char *json_str = hyprland_request("j/activeworkspace");
  if (!json_str) {
    LOG("Failed to query active workspace");
    return WS_FILTER_NONE;
  }

  struct json_object *root = json_tokener_parse(json_str);
  free(json_str);

  if (!root) {
    LOG("Failed to parse active workspace JSON");
    return WS_FILTER_NONE;
  }

  struct json_object *id_obj;
  int ws_id = WS_FILTER_NONE;
  if (json_object_object_get_ex(root, "id", &id_obj)) {
    ws_id = json_object_get_int(id_obj);
    LOG("Active workspace: %d", ws_id);
  }

  json_object_put(root);
  return ws_id;
}

/* --- JSON Parsing --- */

/*
 * Parse the Hyprland client list JSON into AppState.
 *
 * target_ws:       if != WS_FILTER_NONE, only include windows whose
 *                  workspace.id matches this value.
 * ignore_pinned:   skip pinned (always-on-top, all-workspace) windows.
 * ignore_special:  skip windows on special (scratchpad) workspaces
 *                  (workspace id < 0).
 */
static int parse_clients(const char *json_str, AppState *state, int target_ws,
                         bool ignore_pinned, bool ignore_special) {
  struct json_object *root = json_tokener_parse(json_str);
  if (!root || !json_object_is_type(root, json_type_array)) {
    if (root)
      json_object_put(root);
    return -1;
  }

  size_t len = json_object_array_length(root);
  for (size_t i = 0; i < len; i++) {
    struct json_object *obj = json_object_array_get_idx(root, i);
    struct json_object *ws_obj = NULL, *ws_id = NULL, *ws_name = NULL,
                       *addr = NULL, *title = NULL, *cls = NULL,
                       *focus = NULL, *floating = NULL, *pinned = NULL;

    if (!json_object_object_get_ex(obj, "workspace", &ws_obj))
      continue;

    json_object_object_get_ex(ws_obj, "name", &ws_name);
    const char *ws_name_str = ws_name ? json_object_get_string(ws_name) : "";

    int wid;
    if (json_object_object_get_ex(ws_obj, "id", &ws_id)) {
      wid = json_object_get_int(ws_id);
    } else if (ws_name_str && *ws_name_str) {
      /* Hyprland 0.57+ omits "id" for non-numbered workspaces (both
       * special workspaces like "special:special" / "special:term" and
       * named workspaces like "music"). Assign -1 so ignore_special and
       * downstream badge formatting handle them identically to pre-0.57. */
      wid = -1;
    } else {
      continue;
    }

    /* Skip special and named workspaces (negative IDs) when configured.
     * Pre-0.57 Hyprland assigns id < 0 to special and named workspaces;
     * 0.57+ omits "id" for them (mapped to wid = -1 above). */
    if (ignore_special && wid < 0)
      continue;

    /* Workspace filter: skip windows not on the target workspace */
    if (target_ws != WS_FILTER_NONE && wid != target_ws)
      continue;

    json_object_object_get_ex(obj, "address", &addr);
    json_object_object_get_ex(obj, "title", &title);
    json_object_object_get_ex(obj, "class", &cls);
    json_object_object_get_ex(obj, "focusHistoryID", &focus);
    json_object_object_get_ex(obj, "floating", &floating);
    json_object_object_get_ex(obj, "pinned", &pinned);

    /* Pinned filter: skip always-on-top, all-workspace overlays (e.g. browser
     * Picture-in-Picture) when ignore_pinned is set. A pinned window is visible
     * on every workspace already, so cycling to it in an MRU switcher is a no-op. */
    bool is_pinned = pinned ? json_object_get_boolean(pinned) : false;
    if (ignore_pinned && is_pinned)
      continue;

    WindowInfo info;
    info.address = safe_strdup(json_object_get_string(addr));
    info.title = safe_strdup(json_object_get_string(title));
    info.class_name = safe_strdup(json_object_get_string(cls));
    info.workspace_id = wid;
    info.workspace_name = safe_strdup(ws_name_str);
    info.focus_history_id = focus ? json_object_get_int(focus) : 9999;
    info.is_active = (info.focus_history_id == 0);
    info.is_floating = floating ? json_object_get_boolean(floating) : false;
    info.is_pinned = is_pinned;
    info.group_count = 1;

    app_state_add(state, &info);
  }

  json_object_put(root);
  return 0;
}

/* --- Aggregation (Context Mode) --- */
static void aggregate_context(AppState *state) {
  if (state->count <= 1)
    return;

  int count = state->count;
  WindowInfo *out = malloc(count * sizeof(WindowInfo));
  if (!out)
    return;
  int out_count = 0;

  for (int i = 0; i < count; i++) {
    WindowInfo *win = &state->windows[i];

    if (win->is_floating) {
      out[out_count].address = safe_strdup(win->address);
      out[out_count].title = safe_strdup(win->title);
      out[out_count].class_name = safe_strdup(win->class_name);
      out[out_count].workspace_id = win->workspace_id;
      out[out_count].workspace_name = safe_strdup(win->workspace_name);
      out[out_count].focus_history_id = win->focus_history_id;
      out[out_count].is_active = win->is_active;
      out[out_count].is_floating = true;
      out[out_count].is_pinned = win->is_pinned;
      out[out_count].group_count = 1;
      out_count++;
    } else {
      int found = -1;
      for (int j = 0; j < out_count; j++) {
        if (!out[j].is_floating && out[j].workspace_id == win->workspace_id &&
            strcmp(out[j].workspace_name, win->workspace_name) == 0 &&
            strcmp(out[j].class_name, win->class_name) == 0) {
          found = j;
          break;
        }
      }

      if (found >= 0) {
        out[found].group_count++;
      } else {
        out[out_count].address = safe_strdup(win->address);
        out[out_count].title = safe_strdup(win->title);
        out[out_count].class_name = safe_strdup(win->class_name);
        out[out_count].workspace_id = win->workspace_id;
        out[out_count].workspace_name = safe_strdup(win->workspace_name);
        out[out_count].focus_history_id = win->focus_history_id;
        out[out_count].is_active = win->is_active;
        out[out_count].is_floating = false;
        out[out_count].is_pinned = win->is_pinned;
        out[out_count].group_count = 1;
        out_count++;
      }
    }
  }

  for (int i = 0; i < count; i++)
    window_info_free(&state->windows[i]);
  free(state->windows);

  state->windows = out;
  state->count = out_count;
  state->capacity = count;
}

/* --- Public API --- */
int update_window_list(AppState *state, Config *cfg, bool is_linear) {
  if (!state)
    return -1;

  /* Determine workspace filter target */
  int target_ws = WS_FILTER_NONE;
  if (state->filter_workspace) {
    target_ws = get_active_workspace_id();
    if (target_ws == WS_FILTER_NONE)
      LOG("Workspace filter requested but query failed — showing all windows");
  }

  char *json = hyprland_request("j/clients");
  if (!json)
    return -1;

  if (parse_clients(json, state, target_ws, cfg ? cfg->ignore_pinned : false,
                     cfg ? cfg->ignore_special : false) < 0) {
    free(json);
    return -1;
  }
  free(json);

  if (state->count > 1) {
    if (is_linear) {
      qsort(state->windows, state->count, sizeof(WindowInfo), compare_linear);
      LOG("Sorted %d windows in linear order (workspace/address)", state->count);
    } else {
      qsort(state->windows, state->count, sizeof(WindowInfo), compare_mru);
    }
  }

  if (cfg && cfg->mode == MODE_CONTEXT) {
    aggregate_context(state);
  }

  return 0;
}

void switch_to_window(const char *address) {
  if (!address)
    return;

  char cmd1[256];
  char cmd2[256];

  if (use_lua_dispatch) {
    /* 1. Target the specific window */
    snprintf(cmd1, sizeof(cmd1), "dispatch hl.dsp.focus({ window = \"address:%s\" })", address);
    /* 2. Pop it to the front if it's floating (ignored if tiled) */
    snprintf(cmd2, sizeof(cmd2), "dispatch hl.dsp.window.alter_zorder({ mode = \"top\" })");

    /* Fire both dispatches */
    char *resp1 = hyprland_request(cmd1);
    if (resp1) free(resp1);

    char *resp2 = hyprland_request(cmd2);
    if (resp2) free(resp2);

    /* 3. Sledgehammer focus to break the layer-shell trap */
    char cmd3[256];
    snprintf(cmd3, sizeof(cmd3), "dispatch hl.dsp.focus({ window = \"activewindow\" })");
    char *resp3 = hyprland_request(cmd3);
    if (resp3) free(resp3);
  } else {
    /* Legacy Fallback (hyprlang / pre-0.55)
     *
     * 2-step combo:
     *   1. focuswindow   — target window by address (works on all versions)
     *   2. alterzorder   — raise to top of Z-stack (replaces deprecated
     *                      bringactivetotop which is a no-op on v0.55+)
     *
     * We intentionally omit the old Step 3 (focuscurrentorlast).
     * That dispatcher is a TOGGLE — it switches focus to the PREVIOUS
     * window, which undoes Step 1.  The layer-shell focus trap is already
     * broken by the explicit focuswindow dispatch since the panel surface
     * is destroyed before this function is called. */
    snprintf(cmd1, sizeof(cmd1), "dispatch focuswindow address:%s", address);
    snprintf(cmd2, sizeof(cmd2), "dispatch alterzorder top");

    char *resp1 = hyprland_request(cmd1);
    if (resp1) free(resp1);

    /* 5 ms settle time: Hyprland's synchronous IPC processes one
     * connection at a time, but the compositor's internal focus-tree
     * commit may not be fully visible to the next dispatch yet.
     * This brief sleep lets the focus state propagate before we
     * touch Z-order.  Imperceptible to users. */
    {
      struct timespec ts = {.tv_sec = 0, .tv_nsec = 5000000L};
      nanosleep(&ts, NULL);
    }

    char *resp2 = hyprland_request(cmd2);
    if (resp2) free(resp2);
  }
}

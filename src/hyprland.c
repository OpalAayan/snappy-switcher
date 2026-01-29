/* src/hyprland.c - Hyprland IPC and Data Management */
#define _POSIX_C_SOURCE 200809L

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

#define LOG(fmt, ...) fprintf(stderr, "[Hyprland] " fmt "\n", ##__VA_ARGS__)
#define BUFFER_SIZE 65536
#define INITIAL_CAPACITY 32

static char *get_socket_path(void);

int hyprland_backend_init(void) {
  /* Hyprland backend doesn't need special initialization */
  /* Just check if we can connect */
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
}

void window_info_free(WindowInfo *info) {
  if (info) {
    free(info->address);
    free(info->title);
    free(info->class_name);
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
  }
}

static char *safe_strdup(const char *str) {
  return str ? strdup(str) : strdup("");
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

  if (write(fd, cmd, strlen(cmd)) < 0) {
    close(fd);
    return NULL;
  }

  size_t capacity = BUFFER_SIZE;
  char *resp = malloc(capacity);
  size_t total = 0;
  ssize_t n;

  while ((n = read(fd, resp + total, capacity - total - 1)) > 0) {
    total += n;
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

/* --- JSON Parsing --- */
static int parse_clients(const char *json_str, AppState *state) {
  struct json_object *root = json_tokener_parse(json_str);
  if (!root || !json_object_is_type(root, json_type_array)) {
    if (root)
      json_object_put(root);
    return -1;
  }

  size_t len = json_object_array_length(root);
  for (size_t i = 0; i < len; i++) {
    struct json_object *obj = json_object_array_get_idx(root, i);
    struct json_object *ws_obj, *ws_id, *addr, *title, *cls, *focus, *floating;

    if (!json_object_object_get_ex(obj, "workspace", &ws_obj))
      continue;
    if (!json_object_object_get_ex(ws_obj, "id", &ws_id))
      continue;

    int wid = json_object_get_int(ws_id);
    if (wid < 0)
      continue;

    json_object_object_get_ex(obj, "address", &addr);
    json_object_object_get_ex(obj, "title", &title);
    json_object_object_get_ex(obj, "class", &cls);
    json_object_object_get_ex(obj, "focusHistoryID", &focus);
    json_object_object_get_ex(obj, "floating", &floating);

    WindowInfo info;
    info.address = safe_strdup(json_object_get_string(addr));
    info.title = safe_strdup(json_object_get_string(title));
    info.class_name = safe_strdup(json_object_get_string(cls));
    info.workspace_id = wid;
    info.focus_history_id = focus ? json_object_get_int(focus) : 9999;
    info.is_active = (info.focus_history_id == 0);
    info.is_floating = floating ? json_object_get_boolean(floating) : false;
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
  int out_count = 0;

  for (int i = 0; i < count; i++) {
    WindowInfo *win = &state->windows[i];

    if (win->is_floating) {
      out[out_count].address = safe_strdup(win->address);
      out[out_count].title = safe_strdup(win->title);
      out[out_count].class_name = safe_strdup(win->class_name);
      out[out_count].workspace_id = win->workspace_id;
      out[out_count].focus_history_id = win->focus_history_id;
      out[out_count].is_active = win->is_active;
      out[out_count].is_floating = true;
      out[out_count].group_count = 1;
      out_count++;
    } else {
      int found = -1;
      for (int j = 0; j < out_count; j++) {
        if (!out[j].is_floating && out[j].workspace_id == win->workspace_id &&
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
        out[out_count].focus_history_id = win->focus_history_id;
        out[out_count].is_active = win->is_active;
        out[out_count].is_floating = false;
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
int update_window_list(AppState *state, Config *cfg) {
  if (!state)
    return -1;

  char *json = hyprland_request("j/clients");
  if (!json)
    return -1;

  if (parse_clients(json, state) < 0) {
    free(json);
    return -1;
  }
  free(json);

  if (state->count > 1) {
    qsort(state->windows, state->count, sizeof(WindowInfo), compare_mru);
  }

  if (cfg && cfg->mode == MODE_CONTEXT) {
    aggregate_context(state);
  }

  return 0;
}

void switch_to_window(const char *address) {
  if (!address)
    return;
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "dispatch focuswindow address:%s", address);
  char *resp = hyprland_request(cmd);
  if (resp)
    free(resp);
}

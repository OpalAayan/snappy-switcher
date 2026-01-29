/* src/data.h - Data structures for Snappy Switcher */
#ifndef DATA_H
#define DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Information about a single window */
typedef struct {
  char *address;        /* Window address (hex string) */
  char *title;          /* Window title */
  char *class_name;     /* Application class name */
  int workspace_id;     /* Workspace ID (-1 for special workspaces) */
  int focus_history_id; /* Focus history ID (0 = most recently focused) */
  bool is_active;       /* Whether this window is currently focused */
  bool is_floating;     /* Whether this window is floating (not tiled) */
  int group_count;      /* Number of windows in this group */
} WindowInfo;

/* Application state */
typedef struct {
  WindowInfo *windows; /* Dynamic array of windows */
  int count;           /* Number of windows */
  int capacity;        /* Allocated capacity */
  int selected_index;  /* Currently selected window index */

  /* UI Dimensions (Shared with Input/Render) */
  uint32_t width;
  uint32_t height;
} AppState;

/* Initialize AppState */
void app_state_init(AppState *state);

int app_state_add(AppState *state, WindowInfo *info);

/* Free all resources held by AppState */
void app_state_free(AppState *state);

/* Free a single WindowInfo's strings */
void window_info_free(WindowInfo *info);

#endif /* DATA_H */

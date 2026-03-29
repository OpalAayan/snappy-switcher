/* src/data.h - Data structures for Snappy Switcher */
#ifndef DATA_H
#define DATA_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

/* Information about a single window */
typedef struct {
  char *address;        /* Window address (hex string) */
  char *title;          /* Window title */
  char *class_name;     /* Application class name */
  int workspace_id;     /* Workspace ID (Negative for special workspaces) */
  char *workspace_name; /* Workspace name (e.g. "1", "music", "special:scratchpad") */
  int focus_history_id; /* Focus history ID (0 = most recently focused) */
  bool is_active;       /* Whether this window is currently focused */
  bool is_floating;     /* Whether this window is floating (not tiled) */
  int group_count;      /* Number of windows in this group */
} WindowInfo;

/* A single in-flight Wayland buffer and its backing resources.
 * The compositor reads the buffer asynchronously after wl_surface_commit(),
 * so we must keep it alive until the wl_buffer::release event fires. */
typedef struct {
  struct wl_buffer *buffer;  /* Wayland buffer object                */
  struct wl_shm_pool *pool;  /* SHM pool that created the buffer     */
  void *data;                /* mmap'd shared memory                 */
  int size;                  /* Size of the mmap'd region in bytes   */
  int fd;                    /* File descriptor (closed after pool)  */
  bool in_use;               /* True while compositor holds the buf  */
  uint32_t alloc_width;      /* Physical width this buffer was sized for  */
  uint32_t alloc_height;     /* Physical height this buffer was sized for */
} RenderBuffer;

/* Application state */
typedef struct {
  WindowInfo *windows; /* Dynamic array of windows */
  int count;           /* Number of windows */
  int capacity;        /* Allocated capacity */
  int selected_index;  /* Currently selected window index */

  /* UI Dimensions (Shared with Input/Render) */
  uint32_t width;
  uint32_t height;
  int cols; /* Number of columns in the current grid layout */

  /* Dirty flag: set to true by input handlers, consumed by main loop */
  bool needs_render;
} AppState;

/* Initialize AppState */
void app_state_init(AppState *state);

int app_state_add(AppState *state, WindowInfo *info);

/* Free all resources held by AppState */
void app_state_free(AppState *state);

/* Free a single WindowInfo's strings */
void window_info_free(WindowInfo *info);

#endif /* DATA_H */

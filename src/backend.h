/* src/backend.h - Backend abstraction for window switching */
#ifndef BACKEND_H
#define BACKEND_H

#include "config.h"
#include "data.h"

/* Backend types */
typedef enum { BACKEND_HYPRLAND, BACKEND_WLR, BACKEND_UNKNOWN } BackendType;

/* Backend function pointers */
typedef struct {
  BackendType type;
  int (*init)(void);
  void (*cleanup)(void);
  int (*get_windows)(AppState *state, Config *config);
  void (*activate_window)(const char *identifier);
  const char *(*get_name)(void);
} Backend;

/* Initialize backend system, auto-detects which backend to use */
Backend *backend_init(void);

/* Cleanup backend resources */
void backend_cleanup(Backend *backend);

/* Get current backend type */
BackendType backend_get_type(Backend *backend);

#endif /* BACKEND_H */
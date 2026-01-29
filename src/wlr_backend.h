/* src/wlr_backend.h - wlr-foreign-toplevel-management backend */
#ifndef WLR_BACKEND_H
#define WLR_BACKEND_H

#include "backend.h"
#include "data.h"

/* Initialize wlr backend */
int wlr_backend_init(void);

/* Cleanup wlr backend */
void wlr_backend_cleanup(void);

/* Get windows via wlr protocol */
int wlr_get_windows(AppState *state, Config *config);

/* Activate window via wlr protocol */
void wlr_activate_window(const char *identifier);

/* Get backend name */
const char *wlr_get_name(void);

#endif /* WLR_BACKEND_H */
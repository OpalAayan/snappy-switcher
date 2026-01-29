/* src/hyprland.h - Hyprland IPC and Data Management */
#ifndef HYPRLAND_H
#define HYPRLAND_H

#include "config.h"
#include "data.h"

/* Initialize AppState */
void app_state_init(AppState *state);

/* Free AppState resources */
void app_state_free(AppState *state);

/*
 * Update window list from Hyprland.
 * Populates state with windows, sorted by MRU.
 * Handles aggregation if Mode == CONTEXT.
 */
int update_window_list(AppState *state, Config *config);

/* Switch focus to window address */
void switch_to_window(const char *address);

int hyprland_backend_init(void);
void hyprland_backend_cleanup(void);
const char *hyprland_get_name(void);

#endif /* HYPRLAND_H */
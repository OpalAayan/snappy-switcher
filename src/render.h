/* src/render.h - UI Rendering */
#ifndef RENDER_H
#define RENDER_H

#include "config.h"
#include "data.h"
#include <stdint.h>
#include <sys/types.h>
#include <wayland-client.h>

/* Shared Wayland objects needed for rendering */
extern struct wl_shm *shm;
extern struct wl_surface *surface;

/* Set config for rendering */
void render_set_config(Config *config);

/* Calculate optimal window dimensions based on window count */
void calculate_dimensions(AppState *state, uint32_t *width, uint32_t *height);

/* Render the window switcher UI */
void render_ui(AppState *state, uint32_t logical_width, uint32_t logical_height, int scale);

/* Return the index of the window card under the logical surface coordinates
 * (x, y), or -1 if none (padding, gaps, error banner, empty list). */
int render_hit_test(const AppState *state, uint32_t logical_width,
                    uint32_t logical_height, double x, double y);

/* Free any in-flight render buffers (call during shutdown) */
void render_cleanup_buffers(void);

#endif /* RENDER_H */

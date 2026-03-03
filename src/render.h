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
void render_ui(AppState *state, uint32_t width, uint32_t height, int scale);

/* Create a shared memory file for Wayland buffers */
int create_shm_file(off_t size);

#endif /* RENDER_H */

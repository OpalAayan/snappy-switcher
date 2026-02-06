/* src/render.c - Clean Grid UI Rendering */
#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES

#include "render.h"
#include "config.h"
#include "icons.h"
#include <cairo/cairo.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LOG(fmt, ...) fprintf(stderr, "[Render] " fmt "\n", ##__VA_ARGS__)

static Config *cfg = NULL;

/* Palette for letter icon fallbacks */
static const uint32_t icon_colors[] = {
    0xe78284, /* Red */
    0xa6d189, /* Green */
    0x8caaee, /* Blue */
    0xca9ee6, /* Mauve */
    0xe5c890, /* Yellow */
    0x81c8be, /* Teal */
    0xf4b8e4, /* Pink */
};
#define NUM_ICON_COLORS (sizeof(icon_colors) / sizeof(icon_colors[0]))

void render_set_config(Config *config) { cfg = config; }

int create_shm_file(off_t size) {
  char name[] = "/tmp/snappy-shm-XXXXXX";
  int fd = mkstemp(name);
  if (fd < 0)
    return -1;
  unlink(name);
  if (ftruncate(fd, size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static unsigned int hash_string(const char *str) {
  unsigned int hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;
  return hash;
}

/* Helper to start Pango layout with config font */
static PangoLayout *create_layout(cairo_t *cr, int size) {
  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *desc = pango_font_description_new();

  const char *family = cfg ? cfg->font_family : "Sans";
  const char *weight_str = cfg ? cfg->font_weight : "Bold";
  PangoWeight weight = PANGO_WEIGHT_BOLD;
  if (strcasecmp(weight_str, "Normal") == 0)
    weight = PANGO_WEIGHT_NORMAL;

  pango_font_description_set_family(desc, family);
  pango_font_description_set_weight(desc, weight);
  pango_font_description_set_size(desc, size * PANGO_SCALE);

  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  return layout;
}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w,
                              double h, double r) {
  cairo_new_path(cr); /* Critical: reset path */

  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;

  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

static void draw_letter_icon(cairo_t *cr, const char *cls, double cx, double cy,
                             int size, int radius, int letter_size) {
  cairo_save(cr);
  cairo_new_path(cr);

  /* Background */
  uint32_t color = icon_colors[hash_string(cls) % NUM_ICON_COLORS];
  double r, g, b;
  color_to_rgb(color, &r, &g, &b);

  cairo_set_source_rgb(cr, r, g, b);
  draw_rounded_rect(cr, cx - size / 2.0, cy - size / 2.0, size, size, radius);
  cairo_fill(cr);

  /* Letter */
  char letter[2] = {cls && cls[0] ? toupper(cls[0]) : '?', 0};
  PangoLayout *layout = create_layout(cr, letter_size);
  pango_layout_set_text(layout, letter, -1);

  int lw, lh;
  pango_layout_get_pixel_size(layout, &lw, &lh);

  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr, cx - lw / 2.0, cy - lh / 2.0);
  pango_cairo_show_layout(cr, layout);

  g_object_unref(layout);
  cairo_restore(cr);
}

static void draw_icon(cairo_t *cr, const char *cls, double cx, double cy) {
  int size = cfg ? cfg->icon_size : 64;
  int radius = cfg ? cfg->icon_radius : 12;

  cairo_save(cr);

  cairo_surface_t *icon = load_app_icon(cls, size);
  if (icon && cairo_surface_status(icon) == CAIRO_STATUS_SUCCESS) {
    /* Clip mask */
    draw_rounded_rect(cr, cx - size / 2.0, cy - size / 2.0, size, size, radius);
    cairo_clip(cr);

    cairo_set_source_surface(cr, icon, cx - size / 2.0, cy - size / 2.0);
    cairo_paint(cr);
    cairo_surface_destroy(icon);
  } else {
    /* Fallback */
    if (icon)
      cairo_surface_destroy(icon);
    if (!cfg || cfg->show_letter_fallback) {
      draw_letter_icon(cr, cls, cx, cy, size, radius,
                       cfg ? cfg->icon_letter_size : 28);
    }
  }

  cairo_restore(cr);
}

static void draw_card(cairo_t *cr, WindowInfo *win, double x, double y,
                      bool selected) {
  cairo_save(cr);

  double bg_r, bg_g, bg_b;
  double sel_r, sel_g, sel_b;
  double brd_r, brd_g, brd_b;
  double txt_r, txt_g, txt_b;

  if (cfg) {
    color_to_rgb(cfg->card_bg, &bg_r, &bg_g, &bg_b);
    color_to_rgb(cfg->card_selected, &sel_r, &sel_g, &sel_b);
    color_to_rgb(cfg->border_color, &brd_r, &brd_g, &brd_b);
    color_to_rgb(cfg->text_color, &txt_r, &txt_g, &txt_b);
  }

  int w = cfg ? cfg->card_width : 200;
  int h = cfg ? cfg->card_height : 160;
  int r = cfg ? cfg->card_radius : 12;

  /* Stack effect (Context Mode) */
  if (win->group_count > 1) {
    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, 0.5);
    draw_rounded_rect(cr, x + 6, y + 6, w, h, r);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, 0.7);
    draw_rounded_rect(cr, x + 3, y + 3, w, h, r);
    cairo_fill(cr);
  }

  /* Main Card */
  if (selected)
    cairo_set_source_rgb(cr, sel_r, sel_g, sel_b);
  else
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);

  draw_rounded_rect(cr, x, y, w, h, r);
  cairo_fill(cr);

  /* Border */
  if (selected) {
    cairo_set_source_rgb(cr, brd_r, brd_g, brd_b);
    cairo_set_line_width(cr, cfg ? cfg->border_width : 2);
    draw_rounded_rect(cr, x, y, w, h, r);
    cairo_stroke(cr);
  }

  /* Title */
  PangoLayout *title = create_layout(cr, cfg ? cfg->title_size : 12);
  pango_layout_set_width(title, (w - 20) * PANGO_SCALE);
  pango_layout_set_ellipsize(title, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(title, PANGO_ALIGN_CENTER);
  pango_layout_set_text(title, win->title, -1);

  cairo_set_source_rgb(cr, txt_r, txt_g, txt_b);
  cairo_move_to(cr, x + 10, y + 10);
  pango_cairo_show_layout(cr, title);
  g_object_unref(title);

  /* Icon */
  draw_icon(cr, win->class_name, x + w / 2.0,
            y + 10 + 20 + 10 + (cfg ? cfg->icon_size / 2.0 : 32));

  /* Badge (Count) */
  if (win->group_count > 1) {
    char count[12];
    snprintf(count, sizeof(count), "%d", win->group_count);

    double bx = x + w - 24;
    double by = y + h - 24;

    /* Badge BG (Accent) */
    cairo_set_source_rgb(cr, brd_r, brd_g, brd_b);
    cairo_arc(cr, bx, by, 10, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Badge Text (Config Text Color) */
    PangoLayout *bl = create_layout(cr, 10);
    pango_layout_set_text(bl, count, -1);

    int bw, bh;
    pango_layout_get_pixel_size(bl, &bw, &bh);

    /* Use text_color as requested */
    cairo_set_source_rgb(cr, txt_r, txt_g, txt_b);
    cairo_move_to(cr, bx - bw / 2.0, by - bh / 2.0);
    pango_cairo_show_layout(cr, bl);
    g_object_unref(bl);
  }

  cairo_restore(cr);
}

void calculate_dimensions(AppState *state, uint32_t *width, uint32_t *height) {
  int count = (state && state->count > 0) ? state->count : 1;
  int w = cfg ? cfg->card_width : 200;
  int h = cfg ? cfg->card_height : 160;
  int gap = cfg ? cfg->card_gap : 12;
  int pad = cfg ? cfg->padding : 32;
  int cols = cfg ? cfg->max_cols : 5;

  if (count < cols)
    cols = count;
  int rows = (count + cols - 1) / cols;

  *width = (cols * w) + ((cols - 1) * gap) + (pad * 2);
  *height = (rows * h) + ((rows - 1) * gap) + (pad * 2);

  if (*width < 200)
    *width = 200;
  if (*height < 150)
    *height = 150;
}

void render_ui(AppState *state, uint32_t width, uint32_t height) {
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
  int size = stride * height;
  int fd = create_shm_file(size);
  if (fd < 0)
    return;

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return;
  }

  /* CRITICAL FIX 1: Zero buffer */
  memset(data, 0, size);

  cairo_surface_t *surf = cairo_image_surface_create_for_data(
      data, CAIRO_FORMAT_ARGB32, width, height, stride);
  cairo_t *cr = cairo_create(surf);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

  /* CRITICAL FIX 2: Source Clear */
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  /* Background */
  double r, g, b;
  if (cfg)
    color_to_rgb(cfg->background, &r, &g, &b);
  else {
    r = 0.1;
    g = 0.1;
    b = 0.2;
  }

  cairo_set_source_rgba(cr, r, g, b, 0.95);
  int rad = cfg ? cfg->card_radius : 12;
  draw_rounded_rect(cr, 0, 0, width, height, rad + 4);
  cairo_fill(cr);

  /* Border */
  if (cfg)
    color_to_rgb(cfg->border_color, &r, &g, &b);
  cairo_set_source_rgba(cr, r, g, b, 0.3);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, 0.5, 0.5, width - 1, height - 1, rad + 4);
  cairo_stroke(cr);

  /* Content */
  if (!state || state->count == 0) {
    PangoLayout *msg = create_layout(cr, 16);
    pango_layout_set_text(msg, "No windows", -1);
    int mw, mh;
    pango_layout_get_pixel_size(msg, &mw, &mh);

    if (cfg)
      color_to_rgb(cfg->text_color, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 0.5);
    cairo_move_to(cr, (width - mw) / 2.0, (height - mh) / 2.0);
    pango_cairo_show_layout(cr, msg);
    g_object_unref(msg);
  } else {
    int cw = cfg ? cfg->card_width : 200;
    int ch = cfg ? cfg->card_height : 160;
    int gap = cfg ? cfg->card_gap : 12;
    int pad = cfg ? cfg->padding : 32;
    int max_cols = cfg ? cfg->max_cols : 5;

    int cols = (state->count < max_cols) ? state->count : max_cols;
    int rows = (state->count + max_cols - 1) / max_cols;

    int grid_w = (cols * cw) + ((cols - 1) * gap);
    int grid_h = (rows * ch) + ((rows - 1) * gap);

    double start_x = (width - grid_w) / 2.0;
    double start_y = (height - grid_h) / 2.0;
    if (start_x < pad)
      start_x = pad;
    if (start_y < pad)
      start_y = pad;

    for (int i = 0; i < state->count; i++) {
      int r = i / max_cols;
      int c = i % max_cols;
      double x = start_x + c * (cw + gap);
      double y = start_y + r * (ch + gap);
      draw_card(cr, &state->windows[i], x, y, i == state->selected_index);
    }
  }

  /* Wayland Commit */
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, width,
                           height); /* Use damage_buffer for best safety */
  wl_surface_commit(surface);

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  wl_buffer_destroy(buffer);
  wl_shm_pool_destroy(pool);
  close(fd);
  munmap(data, size);
}

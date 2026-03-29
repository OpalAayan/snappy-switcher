/* src/render.c - Clean Grid UI Rendering */
#define _GNU_SOURCE

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
    0xe78284ff, /* Red */
    0xa6d189ff, /* Green */
    0x8caaeeff, /* Blue */
    0xca9ee6ff, /* Mauve */
    0xe5c890ff, /* Yellow */
    0x81c8beff, /* Teal */
    0xf4b8e4ff, /* Pink */
};
#define NUM_ICON_COLORS (sizeof(icon_colors) / sizeof(icon_colors[0]))

void render_set_config(Config *config) { cfg = config; }

int create_shm_file(off_t size) {
  int fd = -1;
#ifdef __linux__
  fd = memfd_create("snappy-shm", MFD_CLOEXEC);
#endif
  if (fd < 0) {
    /* Fallback for kernels without memfd_create */
    char name[] = "/tmp/snappy-shm-XXXXXX";
    fd = mkstemp(name);
    if (fd < 0)
      return -1;
    unlink(name);
  }
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
  double r, g, b, a;
  color_to_rgba(color, &r, &g, &b, &a);

  cairo_set_source_rgba(cr, r, g, b, a);
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

/* --- Workspace Tag Formatting --- */

/*
 * Check if a workspace name is a pure integer string (e.g. "1", "42").
 * Returns true for standard numbered workspaces.
 */
static bool is_numeric_name(const char *name) {
  if (!name || !*name)
    return false;
  const char *p = name;
  if (*p == '-')
    p++; /* allow negative */
  if (!*p)
    return false;
  while (*p) {
    if (*p < '0' || *p > '9')
      return false;
    p++;
  }
  return true;
}

/*
 * Memoizing letter tracker for named workspace deduplication.
 *
 * Caches exact workspace names → assigned occurrence indices so that
 * multiple windows on the same named workspace share a single tag.
 * The next-available index per starting letter is tracked separately.
 */
#define LT_MAX_NAMES 512

typedef struct {
  const char *seen_names[LT_MAX_NAMES]; /* Exact workspace names seen so far */
  int assigned_indices[LT_MAX_NAMES];   /* Occurrence index assigned to each */
  int seen_count;                       /* How many distinct names cached    */
  int next_idx[128];                    /* Next available index per letter   */
} LetterTracker;

/*
 * Zero the tracker.  Call once at the start of each render pass.
 */
static void letter_tracker_init(LetterTracker *lt) {
  memset(lt, 0, sizeof(LetterTracker));
}

/*
 * Look up (or lazily assign) the 0-based occurrence index for a workspace name.
 *
 * If the exact name has been seen before, returns the previously assigned index
 * (idempotent).  Otherwise assigns the next available index for that letter,
 * caches the mapping, and returns the new index.
 */
static int letter_tracker_assign(LetterTracker *lt, const char *name) {
  /* Check cache for an exact match first */
  for (int i = 0; i < lt->seen_count; i++) {
    if (strcmp(lt->seen_names[i], name) == 0)
      return lt->assigned_indices[i];
  }

  /* New name — grab the next index for this starting letter */
  int key = toupper((unsigned char)name[0]);
  if (key < 0 || key >= 128)
    key = 0;

  int idx = lt->next_idx[key]++;

  /* Cache the mapping (bounds-checked) */
  if (lt->seen_count < LT_MAX_NAMES) {
    lt->seen_names[lt->seen_count] = name;   /* borrows pointer — valid for this render pass */
    lt->assigned_indices[lt->seen_count] = idx;
    lt->seen_count++;
  }

  return idx;
}

/*
 * Format a workspace tag string for display.
 *
 * Rules (strict priority):
 *   1. Special workspaces (id < 0 or name starts with "special:"):
 *      -> "[S]" or "[S:F]" if floating
 *
 *   2. Standard numbered workspaces (name is a pure integer):
 *      -> "[N]" or "[F:N]" if floating
 *
 *   3. Named workspaces (the complex case):
 *      First char uppercased, with occurrence index:
 *      - 1st (idx 0):   "[M]"      / "[M:F]"
 *      - 2nd (idx 1):   "[M:1]"    / "[M:1:F]"
 *      - 100th (idx 99): "[M:99]"  / "[M:99:F]"
 *      - 101st (idx 100): "[M:∞]"  (always, even if floating)
 *      - >101 (idx >100): easter egg string
 *
 * Returns a heap-allocated string. Caller must free().
 */
static char *format_workspace_tag(WindowInfo *win, LetterTracker *lt) {
  const char *name = win->workspace_name;

 /* --- Rule 1: Special workspaces --- */
  if (name && strncmp(name, "special:", 8) == 0) {
    return win->is_floating ? strdup("[S:F]") : strdup("[S]");
  }

  /* --- Rule 2: Standard numbered workspaces --- */
  if (!name || !*name || is_numeric_name(name)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "[%s%d]",
             win->is_floating ? "F:" : "", win->workspace_id);
    return strdup(buf);
  }

  /* --- Rule 3: Named workspaces --- */
  char letter = toupper((unsigned char)name[0]);
  int idx = letter_tracker_assign(lt, name);

  /* What if User is a stubborn ass > 101 occurrences (index > 100) */
  if (idx > 100) {
    return strdup("Fuck you user, pick a damn name with different letters");
  }

  /* Exactly 101st (index 100): infinity */
  if (idx == 100) {
    char buf[32];
    snprintf(buf, sizeof(buf), "[%c:\xe2\x88\x9e]", letter); /* ∞ is UTF-8: E2 88 9E */
    return strdup(buf);
  }

  /* Index 0: just the letter */
  if (idx == 0) {
    char buf[16];
    if (win->is_floating)
      snprintf(buf, sizeof(buf), "[%c:F]", letter);
    else
      snprintf(buf, sizeof(buf), "[%c]", letter);
    return strdup(buf);
  }

  /* Index 1..99: letter + index */
  char buf[32];
  if (win->is_floating)
    snprintf(buf, sizeof(buf), "[%c:%d:F]", letter, idx);
  else
    snprintf(buf, sizeof(buf), "[%c:%d]", letter, idx);
  return strdup(buf);
}

/* Module-level tracker, zeroed each render pass */
static LetterTracker g_letter_tracker;

static void draw_card(cairo_t *cr, WindowInfo *win, double x, double y,
                      bool selected) {
  cairo_save(cr);

  double bg_r, bg_g, bg_b, bg_a;
  double sel_r, sel_g, sel_b, sel_a;
  double brd_r, brd_g, brd_b, brd_a;
  double txt_r, txt_g, txt_b, txt_a;
  double bnd_r, bnd_g, bnd_b, bnd_a;
  double bdg_r, bdg_g, bdg_b, bdg_a;
  double bdt_r, bdt_g, bdt_b, bdt_a;

  if (cfg) {
    color_to_rgba(cfg->card_bg, &bg_r, &bg_g, &bg_b, &bg_a);
    color_to_rgba(cfg->card_selected, &sel_r, &sel_g, &sel_b, &sel_a);
    color_to_rgba(cfg->border_color, &brd_r, &brd_g, &brd_b, &brd_a);
    color_to_rgba(cfg->text_color, &txt_r, &txt_g, &txt_b, &txt_a);
    color_to_rgba(cfg->bundle_bg, &bnd_r, &bnd_g, &bnd_b, &bnd_a);
    color_to_rgba(cfg->badge_bg, &bdg_r, &bdg_g, &bdg_b, &bdg_a);
    color_to_rgba(cfg->badge_text_color, &bdt_r, &bdt_g, &bdt_b, &bdt_a);
  }

  int w = cfg ? cfg->card_width : 200;
  int h = cfg ? cfg->card_height : 160;
  int r = cfg ? cfg->card_radius : 12;

  /* Stack effect (Context Mode) */
  if (win->group_count > 1) {
    cairo_set_source_rgba(cr, bnd_r, bnd_g, bnd_b, bnd_a);
    draw_rounded_rect(cr, x + 6, y + 6, w, h, r);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, bnd_r, bnd_g, bnd_b, bnd_a);
    draw_rounded_rect(cr, x + 3, y + 3, w, h, r);
    cairo_fill(cr);
  }

  /* Main Card */
  if (selected)
    cairo_set_source_rgba(cr, sel_r, sel_g, sel_b, sel_a);
  else
    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, bg_a);

  draw_rounded_rect(cr, x, y, w, h, r);
  cairo_fill(cr);

  /* Border */
  if (selected) {
    cairo_set_source_rgba(cr, brd_r, brd_g, brd_b, brd_a);
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

  cairo_set_source_rgba(cr, txt_r, txt_g, txt_b, txt_a);
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

    /* Badge BG */
    cairo_set_source_rgba(cr, bdg_r, bdg_g, bdg_b, bdg_a);
    cairo_arc(cr, bx, by, 10, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Badge Text */
    PangoLayout *bl = create_layout(cr, 10);
    pango_layout_set_text(bl, count, -1);

    int bw, bh;
    pango_layout_get_pixel_size(bl, &bw, &bh);

    cairo_set_source_rgba(cr, bdt_r, bdt_g, bdt_b, bdt_a);
    cairo_move_to(cr, bx - bw / 2.0, by - bh / 2.0);
    pango_cairo_show_layout(cr, bl);
    g_object_unref(bl);
  }

  /* Workspace Badge (bottom-left pill) */
  if (cfg && cfg->show_workspace_badge) {
    char *ws_text = format_workspace_tag(win, &g_letter_tracker);

    PangoLayout *wl = create_layout(cr, 8);
    pango_layout_set_text(wl, ws_text, -1);

    int ww, wh;
    pango_layout_get_pixel_size(wl, &ww, &wh);

    int badge_w = ww + 12;
    int badge_h = wh + 6;
    double wx = x + 10;
    double wy = y + h - badge_h - 10;

    /* Badge background */
    cairo_set_source_rgba(cr, bdg_r, bdg_g, bdg_b, bdg_a);
    draw_rounded_rect(cr, wx, wy, badge_w, badge_h, 4.0);
    cairo_fill(cr);

    /* Badge text */
    cairo_set_source_rgba(cr, bdt_r, bdt_g, bdt_b, bdt_a);
    cairo_move_to(cr, wx + (badge_w - ww) / 2.0, wy + (badge_h - wh) / 2.0);
    pango_cairo_show_layout(cr, wl);
    g_object_unref(wl);

    free(ws_text);
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

  /* Prevent Wayland 0x0 buffer crashes */
  if (*width <= 0) *width = 10;
  if (*height <= 0) *height = 10;
}

/* --- Buffer Lifecycle Management --- */

#define RENDER_BUFFER_COUNT 2
static RenderBuffer render_buffers[RENDER_BUFFER_COUNT] = {0};

/* Called by the compositor when it is done reading a buffer.
 * We destroy the Wayland protocol objects (they're per-commit) but KEEP the
 * backing fd + mmap alive so the next frame can reuse the same memory. */
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  RenderBuffer *buf = (RenderBuffer *)data;
  (void)wl_buffer;

  if (buf->buffer) {
    wl_buffer_destroy(buf->buffer);
    buf->buffer = NULL;
  }
  if (buf->pool) {
    wl_shm_pool_destroy(buf->pool);
    buf->pool = NULL;
  }
  /* fd, data, size, alloc_width, alloc_height are PRESERVED for reuse */
  buf->in_use = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

/* Find a free buffer slot, or NULL if all are in-flight */
static RenderBuffer *acquire_buffer(uint32_t phys_w, uint32_t phys_h,
                                     int stride) {
  int needed_size = stride * (int)phys_h;

  for (int i = 0; i < RENDER_BUFFER_COUNT; i++) {
    RenderBuffer *buf = &render_buffers[i];
    if (buf->in_use)
      continue;

    /* Check if existing allocation matches the requested dimensions */
    if (buf->data && buf->alloc_width == phys_w &&
        buf->alloc_height == phys_h) {
      /* Hot path: reuse existing mmap'd region — zero syscalls */
      return buf;
    }

    /* Dimensions changed (or first use): tear down old allocation */
    if (buf->data) {
      munmap(buf->data, buf->size);
      buf->data = NULL;
    }
    if (buf->fd >= 0) {
      close(buf->fd);
      buf->fd = -1;
    }

    /* Allocate fresh backing memory */
    int fd = create_shm_file(needed_size);
    if (fd < 0)
      return NULL;

    void *data = mmap(NULL, needed_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
      close(fd);
      return NULL;
    }

    buf->fd = fd;
    buf->data = data;
    buf->size = needed_size;
    buf->alloc_width = phys_w;
    buf->alloc_height = phys_h;
    return buf;
  }
  return NULL;
}

/* Force-free all buffer slots (for shutdown) */
void render_cleanup_buffers(void) {
  for (int i = 0; i < RENDER_BUFFER_COUNT; i++) {
    RenderBuffer *buf = &render_buffers[i];
    if (buf->buffer) {
      wl_buffer_destroy(buf->buffer);
      buf->buffer = NULL;
    }
    if (buf->pool) {
      wl_shm_pool_destroy(buf->pool);
      buf->pool = NULL;
    }
    if (buf->data) {
      munmap(buf->data, buf->size);
      buf->data = NULL;
    }
    if (buf->fd >= 0) {
      close(buf->fd);
      buf->fd = -1;
    }
    buf->in_use = false;
    buf->alloc_width = 0;
    buf->alloc_height = 0;
  }
}

void render_ui(AppState *state, uint32_t logical_width, uint32_t logical_height,
               int scale) {
  uint32_t phys_width = logical_width * scale;
  uint32_t phys_height = logical_height * scale;

  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, phys_width);

  /* Acquire a free buffer slot (reuses persistent mmap when possible) */
  RenderBuffer *rbuf = acquire_buffer(phys_width, phys_height, stride);
  if (!rbuf) {
    LOG("All render buffers in use or allocation failed, skipping frame");
    return;
  }

  void *data = rbuf->data;
  int size = rbuf->size;
  int fd = rbuf->fd;

  /* Clear pixel data */
  memset(data, 0, size);

  cairo_surface_t *surf = cairo_image_surface_create_for_data(
      data, CAIRO_FORMAT_ARGB32, phys_width, phys_height, stride);
  cairo_t *cr = cairo_create(surf);
  cairo_scale(cr, scale, scale);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

  /* CRITICAL FIX 2: Source Clear */
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  /* Background */
  double r, g, b, a;
  if (cfg)
    color_to_rgba(cfg->background, &r, &g, &b, &a);
  else {
    r = 0.1;
    g = 0.1;
    b = 0.2;
    a = 0.95;
  }

  cairo_set_source_rgba(cr, r, g, b, a);
  int rad = cfg ? cfg->card_radius : 12;
  draw_rounded_rect(cr, 0, 0, logical_width, logical_height, rad + 4);
  cairo_fill(cr);

  /* Border */
  if (cfg)
    color_to_rgba(cfg->border_color, &r, &g, &b, &a);
  cairo_set_source_rgba(cr, r, g, b, 0.3);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, 0.5, 0.5, logical_width - 1, logical_height - 1,
                    rad + 4);
  cairo_stroke(cr);

  /* Content */
  if (!state || state->count == 0) {
    PangoLayout *msg = create_layout(cr, 16);
    pango_layout_set_text(msg, "No windows", -1);
    int mw, mh;
    pango_layout_get_pixel_size(msg, &mw, &mh);

    if (cfg)
      color_to_rgba(cfg->text_color, &r, &g, &b, &a);
    cairo_set_source_rgba(cr, r, g, b, 0.5);
    cairo_move_to(cr, (logical_width - mw) / 2.0, (logical_height - mh) / 2.0);
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

    double start_x = (logical_width - grid_w) / 2.0;
    double start_y = (logical_height - grid_h) / 2.0;
    if (start_x < pad)
      start_x = pad;
    if (start_y < pad)
      start_y = pad;

    /* Zero the workspace letter tracker for this render pass */
    letter_tracker_init(&g_letter_tracker);

    for (int i = 0; i < state->count; i++) {
      int r = i / max_cols;
      int c = i % max_cols;
      double x = start_x + c * (cw + gap);
      double y = start_y + r * (ch + gap);
      draw_card(cr, &state->windows[i], x, y, i == state->selected_index);
    }
  }

  /* --- Wayland Commit with proper buffer lifecycle --- */
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, phys_width, phys_height, stride, WL_SHM_FORMAT_ARGB8888);

  /* Populate the RenderBuffer slot BEFORE committing */
  rbuf->buffer = buffer;
  rbuf->pool = pool;
  rbuf->data = data;
  rbuf->size = size;
  rbuf->fd = fd;
  rbuf->in_use = true;

  /* Listen for the compositor's release event */
  wl_buffer_add_listener(buffer, &buffer_listener, rbuf);

  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, phys_width, phys_height);
  wl_surface_commit(surface);

  /* Clean up Cairo objects (these are CPU-side only, safe to free now) */
  cairo_destroy(cr);
  cairo_surface_destroy(surf);

  /* NOTE: buffer, pool, fd, and data are NOT freed here.
   * They will be freed in buffer_release() when the compositor is done. */
}

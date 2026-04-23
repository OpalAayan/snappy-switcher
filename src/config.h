/* src/config.h - Configuration and Theming */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* View mode for window display */
typedef enum {
  MODE_OVERVIEW, /* Show all windows individually */
  MODE_CONTEXT   /* Group tiled windows by workspace + app class */
} ViewMode;
/* Theme configuration */
typedef struct {
  /* Colors (0xRRGGBBAA) */
  uint32_t background;
  uint32_t card_bg;
  uint32_t card_selected;
  uint32_t border_color;
  uint32_t text_color;
  uint32_t subtext_color;
  uint32_t bundle_bg;
  uint32_t badge_bg;
  uint32_t badge_text_color;

  /* Layout */
  int card_width;
  int card_height;
  int card_gap;
  int card_radius;
  int border_width;
  int padding;
  int max_cols;
  int icon_size;
  int icon_radius;

  /* Typography */
  char font_family[64];
  char font_weight[32];
  int title_size;
  int icon_letter_size;

  /* Icons */
  char icon_theme[64];
  char icon_fallback[64];
  bool show_letter_fallback;

  /* View Mode */
  bool follow_monitor;
  bool show_workspace_badge;
  ViewMode mode;
  bool sticky_mode;

  /* Dismiss modifier: which modifier release hides the switcher (e.g. alt, super) */
  char dismiss_modifier[64];
} Config;

/* Load config from default path (~/.config/snappy-switcher/config.ini) */
Config *load_config(void);

/* Load config from given path; NULL = use default. Returns default if missing. */
Config *load_config_from(const char *path);

/* Free config memory */
void free_config(Config *config);

/* Get default config (fallback values) */
Config *get_default_config(void);

/* Helper: Convert uint32_t hex color (0xRRGGBBAA) to cairo RGBA (0.0-1.0) */
void color_to_rgba(uint32_t color, double *r, double *g, double *b, double *a);

#endif /* CONFIG_H */

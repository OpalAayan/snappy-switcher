/* src/config.c - Modular Configuration with Theme Support */
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG(fmt, ...) fprintf(stderr, "[Config] " fmt "\n", ##__VA_ARGS__)

/* --- Defaults ("Snappy Slate" Theme) --- */
static void set_defaults(Config *cfg) {
  cfg->mode = MODE_CONTEXT;
  cfg->filter_rule_count = 1;
  cfg->filter_rules[0].exclude = true;
  cfg->filter_rules[0].field = WINDOW_FILTER_WORKSPACE;
  cfg->filter_rules[0].value_kind = WINDOW_FILTER_VALUE_ID;
  cfg->filter_rules[0].id = -1;
  cfg->follow_monitor = false;
  cfg->show_workspace_badge = true;
  cfg->sticky_mode = false;
  strncpy(cfg->dismiss_modifier, "alt", sizeof(cfg->dismiss_modifier) - 1);

  /* Default Theme Colors (0xRRGGBBAA) */
  cfg->background = 0x1e1e2eff;
  cfg->card_bg = 0x313244ff;
  cfg->card_selected = 0x45475aff;
  cfg->text_color = 0xcdd6f4ff;
  cfg->subtext_color = 0xa6adc8ff;
  cfg->border_color = 0x89b4faff;
  cfg->bundle_bg = 0x313244ff;
  cfg->badge_bg = 0x89b4faff;
  cfg->badge_text_color = 0xcdd6f4ff;
  cfg->border_width = 2;
  cfg->card_radius = 12;

  /* Layout */
  cfg->card_width = 160;
  cfg->card_height = 140;
  cfg->card_gap = 10;
  cfg->padding = 20;
  cfg->max_cols = 5;

  /* Icons */
  cfg->icon_size = 56;
  cfg->icon_radius = 12;
  cfg->icon_letter_size = 24;
  strncpy(cfg->icon_theme, "Tela-dracula", sizeof(cfg->icon_theme) - 1);
  strncpy(cfg->icon_fallback, "Tela-circle-dracula",
          sizeof(cfg->icon_fallback) - 1);
  cfg->show_letter_fallback = true;

  /* Font */
  strncpy(cfg->font_family, "Sans", sizeof(cfg->font_family) - 1);
  strncpy(cfg->font_weight, "Bold", sizeof(cfg->font_weight) - 1);
  cfg->title_size = 10;
}

/* --- Hex Color Helper (supports #RRGGBB and #RRGGBBAA) --- */
static uint32_t parse_hex_color(const char *str) {
  if (!str)
    return 0;
  if (str[0] == '#')
    str++;
  uint32_t val = (uint32_t)strtoul(str, NULL, 16);
  size_t len = strlen(str);
  if (len <= 6)
    val = (val << 8) | 0xFF; /* RGB -> RGBA (fully opaque) */
  return val;
}

/* --- String Trimming --- */
static char *trim(char *str) {
  if (!str)
    return str;
  while (isspace(*str))
    str++;
  if (*str == '\0')
    return str;
  char *end = str + strlen(str) - 1;
  while (end > str && (isspace(*end) || *end == '\n' || *end == '\r'))
    *end-- = '\0';
  return str;
}

static bool parse_filter_rule(char *token, WindowFilterRule *rule) {
  token = trim(token);
  if (!token || token[0] == '\0')
    return false;

  rule->exclude = false;
  if (token[0] == '!') {
    rule->exclude = true;
    token = trim(token + 1);
  }

  char *colon = strchr(token, ':');
  if (!colon)
    return false;

  *colon = '\0';
  char *field = trim(token);
  char *value = trim(colon + 1);

  if (strcasecmp(field, "workspace") == 0 ||
      strcasecmp(field, "ws") == 0) {
    rule->field = WINDOW_FILTER_WORKSPACE;
  } else if (strcasecmp(field, "monitor") == 0 ||
             strcasecmp(field, "mon") == 0) {
    rule->field = WINDOW_FILTER_MONITOR;
  } else {
    return false;
  }

  if (strcasecmp(value, "current") == 0 || strcasecmp(value, "active") == 0) {
    rule->value_kind = WINDOW_FILTER_VALUE_CURRENT;
    rule->id = 0;
    return true;
  }

  char *end = NULL;
  long id = strtol(value, &end, 10);
  if (end == value || *trim(end) != '\0')
    return false;

  rule->value_kind = WINDOW_FILTER_VALUE_ID;
  rule->id = (int)id;
  return true;
}

static void add_filter_rule(Config *cfg, WindowFilterRule rule) {
  if (cfg->filter_rule_count >= WINDOW_FILTER_MAX_RULES) {
    LOG("Too many filter rules; ignoring extra rule");
    return;
  }
  cfg->filter_rules[cfg->filter_rule_count++] = rule;
}

static void parse_window_filter(Config *cfg, const char *val) {
  cfg->filter_rule_count = 0;

  if (!val || val[0] == '\0' || strcasecmp(val, "none") == 0 ||
      strcasecmp(val, "off") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "all") == 0) {
    return;
  }

  /* Compatibility with the older single-scope values. */
  if (strcasecmp(val, "current_workspace") == 0 ||
      strcasecmp(val, "current-workspace") == 0 ||
      strcasecmp(val, "current workspace") == 0 ||
      strcasecmp(val, "workspace") == 0) {
    WindowFilterRule rule = {.exclude = false,
                             .field = WINDOW_FILTER_WORKSPACE,
                             .value_kind = WINDOW_FILTER_VALUE_CURRENT,
                             .id = 0};
    add_filter_rule(cfg, rule);
    return;
  }

  if (strcasecmp(val, "current_monitor") == 0 ||
      strcasecmp(val, "current-monitor") == 0 ||
      strcasecmp(val, "current monitor") == 0 ||
      strcasecmp(val, "monitor") == 0) {
    WindowFilterRule rule = {.exclude = false,
                             .field = WINDOW_FILTER_MONITOR,
                             .value_kind = WINDOW_FILTER_VALUE_CURRENT,
                             .id = 0};
    add_filter_rule(cfg, rule);
    return;
  }

  char buf[512];
  strncpy(buf, val, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *saveptr = NULL;
  for (char *token = strtok_r(buf, ",", &saveptr); token;
       token = strtok_r(NULL, ",", &saveptr)) {
    WindowFilterRule rule;
    if (parse_filter_rule(token, &rule)) {
      add_filter_rule(cfg, rule);
    } else {
      LOG("Unknown filter rule: %s", trim(token));
    }
  }
}

/* --- Parse a single key-value pair --- */
static void apply_value(Config *cfg, const char *section, const char *key,
                        const char *val) {
  /* General */
  if (strcasecmp(section, "general") == 0) {
    if (strcasecmp(key, "mode") == 0) {
      if (strcasecmp(val, "context") == 0)
        cfg->mode = MODE_CONTEXT;
      else if (strcasecmp(val, "overview") == 0)
        cfg->mode = MODE_OVERVIEW;
    } else if (strcasecmp(key, "filter") == 0) {
      parse_window_filter(cfg, val);
    } else if (strcasecmp(key, "follow_monitor") == 0) {
      cfg->follow_monitor =
          (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    } else if (strcasecmp(key, "show_workspace_badge") == 0) {
      cfg->show_workspace_badge =
          (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    } else if (strcasecmp(key, "dismiss_modifier") == 0) {
      strncpy(cfg->dismiss_modifier, val, sizeof(cfg->dismiss_modifier) - 1);
      cfg->dismiss_modifier[sizeof(cfg->dismiss_modifier) - 1] = '\0';
    } else if (strcasecmp(key, "sticky_mode") == 0) {
      cfg->sticky_mode = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    }
  }
  /* Colors (from theme or manual override) */
  else if (strcasecmp(section, "colors") == 0 ||
           strcasecmp(section, "theme") == 0) {
    if (strcasecmp(key, "background") == 0)
      cfg->background = parse_hex_color(val);
    else if (strcasecmp(key, "card_bg") == 0)
      cfg->card_bg = parse_hex_color(val);
    else if (strcasecmp(key, "card_selected") == 0)
      cfg->card_selected = parse_hex_color(val);
    else if (strcasecmp(key, "text_color") == 0)
      cfg->text_color = parse_hex_color(val);
    else if (strcasecmp(key, "subtext_color") == 0)
      cfg->subtext_color = parse_hex_color(val);
    else if (strcasecmp(key, "border_color") == 0)
      cfg->border_color = parse_hex_color(val);
    else if (strcasecmp(key, "bundle_bg") == 0)
      cfg->bundle_bg = parse_hex_color(val);
    else if (strcasecmp(key, "badge_bg") == 0)
      cfg->badge_bg = parse_hex_color(val);
    else if (strcasecmp(key, "badge_text_color") == 0)
      cfg->badge_text_color = parse_hex_color(val);
    else if (strcasecmp(key, "border_width") == 0)
      cfg->border_width = atoi(val);
    else if (strcasecmp(key, "corner_radius") == 0)
      cfg->card_radius = atoi(val);
  }
  /* Layout */
  else if (strcasecmp(section, "layout") == 0) {
    if (strcasecmp(key, "card_width") == 0)
      cfg->card_width = atoi(val);
    else if (strcasecmp(key, "card_height") == 0)
      cfg->card_height = atoi(val);
    else if (strcasecmp(key, "card_gap") == 0)
      cfg->card_gap = atoi(val);
    else if (strcasecmp(key, "padding") == 0)
      cfg->padding = atoi(val);
    else if (strcasecmp(key, "max_cols") == 0)
      cfg->max_cols = atoi(val);
    else if (strcasecmp(key, "icon_size") == 0)
      cfg->icon_size = atoi(val);
    else if (strcasecmp(key, "icon_radius") == 0)
      cfg->icon_radius = atoi(val);
  }
  /* Icons */
  else if (strcasecmp(section, "icons") == 0) {
    if (strcasecmp(key, "theme") == 0)
      strncpy(cfg->icon_theme, val, sizeof(cfg->icon_theme) - 1);
    else if (strcasecmp(key, "fallback") == 0)
      strncpy(cfg->icon_fallback, val, sizeof(cfg->icon_fallback) - 1);
    else if (strcasecmp(key, "show_letter_fallback") == 0)
      cfg->show_letter_fallback =
          (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
  }
  /* Font */
  else if (strcasecmp(section, "font") == 0) {
    if (strcasecmp(key, "family") == 0)
      strncpy(cfg->font_family, val, sizeof(cfg->font_family) - 1);
    else if (strcasecmp(key, "weight") == 0)
      strncpy(cfg->font_weight, val, sizeof(cfg->font_weight) - 1);
    else if (strcasecmp(key, "title_size") == 0)
      cfg->title_size = atoi(val);
    else if (strcasecmp(key, "icon_letter_size") == 0)
      cfg->icon_letter_size = atoi(val);
  }
}

/* --- Parse an INI file --- */
static int parse_ini_file(const char *path, Config *cfg, char *theme_name_out,
                          size_t theme_name_size) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  LOG("Loading: %s", path);
  char line[512];
  char current_section[64] = "general";

  while (fgets(line, sizeof(line), f)) {
    char *start = trim(line);
    if (*start == ';' || *start == '#' || *start == '\0')
      continue;

    /* Section [section] */
    if (*start == '[') {
      char *end = strchr(start, ']');
      if (end) {
        *end = '\0';
        strncpy(current_section, start + 1, sizeof(current_section) - 1);
      }
      continue;
    }

    /* Key = Value */
    char *eq = strchr(start, '=');
    if (eq) {
      *eq = '\0';
      char *key = trim(start);
      char *val = trim(eq + 1);

      /* Special: Theme name reference */
      if (strcasecmp(current_section, "theme") == 0 &&
          strcasecmp(key, "name") == 0) {
        if (theme_name_out && theme_name_size > 0) {
          strncpy(theme_name_out, val, theme_name_size - 1);
          theme_name_out[theme_name_size - 1] = '\0';
        }
        continue; /* Don't apply theme name as a color */
      }

      apply_value(cfg, current_section, key, val);
    }
  }

  fclose(f);
  return 0;
}

/* --- Load Theme File --- */
static int load_theme(const char *theme_name, Config *cfg) {
  if (!theme_name || theme_name[0] == '\0')
    return -1;

  const char *home = getenv("HOME");
  char path[1024];

  /* Search Order: User themes first, then system */
  const char *search_paths[] = {"%s/.config/snappy-switcher/themes/%s",
                                "/usr/share/snappy-switcher/themes/%s",
                                "/usr/local/share/snappy-switcher/themes/%s",
                                NULL};

  for (int i = 0; search_paths[i]; i++) {
    if (strstr(search_paths[i], "%s/.config")) {
      if (!home)
        continue;
      snprintf(path, sizeof(path), search_paths[i], home, theme_name);
    } else {
      snprintf(path, sizeof(path), search_paths[i], theme_name);
    }

    if (access(path, R_OK) == 0) {
      LOG("Loading theme: %s", path);
      return parse_ini_file(path, cfg, NULL, 0);
    }
  }

  LOG("Theme not found: %s (using defaults)", theme_name);
  return -1;
}

/* --- Safe recursive mkdir (no shell execution) --- */
static int mkdir_recursive(const char *path, mode_t mode) {
  char tmp[1024];
  char *p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (len == 0)
    return -1;
  if (tmp[len - 1] == '/')
    tmp[len - 1] = '\0';

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }
  return (mkdir(tmp, mode) < 0 && errno != EEXIST) ? -1 : 0;
}

/* --- Create Default Config File (Self-Healing) --- */
static void create_default_config(const char *path) {
  /* Create parent directories if needed */
  char dir_path[1024];
  strncpy(dir_path, path, sizeof(dir_path) - 1);
  dir_path[sizeof(dir_path) - 1] = '\0';

  /* Find last slash to get directory */
  char *last_slash = strrchr(dir_path, '/');
  if (last_slash) {
    *last_slash = '\0';
    /* Create config directory safely (no shell execution) */
    if (mkdir_recursive(dir_path, 0755) < 0 && errno != EEXIST) {
      LOG("Failed to create config directory: %s", dir_path);
    }
  }

  FILE *f = fopen(path, "w");
  if (!f) {
    LOG("Failed to create config file: %s", path);
    return;
  }

  /* Write minimal self-documenting default config */
  fprintf(f, "# Snappy Switcher Configuration\n");
  fprintf(f, "# Auto-generated default config\n\n");
  fprintf(f, "[general]\n");
  fprintf(f, "# mode = context | overview\n");
  fprintf(f, "mode = context\n\n");
  fprintf(f, "# filter = none | workspace:current | monitor:current | !workspace:-1\n");
  fprintf(f, "filter = !workspace:-1\n\n");
  fprintf(f, "[theme]\n");
  fprintf(f, "# Available: snappy-slate, tokyo-night, catppuccin-mocha,\n");
  fprintf(f, "#            dracula, nord, gruvbox-dark, rose-pine, etc.\n");
  fprintf(f, "name = snappy-slate.ini\n\n");
  fprintf(f, "[icons]\n");
  fprintf(f, "theme = Tela-dracula\n");
  fprintf(f, "fallback = Tela-circle-dracula\n");
  fprintf(f, "show_letter_fallback = true\n\n");
  fprintf(f, "# See config.ini.example for all available options.\n");

  fclose(f);
  LOG("Missing config.ini. Created default at: %s", path);
}

/* --- Main Config Loader --- */
Config *load_config_from(const char *path) {
  Config *cfg = malloc(sizeof(Config));
  if (!cfg)
    return NULL;
  memset(cfg, 0, sizeof(Config));

  set_defaults(cfg);

  char config_path[1024];
  bool use_default_search = (path == NULL || path[0] == '\0');

  if (use_default_search) {
    const char *home = getenv("HOME");
    if (!home)
      return cfg;
    snprintf(config_path, sizeof(config_path),
             "%s/.config/snappy-switcher/config.ini", home);
    path = config_path;
  } else {
    strncpy(config_path, path, sizeof(config_path) - 1);
    config_path[sizeof(config_path) - 1] = '\0';
  }

  /* First Pass: Read config and extract theme name */
  char theme_name[256] = "";
  if (parse_ini_file(path, cfg, theme_name, sizeof(theme_name)) < 0) {
    if (use_default_search) {
      /* Try system config */
      if (parse_ini_file("/etc/xdg/snappy-switcher/config.ini", cfg,
                         theme_name, sizeof(theme_name)) < 0) {
        create_default_config(config_path);
        strncpy(theme_name, "snappy-slate.ini", sizeof(theme_name) - 1);
        LOG("Using defaults with snappy-slate theme");
      }
    } else {
      LOG("Config file not found: %s (using defaults)", path);
      strncpy(theme_name, "snappy-slate.ini", sizeof(theme_name) - 1);
    }
  }

  if (theme_name[0] != '\0')
    load_theme(theme_name, cfg);

  parse_ini_file(path, cfg, NULL, 0);

  return cfg;
}

Config *load_config(void) {
  return load_config_from(NULL);
}

Config *get_default_config(void) {
  Config *cfg = malloc(sizeof(Config));
  if (cfg) {
    memset(cfg, 0, sizeof(Config));
    set_defaults(cfg);
  }
  return cfg;
}

void free_config(Config *cfg) { free(cfg); }

void color_to_rgba(uint32_t color, double *r, double *g, double *b, double *a) {
  *r = ((color >> 24) & 0xFF) / 255.0;
  *g = ((color >> 16) & 0xFF) / 255.0;
  *b = ((color >> 8) & 0xFF) / 255.0;
  *a = (color & 0xFF) / 255.0;
}

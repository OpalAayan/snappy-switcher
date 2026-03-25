# Snappy Switcher Themes

This directory contains color themes for Snappy Switcher.

## Available Themes

| Theme | Description |
|-------|-------------|
| `snappy-slate.ini` | Default dark theme (Catppuccin-inspired) |
| `catppuccin-mocha.ini` | Catppuccin Mocha |
| `catppuccin-latte.ini` | Catppuccin Latte (light theme) |
| `nord.ini` | Nord color scheme |
| `nordic.ini` | Nordic (Arctic Aurora) |
| `dracula.ini` | Dracula theme |
| `gruvbox-dark.ini` | Gruvbox Dark |
| `grovestorm.ini` | Grovestorm (Everforest × Gruvbox) |
| `tokyo-night.ini` | Tokyo Night |
| `cyberpunk.ini` | Cyberpunk 2077 |
| `rose-pine.ini` | Rosé Pine |
| `liquid-glassW.ini` | Liquid Glass — White frosted acrylic (requires blur) |
| `liquid-glassB.ini` | Liquid Glass — Black smoked glass (requires blur) |

## How to Use

1. Edit your `~/.config/snappy-switcher/config.ini`
2. Set the theme name:
   ```ini
   [theme]
   name = catppuccin-mocha.ini
   ```
3. Restart the daemon:
   ```bash
   snappy-switcher quit
   snappy-switcher --daemon
   ```

## Liquid Glass Setup

The Liquid Glass themes render as translucent overlays and require compositor blur:

```ini
# In ~/.config/snappy-switcher/config.ini
[theme]
name = liquid-glassW.ini
```

```conf
# In ~/.config/hypr/hyprland.conf
layerrule = blur, snappy-switcher
layerrule = ignorealpha 0.01, snappy-switcher
```

## Theme Locations

Themes are searched in order:
1. `~/.config/snappy-switcher/themes/` (user themes)
2. `/usr/share/snappy-switcher/themes/` (system themes)
3. `/usr/local/share/snappy-switcher/themes/` (local install)

## Creating Custom Themes

Create a new `.ini` file with:

```ini
# My Custom Theme

[colors]
background = #1e1e2eff
card_bg = #313244ff
card_selected = #45475aff
text_color = #cdd6f4ff
subtext_color = #a6adc8ff
border_color = #89b4faff
bundle_bg = #313244ff
badge_bg = #89b4faff
badge_text_color = #cdd6f4ff
workspace_color = #cdd6f4ff
```

All colors use 8-character `#RRGGBBAA` hex codes. The last two digits control transparency (e.g., `ff` = opaque, `80` = 50%, `00` = invisible).

Save it to `~/.config/snappy-switcher/themes/my-theme.ini` and reference it in your config.

## Overriding Colors

You can override specific colors in your `config.ini` after setting a theme:

```ini
[theme]
name = nord.ini
border_color = #ff0000ff  # Override just the border
```

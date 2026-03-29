<div align="center">

# ⚙️ Snappy Switcher Configuration

*Customize every aspect of your window switcher*

</div>

---

## 📍 Configuration Location

```
~/.config/snappy-switcher/config.ini
```

> 💡 **Quick Setup:** Run `snappy-install-config` to create the config file and install themes automatically.

---

## 🚀 Quick Start

```ini
[general]
mode = context

[theme]
name = catppuccin-mocha.ini

[icons]
theme = Papirus-Dark
fallback = hicolor
```

---

## 📋 Configuration Sections

```mermaid
%%{
  init: {
    'theme': 'base',
    'themeVariables': {
      'primaryColor': '#a8b3dc',
      'primaryTextColor': '#0f172a',
      'primaryBorderColor': '#4338ca',
      'lineColor': '#101274',
      'secondaryColor': '#b953d2',
      'tertiaryColor': '#61b3e0'
    }
  }
}%%
mindmap
  root((config.ini))
    general
      mode
    theme
      colors
      borders
      corners
    layout
      card size
      spacing
      grid
    icons
      theme
      fallback
    font
      family
      sizes
```

---

## 🎯 [general] — Mode Settings

Control how windows are displayed and grouped.

```mermaid
flowchart LR
    subgraph Overview["mode = overview"]
        O1["Window 1"]
        O2["Window 2"]
        O3["Window 3"]
        O4["Window 4"]
        O5["Window 5"]
    end
    
    subgraph Context["mode = context"]
        C1["App × 3"]
        C2["Browser"]
        C3["Terminal"]
    end
    
    style O1 fill:#303446,stroke:#f5c2e7,color:#f5c2e7,stroke-width:2px
    style O2 fill:#303446,stroke:#f5c2e7,color:#f5c2e7,stroke-width:2px
    style O3 fill:#303446,stroke:#f5c2e7,color:#f5c2e7,stroke-width:2px
    style O4 fill:#303446,stroke:#f5c2e7,color:#f5c2e7,stroke-width:2px
    style O5 fill:#303446,stroke:#f5c2e7,color:#f5c2e7,stroke-width:2px
    
    style C1 fill:#303446,stroke:#cba6f7,color:#cba6f7,stroke-width:2px
    style C2 fill:#303446,stroke:#a6e3a1,color:#a6e3a1,stroke-width:2px
    style C3 fill:#303446,stroke:#89dceb,color:#89dceb,stroke-width:2px
```

| Key | Values | Default | Description |
|-----|--------|---------|-------------|
| `mode` | `overview`, `context` | `context` | Window grouping mode |
| `dismiss_modifier` | `alt`, `super`, `shift`, `control`, or comma-separated | `alt` | Modifier(s) that dismiss the switcher when released. Must match your Hyprland keybinding. |
| `show_workspace_badge` | `true`, `false` | `true` | Show workspace indicator badge on each card |
| `follow_monitor` | `true`, `false` | `false` | Panel follows the focused monitor |

### Mode Comparison

| Mode | Behavior | Best For |
|------|----------|----------|
| **Overview** | Shows all windows individually | Simple workflows, few windows |
| **Context** | Groups tiled windows by workspace + app class | Power users, many windows |

```ini
[general]
mode = context  # Enable intelligent grouping
```

### 🏷️ Workspace Badge

When `show_workspace_badge = true` (the default), each card displays a small pill-shaped tag in its bottom-left corner that tells you **which workspace the window lives on** at a glance.

**How it works under the hood:**

The renderer inspects each window's `workspace_id` and `workspace_name` (pulled from Hyprland's IPC) and formats a short tag using these rules:

| Workspace Type | Tag Format | Example | When |
|----------------|------------|---------|------|
| **Numbered** (standard) | `[N]` | `[3]` | Workspace name is just a number |
| **Numbered + Floating** | `[F:N]` | `[F:3]` | Floating window on workspace 3 |
| **Named** (1st occurrence) | `[L]` | `[M]` | First named workspace starting with "M" |
| **Named** (2nd+ occurrence) | `[L:n]` | `[M:1]` | Second named workspace starting with "M" |
| **Named + Floating** | `[L:F]` | `[M:F]` | Floating window on a named workspace |
| **Special** | `[S]` | `[S]` | Hyprland special workspace (scratchpad, etc.) |
| **Special + Floating** | `[S:F]` | `[S:F]` | Floating window on a special workspace |

> 💡 **In plain English:** The badge shows a compact label so you always know *where* a window belongs ~ a number for standard workspaces, a letter for named ones, and `S` for special/scratchpad workspaces. If the window is floating, it gets an extra `F:` prefix so you can tell it apart from tiled windows at a glance.

```ini
[general]
show_workspace_badge = true   # Toggle badge visibility
```

---

## 🎨 [theme] — Colors & Styling

All colors use hex format: `#RRGGBB` or `#RRGGBBAA` (with alpha transparency)

```mermaid
graph TB
    subgraph Card["Window Card Anatomy"]
        BG["card_bg\n#1e1e2e"]
        SEL["card_selected\n#2a2a3c"]
        TXT["text_color\n#cdd6f4"]
        SUB["subtext_color\n#6c7086"]
        BOR["border_color\n#ca9ee6"]
    end
    
    style BG fill:#1e1e2e,stroke:#45475a,color:#cdd6f4
    style SEL fill:#2a2a3c,stroke:#45475a,color:#cdd6f4
    style TXT fill:#cdd6f4,stroke:#1e1e2e,color:#1e1e2e
    style SUB fill:#6c7086,stroke:#1e1e2e,color:#1e1e2e
    style BOR fill:#ca9ee6,stroke:#1e1e2e,color:#1e1e2e
```

### Color Options

| Key | Default | Description |
|-----|---------|-------------|
| `name` | `snappy-slate.ini` | Theme file to load |
| `background` | `#1e1e2eff` | Main overlay background |
| `card_bg` | `#313244ff` | Card background color |
| `card_selected` | `#45475aff` | Selected card background |
| `border_color` | `#89b4faff` | Selection border (accent) |
| `text_color` | `#cdd6f4ff` | Primary text color |
| `subtext_color` | `#a6adc8ff` | Secondary/dimmed text |
| `bundle_bg` | `#313244ff` | Context-mode stacked card background |
| `badge_bg` | `#89b4faff` | Group count & workspace badge background |
| `badge_text_color` | `#cdd6f4ff` | Badge text color |

### Border & Corner Options

| Key | Default | Description |
|-----|---------|-------------|
| `border_width` | `2` | Border thickness (px) |
| `corner_radius` | `12` | Rounded corner radius (px) |

```ini
[theme]
name = catppuccin-mocha.ini

# Override specific colors (optional)
background = #11111bff
card_bg = #1e1e2eff
card_selected = #2a2a3cff
border_color = #ca9ee6ff
text_color = #cdd6f4ff
subtext_color = #6c7086ff
bundle_bg = #1e1e2eff
badge_bg = #89b4faff
badge_text_color = #cdd6f4ff
border_width = 2
corner_radius = 12
```

### 🎭 Available Themes

<table>
<tr>
<td align="center">🌙</td>
<td><code>snappy-slate.ini</code></td>
<td>Default dark theme</td>
</tr>
<tr>
<td align="center">🍫</td>
<td><code>catppuccin-mocha.ini</code></td>
<td>Catppuccin Mocha</td>
</tr>
<tr>
<td align="center">☕</td>
<td><code>catppuccin-latte.ini</code></td>
<td>Catppuccin Latte (light)</td>
</tr>
<tr>
<td align="center">🫐</td>
<td><code>catppuccin-frappe.ini</code></td>
<td>Catppuccin Frappé</td>
</tr>
<tr>
<td align="center">🌌</td>
<td><code>tokyo-night.ini</code></td>
<td>Tokyo Night</td>
</tr>
<tr>
<td align="center">❄️</td>
<td><code>nord.ini</code></td>
<td>Nord</td>
</tr>
<tr>
<td align="center">🧛</td>
<td><code>dracula.ini</code></td>
<td>Dracula</td>
</tr>
<tr>
<td align="center">🪵</td>
<td><code>gruvbox-dark.ini</code></td>
<td>Gruvbox Dark</td>
</tr>
<tr>
<td align="center">🌹</td>
<td><code>rose-pine.ini</code></td>
<td>Rosé Pine</td>
</tr>
<tr>
<td align="center">🌲</td>
<td><code>nordic.ini</code></td>
<td>Nordic variant</td>
</tr>
<tr>
<td align="center">🌿</td>
<td><code>grovestorm.ini</code></td>
<td>Grovestorm</td>
</tr>
<tr>
<td align="center">🤖</td>
<td><code>cyberpunk.ini</code></td>
<td>Cyberpunk</td>
</tr>
<tr>
<td align="center">⚡</td>
<td><code>stormlight.ini</code></td>
<td>Stormlight (yellow Catppuccin-inspired)</td>
</tr>
<tr>
<td align="center">🤍</td>
<td><code>liquid-glassW.ini</code></td>
<td>Liquid Glass White (frosted acrylic, needs blur)</td>
</tr>
<tr>
<td align="center">🖤</td>
<td><code>liquid-glassB.ini</code></td>
<td>Liquid Glass Black (smoked glass, needs blur)</td>
</tr>
</table>

---

## 📐 [layout] — Dimensions & Spacing

```mermaid
graph TB
    subgraph Grid["Layout Visualization"]
        subgraph Row1["Row 1"]
            C1["Card"]
            C2["Card"]
            C3["Card"]
        end
        subgraph Row2["Row 2"]
            C4["Card"]
            C5["Card"]
        end
    end
    
    P["← padding →"]
    G["← card_gap →"]
    
    style C1 fill:#313244,stroke:#89b4fa,color:#cdd6f4
    style C2 fill:#313244,stroke:#89b4fa,color:#cdd6f4
    style C3 fill:#313244,stroke:#89b4fa,color:#cdd6f4
    style C4 fill:#313244,stroke:#89b4fa,color:#cdd6f4
    style C5 fill:#2a2a3c,stroke:#cba6f7,color:#cdd6f4
```

### Card Dimensions

| Key | Default | Description |
|-----|---------|-------------|
| `card_width` | `160` | Card width in pixels |
| `card_height` | `140` | Card height in pixels |

### Spacing

| Key | Default | Description |
|-----|---------|-------------|
| `card_gap` | `8` | Gap between cards (px) |
| `padding` | `16` | Window padding (px) |

### Grid & Icons

| Key | Default | Description |
|-----|---------|-------------|
| `max_cols` | `5` | Maximum columns before wrap |
| `icon_size` | `56` | App icon size (px) |
| `icon_radius` | `14` | Icon corner radius (px) |

```ini
[layout]
card_width = 160
card_height = 140
card_gap = 8
padding = 16
max_cols = 5
icon_size = 56
icon_radius = 14
```

---

## 🖼️ [icons] — Icon Theme

```mermaid
flowchart LR
    APP["App Class"]
    APP --> T1["🎨 Primary Theme"]
    T1 -->|not found| T2["🔄 Fallback Theme"]
    T2 -->|not found| T3["🔤 Letter Icon"]
    
    T1 -->|found| ICON["✅ Display Icon"]
    T2 -->|found| ICON
    T3 --> ICON

    style T1 fill:#89b4fa,stroke:#1e1e2e,color:#1e1e2e
    style T2 fill:#a6e3a1,stroke:#1e1e2e,color:#1e1e2e
    style T3 fill:#f9e2af,stroke:#1e1e2e,color:#1e1e2e
```

| Key | Default | Description |
|-----|---------|-------------|
| `theme` | `Tela-dracula` | Primary icon theme |
| `fallback` | `Tela-circle-dracula` | Fallback theme |
| `show_letter_fallback` | `true` | Show letter if no icon found |

### Popular Icon Themes

| Theme | Style |
|-------|-------|
| `Papirus` | Flat, colorful |
| `Papirus-Dark` | Flat, dark variant |
| `Tela` | Modern, gradient |
| `Numix` | Flat, circle |
| `Adwaita` | GNOME default |
| `hicolor` | System fallback |

```ini
[icons]
theme = Papirus-Dark
fallback = hicolor
show_letter_fallback = true
```

---

## ✏️ [font] — Typography

| Key | Default | Description |
|-----|---------|-------------|
| `family` | `Sans` | Font family name |
| `weight` | `Bold` | Font weight |
| `title_size` | `10` | Title font size (px) |
| `icon_letter_size` | `24` | Fallback letter size (px) |

### Recommended Fonts

| Font | Style |
|------|-------|
| `Sans` | System default |
| `Inter` | Clean, modern |
| `Roboto` | Material Design |
| `JetBrainsMono Nerd Font` | Monospace + icons |
| `SF Pro Display` | macOS-like |

```ini
[font]
family = Inter
weight = Bold
title_size = 10
icon_letter_size = 24
```

---

## 📄 Complete Example

<details>
<summary>📋 <b>Click to expand full config.ini</b></summary>

```ini
# ~/.config/snappy-switcher/config.ini
# ═══════════════════════════════════════════════════════════════════

[general]
mode = context
show_workspace_badge = true

[theme]
name = catppuccin-mocha.ini
background = #11111bff
card_bg = #1e1e2eff
card_selected = #2a2a3cff
border_color = #ca9ee6ff
text_color = #cdd6f4ff
subtext_color = #6c7086ff
bundle_bg = #1e1e2eff
badge_bg = #ca9ee6ff
badge_text_color = #1e1e2eff
border_width = 2
corner_radius = 12

[layout]
card_width = 160
card_height = 140
card_gap = 8
padding = 16
max_cols = 5
icon_size = 56
icon_radius = 14

[icons]
theme = Papirus-Dark
fallback = hicolor
show_letter_fallback = true

[font]
family = Inter
weight = Bold
title_size = 10
icon_letter_size = 24
```

</details>

---

## ⌨️ Hyprland Keybindings

Add these to `~/.config/hypr/hyprland.conf`. Use any modifier combo; set `dismiss_modifier` in config to match so the switcher hides when you release the key.

```bash
# Start daemon on login
exec-once = snappy-switcher --daemon

# Alt-Tab (default) — set dismiss_modifier = alt
bind = ALT, Tab, exec, snappy-switcher next
bind = ALT SHIFT, Tab, exec, snappy-switcher prev

# Or Super-Tab — set dismiss_modifier = super
# bind = SUPER, Tab, exec, snappy-switcher next
# bind = SUPER SHIFT, Tab, exec, snappy-switcher prev
```

### Optional Keybindings

```bash
# Toggle visibility
bind = SUPER, Tab, exec, snappy-switcher toggle

# Quick hide
bind = , Escape, exec, snappy-switcher hide
```

---

<div align="center">

**[← Back to README](../README.md)** · **[Architecture Guide →](ARCHITECTURE.md)**

</div>

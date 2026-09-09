# Orbiter

A lightweight Wayland application launcher with search and system icons.
Built with C++17, Wayland (layer-shell), Cairo, and Pango.

---

## Features

- **Fast search** — real-time filtering across desktop entry names, exec fields,
  and generic names as you type
- **System icons** — full XDG Icon Theme Spec resolution (SVG via librsvg, PNG)
- **Scrollable results** — keyboard navigation with arrows, page up/down,
  home/end; scroll arrows indicate off-screen entries
- **Dark theme** — TOML-configurable colors with DE-aware auto-detection
- **Network & battery metrics** — optional real-time speedometer and power
  status in the corner
- **Background detachment** — launched processes are fully detached

---

## Dependencies

| Package           | Debian / Ubuntu              |
|-------------------|------------------------------|
| wayland-client    | `libwayland-dev`             |
| wayland-protocols | `wayland-protocols`          |
| cairo             | `libcairo2-dev`              |
| pango             | `libpango1.0-dev`            |
| librsvg           | `librsvg2-dev`               |
| xkbcommon         | `libxkbcommon-dev`           |

### Quick install

```bash
# Debian / Ubuntu
sudo apt install libwayland-dev wayland-protocols libcairo2-dev \
  libpango1.0-dev librsvg2-dev libxkbcommon-dev
```

The build generates the `wlr-layer-shell-unstable-v1` and `xdg-shell` client
code with `wayland-scanner` (shipped with `libwayland-dev`), so no extra
protocol packages are needed.

---

## Build & Install

```bash
make release
sudo make install
```

The binary is placed at `/usr/local/bin/orbiter`.

---

## Usage

Bind to a key in your Wayland compositor (requires a compositor with
`wlr-layer-shell` support, e.g. sway):

```conf
# sway
bindsym Mod4+space exec orbiter
```

- **Type** to filter applications
- **Arrow keys**, **PgUp/PgDn**, **Home/End** to navigate
- **Enter** to launch, **Esc** to dismiss
- **Tab** to autocomplete from the first match
- **Ctrl+Backspace** to delete the last word

---

## Configuration

Create `~/.config/Orbiter/config.toml`:

```toml
terminal = "foot"
max_results = 20
show_metrics = true
```

Theme colors can be customized in `~/.config/Orbiter/config.toml`:

```toml
bg_color = "#0b081a"
text_color = "#ffffff"
accent_color = "#00e5ff"
```

DE-specific files (`hyprland.conf`, `sway.conf`, etc.) override the base config
when the matching desktop environment is detected.

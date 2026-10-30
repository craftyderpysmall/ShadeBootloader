# Theming Shade

A theme is a folder on the ESP:

```
\EFI\shade\themes\<name>\
    theme.conf          tokens (required, may be nearly empty)
    icons\*.png         optional icon overrides
    fonts\*.ttf         optional fonts
    background.png      optional wallpaper
```

Select it with `theme = <name>` in `shade.conf`. Every theme is layered on top
of the built-in **Neutral** theme, so a theme only needs the keys it changes.
`themes/daylight/theme.conf` is a complete light theme in about 20 lines.

## Tokens

Sizes are pixels at 1× and scale with the display; colors are `#rrggbb` or
`#rrggbbaa`.

| Section        | Keys |
|----------------|------|
| `[theme]`      | `name`, `author`, `description` |
| `[palette]`    | `bg`, `surface`, `raised`, `border`, `border-strong`, `fg`, `fg-muted`, `fg-subtle`, `primary`, `primary-fg`, `danger`, `success`, `warning`, `info` |
| `[shape]`      | `radius-card`, `radius-control`, `radius-pill` |
| `[spacing]`    | `gap` (inner), `pad` (outer / between tiles) |
| `[type]`       | `font-regular`, `font-semibold` (`builtin:Inter-Regular` or a path relative to the theme), `size-title`, `size-label`, `size-body`, `size-small` |
| `[layout]`     | `tile-width`, `tile-height`, `icon-size` |
| `[shadow]`     | `blur`, `offset-y`, `color` |
| `[motion]`     | `enter-ms` (decelerate), `exit-ms` (accelerate), `spring-stiffness`, `spring-damping` (selection glide; 0.78 ≈ 2% settle) |
| `[background]` | `glow` (radial centre color), `vignette` (0–1), `image` (path, cover-fit) |
| `[icons]`      | `tint` — `true`: icons are white masks recolored with `fg`/`fg-muted`; `false`: drawn as-is |

## How tokens map to the screen

- **Ground**: `bg`, lifted towards `glow` in the centre.
- **Selection card**: `surface` fill, 1px `border-strong` (or `border` when
  focus is on the power bar), soft shadow. It glides between tiles on the spring.
- **Tiles**: the icon and title blend from `fg-muted` to `fg` as the card
  arrives; subtitles use `fg-subtle`.
- **Primary action**: the round start button inside the card uses `primary`
  and `primary-fg`. It's the only inverted element on screen.
- **Power bar**: a floating `surface` pill with ghost buttons; hover/focus uses `raised`.
- **Toasts** use `danger` for their status dot.

## Icons

Shade looks icons up by name with a fallback chain, first in your theme's
`icons\` folder and then in the built-in set:

| Name | Used for |
|------|----------|
| `os-windows`, `os-macos`, `os-linux`, `os-generic` | operating systems |
| `os-<distro>` (e.g. `os-ubuntu`, `os-fedora`, `os-arch`) | per-distro art, falls back to `os-linux` |
| `drive-usb`, `drive-disk`, `drive-cd`, `drive-net` | other bootable drives |
| `power`, `restart`, `firmware`, `arrow-right` | power bar & start button |
| `cursor` | mouse pointer (not tinted) |

Any square PNG with alpha works; 256×256 is plenty. Shade resamples once at
startup, so large icons cost nothing per frame. The built-in set is generated
by `tools/mkicons.py`.

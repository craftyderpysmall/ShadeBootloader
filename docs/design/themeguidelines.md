  THEME: "NEUTRAL"  — monochrome dark, shadcn/ui-inspired

  PALETTE (single source of truth — every themed app uses ONLY these values)
  ---------------------------------------------------------------------------
  Surfaces
    bg            #0a0a0a   desktop ground, bar background, terminal-free space
    surface       #171717   cards: windows, terminal, popups, launcher, notifs
    raised        #212121   inputs, hover states, search fields
    border        #262626   1px card borders, dividers, selected list rows
    border-strong #404040   focused borders, scrollbar thumbs, pressed states

  Text
    fg            #fafafa   primary text, headings, icons
    fg-muted      #a1a1a1   secondary text, descriptions, inactive labels
    fg-subtle     #737373   placeholders, hints, disabled, timestamps

  Primary (inverted "Get Started" pill)
    primary       #fafafa   background of the ONE primary action per surface
    primary-fg    #171717   text on primary

  Chart / data scale (light → dark, for graphs & bars)
    #e5e5e5  #a3a3a3  #737373  #525252  #404040

  Semantic (desaturated — dots, badges, terminal ANSI only; never large fills)
    danger  #e5787a   success #8ccf9a   warning #e6c879
    info    #82a9e0   magenta #b49ae6   cyan    #7cc6cf

  GUIDELINES
  ---------------------------------------------------------------------------
  1. Monochrome first. Color is information, not decoration: use semantic
     colors only for state (error, low battery, urgent notif, git dirty).
  2. Layering = lightness. bg (#0a0a0a) < surface (#171717) < raised (#212121).
     Never place a darker surface on top of a lighter one.
  3. Every card gets a 1px `border` (#262626). Focus/active steps up to
     `border-strong` or fg-muted — never a colored or gradient border.
  4. Shape: cards/windows 16px radius, inner controls (inputs, list rows)
     10px, buttons/badges/workspace pills fully rounded (999px).
  5. One primary per surface. Only the main action (active workspace, power
     button, selected tab) gets the inverted white pill. Everything else is
     ghost (transparent, fg-muted text, `raised` on hover) or outline.
  6. Type: Inter (UI) / JetBrainsMono Nerd Font (code, icons). Headings
     semibold fg; body regular fg-muted. Sizes: 11 body, 12-13 titles.
  7. Spacing on a 4px grid: 8 inner gap, 16 outer gap / card padding.
  8. Motion is quick and purposeful. Enter on `decel` (~250-350ms),
     exit on `accel` at ~half the time. Scale changes stay within 94-100%.
     Pointer-driven motion (move/resize) uses the `settle` spring: a ~2%
     settle, never a visible bounce. Slides travel ≤12% of the screen.
  9. Depth comes from soft, large, offset-down black shadows + light blur
     on translucent layers (bar, launcher, notifications) — not glow.

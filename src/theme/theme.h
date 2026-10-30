/*
 * Themes: palette, shape, type, layout, shadow and motion tokens plus icons.
 * The built-in NEUTRAL theme is always loaded first; a theme on the ESP at
 * \EFI\shade\themes\<name>\theme.conf overrides any subset of it.
 */
#ifndef SHADE_THEME_H
#define SHADE_THEME_H

#include <stdbool.h>
#include "gfx/gfx.h"
#include "gfx/font.h"

typedef struct {
    char name[48];

    Color bg, surface, raised, border, border_strong;
    Color fg, fg_muted, fg_subtle, primary, primary_fg;
    Color danger, success, warning, info;

    float radius_card, radius_control, radius_pill;
    float gap, pad;
    float size_title, size_label, size_body, size_small;
    float tile_w, tile_h, icon_size;

    float shadow_blur, shadow_y;
    Color shadow;

    float enter_ms, exit_ms;
    float spring_k, spring_zeta;

    Color glow;
    float vignette;
    char  bg_image[128];
    bool  icon_tint;

    char  font_regular[128], font_semibold[128];
    Font *regular, *semibold;

    /* where theme files live (ESP), empty for built-in only */
    void *root;          /* EFI_FILE_PROTOCOL * of the volume */
    char  dir[128];
} Theme;

void   theme_load(Theme *t, void *root, const char *name);
/* names: comma-separated fallback chain, e.g. "os-ubuntu,os-linux,os-generic" */
Image *theme_icon(Theme *t, const char *names);
Image *theme_background(Theme *t);   /* NULL if none */

#endif

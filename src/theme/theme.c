#include <stdlib.h>
#include <string.h>
#include "theme.h"
#include "core/assets.h"
#include "core/config.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"

#define ICON_CACHE 32

typedef struct {
    char name[48];
    Image *img;      /* NULL = known missing */
} IconSlot;

static IconSlot icons[ICON_CACHE];
static int nicons;

static void set_defaults(Theme *t)
{
    memset(t, 0, sizeof(*t));
    str_copy(t->name, "Neutral", sizeof(t->name));
    t->bg = 0xff0a0a0a; t->surface = 0xff171717; t->raised = 0xff212121;
    t->border = 0xff262626; t->border_strong = 0xff404040;
    t->fg = 0xfffafafa; t->fg_muted = 0xffa1a1a1; t->fg_subtle = 0xff737373;
    t->primary = 0xfffafafa; t->primary_fg = 0xff171717;
    t->danger = 0xffe5787a; t->success = 0xff8ccf9a; t->warning = 0xffe6c879; t->info = 0xff82a9e0;
    t->radius_card = 16; t->radius_control = 10; t->radius_pill = 999;
    t->gap = 8; t->pad = 16;
    t->size_title = 24; t->size_label = 15; t->size_body = 13; t->size_small = 12;
    t->tile_w = 168; t->tile_h = 196; t->icon_size = 88;
    t->shadow_blur = 40; t->shadow_y = 16; t->shadow = 0x99000000;
    t->enter_ms = 320; t->exit_ms = 160; t->spring_k = 420; t->spring_zeta = 0.78f;
    t->glow = 0xff141414; t->vignette = 0.6f;
    t->icon_tint = true;
    str_copy(t->font_regular, "builtin:Inter-Regular", sizeof(t->font_regular));
    str_copy(t->font_semibold, "builtin:Inter-SemiBold", sizeof(t->font_semibold));
}

static void on_key(void *user, const char *sec, const char *key, const char *v)
{
    Theme *t = user;
    #define COL(k, f) if (str_eq_ci(key, k)) { t->f = config_color(v, t->f); return; }
    #define NUM(k, f) if (str_eq_ci(key, k)) { t->f = config_float(v, t->f); return; }
    #define STR(k, f) if (str_eq_ci(key, k)) { str_copy(t->f, v, sizeof(t->f)); return; }
    if (str_eq_ci(sec, "theme")) {
        STR("name", name);
    } else if (str_eq_ci(sec, "palette")) {
        COL("bg", bg) COL("surface", surface) COL("raised", raised)
        COL("border", border) COL("border-strong", border_strong)
        COL("fg", fg) COL("fg-muted", fg_muted) COL("fg-subtle", fg_subtle)
        COL("primary", primary) COL("primary-fg", primary_fg)
        COL("danger", danger) COL("success", success) COL("warning", warning) COL("info", info)
    } else if (str_eq_ci(sec, "shape")) {
        NUM("radius-card", radius_card) NUM("radius-control", radius_control) NUM("radius-pill", radius_pill)
    } else if (str_eq_ci(sec, "spacing")) {
        NUM("gap", gap) NUM("pad", pad)
    } else if (str_eq_ci(sec, "type")) {
        STR("font-regular", font_regular) STR("font-semibold", font_semibold)
        NUM("size-title", size_title) NUM("size-label", size_label)
        NUM("size-body", size_body) NUM("size-small", size_small)
    } else if (str_eq_ci(sec, "layout")) {
        NUM("tile-width", tile_w) NUM("tile-height", tile_h) NUM("icon-size", icon_size)
    } else if (str_eq_ci(sec, "shadow")) {
        NUM("blur", shadow_blur) NUM("offset-y", shadow_y) COL("color", shadow)
    } else if (str_eq_ci(sec, "motion")) {
        NUM("enter-ms", enter_ms) NUM("exit-ms", exit_ms)
        NUM("spring-stiffness", spring_k) NUM("spring-damping", spring_zeta)
    } else if (str_eq_ci(sec, "background")) {
        COL("glow", glow) NUM("vignette", vignette) STR("image", bg_image)
    } else if (str_eq_ci(sec, "icons")) {
        if (str_eq_ci(key, "tint")) { t->icon_tint = config_bool(v, t->icon_tint); return; }
    }
    LOG("theme: unknown key [%s] %s", sec, key);
    #undef COL
    #undef NUM
    #undef STR
}

/* Read a file relative to the theme dir on the ESP. */
static void *theme_file(Theme *t, const char *rel, size_t *size)
{
    if (!t->root || !t->dir[0])
        return NULL;
    char path[256];
    str_fmt(path, sizeof(path), "%s\\%s", t->dir, rel);
    return file_read_all(t->root, path, size);
}

static Font *load_font(Theme *t, const char *spec, const char *fallback)
{
    if (str_starts_ci(spec, "builtin:")) {
        char name[96];
        str_fmt(name, sizeof(name), "fonts/%s.ttf", spec + 8);
        const Asset *a = asset_find(name);
        if (a)
            return font_load(a->data, a->size);
    } else {
        size_t n;
        void *data = theme_file(t, spec, &n);
        Font *f = data ? font_load(data, (int)n) : NULL;
        if (f)
            return f;
        LOG("theme: font %s not loadable, using built-in", spec);
        free(data);
    }
    const Asset *a = asset_find(fallback);
    return font_load(a->data, a->size);
}

void theme_load(Theme *t, void *root, const char *name)
{
    set_defaults(t);
    const Asset *base = asset_find("neutral/theme.conf");
    if (base)
        config_parse((const char *)base->data, on_key, t);

    t->root = root;
    if (root && name && *name) {
        str_fmt(t->dir, sizeof(t->dir), "\\EFI\\shade\\themes\\%s", name);
        size_t n;
        char *text = theme_file(t, "theme.conf", &n);
        if (text) {
            config_parse(text, on_key, t);
            free(text);
            LOG("theme: loaded '%s' from %s", t->name, t->dir);
        } else {
            LOG("theme: %s not found on disk, using built-in", t->dir);
            if (!str_eq_ci(name, "neutral"))
                t->dir[0] = 0;
        }
    }
    t->regular = load_font(t, t->font_regular, "fonts/Inter-Regular.ttf");
    t->semibold = load_font(t, t->font_semibold, "fonts/Inter-SemiBold.ttf");
}

static Image *load_icon(Theme *t, const char *name)
{
    for (int i = 0; i < nicons; i++)
        if (str_eq_ci(icons[i].name, name))
            return icons[i].img;

    char rel[96];
    str_fmt(rel, sizeof(rel), "icons\\%s.png", name);
    size_t n;
    void *data = theme_file(t, rel, &n);
    Image *img = NULL;
    if (data) {
        img = image_load(data, (int)n);
        free(data);
    }
    if (!img) {
        char key[96];
        str_fmt(key, sizeof(key), "neutral/icons/%s.png", name);
        const Asset *a = asset_find(key);
        if (a)
            img = image_load(a->data, a->size);
    }
    if (nicons < ICON_CACHE) {
        str_copy(icons[nicons].name, name, sizeof(icons[nicons].name));
        icons[nicons++].img = img;
    }
    return img;
}

Image *theme_icon(Theme *t, const char *names)
{
    char buf[160];
    str_copy(buf, names, sizeof(buf));
    char *p = buf;
    while (p && *p) {
        char *comma = strchr(p, ',');
        if (comma)
            *comma = 0;
        Image *img = load_icon(t, str_trim(p));
        if (img)
            return img;
        p = comma ? comma + 1 : NULL;
    }
    return NULL;
}

Image *theme_background(Theme *t)
{
    if (!t->bg_image[0])
        return NULL;
    size_t n;
    void *data = theme_file(t, t->bg_image, &n);
    if (!data)
        return NULL;
    Image *img = image_load(data, (int)n);
    free(data);
    return img;
}

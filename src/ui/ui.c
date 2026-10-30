/*
 * Boot picker: a centred row of entry tiles with a selection card that glides
 * between them on a spring, a floating power bar, toasts and an options sheet.
 *
 * Rendering is retained-mode-lite: every frame each screen region computes a
 * signature of the state it depends on, and only regions whose signature
 * changed are redrawn (clipped) and presented. A static screen costs nothing.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "anim/anim.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"
#include "gfx/font.h"
#include "input/input.h"

#define MAX_VIS 64
#define MAX_BTN 3

typedef enum { FOCUS_ROW, FOCUS_BAR } Focus;
typedef enum { BTN_FIRMWARE, BTN_RESTART, BTN_SHUTDOWN } BtnKind;

enum { R_CLOCK, R_HEADER, R_ROW, R_RING, R_BAR, R_TOAST, R_HINTS, R_SHEET, R_COUNT };

typedef struct {
    Entry *e;
    Image *icon;        /* pre-resampled to the tile's icon size */
} Tile;

typedef struct {
    BtnKind kind;
    const char *label;
    Image *icon;
    Rect r;
} Button;

static struct {
    Theme *t;
    Settings *cfg;
    BootSet *boot;
    Canvas *c;
    Canvas bgc;
    int W, H;
    float s;

    Face *f_title, *f_label, *f_body, *f_small;

    /* layout */
    float tile_w, tile_h, pitch, icon_px;
    float row_x0, row_y, row_w, avail_w;
    float title_base, sub_base;
    float bar_x, bar_y, bar_w, bar_h;

    Tile tiles[MAX_VIS];
    int ntiles;
    int sel;
    bool show_hidden;

    Button btns[MAX_BTN];
    int nbtn, bar_sel;
    int hover_btn, press_btn, press_tile;
    Focus focus;

    Spring plate, scroll;
    Tween enter, row_enter;
    double t_enter;

    /* countdown */
    bool counting;
    double count_start;
    float count_total;
    Entry *default_entry;

    /* toast */
    char toast[160];
    double toast_at;

    /* options sheet */
    bool sheet;
    Tween sheet_tw;
    char sheet_text[512];

    /* pointer */
    bool cursor_on;
    int mx, my;
    Image *cursor;
    Image *arrow;

    /* exit animation: chrome + other tiles fade first, then the chosen tile */
    float fade_chrome, fade_sel;

    uint64_t sig[R_COUNT];
    int last_minute;
    char clock_text[48];
} ui;

/* ---- helpers --------------------------------------------------------- */

static float S(float v) { return v * ui.s; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint64_t mix64(uint64_t h, int64_t v)
{
    h ^= (uint64_t)v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t hash_str(uint64_t h, const char *s)
{
    while (s && *s)
        h = mix64(h, *s++);
    return h;
}

static Image *icon_sized(const char *names, float px)
{
    Image *src = theme_icon(ui.t, names);
    if (!src)
        return NULL;
    int n = (int)(px + 0.5f);
    return image_resample(src, n, n);
}

static Color icon_tint(Color c)
{
    return ui.t->icon_tint ? c : 0;
}

static double now_s(void)
{
    return time_sec();
}

/* ---- background ------------------------------------------------------ */

static void build_background(void)
{
    Theme *t = ui.t;
    ui.bgc = canvas_alloc(ui.W, ui.H);
    Image *img = theme_background(t);
    float cx = ui.W * 0.5f, cy = ui.H * 0.42f;
    float maxr = sqrtf(cx * cx + cy * cy);
    uint32_t seed = 0x12345678;
    for (int y = 0; y < ui.H; y++) {
        uint32_t *row = ui.bgc.px + y * ui.bgc.stride;
        float dy = (y - cy) / (ui.H * 0.75f);
        for (int x = 0; x < ui.W; x++) {
            float dx = (x - cx) / (ui.W * 0.6f);
            float d = sqrtf(dx * dx + dy * dy);
            float g = clampf(1 - d, 0, 1);
            g = g * g * (3 - 2 * g);
            float v = clampf(sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / maxr, 0, 1);
            float dark = v * v * t->vignette * 0.35f;
            /* ordered-ish noise to keep the subtle gradient from banding */
            seed = seed * 1664525u + 1013904223u;
            float n = ((seed >> 24) / 255.0f - 0.5f);
            float r = COLOR_R(t->bg) + (COLOR_R(t->glow) - (float)COLOR_R(t->bg)) * g;
            float gg = COLOR_G(t->bg) + (COLOR_G(t->glow) - (float)COLOR_G(t->bg)) * g;
            float b = COLOR_B(t->bg) + (COLOR_B(t->glow) - (float)COLOR_B(t->bg)) * g;
            r = r * (1 - dark) + n;
            gg = gg * (1 - dark) + n;
            b = b * (1 - dark) + n;
            row[x] = RGBA((uint32_t)clampf(r + 0.5f, 0, 255), (uint32_t)clampf(gg + 0.5f, 0, 255),
                          (uint32_t)clampf(b + 0.5f, 0, 255), 0);
        }
    }
    if (img) {
        float sc = (float)ui.W / img->w > (float)ui.H / img->h ? (float)ui.W / img->w : (float)ui.H / img->h;
        float w = img->w * sc, h = img->h * sc;
        Image *fit = image_resample(img, (int)(w + 0.5f), (int)(h + 0.5f));
        draw_image(&ui.bgc, fit, (ui.W - fit->w) / 2.0f, (ui.H - fit->h) / 2.0f,
                   (float)fit->w, (float)fit->h, 0, 1);
        image_free(fit);
        image_free(img);
    }
}

/* ---- model ----------------------------------------------------------- */

static void build_tiles(Entry *keep)
{
    ui.ntiles = 0;
    EntryList *l = &ui.boot->entries;
    for (int i = 0; i < l->count && ui.ntiles < MAX_VIS; i++) {
        Entry *e = &l->items[i];
        if (e->hidden && !ui.show_hidden)
            continue;
        Tile *t = &ui.tiles[ui.ntiles++];
        t->e = e;
        t->icon = icon_sized(e->icon[0] ? e->icon : "os-generic", ui.icon_px);
    }
    ui.sel = 0;
    for (int i = 0; i < ui.ntiles; i++)
        if (ui.tiles[i].e == keep)
            ui.sel = i;
}

static void layout(void)
{
    Theme *t = ui.t;
    ui.tile_w = S(t->tile_w);
    ui.tile_h = S(t->tile_h);
    ui.pitch = ui.tile_w + S(t->pad);
    ui.icon_px = S(t->icon_size);

    float title_h = face_ascent(ui.f_title) + face_descent(ui.f_title);
    float sub_h = face_ascent(ui.f_label) + face_descent(ui.f_label);
    float block_h = title_h + S(8) + sub_h + S(40) + ui.tile_h;
    float block_y = (ui.H - block_h) * 0.5f - S(28);
    ui.title_base = block_y + face_ascent(ui.f_title);
    ui.sub_base = block_y + title_h + S(8) + face_ascent(ui.f_label);
    ui.row_y = block_y + title_h + S(8) + sub_h + S(40);

    ui.bar_h = S(48);
    ui.bar_y = ui.H - ui.bar_h - (ui.cfg->hints ? S(52) : S(32));

    /* bar buttons */
    float x = 0, bh = S(36);
    for (int i = 0; i < ui.nbtn; i++) {
        Button *b = &ui.btns[i];
        float w = S(14) + S(16) + S(8) + text_width(ui.f_body, b->label) + S(16);
        b->r = rect_make((int)x, 0, (int)(w + 0.5f), (int)bh);
        x += w + S(4);
    }
    ui.bar_w = x - S(4) + S(12);
    ui.bar_x = floorf((ui.W - ui.bar_w) * 0.5f);
    for (int i = 0; i < ui.nbtn; i++) {
        ui.btns[i].r.x += (int)(ui.bar_x + S(6));
        ui.btns[i].r.y = (int)(ui.bar_y + (ui.bar_h - bh) * 0.5f);
    }
}

static void relayout_row(void)
{
    ui.row_w = ui.ntiles * ui.tile_w + (ui.ntiles > 0 ? (ui.ntiles - 1) * S(ui.t->pad) : 0);
    ui.avail_w = ui.W - S(96);
    ui.row_x0 = ui.row_w <= ui.avail_w ? floorf((ui.W - ui.row_w) * 0.5f) : S(48);
}

static float scroll_target(void)
{
    if (ui.row_w <= ui.avail_w)
        return 0;
    float centre = ui.row_x0 + ui.sel * ui.pitch + ui.tile_w * 0.5f - ui.W * 0.5f;
    return clampf(centre, 0, ui.row_w - ui.avail_w);
}

static float tile_x(int i)
{
    return ui.row_x0 + i * ui.pitch - ui.scroll.x;
}

static void select_tile(int i, bool animate)
{
    if (ui.ntiles == 0)
        return;
    ui.sel = i < 0 ? 0 : (i >= ui.ntiles ? ui.ntiles - 1 : i);
    ui.plate.target = ui.row_x0 + ui.sel * ui.pitch;
    ui.scroll.target = scroll_target();
    if (!animate) {
        ui.plate.x = ui.plate.target;
        ui.plate.v = 0;
        ui.scroll.x = ui.scroll.target;
        ui.scroll.v = 0;
    }
}

static void stop_countdown(void)
{
    ui.counting = false;
}

static float countdown_left(double now)
{
    if (!ui.counting)
        return 0;
    float left = ui.count_total - (float)(now - ui.count_start);
    return left > 0 ? left : 0;
}

static void show_toast(const char *msg)
{
    str_copy(ui.toast, msg, sizeof(ui.toast));
    ui.toast_at = now_s();
}

/* ---- drawing --------------------------------------------------------- */

static void draw_card(float x, float y, float w, float h, float r, Color fill, Color border, float op, bool shadow)
{
    Theme *t = ui.t;
    if (shadow)
        draw_shadow(ui.c, x, y, w, h, r, S(t->shadow_blur), S(t->shadow_y), color_fade(t->shadow, op));
    draw_rrect(ui.c, x, y, w, h, r, color_fade(fill, op), color_fade(border, op), S(1) < 1 ? 1 : floorf(S(1)));
}

static void text_center(Face *f, float cx, float base, const char *s, Color col)
{
    float w = text_width(f, s);
    text_draw(ui.c, f, cx - w * 0.5f, base, s, col);
}

static float enter_at(double now, float delay_ms)
{
    float d = ui.t->enter_ms / 1000.0f;
    float p = (float)((now - ui.t_enter - delay_ms / 1000.0f) / d);
    return ease(EASE_DECEL, clampf(p, 0, 1));
}

static void draw_clock(double now)
{
    if (!ui.cfg->show_clock)
        return;
    float op = enter_at(now, 120) * ui.fade_chrome;
    Theme *t = ui.t;
    float y = S(28) + face_ascent(ui.f_body);
    float w = text_width(ui.f_body, ui.clock_text);
    text_draw(ui.c, ui.f_body, ui.W - S(32) - w, y, ui.clock_text, color_fade(t->fg_muted, op));
    text_draw(ui.c, ui.f_label, S(32), y, "Shade", color_fade(t->fg_subtle, op));
}

static void header_text(double now, char *title, char *sub, int cap)
{
    str_copy(title, "Choose a startup disk", cap);
    if (ui.ntiles == 0) {
        str_copy(title, "No bootable systems found", cap);
        str_copy(sub, ui.show_hidden ? "Nothing hidden either. Check your drives and restart."
                                     : "Press F3 to show hidden entries", cap);
    } else if (ui.counting && ui.default_entry) {
        int secs = (int)ceilf(countdown_left(now));
        str_fmt(sub, cap, "Starting %s in %d second%s", ui.default_entry->title, secs, secs == 1 ? "" : "s");
    } else if (ui.show_hidden) {
        str_copy(sub, "Showing hidden entries \xe2\x80\x94 F3 to hide them again", cap);
    } else {
        str_copy(sub, "Select a system and press Enter", cap);
    }
}

static void draw_header(double now)
{
    char title[96], sub[160];
    header_text(now, title, sub, sizeof(title));
    float op = enter_at(now, 0) * ui.fade_chrome;
    float rise = (1 - enter_at(now, 0)) * S(8);
    text_center(ui.f_title, ui.W * 0.5f, ui.title_base + rise, title, color_fade(ui.t->fg, op));
    text_center(ui.f_label, ui.W * 0.5f, ui.sub_base + rise, sub, color_fade(ui.t->fg_muted, op));
}

static float emphasis(int i)
{
    float d = fabsf(ui.plate.x - (ui.row_x0 + i * ui.pitch)) / ui.pitch;
    return clampf(1 - d, 0, 1);
}

static void ring_geom(float *cx, float *cy, float *r)
{
    *cx = ui.plate.x - ui.scroll.x + ui.tile_w * 0.5f;
    *cy = ui.row_y + ui.tile_h - S(32);
    *r = S(15);
}

static void draw_row(double now)
{
    Theme *t = ui.t;
    if (ui.ntiles == 0)
        return;
    float row_op = tween_value(&ui.row_enter, now);

    /* selection card */
    float plate_op = enter_at(now, 60) * row_op * ui.fade_sel;
    float px = ui.plate.x - ui.scroll.x;
    Color border = ui.focus == FOCUS_ROW ? t->border_strong : t->border;
    draw_card(px, ui.row_y + (1 - plate_op) * S(12), ui.tile_w, ui.tile_h, S(t->radius_card),
              t->surface, border, plate_op, true);

    for (int i = 0; i < ui.ntiles; i++) {
        Tile *tl = &ui.tiles[i];
        float x = tile_x(i);
        if (x + ui.tile_w < -S(64) || x > ui.W + S(64))
            continue;
        float in = enter_at(now, 40.0f * i) * row_op;
        float op = in * (i == ui.sel ? ui.fade_sel : ui.fade_chrome);
        float y = ui.row_y + (1 - in) * S(12);
        float e = emphasis(i);
        float dim = tl->e->hidden ? 0.45f : 1;

        if (tl->icon) {
            float sc = 0.94f + 0.06f * e;
            float isz = ui.icon_px * sc;
            float ix = x + (ui.tile_w - isz) * 0.5f;
            float iy = y + S(26) + (ui.icon_px - isz) * 0.5f;
            draw_image(ui.c, tl->icon, ix, iy, isz, isz, icon_tint(color_mix(t->fg_muted, t->fg, e)), op * dim);
        }
        char buf[96];
        float max_w = ui.tile_w - S(20);
        float label_base = y + S(26) + ui.icon_px + S(18) + face_ascent(ui.f_label);
        text_ellipsize(ui.f_label, tl->e->title, max_w, buf, sizeof(buf));
        text_center(ui.f_label, x + ui.tile_w * 0.5f, label_base, buf,
                    color_fade(color_mix(t->fg_muted, t->fg, e), op * dim));
        text_ellipsize(ui.f_small, tl->e->subtitle, max_w, buf, sizeof(buf));
        text_center(ui.f_small, x + ui.tile_w * 0.5f, label_base + S(6) + face_ascent(ui.f_small), buf,
                    color_fade(t->fg_subtle, op * dim));

        if (tl->e->hidden) {
            const char *badge = tl->e->why[0] ? tl->e->why : "hidden";
            text_ellipsize(ui.f_small, badge, ui.tile_w - S(32), buf, sizeof(buf));
            float bw = text_width(ui.f_small, buf) + S(16), bh = S(20);
            float bx = x + (ui.tile_w - bw) * 0.5f, by = y + S(8);
            draw_rrect(ui.c, bx, by, bw, bh, bh * 0.5f, 0, color_fade(t->border_strong, op), floorf(S(1)));
            text_center(ui.f_small, x + ui.tile_w * 0.5f, by + bh * 0.5f + face_ascent(ui.f_small) * 0.36f,
                        buf, color_fade(t->fg_subtle, op));
        }

        /* primary "start" button: the one inverted pill on this surface */
        float bop = e * e * op;
        if (bop > 0.01f) {
            float cx = x + ui.tile_w * 0.5f, cy = ui.row_y + ui.tile_h - S(32) + (1 - in) * S(12);
            float r = S(15);
            draw_circle(ui.c, cx, cy, r, color_fade(t->primary, bop * (ui.focus == FOCUS_ROW ? 1 : 0.6f)));
            Image *arrow = ui.arrow;
            if (arrow)
                draw_image(ui.c, arrow, cx - arrow->w * 0.5f, cy - arrow->h * 0.5f, (float)arrow->w,
                           (float)arrow->h, t->primary_fg, bop);
        }
    }
}

static void draw_ring(double now)
{
    if (!ui.counting || ui.ntiles == 0)
        return;
    float cx, cy, r;
    ring_geom(&cx, &cy, &r);
    float frac = countdown_left(now) / ui.count_total;
    float op = enter_at(now, 200) * ui.fade_sel;
    draw_arc(ui.c, cx, cy, r + S(5), S(2), 1, color_fade(ui.t->border, op));
    draw_arc(ui.c, cx, cy, r + S(5), S(2), frac, color_fade(ui.t->fg_muted, op));
}

static void draw_bar(double now)
{
    Theme *t = ui.t;
    if (!ui.nbtn)
        return;
    float op = enter_at(now, 100) * ui.fade_chrome;
    float rise = (1 - enter_at(now, 100)) * S(12);
    draw_card(ui.bar_x, ui.bar_y + rise, ui.bar_w, ui.bar_h, ui.bar_h * 0.5f, t->surface, t->border, op, true);
    for (int i = 0; i < ui.nbtn; i++) {
        Button *b = &ui.btns[i];
        bool focused = (ui.focus == FOCUS_BAR && ui.bar_sel == i) || ui.hover_btn == i;
        bool pressed = ui.press_btn == i;
        float y = b->r.y + rise;
        if (focused || pressed)
            draw_rrect(ui.c, (float)b->r.x, y, (float)b->r.w, (float)b->r.h, b->r.h * 0.5f,
                       color_fade(t->raised, op), color_fade(pressed ? t->border_strong : 0, op), floorf(S(1)));
        Color fg = focused ? t->fg : t->fg_muted;
        float isz = S(16);
        float ix = b->r.x + S(14), iy = y + (b->r.h - isz) * 0.5f;
        if (b->icon)
            draw_image(ui.c, b->icon, ix, iy, isz, isz, fg, op);
        float base = y + b->r.h * 0.5f + face_ascent(ui.f_body) * 0.36f;
        text_draw(ui.c, ui.f_body, ix + isz + S(8), base, b->label, color_fade(fg, op));
    }
}

static void draw_hints(double now)
{
    if (!ui.cfg->hints)
        return;
    float op = enter_at(now, 200) * ui.fade_chrome;
    const char *h = ui.sheet ? "Enter  boot      Esc  cancel"
                             : "\xe2\x86\x90 \xe2\x86\x92  choose      Enter  start      Tab  power      F2  options      F3  hidden";
    text_center(ui.f_small, ui.W * 0.5f, ui.H - S(22), h, color_fade(ui.t->fg_subtle, op));
}

static Rect toast_rect(void)
{
    float w = text_width(ui.f_body, ui.toast) + S(48);
    float h = S(40);
    return rect_make((int)((ui.W - w) * 0.5f), (int)(ui.bar_y - S(20) - h), (int)w, (int)h);
}

static float toast_alpha(double now)
{
    if (!ui.toast[0])
        return 0;
    float age = (float)(now - ui.toast_at);
    float in = ease(EASE_DECEL, clampf(age / (ui.t->enter_ms / 1000.0f), 0, 1));
    float out = 1 - ease(EASE_ACCEL, clampf((age - 6.0f) / (ui.t->exit_ms / 1000.0f), 0, 1));
    return in * out;
}

static void draw_toast(double now)
{
    float a = toast_alpha(now) * ui.fade_chrome;
    if (a <= 0)
        return;
    Theme *t = ui.t;
    Rect r = toast_rect();
    float y = r.y + (1 - a) * S(8);
    draw_card((float)r.x, y, (float)r.w, (float)r.h, r.h * 0.5f, t->surface, t->border, a, true);
    draw_circle(ui.c, r.x + S(20), y + r.h * 0.5f, S(4), color_fade(t->danger, a));
    text_draw(ui.c, ui.f_body, r.x + S(32), y + r.h * 0.5f + face_ascent(ui.f_body) * 0.36f, ui.toast,
              color_fade(t->fg, a));
}

/* options sheet geometry */
static void sheet_geom(float *x, float *y, float *w, float *h)
{
    *w = S(560) < ui.W - S(64) ? S(560) : ui.W - S(64);
    *h = S(212);
    *x = floorf((ui.W - *w) * 0.5f);
    *y = floorf((ui.H - *h) * 0.5f);
}

static void sheet_buttons(Rect *cancel, Rect *ok)
{
    float x, y, w, h;
    sheet_geom(&x, &y, &w, &h);
    float bh = S(36);
    float okw = text_width(ui.f_body, "Boot") + S(40);
    float cw = text_width(ui.f_body, "Cancel") + S(36);
    *ok = rect_make((int)(x + w - S(20) - okw), (int)(y + h - S(20) - bh), (int)okw, (int)bh);
    *cancel = rect_make((int)(ok->x - S(8) - cw), ok->y, (int)cw, (int)bh);
}

static void draw_sheet(double now)
{
    float a = tween_value(&ui.sheet_tw, now);
    if (a <= 0.001f)
        return;
    Theme *t = ui.t;
    draw_fill(ui.c, rect_make(0, 0, ui.W, ui.H), color_fade(0x99000000, a));
    float x, y, w, h;
    sheet_geom(&x, &y, &w, &h);
    float sc = 0.96f + 0.04f * a;
    float dw = w * sc, dh = h * sc;
    x += (w - dw) * 0.5f;
    y += (h - dh) * 0.5f + (1 - a) * S(8);
    draw_card(x, y, dw, dh, S(t->radius_card), t->surface, t->border, a, true);
    if (a < 0.98f)
        return;     /* content appears once the card has settled */

    Entry *e = ui.tiles[ui.sel].e;
    float px = x + S(20);
    float base = y + S(20) + face_ascent(ui.f_label);
    text_draw(ui.c, ui.f_label, px, base, "Boot options", t->fg);
    char sub[128];
    str_fmt(sub, sizeof(sub), "Command line passed to %s for this boot only.", e->title);
    text_draw(ui.c, ui.f_small, px, base + S(8) + face_ascent(ui.f_small), sub, t->fg_muted);

    float fy = base + S(28) + face_ascent(ui.f_small);
    float fw = w - S(40), fh = S(40);
    draw_rrect(ui.c, px, fy, fw, fh, S(t->radius_control), t->raised, t->border_strong, floorf(S(1)));
    Rect old = ui.c->clip;
    canvas_set_clip(ui.c, rect_intersect(old, rect_from_f(px + S(12), fy, fw - S(24), fh, 0)));
    float tw = text_width(ui.f_body, ui.sheet_text);
    float shift = tw > fw - S(30) ? tw - (fw - S(30)) : 0;
    float tbase = fy + fh * 0.5f + face_ascent(ui.f_body) * 0.36f;
    if (ui.sheet_text[0])
        text_draw(ui.c, ui.f_body, px + S(12) - shift, tbase, ui.sheet_text, t->fg);
    else
        text_draw(ui.c, ui.f_body, px + S(16), tbase, "no options", t->fg_subtle);
    if (((int)(now * 2)) % 2 == 0)
        draw_fill(ui.c, rect_from_f(px + S(12) - shift + tw + 1, fy + S(11), floorf(S(1.5f)) + 1, fh - S(22), 0), t->fg);
    ui.c->clip = old;

    Rect cancel, ok;
    sheet_buttons(&cancel, &ok);
    draw_rrect(ui.c, (float)cancel.x, (float)cancel.y, (float)cancel.w, (float)cancel.h, cancel.h * 0.5f,
               0, t->border_strong, floorf(S(1)));
    text_center(ui.f_body, cancel.x + cancel.w * 0.5f, cancel.y + cancel.h * 0.5f + face_ascent(ui.f_body) * 0.36f,
                "Cancel", t->fg);
    draw_rrect(ui.c, (float)ok.x, (float)ok.y, (float)ok.w, (float)ok.h, ok.h * 0.5f, t->primary, 0, 0);
    text_center(ui.f_body, ok.x + ok.w * 0.5f, ok.y + ok.h * 0.5f + face_ascent(ui.f_body) * 0.36f,
                "Boot", t->primary_fg);
}

static void draw_scene(Rect clip, double now)
{
    canvas_set_clip(ui.c, clip);
    draw_blit(ui.c, &ui.bgc, clip);
    draw_clock(now);
    draw_header(now);
    draw_row(now);
    draw_ring(now);
    draw_bar(now);
    draw_toast(now);
    draw_hints(now);
    draw_sheet(now);
}

/* ---- dirty tracking -------------------------------------------------- */

static Rect region_rect(int r)
{
    float pad = S(ui.t->shadow_blur) + S(ui.t->shadow_y) + S(4);
    switch (r) {
    case R_CLOCK:  return rect_make(0, 0, ui.W, (int)S(64));
    case R_HEADER: return rect_from_f(0, ui.title_base - face_ascent(ui.f_title) - S(4), (float)ui.W,
                                      ui.row_y - ui.title_base + face_ascent(ui.f_title) - S(8), 0);
    case R_ROW: {
        /* tiles' span, or the full width when the row scrolls */
        float x0 = 0, w = (float)ui.W;
        if (ui.row_w <= ui.avail_w) {
            x0 = ui.row_x0 - pad;
            w = ui.row_w + 2 * pad;
        }
        return rect_from_f(x0, ui.row_y - pad, w, ui.tile_h + 2 * pad + S(12), 0);
    }
    case R_RING: {
        float cx, cy, rr;
        ring_geom(&cx, &cy, &rr);
        return rect_from_f(cx - rr - S(8), cy - rr - S(8), 2 * (rr + S(8)), 2 * (rr + S(8)), 1);
    }
    case R_BAR:    return rect_from_f(ui.bar_x - pad, ui.bar_y - pad, ui.bar_w + 2 * pad, ui.bar_h + 2 * pad + S(12), 0);
    case R_TOAST: {
        Rect t = toast_rect();
        return rect_from_f((float)t.x - pad, (float)t.y - pad, t.w + 2 * pad, t.h + 2 * pad + S(8), 0);
    }
    case R_HINTS:  return rect_from_f(0, ui.H - S(44), (float)ui.W, S(44), 0);
    default:       return rect_make(0, 0, ui.W, ui.H);
    }
}

static uint64_t region_sig(int r, double now)
{
    uint64_t h = 1469598103934665603ULL;
    int intro = (int)(enter_at(now, 400) * 1000);   /* covers every staggered intro */
    switch (r) {
    case R_CLOCK:
        return hash_str(mix64(h, intro), ui.clock_text);
    case R_HEADER: {
        char title[96], sub[160];
        header_text(now, title, sub, sizeof(title));
        return hash_str(hash_str(mix64(h, intro), title), sub);
    }
    case R_ROW:
        h = mix64(h, (int64_t)(ui.plate.x * 8));
        h = mix64(h, (int64_t)(ui.scroll.x * 8));
        h = mix64(h, ui.focus);
        h = mix64(h, ui.ntiles);
        h = mix64(h, ui.show_hidden);
        h = mix64(h, (int64_t)(tween_value(&ui.row_enter, now) * 1000));
        h = mix64(h, (int64_t)(enter_at(now, 40.0f * ui.ntiles + 60) * 1000));
        return h;
    case R_RING:
        h = mix64(h, ui.counting);
        h = mix64(h, (int64_t)(ui.counting ? countdown_left(now) / ui.count_total * 400 : 0));
        return mix64(h, (int64_t)(ui.plate.x * 8));
    case R_BAR:
        h = mix64(h, intro);
        h = mix64(h, ui.focus == FOCUS_BAR ? ui.bar_sel : -1);
        h = mix64(h, ui.hover_btn);
        return mix64(h, ui.press_btn);
    case R_TOAST:
        return hash_str(mix64(h, (int64_t)(toast_alpha(now) * 1000)), ui.toast);
    case R_HINTS:
        return mix64(mix64(h, intro), ui.sheet);
    case R_SHEET:
        h = mix64(h, (int64_t)(tween_value(&ui.sheet_tw, now) * 1000));
        if (ui.sheet)
            h = mix64(hash_str(h, ui.sheet_text), ((int)(now * 2)) % 2);
        return h;
    }
    return h;
}

static void render(double now, bool force)
{
    Rect dirty[R_COUNT];
    int nd = 0;
    bool full = force;
    for (int r = 0; r < R_COUNT; r++) {
        uint64_t s = region_sig(r, now);
        if (s != ui.sig[r]) {
            ui.sig[r] = s;
            if (r == R_SHEET && (ui.sheet || tween_active(&ui.sheet_tw, now) || s != 0))
                full = true;
            else
                dirty[nd++] = region_rect(r);
        }
    }
    /* the sheet's scrim covers everything; while it is up, redraw in full */
    if (ui.sheet || tween_active(&ui.sheet_tw, now)) {
        if (nd)
            full = true;
    }
    if (full) {
        nd = 1;
        dirty[0] = rect_make(0, 0, ui.W, ui.H);
    }
    /* merge overlapping rects to avoid double work */
    for (int i = 0; i < nd; i++)
        for (int j = i + 1; j < nd; j++)
            if (!rect_empty(rect_intersect(dirty[i], dirty[j]))) {
                dirty[i] = rect_union(dirty[i], dirty[j]);
                dirty[j--] = dirty[--nd];
                i = -1;
                break;
            }
    for (int i = 0; i < nd; i++) {
        draw_scene(dirty[i], now);
        fb_present(dirty[i]);
    }
}

/* ---- clock ----------------------------------------------------------- */

static void update_clock(void)
{
    EFI_TIME tm;
    if (gRT->GetTime(&tm, NULL) != EFI_SUCCESS) {
        ui.clock_text[0] = 0;
        return;
    }
    int minute = tm.Hour * 60 + tm.Minute;
    if (minute == ui.last_minute)
        return;
    ui.last_minute = minute;
    static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int y = tm.Year, m = tm.Month, d = tm.Day;
    if (m < 3) { m += 12; y--; }
    int dow = (d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400 + 6) % 7;   /* Zeller -> Sun=0 */
    str_fmt(ui.clock_text, sizeof(ui.clock_text), "%s %d %s   %02d:%02d", days[dow], tm.Day,
            months[(tm.Month - 1) % 12], tm.Hour, tm.Minute);
}

/* ---- input ----------------------------------------------------------- */

typedef enum { ACT_NONE, ACT_BOOT, ACT_BUTTON } Act;

static int hit_tile(int x, int y)
{
    if (y < ui.row_y || y > ui.row_y + ui.tile_h)
        return -1;
    for (int i = 0; i < ui.ntiles; i++) {
        float tx = tile_x(i);
        if (x >= tx && x < tx + ui.tile_w)
            return i;
    }
    return -1;
}

static int hit_button(int x, int y)
{
    for (int i = 0; i < ui.nbtn; i++)
        if (rect_contains(ui.btns[i].r, x, y))
            return i;
    return -1;
}

static void open_sheet(double now)
{
    if (ui.ntiles == 0)
        return;
    ui.sheet = true;
    str_copy(ui.sheet_text, ui.tiles[ui.sel].e->options, sizeof(ui.sheet_text));
    tween_start(&ui.sheet_tw, tween_value(&ui.sheet_tw, now), 1, ui.t->enter_ms, EASE_DECEL, now);
}

static void close_sheet(double now)
{
    ui.sheet = false;
    tween_start(&ui.sheet_tw, tween_value(&ui.sheet_tw, now), 0, ui.t->exit_ms, EASE_ACCEL, now);
}

static void toggle_hidden(double now)
{
    Entry *keep = ui.ntiles ? ui.tiles[ui.sel].e : NULL;
    ui.show_hidden = !ui.show_hidden;
    build_tiles(keep);
    relayout_row();
    select_tile(ui.sel, false);
    tween_start(&ui.row_enter, 0.3f, 1, ui.t->enter_ms, EASE_DECEL, now);
}

static void set_cursor(bool on)
{
    Rect old = fb_cursor_rect();
    bool was = ui.cursor_on;
    ui.cursor_on = on && ui.cursor;
    fb_move_cursor(ui.mx, ui.my, ui.cursor_on);
    if (was)
        fb_present(old);
    if (ui.cursor_on)
        fb_present(fb_cursor_rect());
}

static Act handle_key(const Event *ev, double now, int *arg)
{
    uint16_t sc = ev->scan, ch = ev->ch;
    stop_countdown();
    if (ui.cursor_on)
        set_cursor(false);

    if (ui.sheet) {
        size_t n = strlen(ui.sheet_text);
        if (sc == SCAN_ESC) {
            close_sheet(now);
        } else if (ch == '\r') {
            str_copy(ui.tiles[ui.sel].e->options, ui.sheet_text, sizeof(ui.tiles[ui.sel].e->options));
            close_sheet(now);
            return ACT_BOOT;
        } else if (ch == 8) {
            if (n)
                ui.sheet_text[n - 1] = 0;
        } else if (ch >= 0x20 && ch < 0x7f && n + 1 < sizeof(ui.sheet_text)) {
            ui.sheet_text[n] = (char)ch;
            ui.sheet_text[n + 1] = 0;
        }
        return ACT_NONE;
    }

    if (sc == SCAN_F3) {
        toggle_hidden(now);
    } else if (sc == SCAN_F2 || ch == 'e') {
        open_sheet(now);
    } else if (ch == '\t' || (ch == 0 && sc == 0 && ev->shift)) {
        if (ui.focus == FOCUS_ROW) {
            ui.focus = FOCUS_BAR;
            ui.bar_sel = ev->shift ? ui.nbtn - 1 : 0;
        } else {
            int next = ui.bar_sel + (ev->shift ? -1 : 1);
            if (next < 0 || next >= ui.nbtn)
                ui.focus = FOCUS_ROW;
            else
                ui.bar_sel = next;
        }
    } else if (sc == SCAN_DOWN) {
        if (ui.nbtn) {
            if (ui.focus == FOCUS_ROW)
                ui.bar_sel = ui.nbtn / 2;
            ui.focus = FOCUS_BAR;
        }
    } else if (sc == SCAN_UP) {
        ui.focus = FOCUS_ROW;
    } else if (sc == SCAN_LEFT || sc == SCAN_RIGHT) {
        int d = sc == SCAN_LEFT ? -1 : 1;
        if (ui.focus == FOCUS_ROW)
            select_tile(ui.sel + d, true);
        else
            ui.bar_sel = (ui.bar_sel + d + ui.nbtn) % ui.nbtn;
    } else if (sc == SCAN_HOME) {
        select_tile(0, true);
    } else if (sc == SCAN_END) {
        select_tile(ui.ntiles - 1, true);
    } else if (ch >= '1' && ch <= '9') {
        ui.focus = FOCUS_ROW;
        select_tile(ch - '1', true);
    } else if (ch == '\r' || ch == ' ') {
        if (ui.focus == FOCUS_BAR) {
            *arg = ui.bar_sel;
            return ACT_BUTTON;
        }
        if (ui.ntiles)
            return ACT_BOOT;
    } else if (sc == SCAN_ESC) {
        if (ui.show_hidden)
            toggle_hidden(now);
        ui.focus = FOCUS_ROW;
    }
    return ACT_NONE;
}

static Act handle_pointer(const Event *ev, double now, int *arg)
{
    ui.mx = ev->x;
    ui.my = ev->y;
    if (ev->type == EV_POINTER_MOVE) {
        if (!ui.cursor_on)
            stop_countdown();
        set_cursor(true);
    }
    if (ui.sheet) {
        Rect cancel, ok;
        sheet_buttons(&cancel, &ok);
        if (ev->type == EV_POINTER_UP) {
            if (rect_contains(ok, ev->x, ev->y)) {
                str_copy(ui.tiles[ui.sel].e->options, ui.sheet_text, sizeof(ui.tiles[ui.sel].e->options));
                close_sheet(now);
                return ACT_BOOT;
            }
            if (rect_contains(cancel, ev->x, ev->y))
                close_sheet(now);
        }
        return ACT_NONE;
    }
    int t = hit_tile(ev->x, ev->y);
    int b = hit_button(ev->x, ev->y);
    ui.hover_btn = b;
    if (ev->type == EV_POINTER_MOVE) {
        if (t >= 0 && t != ui.sel) {
            ui.focus = FOCUS_ROW;
            select_tile(t, true);
        }
    } else if (ev->type == EV_POINTER_DOWN) {
        stop_countdown();
        ui.press_tile = t;
        ui.press_btn = b;
        if (t >= 0) {
            ui.focus = FOCUS_ROW;
            select_tile(t, true);
        }
    } else if (ev->type == EV_POINTER_UP) {
        Act a = ACT_NONE;
        if (t >= 0 && t == ui.press_tile)
            a = ACT_BOOT;
        else if (b >= 0 && b == ui.press_btn) {
            *arg = b;
            a = ACT_BUTTON;
        }
        ui.press_tile = ui.press_btn = -1;
        return a;
    }
    return ACT_NONE;
}

/* ---- exit animation --------------------------------------------------- */

static void play_exit(bool keep_selected)
{
    double start = now_s();
    float dur = ui.t->exit_ms / 1000.0f;
    float total = keep_selected ? dur * 2 : dur;
    Rect full = rect_make(0, 0, ui.W, ui.H);
    ui.counting = false;
    if (ui.cursor_on)
        set_cursor(false);
    for (;;) {
        double now = now_s();
        float p = (float)((now - start) / total);
        if (p >= 1)
            break;
        if (keep_selected) {
            ui.fade_chrome = 1 - ease(EASE_ACCEL, clampf(p * 2, 0, 1));
            ui.fade_sel = 1 - ease(EASE_ACCEL, clampf(p * 2 - 1, 0, 1));
        } else {
            ui.fade_chrome = ui.fade_sel = 1 - ease(EASE_ACCEL, p);
        }
        draw_scene(full, now);
        fb_present(full);
    }
    ui.fade_chrome = ui.fade_sel = 1;
    fb_clear(0);
}

/* ---- public ----------------------------------------------------------- */

static void add_button(BtnKind k, const char *label, const char *icon)
{
    Button *b = &ui.btns[ui.nbtn++];
    b->kind = k;
    b->label = label;
    b->icon = icon_sized(icon, S(16));
}

void ui_init(Theme *theme, Settings *settings, BootSet *boot)
{
    memset(&ui, 0, sizeof(ui));
    ui.t = theme;
    ui.cfg = settings;
    ui.boot = boot;
    ui.c = fb_back();
    ui.W = ui.c->w;
    ui.H = ui.c->h;
    ui.s = settings->scale > 0 ? settings->scale : clampf(ui.H / 900.0f, 1, 3);
    ui.f_title = font_face(theme->semibold, roundf(S(theme->size_title)));
    ui.f_label = font_face(theme->semibold, roundf(S(theme->size_label)));
    ui.f_body = font_face(theme->regular, roundf(S(theme->size_body)));
    ui.f_small = font_face(theme->regular, roundf(S(theme->size_small)));
    ui.hover_btn = ui.press_btn = ui.press_tile = -1;
    ui.last_minute = -1;

    if (firmware_setup_supported())
        add_button(BTN_FIRMWARE, "Firmware Settings", "firmware");
    add_button(BTN_RESTART, "Restart", "restart");
    add_button(BTN_SHUTDOWN, "Shut Down", "power");

    layout();
    build_background();
    ui.arrow = icon_sized("arrow-right", S(16));

    Image *cur = theme_icon(theme, "cursor");
    if (cur) {
        int n = (int)(S(24) + 0.5f);
        ui.cursor = image_resample(cur, n, n);
        fb_set_cursor(ui.cursor, (int)(n * 0.156f), (int)(n * 0.078f));
    }
    input_init(ui.W, ui.H, settings->mouse_speed);
    ui.mx = ui.W / 2;
    ui.my = ui.H / 2;

    /* default selection */
    char want[64] = "";
    if (str_eq_ci(settings->default_entry, "last"))
        recall_last(want, sizeof(want));
    else
        str_copy(want, settings->default_entry, sizeof(want));
    build_tiles(NULL);
    ui.default_entry = ui.ntiles ? ui.tiles[0].e : NULL;
    if (want[0])
        for (int i = 0; i < ui.ntiles; i++)
            if (str_glob_ci(want, ui.tiles[i].e->title)) {
                ui.default_entry = ui.tiles[i].e;
                break;
            }
    build_tiles(ui.default_entry);
    relayout_row();
    spring_init(&ui.plate, 0, theme->spring_k, theme->spring_zeta);
    spring_init(&ui.scroll, 0, theme->spring_k, theme->spring_zeta);
    select_tile(ui.sel, false);
}

UiResult ui_run(const char *toast, bool autoboot)
{
    UiResult res = { UI_BOOT, NULL };
    double now = now_s();
    ui.t_enter = now;
    tween_start(&ui.row_enter, 1, 1, 0, EASE_LINEAR, now);
    tween_start(&ui.sheet_tw, 0, 0, 0, EASE_LINEAR, now);
    ui.sheet = false;
    ui.fade_chrome = ui.fade_sel = 1;
    ui.toast[0] = 0;
    if (toast)
        show_toast(toast);
    ui.counting = autoboot && ui.cfg->timeout > 0 && ui.default_entry && ui.ntiles > 0;
    ui.count_total = (float)ui.cfg->timeout;
    ui.count_start = now;
    memset(ui.sig, 0, sizeof(ui.sig));
    update_clock();
    render(now, true);

    double last = now, clock_check = now;
    for (;;) {
        now = now_s();
        float dt = (float)(now - last);
        last = now;

        Event ev;
        Act act = ACT_NONE;
        int arg = -1;
        while (act == ACT_NONE && input_poll(&ev))
            act = ev.type == EV_KEY ? handle_key(&ev, now, &arg) : handle_pointer(&ev, now, &arg);

        if (ui.counting && countdown_left(now) <= 0 && ui.default_entry) {
            for (int i = 0; i < ui.ntiles; i++)
                if (ui.tiles[i].e == ui.default_entry)
                    select_tile(i, false);
            act = ACT_BOOT;
        }

        if (act == ACT_BOOT && ui.ntiles) {
            ui.counting = false;
            res.action = UI_BOOT;
            res.entry = ui.tiles[ui.sel].e;
            play_exit(true);
            return res;
        }
        if (act == ACT_BUTTON && arg >= 0) {
            BtnKind k = ui.btns[arg].kind;
            res.action = k == BTN_FIRMWARE ? UI_FIRMWARE : k == BTN_RESTART ? UI_RESTART : UI_SHUTDOWN;
            play_exit(false);
            return res;
        }

        spring_step(&ui.plate, dt);
        spring_step(&ui.scroll, dt);
        if (now - clock_check > 1.0) {
            update_clock();
            clock_check = now;
        }

        double t0 = now_s();
        render(now, false);
        bool animating = !spring_settled(&ui.plate) || !spring_settled(&ui.scroll) ||
                         now - ui.t_enter < ui.t->enter_ms / 1000.0f + 0.6f ||
                         tween_active(&ui.row_enter, now) || tween_active(&ui.sheet_tw, now) ||
                         (ui.toast[0] && toast_alpha(now) > 0) || ui.counting;
        int spent = (int)((now_s() - t0) * 1000);
        int wait = animating ? 16 - spent : (ui.sheet ? 250 : 500);
        if (wait < 1)
            wait = 1;
        input_wait(wait);
    }
}

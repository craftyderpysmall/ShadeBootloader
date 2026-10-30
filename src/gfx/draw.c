/* Anti-aliased primitives on 32-bit canvases. All drawing honors canvas clip. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gfx.h"

/* ---- helpers --------------------------------------------------------- */

Rect rect_make(int x, int y, int w, int h)
{
    Rect r = { x, y, w, h };
    return r;
}

bool rect_empty(Rect r)
{
    return r.w <= 0 || r.h <= 0;
}

Rect rect_intersect(Rect a, Rect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return rect_make(x0, y0, 0, 0);
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

Rect rect_union(Rect a, Rect b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

bool rect_contains(Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

Rect rect_from_f(float x, float y, float w, float h, int pad)
{
    int x0 = (int)floorf(x) - pad, y0 = (int)floorf(y) - pad;
    int x1 = (int)ceilf(x + w) + pad, y1 = (int)ceilf(y + h) + pad;
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

Color color_mix(Color a, Color b, float t)
{
    if (t <= 0) return a;
    if (t >= 1) return b;
    #define MIXC(s) ((Color)((((a) >> s) & 0xff) * (1 - t) + (((b) >> s) & 0xff) * t + 0.5f) << s)
    return MIXC(24) | MIXC(16) | MIXC(8) | MIXC(0);
    #undef MIXC
}

Canvas canvas_alloc(int w, int h)
{
    Canvas c;
    c.w = w;
    c.h = h;
    c.stride = w;
    c.px = malloc((size_t)w * h * 4);
    c.clip = rect_make(0, 0, w, h);
    if (c.px)
        memset(c.px, 0, (size_t)w * h * 4);
    return c;
}

void canvas_set_clip(Canvas *c, Rect r)
{
    c->clip = rect_intersect(r, rect_make(0, 0, c->w, c->h));
}

/* dst = dst*(255-a) + rgb*a, exact /255, two channels per lane */
static inline uint32_t blend(uint32_t d, uint32_t rgb, uint32_t a)
{
    uint32_t ia = 255 - a;
    uint32_t rb = (rgb & 0xff00ff) * a + (d & 0xff00ff) * ia;
    uint32_t g  = (rgb & 0x00ff00) * a + (d & 0x00ff00) * ia;
    rb = ((rb + 0x800080 + ((rb >> 8) & 0xff00ff)) >> 8) & 0xff00ff;
    g  = ((g + 0x008000 + ((g >> 8) & 0x00ff00)) >> 8) & 0x00ff00;
    return rb | g;
}

/* premultiplied source over: dst = src + dst*(255-sa) */
static inline uint32_t blend_pm(uint32_t d, uint32_t s)
{
    uint32_t ia = 255 - (s >> 24);
    uint32_t rb = (d & 0xff00ff) * ia;
    uint32_t g  = (d & 0x00ff00) * ia;
    rb = ((rb + 0x800080 + ((rb >> 8) & 0xff00ff)) >> 8) & 0xff00ff;
    g  = ((g + 0x008000 + ((g >> 8) & 0x00ff00)) >> 8) & 0x00ff00;
    return ((s & 0xffffff) + (rb | g)) & 0xffffff;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- fills ----------------------------------------------------------- */

void draw_fill(Canvas *c, Rect r, Color col)
{
    r = rect_intersect(r, c->clip);
    uint32_t a = COLOR_A(col), rgb = col & 0xffffff;
    if (rect_empty(r) || a == 0)
        return;
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *p = c->px + y * c->stride + r.x;
        if (a == 255)
            for (int x = 0; x < r.w; x++)
                p[x] = rgb;
        else
            for (int x = 0; x < r.w; x++)
                p[x] = blend(p[x], rgb, a);
    }
}

void draw_blit(Canvas *dst, const Canvas *src, Rect r)
{
    r = rect_intersect(r, dst->clip);
    r = rect_intersect(r, rect_make(0, 0, src->w, src->h));
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(dst->px + y * dst->stride + r.x, src->px + y * src->stride + r.x, (size_t)r.w * 4);
}

/* ---- rounded rectangles ---------------------------------------------- */

/* Signed-distance coverage of a rounded box centred at (cx,cy). */
static inline float rbox_cov(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    if (hw <= 0 || hh <= 0)
        return 0;
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    float d = sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
    return clampf(0.5f - d, 0, 1);
}

/* Half-width of the shape at vertical distance dy from centre, or -1. */
static inline float rbox_extent(float dy, float hw, float hh, float r)
{
    if (dy > hh)
        return -1;
    if (dy <= hh - r)
        return hw;
    float t = dy - (hh - r);
    return hw - r + sqrtf(r * r - t * t);
}

void draw_rrect(Canvas *c, float x, float y, float w, float h, float radius,
                Color fill, Color border, float bw)
{
    float hw = w * 0.5f, hh = h * 0.5f;
    float cx = x + hw, cy = y + hh;
    float r = clampf(radius, 0, hw < hh ? hw : hh);
    bool has_border = bw > 0 && COLOR_A(border) > 0;
    if (!has_border)
        bw = 0;
    float ihw = hw - bw, ihh = hh - bw, ir = r - bw > 0 ? r - bw : 0;
    uint32_t fa = COLOR_A(fill), frgb = fill & 0xffffff;
    uint32_t ba = COLOR_A(border), brgb = border & 0xffffff;

    Rect box = rect_intersect(rect_from_f(x, y, w, h, 1), c->clip);
    if (rect_empty(box))
        return;

    for (int iy = box.y; iy < box.y + box.h; iy++) {
        float py = iy + 0.5f;
        uint32_t *row = c->px + iy * c->stride;
        /* span of pixels fully inside the inner shape: plain fill */
        float ext = rbox_extent(fabsf(py - cy) + 0.5f, ihw, ihh, ir);
        int s0 = box.x + box.w, s1 = box.x + box.w;
        if (ext > 0) {
            s0 = (int)ceilf(cx - ext);
            s1 = (int)floorf(cx + ext);
            if (s0 < box.x) s0 = box.x;
            if (s1 > box.x + box.w) s1 = box.x + box.w;
            if (s1 < s0) s1 = s0;
        }
        for (int ix = box.x; ix < box.x + box.w; ix++) {
            if (ix == s0 && s1 > s0) {
                if (fa == 255)
                    for (; ix < s1; ix++) row[ix] = frgb;
                else if (fa)
                    for (; ix < s1; ix++) row[ix] = blend(row[ix], frgb, fa);
                else
                    ix = s1;
                if (ix >= box.x + box.w)
                    break;
            }
            float px = ix + 0.5f;
            float outer = rbox_cov(px, py, cx, cy, hw, hh, r);
            if (outer <= 0)
                continue;
            float inner = has_border ? rbox_cov(px, py, cx, cy, ihw, ihh, ir) : outer;
            uint32_t d = row[ix];
            if (fa && inner > 0)
                d = blend(d, frgb, (uint32_t)(fa * inner + 0.5f));
            if (has_border && outer > inner)
                d = blend(d, brgb, (uint32_t)(ba * (outer - inner) + 0.5f));
            row[ix] = d;
        }
    }
}

/* ---- shadows --------------------------------------------------------- */

typedef struct {
    int w, h, r, blur;
    int mw, mh, pad;
    uint8_t *m;
} ShadowMask;

#define SHADOW_CACHE 8
static ShadowMask shadow_cache[SHADOW_CACHE];
static int shadow_next;

static void box_blur(const uint8_t *src, uint8_t *dst, int stride, int count, int lines, int line_stride, int rad)
{
    /* blur `lines` independent lines of `count` samples each */
    int win = rad * 2 + 1;
    for (int l = 0; l < lines; l++) {
        const uint8_t *s = src + l * line_stride;
        uint8_t *d = dst + l * line_stride;
        int sum = 0;
        for (int i = -rad; i <= rad; i++)
            sum += (i >= 0 && i < count) ? s[i * stride] : 0;
        for (int i = 0; i < count; i++) {
            d[i * stride] = (uint8_t)((sum + win / 2) / win);
            int add = i + rad + 1, sub = i - rad;
            sum += (add < count ? s[add * stride] : 0) - (sub >= 0 ? s[sub * stride] : 0);
        }
    }
}

static ShadowMask *shadow_mask(int w, int h, int r, int blur)
{
    for (int i = 0; i < SHADOW_CACHE; i++) {
        ShadowMask *s = &shadow_cache[i];
        if (s->m && s->w == w && s->h == h && s->r == r && s->blur == blur)
            return s;
    }
    ShadowMask *s = &shadow_cache[shadow_next];
    shadow_next = (shadow_next + 1) % SHADOW_CACHE;
    free(s->m);
    s->w = w; s->h = h; s->r = r; s->blur = blur;
    s->pad = blur + 2;
    s->mw = w + 2 * s->pad;
    s->mh = h + 2 * s->pad;
    size_t n = (size_t)s->mw * s->mh;
    s->m = calloc(n, 1);
    uint8_t *tmp = malloc(n);
    float hw = w * 0.5f, hh = h * 0.5f;
    for (int y = 0; y < s->mh; y++)
        for (int x = 0; x < s->mw; x++)
            s->m[y * s->mw + x] = (uint8_t)(255 * rbox_cov(x + 0.5f, y + 0.5f, s->pad + hw, s->pad + hh, hw, hh, (float)r));
    /* three box passes ~ gaussian with total spread `blur` */
    int rad = blur / 3 > 0 ? blur / 3 : 1;
    for (int pass = 0; pass < 3; pass++) {
        box_blur(s->m, tmp, 1, s->mw, s->mh, s->mw, rad);
        box_blur(tmp, s->m, s->mw, s->mh, s->mw, 1, rad);
    }
    free(tmp);
    return s;
}

void draw_shadow(Canvas *c, float x, float y, float w, float h, float radius,
                 float blur, float offset_y, Color col)
{
    if (COLOR_A(col) == 0 || w < 1 || h < 1)
        return;
    ShadowMask *s = shadow_mask((int)(w + 0.5f), (int)(h + 0.5f), (int)(radius + 0.5f), (int)(blur + 0.5f));
    int ox = (int)floorf(x + 0.5f) - s->pad;
    int oy = (int)floorf(y + offset_y + 0.5f) - s->pad;
    Rect box = rect_intersect(rect_make(ox, oy, s->mw, s->mh), c->clip);
    uint32_t ca = COLOR_A(col), rgb = col & 0xffffff;
    for (int yy = box.y; yy < box.y + box.h; yy++) {
        uint32_t *row = c->px + yy * c->stride;
        const uint8_t *m = s->m + (yy - oy) * s->mw - ox;
        for (int xx = box.x; xx < box.x + box.w; xx++) {
            uint32_t a = (m[xx] * ca + 127) / 255;
            if (a)
                row[xx] = blend(row[xx], rgb, a);
        }
    }
}

/* ---- circles & arcs -------------------------------------------------- */

static float atan2_cw_from_top(float dx, float dy)
{
    /* angle in [0, 2pi) measured clockwise from 12 o'clock (screen y down) */
    float a = (float)acos(clampf(-dy / sqrtf(dx * dx + dy * dy + 1e-12f), -1, 1));
    return dx >= 0 ? a : 6.2831853f - a;
}

void draw_circle(Canvas *c, float cx, float cy, float radius, Color col)
{
    draw_rrect(c, cx - radius, cy - radius, radius * 2, radius * 2, radius, col, 0, 0);
}

void draw_arc(Canvas *c, float cx, float cy, float radius, float thickness, float frac, Color col)
{
    if (frac <= 0)
        return;
    if (frac > 1)
        frac = 1;
    float ht = thickness * 0.5f;
    Rect box = rect_intersect(rect_from_f(cx - radius - ht, cy - radius - ht,
                                          2 * (radius + ht), 2 * (radius + ht), 1), c->clip);
    float end = frac * 6.2831853f;
    /* round caps */
    float ex = cx + radius * (float)sin(end), ey = cy - radius * (float)cos(end);
    float sx = cx, sy = cy - radius;
    uint32_t ca = COLOR_A(col), rgb = col & 0xffffff;
    for (int iy = box.y; iy < box.y + box.h; iy++) {
        uint32_t *row = c->px + iy * c->stride;
        for (int ix = box.x; ix < box.x + box.w; ix++) {
            float dx = ix + 0.5f - cx, dy = iy + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float ring = clampf(ht + 0.5f - fabsf(d - radius), 0, 1);
            float cov = 0;
            if (ring > 0) {
                if (frac >= 1) {
                    cov = ring;
                } else {
                    float a = atan2_cw_from_top(dx, dy);
                    float ang = clampf((end - a) * d + 0.5f, 0, 1) * clampf(a * d + 0.5f, 0, 1);
                    cov = ring * ang;
                }
            }
            if (frac < 1) {
                float e1 = sqrtf((ix + 0.5f - ex) * (ix + 0.5f - ex) + (iy + 0.5f - ey) * (iy + 0.5f - ey));
                float e0 = sqrtf((ix + 0.5f - sx) * (ix + 0.5f - sx) + (iy + 0.5f - sy) * (iy + 0.5f - sy));
                float cap = clampf(ht + 0.5f - (e0 < e1 ? e0 : e1), 0, 1);
                if (cap > cov)
                    cov = cap;
            }
            if (cov > 0)
                row[ix] = blend(row[ix], rgb, (uint32_t)(ca * cov + 0.5f));
        }
    }
}

/* ---- images ---------------------------------------------------------- */

static inline uint32_t texel(const Image *img, int x, int y)
{
    if (x < 0 || y < 0 || x >= img->w || y >= img->h)
        return 0;
    return img->px[y * img->w + x];
}

static inline uint32_t lerp_pm(uint32_t a, uint32_t b, uint32_t t /* 0..256 */)
{
    uint32_t rb = ((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8;
    uint32_t ag = (((a >> 8) & 0xff00ff) * (256 - t) + ((b >> 8) & 0xff00ff) * t) >> 8;
    return (rb & 0xff00ff) | ((ag & 0xff00ff) << 8);
}

static inline uint32_t scale_pm(uint32_t p, uint32_t s /* 0..256 */)
{
    uint32_t rb = ((p & 0xff00ff) * s) >> 8;
    uint32_t ag = (((p >> 8) & 0xff00ff) * s) >> 8;
    return (rb & 0xff00ff) | ((ag & 0xff00ff) << 8);
}

void draw_image(Canvas *c, const Image *img, float x, float y, float w, float h,
                Color tint, float opacity)
{
    if (!img || w <= 0 || h <= 0 || opacity <= 0)
        return;
    Rect box = rect_intersect(rect_from_f(x, y, w, h, 0), c->clip);
    if (rect_empty(box))
        return;
    uint32_t op = (uint32_t)(clampf(opacity, 0, 1) * 256);
    uint32_t ta = COLOR_A(tint), trgb = tint & 0xffffff;
    bool exact = w == img->w && h == img->h && x == floorf(x) && y == floorf(y);
    float sx = img->w / w, sy = img->h / h;

    for (int iy = box.y; iy < box.y + box.h; iy++) {
        uint32_t *row = c->px + iy * c->stride;
        float v = (iy + 0.5f - y) * sy - 0.5f;
        int v0 = (int)floorf(v);
        uint32_t fv = (uint32_t)((v - v0) * 256);
        for (int ix = box.x; ix < box.x + box.w; ix++) {
            uint32_t p;
            if (exact) {
                p = texel(img, ix - (int)x, iy - (int)y);
            } else {
                float u = (ix + 0.5f - x) * sx - 0.5f;
                int u0 = (int)floorf(u);
                uint32_t fu = (uint32_t)((u - u0) * 256);
                uint32_t top = lerp_pm(texel(img, u0, v0), texel(img, u0 + 1, v0), fu);
                uint32_t bot = lerp_pm(texel(img, u0, v0 + 1), texel(img, u0 + 1, v0 + 1), fu);
                p = lerp_pm(top, bot, fv);
            }
            if (op < 256)
                p = scale_pm(p, op);
            uint32_t a = p >> 24;
            if (!a)
                continue;
            if (ta)
                row[ix] = blend(row[ix], trgb, (a * ta + 127) / 255);
            else
                row[ix] = blend_pm(row[ix], p);
        }
    }
}

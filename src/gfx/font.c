#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font.h"
#include "core/str.h"
#include "stb_truetype.h"

#define SUBPIXELS 4
#define GLYPH_SLOTS 512   /* open-addressed, power of two */
#define MAX_FACES 12
#define ADV_SLOTS 512     /* cached advances, open-addressed by codepoint */
#define KERN_SLOTS 2048   /* cached kerning pairs */

typedef struct {
    int key;              /* codepoint * SUBPIXELS + subpixel, -1 = empty */
    int w, h, xoff, yoff;
    uint8_t *bmp;
} Glyph;

struct Face {
    Font *font;
    float px, scale;
    float ascent, descent;
    Glyph glyphs[GLYPH_SLOTS];
    struct { int cp; float adv; } advs[ADV_SLOTS];
    struct { uint32_t pair; float kern; } kerns[KERN_SLOTS];
};

struct Font {
    stbtt_fontinfo info;
    Face *faces[MAX_FACES];
    int nfaces;
};

/* Light-on-dark text reads thin with linear coverage; lift midtones a bit. */
static uint8_t gamma_lut[256];

Font *font_load(const void *ttf, int size)
{
    (void)size;
    Font *f = calloc(1, sizeof(Font));
    if (!stbtt_InitFont(&f->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        free(f);
        return NULL;
    }
    if (!gamma_lut[255])
        for (int i = 0; i < 256; i++)
            gamma_lut[i] = (uint8_t)(255.0 * pow(i / 255.0, 0.8) + 0.5);
    return f;
}

Face *font_face(Font *f, float px)
{
    for (int i = 0; i < f->nfaces; i++)
        if (f->faces[i]->px == px)
            return f->faces[i];
    Face *face = calloc(1, sizeof(Face));
    face->font = f;
    face->px = px;
    face->scale = stbtt_ScaleForMappingEmToPixels(&f->info, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    face->ascent = asc * face->scale;
    face->descent = -desc * face->scale;
    for (int i = 0; i < GLYPH_SLOTS; i++)
        face->glyphs[i].key = -1;
    for (int i = 0; i < ADV_SLOTS; i++)
        face->advs[i].cp = -1;
    for (int i = 0; i < KERN_SLOTS; i++)
        face->kerns[i].pair = 0xffffffffu;
    if (f->nfaces < MAX_FACES)
        f->faces[f->nfaces++] = face;
    else
        f->faces[(int)px % MAX_FACES] = face;   /* rare: evict (leaks old face, bounded) */
    return face;
}

float face_ascent(const Face *f) { return f->ascent; }
float face_descent(const Face *f) { return f->descent; }

static Glyph *glyph_get(Face *f, int cp, int sub)
{
    int key = cp * SUBPIXELS + sub;
    unsigned h = ((unsigned)key * 2654435761u) & (GLYPH_SLOTS - 1);
    for (int probe = 0; probe < GLYPH_SLOTS; probe++) {
        Glyph *g = &f->glyphs[(h + probe) & (GLYPH_SLOTS - 1)];
        if (g->key == key)
            return g;
        if (g->key == -1) {
            g->key = key;
            float shift = (float)sub / SUBPIXELS;
            g->bmp = stbtt_GetCodepointBitmapSubpixel(&f->font->info, f->scale, f->scale, shift, 0,
                                                      cp, &g->w, &g->h, &g->xoff, &g->yoff);
            if (g->bmp)
                for (int i = 0; i < g->w * g->h; i++)
                    g->bmp[i] = gamma_lut[g->bmp[i]];
            return g;
        }
    }
    return NULL;
}

static int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int cp = *p++;
    if (cp >= 0xf0 && p[0] && p[1] && p[2]) {
        cp = ((cp & 0x07) << 18) | ((p[0] & 0x3f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        p += 3;
    } else if (cp >= 0xe0 && p[0] && p[1]) {
        cp = ((cp & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
        p += 2;
    } else if (cp >= 0xc0 && p[0]) {
        cp = ((cp & 0x1f) << 6) | (p[0] & 0x3f);
        p += 1;
    }
    *s = (const char *)p;
    return cp;
}

/* stb_truetype walks cmap/hmtx/GPOS on every query; cache both lookups. */
static float glyph_advance(Face *f, int cp)
{
    unsigned h = ((unsigned)cp * 2654435761u) & (ADV_SLOTS - 1);
    for (int probe = 0; probe < ADV_SLOTS; probe++) {
        unsigned i = (h + probe) & (ADV_SLOTS - 1);
        if (f->advs[i].cp == cp)
            return f->advs[i].adv;
        if (f->advs[i].cp == -1) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&f->font->info, cp, &adv, &lsb);
            f->advs[i].cp = cp;
            f->advs[i].adv = adv * f->scale;
            return f->advs[i].adv;
        }
    }
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->font->info, cp, &adv, &lsb);
    return adv * f->scale;
}

static float pair_kern(Face *f, int a, int b)
{
    if (a > 0xffff || b > 0xffff)
        return stbtt_GetCodepointKernAdvance(&f->font->info, a, b) * f->scale;
    uint32_t pair = ((uint32_t)a << 16) | (uint32_t)b;
    unsigned h = (pair * 2654435761u) & (KERN_SLOTS - 1);
    for (int probe = 0; probe < 16; probe++) {
        unsigned i = (h + probe) & (KERN_SLOTS - 1);
        if (f->kerns[i].pair == pair)
            return f->kerns[i].kern;
        if (f->kerns[i].pair == 0xffffffffu) {
            f->kerns[i].pair = pair;
            f->kerns[i].kern = stbtt_GetCodepointKernAdvance(&f->font->info, a, b) * f->scale;
            return f->kerns[i].kern;
        }
    }
    return stbtt_GetCodepointKernAdvance(&f->font->info, a, b) * f->scale;
}

static float advance(Face *f, int cp, int next)
{
    float a = glyph_advance(f, cp);
    if (next)
        a += pair_kern(f, cp, next);
    return a;
}

float text_width(Face *f, const char *s)
{
    float w = 0;
    while (*s) {
        int cp = utf8_next(&s);
        const char *peek = s;
        int next = *s ? utf8_next(&peek) : 0;
        w += advance(f, cp, next);
    }
    return w;
}

float text_draw(Canvas *c, Face *f, float x, float y, const char *s, Color col)
{
    float start = x;
    int by = (int)floorf(y + 0.5f);
    /* whole line outside the clip band: nothing to draw */
    if (by - f->ascent - 2 > c->clip.y + c->clip.h || by + f->descent + 2 < c->clip.y)
        return 0;
    uint32_t ca = COLOR_A(col), rgb = col & 0xffffff;
    while (*s) {
        int cp = utf8_next(&s);
        const char *peek = s;
        int next = *s ? utf8_next(&peek) : 0;
        int ix = (int)floorf(x);
        int sub = (int)((x - ix) * SUBPIXELS);
        Glyph *g = glyph_get(f, cp, sub);
        if (g && g->bmp && ca) {
            int gx = ix + g->xoff, gy = by + g->yoff;
            Rect box = rect_intersect(rect_make(gx, gy, g->w, g->h), c->clip);
            for (int yy = box.y; yy < box.y + box.h; yy++) {
                uint32_t *row = c->px + yy * c->stride;
                const uint8_t *src = g->bmp + (yy - gy) * g->w - gx;
                for (int xx = box.x; xx < box.x + box.w; xx++) {
                    uint32_t a = src[xx];
                    if (!a)
                        continue;
                    a = (a * ca + 127) / 255;
                    uint32_t d = row[xx], ia = 255 - a;
                    uint32_t rb = (rgb & 0xff00ff) * a + (d & 0xff00ff) * ia;
                    uint32_t gg = (rgb & 0x00ff00) * a + (d & 0x00ff00) * ia;
                    rb = ((rb + 0x800080 + ((rb >> 8) & 0xff00ff)) >> 8) & 0xff00ff;
                    gg = ((gg + 0x008000 + ((gg >> 8) & 0x00ff00)) >> 8) & 0x00ff00;
                    row[xx] = rb | gg;
                }
            }
        }
        x += advance(f, cp, next);
    }
    return x - start;
}

void text_ellipsize(Face *f, const char *s, float max_w, char *out, int cap)
{
    str_copy(out, s, (size_t)cap);
    if (text_width(f, out) <= max_w)
        return;
    const char *ell = "\xe2\x80\xa6";   /* … */
    float ew = text_width(f, ell);
    int n = (int)strlen(out);
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xc0) == 0x80)   /* utf-8 boundary */
            n--;
        out[n] = 0;
        if (text_width(f, out) + ew <= max_w)
            break;
    }
    while (n > 0 && out[n - 1] == ' ')
        out[--n] = 0;
    if (n + 4 <= cap)
        memcpy(out + n, ell, 4);
}

/*
 * Software renderer: 32-bit canvases, anti-aliased primitives and images.
 * Colors are 0xAARRGGBB, straight (non-premultiplied) alpha.
 */
#ifndef SHADE_GFX_H
#define SHADE_GFX_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t Color;

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    uint32_t *px;
    int w, h, stride;   /* stride in pixels */
    Rect clip;
} Canvas;

/* Premultiplied ARGB image. */
typedef struct {
    int w, h;
    uint32_t *px;
} Image;

#define COLOR_A(c) (((c) >> 24) & 0xff)
#define COLOR_R(c) (((c) >> 16) & 0xff)
#define COLOR_G(c) (((c) >> 8) & 0xff)
#define COLOR_B(c) ((c) & 0xff)
#define RGBA(r, g, b, a) (((Color)(a) << 24) | ((Color)(r) << 16) | ((Color)(g) << 8) | (Color)(b))

static inline Color color_fade(Color c, float opacity)
{
    if (opacity >= 1.0f)
        return c;
    if (opacity <= 0.0f)
        return c & 0x00ffffff;
    return (c & 0x00ffffff) | ((Color)(COLOR_A(c) * opacity + 0.5f) << 24);
}

Color color_mix(Color a, Color b, float t);   /* lerp, including alpha */

/* Rect helpers */
Rect rect_make(int x, int y, int w, int h);
Rect rect_intersect(Rect a, Rect b);
Rect rect_union(Rect a, Rect b);
bool rect_empty(Rect r);
bool rect_contains(Rect r, int x, int y);
Rect rect_from_f(float x, float y, float w, float h, int pad);   /* covering integer rect */

/* Canvas */
Canvas canvas_alloc(int w, int h);
void canvas_set_clip(Canvas *c, Rect r);   /* intersected with bounds */

/* ---- primitives (draw.c) -------------------------------------------- */
void draw_fill(Canvas *c, Rect r, Color col);
void draw_blit(Canvas *dst, const Canvas *src, Rect r);   /* copy same-position region */

/* Anti-aliased rounded rectangle with optional inside border of width bw. */
void draw_rrect(Canvas *c, float x, float y, float w, float h, float radius,
                Color fill, Color border, float bw);
/* Soft drop shadow for a rounded rect (cached gaussian-ish mask). */
void draw_shadow(Canvas *c, float x, float y, float w, float h, float radius,
                 float blur, float offset_y, Color col);
/* Progress ring: arc from 12 o'clock clockwise covering frac of the circle. */
void draw_arc(Canvas *c, float cx, float cy, float radius, float thickness, float frac, Color col);
void draw_circle(Canvas *c, float cx, float cy, float radius, Color col);
/* Image scaled into a float rect with bilinear filtering. If tint has non-zero
 * alpha, the image is used as an alpha mask colored by tint. */
void draw_image(Canvas *c, const Image *img, float x, float y, float w, float h,
                Color tint, float opacity);

/* ---- images (image.c) ----------------------------------------------- */
Image *image_load(const void *data, int size);           /* PNG via stb_image */
Image *image_resample(const Image *src, int w, int h);   /* high quality, cached by caller */
void   image_free(Image *img);

/* ---- display (fb.c) ------------------------------------------------- */
bool    fb_init(const char *resolution);   /* "auto", "max" or "WxH" */
Canvas *fb_back(void);
void    fb_present(Rect r);
void    fb_set_cursor(const Image *img, int hot_x, int hot_y);
void    fb_move_cursor(int x, int y, bool visible);  /* caller presents old/new rects */
Rect    fb_cursor_rect(void);
void    fb_clear(Color col);                         /* fill + present whole screen */
void    fb_restore(void);                            /* re-apply our mode after a loader returns */

#endif

/* TrueType text via stb_truetype with a per-size, subpixel-positioned glyph cache. */
#ifndef SHADE_FONT_H
#define SHADE_FONT_H

#include "gfx.h"

typedef struct Font Font;
typedef struct Face Face;

Font *font_load(const void *ttf, int size);    /* data must outlive the font */
Face *font_face(Font *f, float px);            /* cached per pixel size */

float face_ascent(const Face *f);
float face_descent(const Face *f);             /* positive */
float text_width(Face *f, const char *utf8);
/* Draws at baseline y; returns advance. */
float text_draw(Canvas *c, Face *f, float x, float y, const char *utf8, Color col);
/* Copies s into out, truncated with an ellipsis to fit max_w. */
void  text_ellipsize(Face *f, const char *s, float max_w, char *out, int cap);

#endif

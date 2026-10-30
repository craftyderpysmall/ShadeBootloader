/* PNG decoding and high-quality resampling into premultiplied ARGB. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gfx.h"
#define STBI_NO_STDIO
#include "stb_image.h"

Image *image_load(const void *data, int size)
{
    int w, h, n;
    unsigned char *rgba = stbi_load_from_memory(data, size, &w, &h, &n, 4);
    if (!rgba)
        return NULL;
    Image *img = malloc(sizeof(Image));
    img->w = w;
    img->h = h;
    img->px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint32_t r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        r = (r * a + 127) / 255;
        g = (g * a + 127) / 255;
        b = (b * a + 127) / 255;
        img->px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    stbi_image_free(rgba);
    return img;
}

void image_free(Image *img)
{
    if (img) {
        free(img->px);
        free(img);
    }
}

/*
 * Separable resample. Downscaling averages every source texel the output
 * pixel covers (box filter with fractional edges) so large icons shrink
 * without aliasing; upscaling falls back to linear interpolation.
 */
static void resample_line(const float *src, int sn, float *dst, int dn, int stride_s, int stride_d)
{
    float scale = (float)sn / dn;
    for (int i = 0; i < dn; i++) {
        float acc[4] = { 0, 0, 0, 0 }, wsum = 0;
        if (scale > 1) {
            float s0 = i * scale, s1 = s0 + scale;
            for (int j = (int)s0; j < (int)ceilf(s1) && j < sn; j++) {
                float lo = j > s0 ? j : s0, hi = j + 1 < s1 ? j + 1 : s1;
                float wgt = hi - lo;
                for (int k = 0; k < 4; k++)
                    acc[k] += src[j * stride_s + k] * wgt;
                wsum += wgt;
            }
        } else {
            float u = (i + 0.5f) * scale - 0.5f;
            int u0 = (int)floorf(u);
            float f = u - u0;
            int a = u0 < 0 ? 0 : u0, b = u0 + 1 >= sn ? sn - 1 : u0 + 1;
            for (int k = 0; k < 4; k++)
                acc[k] = src[a * stride_s + k] * (1 - f) + src[b * stride_s + k] * f;
            wsum = 1;
        }
        for (int k = 0; k < 4; k++)
            dst[i * stride_d + k] = wsum > 0 ? acc[k] / wsum : 0;
    }
}

Image *image_resample(const Image *src, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    float *a = malloc((size_t)src->w * src->h * 4 * sizeof(float));
    float *b = malloc((size_t)w * src->h * 4 * sizeof(float));
    float *c = malloc((size_t)w * h * 4 * sizeof(float));
    for (int i = 0; i < src->w * src->h; i++) {
        uint32_t p = src->px[i];
        a[i * 4 + 0] = (float)(p >> 24);
        a[i * 4 + 1] = (float)((p >> 16) & 0xff);
        a[i * 4 + 2] = (float)((p >> 8) & 0xff);
        a[i * 4 + 3] = (float)(p & 0xff);
    }
    for (int y = 0; y < src->h; y++)
        resample_line(a + y * src->w * 4, src->w, b + y * w * 4, w, 4, 4);
    for (int x = 0; x < w; x++)
        resample_line(b + x * 4, src->h, c + x * 4, h, w * 4, w * 4);

    Image *img = malloc(sizeof(Image));
    img->w = w;
    img->h = h;
    img->px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint32_t ch[4];
        for (int k = 0; k < 4; k++) {
            float v = c[i * 4 + k] + 0.5f;
            ch[k] = v < 0 ? 0 : (v > 255 ? 255 : (uint32_t)v);
        }
        for (int k = 1; k < 4; k++)   /* keep premultiplied invariant */
            if (ch[k] > ch[0])
                ch[k] = ch[0];
        img->px[i] = (ch[0] << 24) | (ch[1] << 16) | (ch[2] << 8) | ch[3];
    }
    free(a);
    free(b);
    free(c);
    return img;
}

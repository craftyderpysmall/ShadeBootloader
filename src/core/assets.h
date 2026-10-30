/* Files compiled into the binary (fonts, default theme, icons). */
#ifndef SHADE_ASSETS_H
#define SHADE_ASSETS_H

typedef struct {
    const char *name;
    const unsigned char *data;
    int size;
} Asset;

extern const Asset g_assets[];

const Asset *asset_find(const char *name);

#endif

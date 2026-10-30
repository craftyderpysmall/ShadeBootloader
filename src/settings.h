/* shade.conf: behaviour, ignore rules, per-entry options and custom entries. */
#ifndef SHADE_SETTINGS_H
#define SHADE_SETTINGS_H

#include <stdbool.h>
#include "boot/entry.h"

#define MAX_CUSTOM 16
#define MAX_OPTIONS 16

typedef struct {
    char id[32];
    char title[64];
    char path[256];
    char volume[64];        /* label glob; empty = Shade's own volume */
    char partuuid[40];
    char options[512];
    char icon[96];
    int  order;
} CustomEntry;

typedef struct {
    int   timeout;              /* seconds; -1 (default) waits for a choice, 0 boots immediately */
    char  default_entry[64];    /* title glob, or "last" */
    char  theme[48];
    char  resolution[24];       /* auto | max | WxH */
    float scale;                /* 0 = auto from screen height */
    bool  log;
    bool  connect_all;
    bool  scan_nvram;
    bool  hints;                /* keyboard hint line */
    bool  show_clock;
    float mouse_speed;

    IgnoreRules ignore;

    /* [options] title-glob = load options */
    const char *opt_match[MAX_OPTIONS];
    const char *opt_value[MAX_OPTIONS];
    int nopts;

    CustomEntry custom[MAX_CUSTOM];
    int ncustom;
} Settings;

/* root is the EFI_FILE_PROTOCOL of Shade's own volume, may be NULL. */
void settings_load(Settings *s, void *root, const char *self_dir);

#endif

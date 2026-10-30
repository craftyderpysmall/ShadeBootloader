/*
 * INI-style parser shared by shade.conf and theme.conf.
 *
 *   # comment            ; comment
 *   [section]            [entry.my-linux]
 *   key = value          key = "quoted value"
 *
 * Keys may repeat (the [ignore] section relies on that).
 */
#ifndef SHADE_CONFIG_H
#define SHADE_CONFIG_H

#include <stdbool.h>

typedef void (*ConfigCb)(void *user, const char *section, const char *key, const char *value);

/* Parses text in place-safe fashion (text is copied internally). */
void config_parse(const char *text, ConfigCb cb, void *user);

bool  config_bool(const char *v, bool def);
int   config_int(const char *v, int def);
float config_float(const char *v, float def);
/* "#rrggbb" or "#rrggbbaa" -> 0xAARRGGBB; returns def on parse failure. */
unsigned config_color(const char *v, unsigned def);

#endif

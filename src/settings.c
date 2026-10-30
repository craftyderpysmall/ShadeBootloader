#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "core/config.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"

static void add_rule(const char **list, int *n, const char *v)
{
    if (*n < MAX_RULES)
        list[(*n)++] = str_dup(v);
}

static CustomEntry *custom_for(Settings *s, const char *id)
{
    for (int i = 0; i < s->ncustom; i++)
        if (str_eq_ci(s->custom[i].id, id))
            return &s->custom[i];
    if (s->ncustom == MAX_CUSTOM)
        return NULL;
    CustomEntry *c = &s->custom[s->ncustom++];
    memset(c, 0, sizeof(*c));
    str_copy(c->id, id, sizeof(c->id));
    str_copy(c->title, id, sizeof(c->title));
    c->order = 80;
    return c;
}

static void on_key(void *user, const char *sec, const char *key, const char *v)
{
    Settings *s = user;
    if (!*sec) {
        if (str_eq_ci(key, "timeout")) s->timeout = config_int(v, s->timeout);
        else if (str_eq_ci(key, "default")) str_copy(s->default_entry, v, sizeof(s->default_entry));
        else if (str_eq_ci(key, "theme")) str_copy(s->theme, v, sizeof(s->theme));
        else if (str_eq_ci(key, "resolution")) str_copy(s->resolution, v, sizeof(s->resolution));
        else if (str_eq_ci(key, "scale")) s->scale = config_float(v, s->scale);
        else if (str_eq_ci(key, "log")) s->log = str_eq_ci(v, "debugcon") || config_bool(v, false);
        else if (str_eq_ci(key, "connect-all")) s->connect_all = config_bool(v, s->connect_all);
        else if (str_eq_ci(key, "scan-nvram")) s->scan_nvram = config_bool(v, s->scan_nvram);
        else if (str_eq_ci(key, "hints")) s->hints = config_bool(v, s->hints);
        else if (str_eq_ci(key, "clock")) s->show_clock = config_bool(v, s->show_clock);
        else if (str_eq_ci(key, "mouse-speed")) s->mouse_speed = config_float(v, s->mouse_speed);
        else LOG("config: unknown key %s", key);
    } else if (str_eq_ci(sec, "ignore")) {
        if (str_eq_ci(key, "volume")) add_rule(s->ignore.volume, &s->ignore.nvolume, v);
        else if (str_eq_ci(key, "partuuid")) add_rule(s->ignore.partuuid, &s->ignore.npartuuid, v);
        else if (str_eq_ci(key, "path")) add_rule(s->ignore.path, &s->ignore.npath, v);
        else if (str_eq_ci(key, "entry")) add_rule(s->ignore.entry, &s->ignore.nentry, v);
        else LOG("config: unknown ignore rule %s", key);
    } else if (str_eq_ci(sec, "options")) {
        if (s->nopts < MAX_OPTIONS) {
            s->opt_match[s->nopts] = str_dup(key);
            s->opt_value[s->nopts++] = str_dup(v);
        }
    } else if (str_starts_ci(sec, "entry.")) {
        CustomEntry *c = custom_for(s, sec + 6);
        if (!c) return;
        if (str_eq_ci(key, "title")) str_copy(c->title, v, sizeof(c->title));
        else if (str_eq_ci(key, "path")) str_copy(c->path, v, sizeof(c->path));
        else if (str_eq_ci(key, "volume")) str_copy(c->volume, v, sizeof(c->volume));
        else if (str_eq_ci(key, "partuuid")) str_copy(c->partuuid, v, sizeof(c->partuuid));
        else if (str_eq_ci(key, "options")) str_copy(c->options, v, sizeof(c->options));
        else if (str_eq_ci(key, "icon")) str_copy(c->icon, v, sizeof(c->icon));
        else if (str_eq_ci(key, "order")) c->order = config_int(v, c->order);
    } else {
        LOG("config: unknown section [%s]", sec);
    }
}

void settings_load(Settings *s, void *root, const char *self_dir)
{
    memset(s, 0, sizeof(*s));
    s->timeout = -1;        /* manual: wait until someone picks */
    str_copy(s->theme, "neutral", sizeof(s->theme));
    str_copy(s->resolution, "auto", sizeof(s->resolution));
    s->connect_all = true;
    s->scan_nvram = true;
    s->hints = true;
    s->show_clock = true;
    s->mouse_speed = 1.0f;

    if (!root)
        return;
    char path[256];
    size_t n;
    char *text = NULL;
    if (self_dir && *self_dir) {
        str_fmt(path, sizeof(path), "%s\\shade.conf", self_dir);
        text = file_read_all(root, path, &n);
    }
    if (!text) {
        str_copy(path, "\\EFI\\shade\\shade.conf", sizeof(path));
        text = file_read_all(root, path, &n);
    }
    if (text) {
        config_parse(text, on_key, s);
        free(text);
        log_enable(s->log);
        LOG("config: loaded %s", path);
    }
}

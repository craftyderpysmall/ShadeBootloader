#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "str.h"

static char *unquote(char *v)
{
    size_t n = strlen(v);
    if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') || (v[0] == '\'' && v[n - 1] == '\''))) {
        v[n - 1] = 0;
        return v + 1;
    }
    /* strip trailing inline comment on unquoted values: "5   # seconds" */
    for (char *p = v; *p; p++) {
        if ((*p == '#' || *p == ';') && p > v && (p[-1] == ' ' || p[-1] == '\t')) {
            *p = 0;
            return str_trim(v);
        }
    }
    return v;
}

void config_parse(const char *text, ConfigCb cb, void *user)
{
    char *copy = str_dup(text);
    char section[64] = "";
    char *line = copy;
    /* skip UTF-8 BOM */
    if ((unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf)
        line += 3;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *s = str_trim(line);
        if (*s == '#' || *s == ';' || *s == 0) {
            /* comment / blank */
        } else if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = 0;
                str_copy(section, str_trim(s + 1), sizeof(section));
            }
        } else {
            char *eq = strchr(s, '=');
            if (eq) {
                *eq = 0;
                char *key = str_trim(s);
                char *val = unquote(str_trim(eq + 1));
                cb(user, section, key, val);
            }
        }
        line = next;
    }
    free(copy);
}

bool config_bool(const char *v, bool def)
{
    if (!v)
        return def;
    if (str_eq_ci(v, "true") || str_eq_ci(v, "yes") || str_eq_ci(v, "on") || str_eq_ci(v, "1"))
        return true;
    if (str_eq_ci(v, "false") || str_eq_ci(v, "no") || str_eq_ci(v, "off") || str_eq_ci(v, "0"))
        return false;
    return def;
}

int config_int(const char *v, int def)
{
    if (!v || !*v)
        return def;
    char *end;
    long n = strtol(v, &end, 0);
    return end == v ? def : (int)n;
}

float config_float(const char *v, float def)
{
    if (!v || !*v)
        return def;
    float sign = 1, val = 0, frac = 0.1f;
    const char *p = v;
    if (*p == '-') { sign = -1; p++; }
    if (!(*p >= '0' && *p <= '9') && *p != '.')
        return def;
    while (*p >= '0' && *p <= '9')
        val = val * 10 + (*p++ - '0');
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            val += (*p++ - '0') * frac;
            frac *= 0.1f;
        }
    }
    return sign * val;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = str_lower_c(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

unsigned config_color(const char *v, unsigned def)
{
    if (!v || *v != '#')
        return def;
    v++;
    size_t n = strlen(v);
    if (n != 6 && n != 8)
        return def;
    unsigned c = 0;
    for (size_t i = 0; i < n; i++) {
        int h = hexval(v[i]);
        if (h < 0)
            return def;
        c = (c << 4) | (unsigned)h;
    }
    if (n == 6)
        return 0xff000000u | c;
    return (c >> 8) | ((c & 0xff) << 24);   /* rrggbbaa -> aarrggbb */
}

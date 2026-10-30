#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"

char str_lower_c(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int str_casecmp(const char *a, const char *b)
{
    while (*a && str_lower_c(*a) == str_lower_c(*b))
        a++, b++;
    return (uint8_t)str_lower_c(*a) - (uint8_t)str_lower_c(*b);
}

int str_ncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        char x = str_lower_c(*a), y = str_lower_c(*b);
        if (x != y || !x)
            return (uint8_t)x - (uint8_t)y;
    }
    return 0;
}

bool str_eq_ci(const char *a, const char *b)
{
    return a && b && str_casecmp(a, b) == 0;
}

bool str_starts_ci(const char *s, const char *prefix)
{
    return str_ncasecmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && str_casecmp(s + n - m, suffix) == 0;
}

bool str_contains_ci(const char *s, const char *needle)
{
    size_t m = strlen(needle);
    for (; *s; s++)
        if (str_ncasecmp(s, needle, m) == 0)
            return true;
    return m == 0;
}

static char glob_norm(char c)
{
    return c == '/' ? '\\' : str_lower_c(c);
}

bool str_glob_ci(const char *p, const char *s)
{
    const char *star = NULL, *retry = NULL;
    while (*s) {
        if (*p == '*') {
            star = ++p;
            retry = s;
        } else if (*p == '?' || (*p && glob_norm(*p) == glob_norm(*s))) {
            p++, s++;
        } else if (star) {
            p = star;
            s = ++retry;
        } else {
            return false;
        }
    }
    while (*p == '*')
        p++;
    return *p == 0;
}

char *str_ndup(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *str_dup(const char *s)
{
    return s ? str_ndup(s, strlen(s)) : NULL;
}

char *str_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

const char *str_basename(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            b = p + 1;
    return b;
}

size_t str_copy(char *dst, const char *src, size_t cap)
{
    size_t n = 0;
    if (!cap)
        return 0;
    while (src && src[n] && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    return n;
}

/* ---- formatting ------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t cap, len;
} Out;

static void out_c(Out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void out_num(Out *o, uint64_t v, int base, bool upper, bool neg, int width, char pad, bool left)
{
    char tmp[24];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    if (neg)
        tmp[n++] = '-';
    if (!left)
        for (int i = n; i < width; i++)
            out_c(o, pad);
    while (n)
        out_c(o, tmp[--n]);
}

int str_vfmt(char *buf, size_t cap, const char *fmt, va_list ap)
{
    Out o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        int width = 0, lng = 0;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng++; fmt++; }
        size_t start = o.len;
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t n = strlen(s);
            if (!left) for (size_t i = n; (int)i < width; i++) out_c(&o, ' ');
            while (*s) out_c(&o, *s++);
            break;
        }
        case 'c':
            out_c(&o, (char)va_arg(ap, int));
            break;
        case 'd': case 'i': {
            int64_t v = lng >= 2 ? va_arg(ap, int64_t) : lng ? (int64_t)va_arg(ap, long) : va_arg(ap, int);
            out_num(&o, v < 0 ? (uint64_t)-v : (uint64_t)v, 10, false, v < 0, width, pad, left);
            break;
        }
        case 'u':
            out_num(&o, lng >= 2 ? va_arg(ap, uint64_t) : lng ? (uint64_t)va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, false, false, width, pad, left);
            break;
        case 'x': case 'X':
            out_num(&o, lng >= 2 ? va_arg(ap, uint64_t) : lng ? (uint64_t)va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, *fmt == 'X', false, width, pad, left);
            break;
        case 'p':
            out_c(&o, '0'); out_c(&o, 'x');
            out_num(&o, (uint64_t)(uintptr_t)va_arg(ap, void *), 16, false, false, 0, ' ', false);
            break;
        case '%':
            out_c(&o, '%');
            break;
        default:
            out_c(&o, '%');
            if (*fmt) out_c(&o, *fmt); else fmt--;
            break;
        }
        if (left)
            while ((int)(o.len - start) < width)
                out_c(&o, ' ');
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int str_fmt(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = str_vfmt(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

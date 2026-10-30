/* Pure string helpers: no firmware dependencies, also built for host tests. */
#ifndef SHADE_STR_H
#define SHADE_STR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

char  str_lower_c(char c);
int   str_casecmp(const char *a, const char *b);
int   str_ncasecmp(const char *a, const char *b, size_t n);
bool  str_eq_ci(const char *a, const char *b);
bool  str_starts_ci(const char *s, const char *prefix);
bool  str_ends_ci(const char *s, const char *suffix);
bool  str_contains_ci(const char *s, const char *needle);
/* Case-insensitive glob: '*' any run, '?' one char; '/' and '\' are equal. */
bool  str_glob_ci(const char *pattern, const char *s);
char *str_dup(const char *s);
char *str_ndup(const char *s, size_t n);
char *str_trim(char *s);                 /* in place, returns s */
const char *str_basename(const char *path);
size_t str_copy(char *dst, const char *src, size_t cap);   /* always NUL-terminates */

/* Minimal printf: %s %d %i %u %x %X %p %c %%, with optional width / '0' / '-' / 'l'. */
int str_vfmt(char *buf, size_t cap, const char *fmt, va_list ap);
int str_fmt(char *buf, size_t cap, const char *fmt, ...);

#endif

/* Freestanding stdlib.h shim: implemented in src/core/libc.c */
#ifndef SHADE_LIBC_STDLIB_H
#define SHADE_LIBC_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void  free(void *p);
int   abs(int x);
long  strtol(const char *s, char **end, int base);
#endif

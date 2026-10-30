/*
 * Freestanding C runtime for the UEFI build: memory, heap and the handful of
 * math routines stb_truetype / stb_image and the renderer need.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "efi_util.h"

/* ---- memory ---------------------------------------------------------- */

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n];
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a)
            return (uint8_t)*a - (uint8_t)*b;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

/* ---- heap (AllocatePool with a size header for realloc) -------------- */

typedef struct {
    size_t size;
    size_t pad;     /* keep payload 16-byte aligned */
} HeapHdr;

void *malloc(size_t n)
{
    HeapHdr *h;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(HeapHdr) + n, (void **)&h) != EFI_SUCCESS)
        return NULL;
    h->size = n;
    return h + 1;
}

void *calloc(size_t n, size_t m)
{
    void *p = malloc(n * m);
    if (p)
        memset(p, 0, n * m);
    return p;
}

void free(void *p)
{
    if (p)
        gBS->FreePool((HeapHdr *)p - 1);
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    HeapHdr *h = (HeapHdr *)p - 1;
    if (n <= h->size)
        return p;
    void *q = malloc(n);
    if (q) {
        memcpy(q, p, h->size);
        free(p);
    }
    return q;
}

int abs(int x) { return x < 0 ? -x : x; }

long strtol(const char *s, char **end, int base)
{
    long v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-' || *s == '+')
        neg = (*s++ == '-');
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    if (base == 0)
        base = 10;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base)
            break;
        v = v * base + d;
    }
    if (end)
        *end = (char *)s;
    return neg ? -v : v;
}

/* ---- math ------------------------------------------------------------ */

/* MSVC-ABI marker the linker expects when floating point is used. */
int _fltused = 1;

#define PI 3.14159265358979323846

double sqrt(double x)
{
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

float sqrtf(float x)
{
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }

double floor(double x)
{
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0 || x != x)
        return x;
    double i = (double)(int64_t)x;
    return i > x ? i - 1.0 : i;
}

double ceil(double x)
{
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0 || x != x)
        return x;
    double i = (double)(int64_t)x;
    return i < x ? i + 1.0 : i;
}

float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }

double fmod(double x, double y)
{
    if (y == 0)
        return 0;
    double q = x / y;
    q = q < 0 ? ceil(q) : floor(q);
    return x - q * y;
}

double ldexp(double x, int e)
{
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x *= 0.5; e++; }
    return x;
}

double exp(double x)
{
    if (x > 709)
        return 1.0 / 0.0;
    if (x < -745)
        return 0;
    /* x = k ln2 + r, |r| <= ln2/2 */
    const double ln2 = 0.69314718055994530942;
    int k = (int)floor(x / ln2 + 0.5);
    double r = x - k * ln2, term = 1, sum = 1;
    for (int i = 1; i < 18; i++) {
        term *= r / i;
        sum += term;
    }
    return ldexp(sum, k);
}

double log(double x)
{
    if (x <= 0)
        return -1.0 / 0.0;
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x < 1.0) { x *= 2.0; e--; }
    /* log(m) = 2 atanh((m-1)/(m+1)) */
    double s = (x - 1) / (x + 1), s2 = s * s, term = s, sum = 0;
    for (int i = 1; i < 40; i += 2) {
        sum += term / i;
        term *= s2;
    }
    return 2 * sum + e * 0.69314718055994530942;
}

double pow(double x, double y)
{
    if (y == 0)
        return 1;
    if (x == 0)
        return 0;
    if (x < 0) {
        double iy = floor(y);
        if (iy != y)
            return 0.0 / 0.0;
        double r = exp(y * log(-x));
        return ((int64_t)iy & 1) ? -r : r;
    }
    return exp(y * log(x));
}

float powf(float x, float y) { return (float)pow(x, y); }
float expf(float x) { return (float)exp(x); }

double sin(double x)
{
    x = fmod(x, 2 * PI);
    if (x > PI) x -= 2 * PI;
    if (x < -PI) x += 2 * PI;
    double term = x, sum = x, x2 = x * x;
    for (int i = 1; i < 12; i++) {
        term *= -x2 / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

double cos(double x) { return sin(x + PI / 2); }

static double atan_small(double x)
{
    /* |x| <= 0.42: series converges quickly */
    double term = x, sum = x, x2 = x * x;
    for (int i = 1; i < 30; i++) {
        term *= -x2;
        sum += term / (2 * i + 1);
    }
    return sum;
}

static double atan_(double x)
{
    if (x < 0)
        return -atan_(-x);
    if (x > 1)
        return PI / 2 - atan_(1 / x);
    if (x > 0.41421356)   /* tan(pi/8) */
        return PI / 4 + atan_small((x - 1) / (x + 1));
    return atan_small(x);
}

double acos(double x)
{
    if (x <= -1) return PI;
    if (x >= 1) return 0;
    return 2 * atan_(sqrt((1 - x) / (1 + x)));
}

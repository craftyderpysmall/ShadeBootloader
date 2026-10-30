/* Freestanding math.h shim: implemented in src/core/libc.c */
#ifndef SHADE_LIBC_MATH_H
#define SHADE_LIBC_MATH_H
double sqrt(double x);
double floor(double x);
double ceil(double x);
double fabs(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double cos(double x);
double sin(double x);
double acos(double x);
double ldexp(double x, int e);
float  sqrtf(float x);
float  floorf(float x);
float  ceilf(float x);
float  fabsf(float x);
float  powf(float x, float y);
float  expf(float x);
#endif

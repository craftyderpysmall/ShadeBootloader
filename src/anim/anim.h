/*
 * Motion primitives following the theme's motion rules:
 *   enter  -> decelerate curve, ~300ms
 *   exit   -> accelerate curve, ~half the enter time
 *   follow -> "settle" spring: critically-ish damped, ~2% overshoot
 */
#ifndef SHADE_ANIM_H
#define SHADE_ANIM_H

#include <stdbool.h>

typedef enum { EASE_LINEAR, EASE_DECEL, EASE_ACCEL, EASE_STANDARD } Ease;

float ease(Ease e, float t);

typedef struct {
    float from, to;
    double start;       /* seconds */
    float dur;          /* seconds */
    Ease curve;
} Tween;

void  tween_start(Tween *t, float from, float to, float dur_ms, Ease e, double now);
float tween_value(const Tween *t, double now);
bool  tween_active(const Tween *t, double now);

typedef struct {
    float x, v, target;
    float k, zeta;      /* stiffness, damping ratio */
} Spring;

void spring_init(Spring *s, float x, float k, float zeta);
void spring_step(Spring *s, float dt);
bool spring_settled(const Spring *s);

#endif

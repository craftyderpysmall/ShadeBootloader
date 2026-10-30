#include <math.h>
#include "anim.h"

/* cubic-bezier(x1, y1, x2, y2) evaluated at x = t, CSS semantics */
static float bezier(float x1, float y1, float x2, float y2, float t)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx;
    float cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
    float u = t;
    for (int i = 0; i < 8; i++) {   /* Newton on x(u) = t */
        float x = ((ax * u + bx) * u + cx) * u - t;
        float dx = (3 * ax * u + 2 * bx) * u + cx;
        if (fabsf(x) < 1e-5f || fabsf(dx) < 1e-6f)
            break;
        u -= x / dx;
    }
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    return ((ay * u + by) * u + cy) * u;
}

float ease(Ease e, float t)
{
    switch (e) {
    case EASE_DECEL:    return bezier(0.05f, 0.7f, 0.1f, 1.0f, t);
    case EASE_ACCEL:    return bezier(0.3f, 0.0f, 0.8f, 0.15f, t);
    case EASE_STANDARD: return bezier(0.2f, 0.0f, 0.0f, 1.0f, t);
    default:            return t < 0 ? 0 : (t > 1 ? 1 : t);
    }
}

void tween_start(Tween *t, float from, float to, float dur_ms, Ease e, double now)
{
    t->from = from;
    t->to = to;
    t->start = now;
    t->dur = dur_ms / 1000.0f;
    t->curve = e;
}

float tween_value(const Tween *t, double now)
{
    if (t->dur <= 0)
        return t->to;
    float p = (float)((now - t->start) / t->dur);
    return t->from + (t->to - t->from) * ease(t->curve, p);
}

bool tween_active(const Tween *t, double now)
{
    return t->dur > 0 && now - t->start < t->dur;
}

void spring_init(Spring *s, float x, float k, float zeta)
{
    s->x = s->target = x;
    s->v = 0;
    s->k = k;
    s->zeta = zeta;
}

void spring_step(Spring *s, float dt)
{
    if (dt > 0.1f)
        dt = 0.1f;      /* after a stall, don't explode */
    float c = 2 * s->zeta * sqrtf(s->k);
    /* fixed small substeps keep semi-implicit Euler stable at any frame rate */
    while (dt > 0) {
        float h = dt > 0.004f ? 0.004f : dt;
        float a = -s->k * (s->x - s->target) - c * s->v;
        s->v += a * h;
        s->x += s->v * h;
        dt -= h;
    }
    if (spring_settled(s)) {
        s->x = s->target;
        s->v = 0;
    }
}

bool spring_settled(const Spring *s)
{
    return fabsf(s->x - s->target) < 0.25f && fabsf(s->v) < 2.0f;
}

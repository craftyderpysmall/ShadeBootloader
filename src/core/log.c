#include <stdarg.h>
#include <stdint.h>
#include "log.h"
#include "str.h"

static bool enabled;

void log_enable(bool on)
{
    enabled = on;
}

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

void log_msg(const char *fmt, ...)
{
    if (!enabled)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    str_vfmt(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (const char *p = "[shade] "; *p; p++)
        outb(0xe9, (uint8_t)*p);
    for (const char *p = buf; *p; p++)
        outb(0xe9, (uint8_t)*p);
    outb(0xe9, '\n');
}

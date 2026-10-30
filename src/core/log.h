/*
 * Debug log. Lines go to the QEMU/Bochs debug console (I/O port 0xE9) when
 * enabled with `log = debugcon` in shade.conf; on real hardware that port
 * write is ignored, so the default is off.
 */
#ifndef SHADE_LOG_H
#define SHADE_LOG_H

#include <stdbool.h>

void log_enable(bool on);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG(...) log_msg(__VA_ARGS__)

#endif

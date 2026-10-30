/*
 * Minimal HID report parsing for pointers (mice, touchpads in mouse mode,
 * tablets). Pure C so it can be unit-tested on the host.
 */
#ifndef SHADE_HID_H
#define SHADE_HID_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int  off, size;         /* bit offset (after any report ID byte) and width; size 0 = absent */
    bool sign;              /* logical minimum < 0 */
    int32_t lmin, lmax;
    int  rid;               /* report ID the field lives in */
} HidField;

typedef struct {
    bool     ok;            /* found X and Y */
    bool     absolute;      /* X/Y are absolute (tablet, touchscreen) */
    int      report_id;     /* 0 when the device doesn't use report IDs */
    HidField x, y, wheel;
    HidField btn[3];        /* left, right, middle */
} HidLayout;

typedef struct {
    int32_t x, y, wheel;    /* raw values (deltas when relative) */
    unsigned buttons;       /* bit 0 left, 1 right, 2 middle */
} HidReport;

/* Parse a HID report descriptor; returns false if it describes no pointer. */
bool hid_parse(const uint8_t *desc, int len, HidLayout *out);

/* The fixed 3-byte boot-protocol mouse layout. */
void hid_boot_mouse(HidLayout *out);

/* Decode one input report; false if it isn't the pointer report. */
bool hid_decode(const HidLayout *l, const uint8_t *data, int len, HidReport *out);

#endif

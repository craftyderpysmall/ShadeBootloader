#include <string.h>
#include "hid.h"

#define MAX_USAGES 16

enum { PAGE_DESKTOP = 0x01, PAGE_BUTTON = 0x09 };
enum { USAGE_X = 0x30, USAGE_Y = 0x31, USAGE_WHEEL = 0x38 };

static uint32_t item_u(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static int32_t item_s(const uint8_t *p, int n)
{
    uint32_t v = item_u(p, n);
    if (n > 0 && n < 4 && (v & (1u << (8 * n - 1))))
        v |= ~0u << (8 * n);     /* sign-extend */
    return (int32_t)v;
}

static void set_field(HidField *f, int rid, int off, int size, int32_t lmin, int32_t lmax)
{
    if (f->size)
        return;     /* keep the first match */
    f->rid = rid;
    f->off = off;
    f->size = size;
    f->lmin = lmin;
    f->lmax = lmax;
    f->sign = lmin < 0;
}

bool hid_parse(const uint8_t *d, int len, HidLayout *out)
{
    memset(out, 0, sizeof(*out));
    uint32_t page = 0, rsize = 0, rcount = 0;
    int32_t lmin = 0, lmax = 0;
    int rid = 0;
    bool uses_ids = false;
    uint32_t usages[MAX_USAGES];
    int nusage = 0;
    uint32_t umin = 0, umax = 0;
    bool range = false;
    int offsets[256];       /* input bit offset per report ID */
    int ptr_rid = -1;       /* report that carries X/Y */
    memset(offsets, 0, sizeof(offsets));

    for (int i = 0; i < len;) {
        uint8_t b = d[i];
        if (b == 0xfe) {                    /* long item: skip */
            if (i + 2 >= len)
                break;
            i += 3 + d[i + 1];
            continue;
        }
        int n = b & 3;
        if (n == 3)
            n = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        if (i + 1 + n > len)
            break;                          /* truncated descriptor */
        const uint8_t *data = d + i + 1;
        uint32_t u = item_u(data, n);
        i += 1 + n;

        if (type == 1) {                    /* global */
            switch (tag) {
            case 0: page = u; break;
            case 1: lmin = item_s(data, n); break;
            case 2: lmax = item_s(data, n); break;
            case 7: rsize = u; break;
            case 8: rid = (int)(u & 0xff); uses_ids = true; break;
            case 9: rcount = u; break;
            }
        } else if (type == 2) {             /* local */
            uint32_t full = n == 4 ? u : (page << 16) | u;
            if (tag == 0 && nusage < MAX_USAGES)
                usages[nusage++] = full;
            else if (tag == 1)
                umin = full, range = true;
            else if (tag == 2)
                umax = full, range = true;
        } else if (type == 0) {             /* main */
            if (tag == 8) {                 /* Input */
                /* logical max can be an unsigned value that looks negative */
                int32_t lo = lmin, hi = lmax;
                if (lo >= 0 && hi < 0 && rsize < 32)
                    hi = (int32_t)(lmax & ((1u << rsize) - 1));
                for (uint32_t k = 0; k < rcount && k < 4096; k++) {
                    uint32_t us;
                    if (range)
                        us = umin + k <= umax ? umin + k : umax;
                    else
                        us = nusage ? usages[k < (uint32_t)nusage ? k : (uint32_t)nusage - 1] : 0;
                    int off = offsets[rid];
                    offsets[rid] += (int)rsize;
                    if ((u & 1) || !(u & 2))    /* constant padding, or array */
                        continue;
                    uint32_t up = us >> 16, uu = us & 0xffff;
                    if (up == PAGE_DESKTOP && (uu == USAGE_X || uu == USAGE_Y)) {
                        if (ptr_rid < 0)
                            ptr_rid = rid;
                        if (rid != ptr_rid)
                            continue;
                        set_field(uu == USAGE_X ? &out->x : &out->y, rid, off, (int)rsize, lo, hi);
                        out->absolute = !(u & 4);
                    } else if (up == PAGE_DESKTOP && uu == USAGE_WHEEL) {
                        set_field(&out->wheel, rid, off, (int)rsize, lo, hi);
                    } else if (up == PAGE_BUTTON && uu >= 1 && uu <= 3) {
                        set_field(&out->btn[uu - 1], rid, off, (int)rsize, lo, hi);
                    }
                }
            }
            nusage = 0;                     /* locals reset after every main item */
            range = false;
            umin = umax = 0;
        }
    }
    if (!out->x.size || !out->y.size)
        return false;
    out->report_id = uses_ids ? ptr_rid : 0;
    /* buttons/wheel from a different report than X/Y can't be decoded with it */
    if (out->wheel.rid != ptr_rid)
        memset(&out->wheel, 0, sizeof(out->wheel));
    for (int k = 0; k < 3; k++)
        if (out->btn[k].rid != ptr_rid)
            memset(&out->btn[k], 0, sizeof(out->btn[k]));
    out->ok = true;
    return true;
}

void hid_boot_mouse(HidLayout *l)
{
    memset(l, 0, sizeof(*l));
    l->ok = true;
    for (int i = 0; i < 3; i++) {
        l->btn[i].off = i;
        l->btn[i].size = 1;
        l->btn[i].lmax = 1;
    }
    l->x = (HidField){ 8, 8, true, -127, 127, 0 };
    l->y = (HidField){ 16, 8, true, -127, 127, 0 };
}

static int32_t extract(const uint8_t *data, int bytes, const HidField *f)
{
    if (!f->size || f->size > 32 || f->off + f->size > bytes * 8)
        return 0;
    uint64_t v = 0;
    for (int b = 0; b < f->size; b++) {
        int bit = f->off + b;
        v |= (uint64_t)((data[bit >> 3] >> (bit & 7)) & 1) << b;
    }
    if (f->sign && f->size < 32 && (v & (1ull << (f->size - 1))))
        v |= ~0ull << f->size;
    return (int32_t)v;
}

bool hid_decode(const HidLayout *l, const uint8_t *data, int len, HidReport *out)
{
    if (!l->ok || len <= 0)
        return false;
    if (l->report_id) {
        if (data[0] != l->report_id)
            return false;
        data++;
        len--;
    }
    if (l->x.off + l->x.size > len * 8 || l->y.off + l->y.size > len * 8)
        return false;
    out->x = extract(data, len, &l->x);
    out->y = extract(data, len, &l->y);
    out->wheel = extract(data, len, &l->wheel);
    out->buttons = 0;
    for (int i = 0; i < 3; i++)
        if (extract(data, len, &l->btn[i]))
            out->buttons |= 1u << i;
    return true;
}

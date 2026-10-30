/*
 * Fallback USB HID pointer support.
 *
 * Plenty of firmware (OVMF included) ships no USB mouse driver, so the
 * console's pointer protocols never report anything. For every HID mouse or
 * tablet interface that no firmware driver has claimed, Shade reads the
 * device itself: it fetches the report descriptor, then lets the host
 * controller poll the interrupt endpoint and decodes reports in a callback.
 */
#include <stdlib.h>
#include <string.h>
#include "usbhid.h"
#include "hid.h"
#include "core/efi_util.h"
#include "core/log.h"

#define MAX_DEVS 4

typedef struct {
    EFI_USB_IO_PROTOCOL *io;
    UINT8 ep;
    UINTN pkt;
    HidLayout layout;
    bool running;
    /* written by the callback at TPL_NOTIFY, read with that TPL raised */
    int32_t dx, dy, wheel;
    int32_t ax, ay;
    unsigned buttons;
    bool fresh;
} HidDev;

static HidDev devs[MAX_DEVS];
static int ndevs;
static EFI_EVENT ready;

static EFI_GUID gUsbIoGuid = EFI_USB_IO_PROTOCOL_GUID;
static EFI_GUID gTextInGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;

static bool has_protocol(EFI_HANDLE h, EFI_GUID *g)
{
    void *p;
    return gBS->HandleProtocol(h, g, &p) == EFI_SUCCESS;
}

static EFI_STATUS EFIAPI on_report(VOID *data, UINTN len, VOID *ctx, UINT32 status)
{
    HidDev *d = ctx;
    HidReport r;
    if (status != 0 || !data || !hid_decode(&d->layout, data, (int)len, &r))
        return EFI_SUCCESS;
    if (d->layout.absolute) {
        d->ax = r.x;
        d->ay = r.y;
    } else {
        d->dx += r.x;
        d->dy += r.y;
    }
    d->wheel += r.wheel;
    d->buttons = r.buttons;
    d->fresh = true;
    if (ready)
        gBS->SignalEvent(ready);
    return EFI_SUCCESS;
}

static EFI_STATUS control(EFI_USB_IO_PROTOCOL *io, UINT8 type, UINT8 req, UINT16 value, UINT16 index,
                          void *data, UINT16 len)
{
    EFI_USB_DEVICE_REQUEST r = { type, req, value, index, len };
    UINT32 st = 0;
    return io->UsbControlTransfer(io, &r, data ? EfiUsbDataIn : EfiUsbNoData, 1000, data, len, &st);
}

/* Report descriptor length, from the HID descriptor inside the configuration
 * descriptor (devices aren't required to answer for it on its own). */
static UINT16 report_desc_len(EFI_USB_IO_PROTOCOL *io, UINT8 ifn)
{
    uint8_t head[9];
    if (control(io, 0x80, 6, 0x0200, 0, head, sizeof(head)) != EFI_SUCCESS || head[1] != 2)
        return 0;
    UINT16 total = (UINT16)(head[2] | (head[3] << 8));
    if (total < 9)
        return 0;
    uint8_t *cfg = malloc(total);
    UINT16 len = 0;
    if (cfg && control(io, 0x80, 6, 0x0200, 0, cfg, total) == EFI_SUCCESS) {
        int cur = -1;
        for (UINT16 p = 0; p + 2 <= total && cfg[p] >= 2; p += cfg[p]) {
            if (cfg[p + 1] == 4 && p + 3 <= total)
                cur = cfg[p + 2];
            else if (cfg[p + 1] == 0x21 && cur == ifn && p + 9 <= total) {
                len = (UINT16)(cfg[p + 7] | (cfg[p + 8] << 8));
                break;
            }
        }
    }
    free(cfg);
    return len;
}

static bool start(HidDev *d)
{
    EFI_STATUS st = d->io->UsbAsyncInterruptTransfer(d->io, d->ep, TRUE, 8, d->pkt, on_report, d);
    d->running = st == EFI_SUCCESS;
    return d->running;
}

static bool probe(EFI_HANDLE h)
{
    EFI_USB_IO_PROTOCOL *io;
    EFI_USB_INTERFACE_DESCRIPTOR ifd;
    if (gBS->HandleProtocol(h, &gUsbIoGuid, (void **)&io) != EFI_SUCCESS ||
        io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS)
        return false;
    if (ifd.InterfaceClass != 3 || ifd.InterfaceProtocol == 1)     /* HID, not a keyboard */
        return false;
    /* A firmware driver already owns it: its pointer protocol will report. */
    if (has_protocol(h, &gSimplePointerGuid) || has_protocol(h, &gAbsPointerGuid) ||
        has_protocol(h, &gTextInGuid))
        return false;

    HidDev d;
    memset(&d, 0, sizeof(d));
    d.io = io;
    for (UINT8 i = 0; i < ifd.NumEndpoints; i++) {
        EFI_USB_ENDPOINT_DESCRIPTOR ep;
        if (io->UsbGetEndpointDescriptor(io, i, &ep) == EFI_SUCCESS &&
            (ep.Attributes & 3) == 3 && (ep.EndpointAddress & 0x80)) {
            d.ep = ep.EndpointAddress;
            d.pkt = ep.MaxPacketSize & 0x7ff;
            break;
        }
    }
    if (!d.ep || !d.pkt)
        return false;

    /* Ask for exactly the report descriptor's length: asking for more makes
     * some host controller drivers (OVMF's xHCI) fail the short transfer. */
    UINT16 ifn = ifd.InterfaceNumber;
    bool parsed = false;
    UINT16 len = report_desc_len(io, (UINT8)ifn);
    uint8_t *desc = len ? malloc(len) : NULL;
    if (desc && control(io, 0x81, 6, 0x2200, ifn, desc, len) == EFI_SUCCESS)
        parsed = hid_parse(desc, len, &d.layout);
    free(desc);
    if (!parsed) {
        if (ifd.InterfaceSubClass != 1 || ifd.InterfaceProtocol != 2)
            return false;
        control(io, 0x21, 0x0b, 0, ifn, NULL, 0);       /* SET_PROTOCOL(boot) */
        hid_boot_mouse(&d.layout);
    }
    control(io, 0x21, 0x0a, 0, ifn, NULL, 0);           /* SET_IDLE: report on change only */

    HidDev *slot = &devs[ndevs];
    *slot = d;
    if (!start(slot))
        return false;
    LOG("usbhid: driving interface %u (%s, report id %d, ep %x)", ifn,
        d.layout.absolute ? "absolute" : "relative", d.layout.report_id, d.ep);
    ndevs++;
    return true;
}

int usbhid_init(void)
{
    UINTN n = 0;
    EFI_HANDLE *h = NULL;
    if (!ready)
        gBS->CreateEvent(0, 0, NULL, NULL, &ready);
    if (gBS->LocateHandleBuffer(ByProtocol, &gUsbIoGuid, NULL, &n, &h) != EFI_SUCCESS)
        return 0;
    for (UINTN i = 0; i < n && ndevs < MAX_DEVS; i++)
        probe(h[i]);
    gBS->FreePool(h);
    return ndevs;
}

EFI_EVENT usbhid_event(void)
{
    return ndevs ? ready : NULL;
}

bool usbhid_read(int i, UsbHidState *out)
{
    if (i >= ndevs)
        return false;
    HidDev *d = &devs[i];
    EFI_TPL old = gBS->RaiseTPL(TPL_NOTIFY);
    bool fresh = d->fresh;
    out->absolute = d->layout.absolute;
    out->dx = d->dx;
    out->dy = d->dy;
    out->wheel = d->wheel;
    out->buttons = d->buttons;
    d->dx = d->dy = d->wheel = 0;
    d->fresh = false;
    int32_t ax = d->ax, ay = d->ay;
    gBS->RestoreTPL(old);
    if (out->absolute) {
        const HidField *fx = &d->layout.x, *fy = &d->layout.y;
        float rx = (float)(fx->lmax - fx->lmin), ry = (float)(fy->lmax - fy->lmin);
        out->nx = rx > 0 ? (ax - fx->lmin) / rx : 0;
        out->ny = ry > 0 ? (ay - fy->lmin) / ry : 0;
        out->nx = out->nx < 0 ? 0 : (out->nx > 1 ? 1 : out->nx);
        out->ny = out->ny < 0 ? 0 : (out->ny > 1 ? 1 : out->ny);
    }
    return fresh;
}

int usbhid_count(void)
{
    return ndevs;
}

void usbhid_suspend(void)
{
    for (int i = 0; i < ndevs; i++)
        if (devs[i].running) {
            devs[i].io->UsbAsyncInterruptTransfer(devs[i].io, devs[i].ep, FALSE, 0, 0, NULL, NULL);
            devs[i].running = false;
        }
}

void usbhid_resume(void)
{
    for (int i = 0; i < ndevs; i++)
        if (!devs[i].running)
            start(&devs[i]);
}

/* Shade's own USB HID pointer reader, for firmware without a mouse driver. */
#ifndef SHADE_USBHID_H
#define SHADE_USBHID_H

#include <stdbool.h>
#include <stdint.h>
#include "efi.h"

typedef struct {
    bool absolute;
    float nx, ny;           /* absolute position, 0..1 */
    int32_t dx, dy, wheel;  /* relative movement since the last read */
    unsigned buttons;
} UsbHidState;

int  usbhid_init(void);                 /* returns devices driven */
int  usbhid_count(void);
EFI_EVENT usbhid_event(void);           /* signalled on new reports, NULL if none */
bool usbhid_read(int i, UsbHidState *out);   /* true if anything arrived */
/* Stop polling while another image runs, restart when it returns. */
void usbhid_suspend(void);
void usbhid_resume(void);

#endif

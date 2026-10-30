#include <stdlib.h>
#include "input.h"
#include "usbhid.h"
#include "core/efi_util.h"
#include "core/log.h"

#define MAX_POINTERS 4

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *kbd_ex;
static EFI_SIMPLE_POINTER_PROTOCOL *rel[MAX_POINTERS];
static EFI_ABSOLUTE_POINTER_PROTOCOL *absp[MAX_POINTERS];
static int nrel, nabs;
static int sw, sh;
static float px, py, speed;
static bool button;
static EFI_EVENT timer;

/* small queue: one poll may yield move + button events */
#define QUEUE 16
static Event queue[QUEUE];
static int qhead, qtail;

static void push(Event e)
{
    int next = (qtail + 1) % QUEUE;
    if (next != qhead) {
        queue[qtail] = e;
        qtail = next;
    }
}

/*
 * Only pointers on real devices count. The console splitter publishes both
 * pointer protocols on a virtual handle (no device path) even when no mouse
 * driver exists, and it merely re-reports what the real devices say.
 */
static int find_pointers(EFI_GUID *guid, void **out)
{
    UINTN n = 0;
    EFI_HANDLE *h = NULL;
    int found = 0;
    if (gBS->LocateHandleBuffer(ByProtocol, guid, NULL, &n, &h) != EFI_SUCCESS)
        return 0;
    for (UINTN i = 0; i < n && found < MAX_POINTERS; i++)
        if (dp_of(h[i]) && gBS->HandleProtocol(h[i], guid, &out[found]) == EFI_SUCCESS)
            found++;
    gBS->FreePool(h);
    return found;
}

void input_init(int screen_w, int screen_h, float mouse_speed)
{
    sw = screen_w;
    sh = screen_h;
    px = sw / 2.0f;
    py = sh / 2.0f;
    speed = mouse_speed > 0 ? mouse_speed : 1;

    if (gBS->HandleProtocol(gST->ConsoleInHandle, &gTextInExGuid, (void **)&kbd_ex) != EFI_SUCCESS)
        kbd_ex = NULL;

    nrel = find_pointers(&gSimplePointerGuid, (void **)rel);
    nabs = find_pointers(&gAbsPointerGuid, (void **)absp);
    for (int i = 0; i < nrel; i++)
        rel[i]->Reset(rel[i], FALSE);
    for (int i = 0; i < nabs; i++)
        absp[i]->Reset(absp[i], FALSE);
    int nhid = usbhid_init();
    gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer);
    LOG("input: keyboard-ex=%d relative=%d absolute=%d usb-hid=%d", kbd_ex != NULL, nrel, nabs, nhid);
}

bool input_has_pointer(void)
{
    return nrel + nabs + usbhid_count() > 0;
}

void input_suspend(void)
{
    usbhid_suspend();
}

void input_resume(void)
{
    usbhid_resume();
}

static bool read_key(Event *ev)
{
    for (;;) {
        if (kbd_ex) {
            EFI_KEY_DATA k;
            if (kbd_ex->ReadKeyStrokeEx(kbd_ex, &k) != EFI_SUCCESS)
                return false;
            ev->scan = k.Key.ScanCode;
            ev->ch = k.Key.UnicodeChar;
            ev->shift = (k.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) &&
                        (k.KeyState.KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED));
        } else {
            EFI_INPUT_KEY k;
            if (gST->ConIn->ReadKeyStroke(gST->ConIn, &k) != EFI_SUCCESS)
                return false;
            ev->scan = k.ScanCode;
            ev->ch = k.UnicodeChar;
            ev->shift = false;
        }
        /* firmware with partial-keystroke reporting sends bare modifier changes */
        if (ev->scan || ev->ch)
            return true;
    }
}

static void set_button(bool down)
{
    if (down == button)
        return;
    button = down;
    Event e = { down ? EV_POINTER_DOWN : EV_POINTER_UP, 0, 0, false, (int)px, (int)py };
    push(e);
}

static void move_to(float nx, float ny)
{
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > sw - 1) nx = (float)(sw - 1);
    if (ny > sh - 1) ny = (float)(sh - 1);
    bool moved = (int)nx != (int)px || (int)ny != (int)py;
    px = nx;
    py = ny;
    if (moved) {
        Event e = { EV_POINTER_MOVE, 0, 0, false, (int)px, (int)py };
        push(e);
    }
}

/* relative counts -> pixels, a little faster on big screens */
static float rel_gain(void)
{
    return speed * (sh > 1080 ? sh / 1080.0f : 1.0f);
}

static void poll_pointers(void)
{
    for (int i = 0; i < nabs; i++) {
        EFI_ABSOLUTE_POINTER_STATE st;
        if (absp[i]->GetState(absp[i], &st) != EFI_SUCCESS)
            continue;
        EFI_ABSOLUTE_POINTER_MODE *m = absp[i]->Mode;
        if (m->AbsoluteMaxX <= m->AbsoluteMinX || m->AbsoluteMaxY <= m->AbsoluteMinY)
            continue;
        float rx = (float)(m->AbsoluteMaxX - m->AbsoluteMinX);
        float ry = (float)(m->AbsoluteMaxY - m->AbsoluteMinY);
        move_to((float)(st.CurrentX - m->AbsoluteMinX) / rx * (sw - 1),
                (float)(st.CurrentY - m->AbsoluteMinY) / ry * (sh - 1));
        set_button(st.ActiveButtons & 1);
    }
    for (int i = 0; i < nrel; i++) {
        EFI_SIMPLE_POINTER_STATE st;
        if (rel[i]->GetState(rel[i], &st) != EFI_SUCCESS)
            continue;
        /* resolution is counts per mm (0 = unknown); ~4 px per mm feels natural */
        float resx = rel[i]->Mode->ResolutionX ? (float)rel[i]->Mode->ResolutionX : 4;
        float resy = rel[i]->Mode->ResolutionY ? (float)rel[i]->Mode->ResolutionY : 4;
        float k = 4.0f * rel_gain();
        move_to(px + st.RelativeMovementX / resx * k, py + st.RelativeMovementY / resy * k);
        set_button(st.LeftButton);
    }
    for (int i = 0; i < usbhid_count(); i++) {
        UsbHidState st;
        if (!usbhid_read(i, &st))
            continue;
        if (st.absolute)
            move_to(st.nx * (sw - 1), st.ny * (sh - 1));
        else
            move_to(px + st.dx * rel_gain(), py + st.dy * rel_gain());
        set_button(st.buttons & 1);
    }
}

bool input_poll(Event *ev)
{
    if (qhead == qtail) {
        Event k = { EV_KEY, 0, 0, false, 0, 0 };
        if (read_key(&k))
            push(k);
        else
            poll_pointers();
    }
    if (qhead == qtail)
        return false;
    *ev = queue[qhead];
    qhead = (qhead + 1) % QUEUE;
    return true;
}

void input_wait(int timeout_ms)
{
    EFI_EVENT events[3 + 2 * MAX_POINTERS];
    UINTN n = 0, idx;
    events[n++] = kbd_ex ? kbd_ex->WaitForKeyEx : gST->ConIn->WaitForKey;
    for (int i = 0; i < nrel; i++)
        events[n++] = rel[i]->WaitForInput;
    for (int i = 0; i < nabs; i++)
        events[n++] = absp[i]->WaitForInput;
    if (usbhid_event())
        events[n++] = usbhid_event();
    gBS->SetTimer(timer, TimerRelative, (UINT64)timeout_ms * 10000);
    events[n++] = timer;
    gBS->WaitForEvent(n, events, &idx);
}

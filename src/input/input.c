#include <stdlib.h>
#include "input.h"
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
static Event queue[8];
static int qhead, qtail;

static void push(Event e)
{
    int next = (qtail + 1) % 8;
    if (next != qhead) {
        queue[qtail] = e;
        qtail = next;
    }
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

    UINTN n = 0;
    EFI_HANDLE *h = NULL;
    if (gBS->LocateHandleBuffer(ByProtocol, &gSimplePointerGuid, NULL, &n, &h) == EFI_SUCCESS) {
        for (UINTN i = 0; i < n && nrel < MAX_POINTERS; i++)
            if (gBS->HandleProtocol(h[i], &gSimplePointerGuid, (void **)&rel[nrel]) == EFI_SUCCESS) {
                rel[nrel]->Reset(rel[nrel], FALSE);
                nrel++;
            }
        gBS->FreePool(h);
    }
    if (gBS->LocateHandleBuffer(ByProtocol, &gAbsPointerGuid, NULL, &n, &h) == EFI_SUCCESS) {
        for (UINTN i = 0; i < n && nabs < MAX_POINTERS; i++)
            if (gBS->HandleProtocol(h[i], &gAbsPointerGuid, (void **)&absp[nabs]) == EFI_SUCCESS) {
                absp[nabs]->Reset(absp[nabs], FALSE);
                nabs++;
            }
        gBS->FreePool(h);
    }
    gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer);
    LOG("input: keyboard-ex=%d relative=%d absolute=%d", kbd_ex != NULL, nrel, nabs);
}

bool input_has_pointer(void)
{
    return nrel + nabs > 0;
}

static bool read_key(Event *ev)
{
    if (kbd_ex) {
        EFI_KEY_DATA k;
        if (kbd_ex->ReadKeyStrokeEx(kbd_ex, &k) != EFI_SUCCESS)
            return false;
        ev->scan = k.Key.ScanCode;
        ev->ch = k.Key.UnicodeChar;
        ev->shift = (k.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) &&
                    (k.KeyState.KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED));
        return true;
    }
    EFI_INPUT_KEY k;
    if (gST->ConIn->ReadKeyStroke(gST->ConIn, &k) != EFI_SUCCESS)
        return false;
    ev->scan = k.ScanCode;
    ev->ch = k.UnicodeChar;
    ev->shift = false;
    return true;
}

static void set_button(bool down)
{
    if (down == button)
        return;
    button = down;
    Event e = { down ? EV_POINTER_DOWN : EV_POINTER_UP, 0, 0, false, (int)px, (int)py };
    push(e);
}

static void poll_pointers(void)
{
    for (int i = 0; i < nabs; i++) {
        EFI_ABSOLUTE_POINTER_STATE st;
        if (absp[i]->GetState(absp[i], &st) != EFI_SUCCESS)
            continue;
        EFI_ABSOLUTE_POINTER_MODE *m = absp[i]->Mode;
        float rx = (float)(m->AbsoluteMaxX - m->AbsoluteMinX);
        float ry = (float)(m->AbsoluteMaxY - m->AbsoluteMinY);
        if (rx <= 0 || ry <= 0)
            continue;
        float nx = (float)(st.CurrentX - m->AbsoluteMinX) / rx * (sw - 1);
        float ny = (float)(st.CurrentY - m->AbsoluteMinY) / ry * (sh - 1);
        if ((int)nx != (int)px || (int)ny != (int)py) {
            px = nx;
            py = ny;
            Event e = { EV_POINTER_MOVE, 0, 0, false, (int)px, (int)py };
            push(e);
        }
        set_button(st.ActiveButtons & 1);
    }
    for (int i = 0; i < nrel; i++) {
        EFI_SIMPLE_POINTER_STATE st;
        if (rel[i]->GetState(rel[i], &st) != EFI_SUCCESS)
            continue;
        float resx = rel[i]->Mode->ResolutionX ? (float)rel[i]->Mode->ResolutionX : 1;
        float resy = rel[i]->Mode->ResolutionY ? (float)rel[i]->Mode->ResolutionY : 1;
        /* resolution is counts per mm; ~4 px per mm feels natural at 1080p */
        float k = 4.0f * speed * (sh / 1080.0f > 1 ? sh / 1080.0f : 1);
        float dx = st.RelativeMovementX / resx * k, dy = st.RelativeMovementY / resy * k;
        if (dx != 0 || dy != 0) {
            px += dx;
            py += dy;
            if (px < 0) px = 0;
            if (py < 0) py = 0;
            if (px > sw - 1) px = (float)(sw - 1);
            if (py > sh - 1) py = (float)(sh - 1);
            Event e = { EV_POINTER_MOVE, 0, 0, false, (int)px, (int)py };
            push(e);
        }
        set_button(st.LeftButton);
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
    qhead = (qhead + 1) % 8;
    return true;
}

void input_wait(int timeout_ms)
{
    EFI_EVENT events[2 + 2 * MAX_POINTERS];
    UINTN n = 0, idx;
    events[n++] = kbd_ex ? kbd_ex->WaitForKeyEx : gST->ConIn->WaitForKey;
    for (int i = 0; i < nrel; i++)
        events[n++] = rel[i]->WaitForInput;
    for (int i = 0; i < nabs; i++)
        events[n++] = absp[i]->WaitForInput;
    gBS->SetTimer(timer, TimerRelative, (UINT64)timeout_ms * 10000);
    events[n++] = timer;
    gBS->WaitForEvent(n, events, &idx);
}

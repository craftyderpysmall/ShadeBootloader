/* Display: GOP mode selection, back buffer, dirty-rect present, software cursor. */
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static Canvas back;
static uint32_t our_mode;

static const Image *cursor_img;
static int cursor_hx, cursor_hy, cursor_x, cursor_y;
static bool cursor_on;

static void parse_res(const char *s, uint32_t *w, uint32_t *h)
{
    *w = *h = 0;
    char *end;
    long a = strtol(s, &end, 10);
    if (*end == 'x' || *end == 'X') {
        *w = (uint32_t)a;
        *h = (uint32_t)strtol(end + 1, NULL, 10);
    }
}

static void pick_mode(const char *resolution)
{
    uint32_t cur_w = gop->Mode->Info->HorizontalResolution;
    uint32_t want_w = 0, want_h = 0;
    bool max = resolution && str_eq_ci(resolution, "max");

    if (resolution && !max)
        parse_res(resolution, &want_w, &want_h);
    /* "auto": firmware's current mode is normally the panel's native one;
     * only upgrade if it is something tiny like 800x600. */
    if (!want_w && !max && cur_w >= 1024)
        return;

    int best = -1;
    uint64_t best_area = 0;
    for (uint32_t i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN size;
        if (gop->QueryMode(gop, i, &size, &info) != EFI_SUCCESS)
            continue;
        uint32_t w = info->HorizontalResolution, h = info->VerticalResolution;
        if (want_w) {
            if (w == want_w && h == want_h) {
                best = (int)i;
                break;
            }
            continue;
        }
        uint64_t area = (uint64_t)w * h;
        /* auto: prefer the largest mode up to 1920x1080 for a small default */
        if (!max && (w > 1920 || h > 1080))
            continue;
        if (area > best_area) {
            best_area = area;
            best = (int)i;
        }
    }
    if (best >= 0 && (uint32_t)best != gop->Mode->Mode) {
        LOG("gop: switching to mode %d", best);
        gop->SetMode(gop, (uint32_t)best);
    }
}

bool fb_init(const char *resolution)
{
    if (gBS->HandleProtocol(gST->ConsoleOutHandle, &gGopGuid, (void **)&gop) != EFI_SUCCESS &&
        gBS->LocateProtocol(&gGopGuid, NULL, (void **)&gop) != EFI_SUCCESS) {
        LOG("gop: not found");
        return false;
    }
    pick_mode(resolution);
    int w = (int)gop->Mode->Info->HorizontalResolution;
    int h = (int)gop->Mode->Info->VerticalResolution;
    LOG("gop: %dx%d (mode %u of %u)", w, h, gop->Mode->Mode, gop->Mode->MaxMode);
    our_mode = gop->Mode->Mode;
    back = canvas_alloc(w, h);
    return back.px != NULL;
}

Canvas *fb_back(void)
{
    return &back;
}

void fb_set_cursor(const Image *img, int hot_x, int hot_y)
{
    cursor_img = img;
    cursor_hx = hot_x;
    cursor_hy = hot_y;
}

void fb_move_cursor(int x, int y, bool visible)
{
    cursor_x = x;
    cursor_y = y;
    cursor_on = visible && cursor_img;
}

Rect fb_cursor_rect(void)
{
    if (!cursor_img)
        return rect_make(0, 0, 0, 0);
    return rect_make(cursor_x - cursor_hx, cursor_y - cursor_hy, cursor_img->w, cursor_img->h);
}

void fb_present(Rect r)
{
    r = rect_intersect(r, rect_make(0, 0, back.w, back.h));
    if (rect_empty(r))
        return;

    /* Composite the cursor into the back buffer just for this Blt, then restore. */
    static uint32_t saved[96 * 96];
    Rect cr = rect_intersect(fb_cursor_rect(), r);
    bool draw_cursor = cursor_on && !rect_empty(cr) && cr.w <= 96 && cr.h <= 96;
    if (draw_cursor) {
        for (int y = 0; y < cr.h; y++)
            memcpy(&saved[y * cr.w], &back.px[(cr.y + y) * back.stride + cr.x], cr.w * 4);
        Rect old = back.clip;
        canvas_set_clip(&back, cr);
        Rect full = fb_cursor_rect();
        draw_image(&back, cursor_img, (float)full.x, (float)full.y, (float)full.w, (float)full.h, 0, 1.0f);
        back.clip = old;
    }

    gop->Blt(gop, back.px, EfiBltBufferToVideo,
             (UINTN)r.x, (UINTN)r.y, (UINTN)r.x, (UINTN)r.y, (UINTN)r.w, (UINTN)r.h, (UINTN)back.stride * 4);

    if (draw_cursor)
        for (int y = 0; y < cr.h; y++)
            memcpy(&back.px[(cr.y + y) * back.stride + cr.x], &saved[y * cr.w], cr.w * 4);
}

void fb_clear(Color col)
{
    canvas_set_clip(&back, rect_make(0, 0, back.w, back.h));
    draw_fill(&back, back.clip, col | 0xff000000);
    bool was = cursor_on;
    cursor_on = false;
    fb_present(back.clip);
    cursor_on = was;
}

void fb_restore(void)
{
    if (gop && gop->Mode->Mode != our_mode)
        gop->SetMode(gop, our_mode);
    gST->ConOut->EnableCursor(gST->ConOut, FALSE);
}

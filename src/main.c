/*
 * Shade — a modern, themeable UEFI boot picker.
 *
 *   init firmware helpers -> read shade.conf -> set up display + theme
 *   -> discover entries -> picker UI -> boot / power action
 */
#include <string.h>
#include "boot/boot.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"
#include "gfx/gfx.h"
#include "input/input.h"
#include "settings.h"
#include "theme/theme.h"
#include "ui/ui.h"

static Settings settings;
static Theme theme;
static BootSet boot;

/* timeout = 0: boot the default straight away unless a key is already held. */
static bool instant_boot(void)
{
    if (settings.timeout != 0)
        return false;
    EFI_INPUT_KEY k;
    if (gST->ConIn->ReadKeyStroke(gST->ConIn, &k) == EFI_SUCCESS)
        return false;
    char want[64] = "";
    if (str_eq_ci(settings.default_entry, "last"))
        recall_last(want, sizeof(want));
    else
        str_copy(want, settings.default_entry, sizeof(want));
    for (int i = 0; i < boot.entries.count; i++) {
        Entry *e = &boot.entries.items[i];
        if (e->hidden || (want[0] && !str_glob_ci(want, e->title)))
            continue;
        char err[160];
        LOG("instant boot: %s", e->title);
        boot_entry(&boot, e, err, sizeof(err));
        return false;   /* only returns on failure: fall through to the UI */
    }
    return false;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    efi_init(image, st);
    time_init();

    char self_dir[256];
    EFI_FILE_PROTOCOL *root = boot_self_root(self_dir, sizeof(self_dir));
    settings_load(&settings, root, self_dir);
    LOG("Shade starting, firmware rev %x", st->FirmwareRevision);

    discover(&settings, &boot);
    instant_boot();

    if (!fb_init(settings.resolution)) {
        st->ConOut->OutputString(st->ConOut, (CHAR16 *)u"Shade: no graphics output available.\r\n");
        gBS->Stall(3000000);
        return EFI_UNSUPPORTED;
    }
    theme_load(&theme, root, settings.theme);
    ui_init(&theme, &settings, &boot);

    const char *toast = NULL;
    char err[192];
    bool first = true;
    for (;;) {
        UiResult r = ui_run(toast, first);
        first = false;
        toast = NULL;
        switch (r.action) {
        case UI_SHUTDOWN:
            power_off();
            break;
        case UI_RESTART:
            power_restart();
            break;
        case UI_FIRMWARE:
            reboot_to_firmware();
            break;
        case UI_BOOT:
            if (str_eq_ci(settings.default_entry, "last"))
                remember_last(r.entry->title);
            input_suspend();    /* hand USB pointers over to the loader */
            if (!boot_entry(&boot, r.entry, err, sizeof(err)))
                toast = err;
            input_resume();
            fb_restore();
            break;
        }
    }
}

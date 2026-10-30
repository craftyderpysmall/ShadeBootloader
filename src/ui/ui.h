/* The boot picker screen. */
#ifndef SHADE_UI_H
#define SHADE_UI_H

#include "boot/boot.h"
#include "settings.h"
#include "theme/theme.h"

typedef enum {
    UI_BOOT,
    UI_SHUTDOWN,
    UI_RESTART,
    UI_FIRMWARE,
} UiAction;

typedef struct {
    UiAction action;
    Entry *entry;       /* for UI_BOOT */
} UiResult;

void ui_init(Theme *theme, Settings *settings, BootSet *boot);
/* Runs until the user picks something. toast: error to show on entry, or NULL.
 * autoboot: honour the countdown (only on the first run). */
UiResult ui_run(const char *toast, bool autoboot);

#endif

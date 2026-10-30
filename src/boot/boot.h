/* Firmware-side discovery, launching and power actions. */
#ifndef SHADE_BOOT_H
#define SHADE_BOOT_H

#include "entry.h"
#include "settings.h"

typedef struct {
    Volume *vols;
    int nvols;
    int self_vol;
    char self_path[256];
    EntryList entries;
} BootSet;

/* Locate Shade's own volume/path (needed before settings are loaded). */
void *boot_self_root(char *self_dir, int cap);

void discover(const Settings *s, BootSet *out);

/* Returns only on failure (message in err) or when the loader exits. */
bool boot_entry(const BootSet *b, const Entry *e, char *err, int cap);

bool firmware_setup_supported(void);
void reboot_to_firmware(void);
void power_off(void);
void power_restart(void);

/* Remember / recall the last booted entry title (for default = last). */
void remember_last(const char *title);
bool recall_last(char *title, int cap);

#endif

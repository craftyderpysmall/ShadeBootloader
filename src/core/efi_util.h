/* Firmware globals and thin wrappers around boot/runtime services. */
#ifndef SHADE_EFI_UTIL_H
#define SHADE_EFI_UTIL_H

#include <stdbool.h>
#include "efi.h"

extern EFI_SYSTEM_TABLE  *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_HANDLE gImage;
extern EFI_LOADED_IMAGE_PROTOCOL *gLoadedImage;

extern EFI_GUID gGopGuid, gTextInExGuid, gSimplePointerGuid, gAbsPointerGuid,
                gLoadedImageGuid, gSimpleFsGuid, gFileInfoGuid, gFsInfoGuid,
                gDevicePathGuid, gBlockIoGuid, gGlobalVarGuid, gShadeVarGuid;

void efi_init(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);

/* Wide <-> UTF-8 (ASCII subset is all firmware paths ever need). */
CHAR16 *wstr_from(const char *s);              /* malloc'd */
char   *str_from_w(const CHAR16 *w);           /* malloc'd */
size_t  wstrlen(const CHAR16 *w);

/* Files. Paths use backslashes and are relative to the volume root. */
EFI_FILE_PROTOCOL *vol_open_root(EFI_HANDLE fs_handle);
bool  file_exists(EFI_FILE_PROTOCOL *root, const char *path);
void *file_read_all(EFI_FILE_PROTOCOL *root, const char *path, size_t *size); /* malloc'd, NUL-padded */
EFI_FILE_INFO *file_info(EFI_FILE_PROTOCOL *f);                                /* malloc'd */
char *vol_label(EFI_FILE_PROTOCOL *root);                                      /* malloc'd */

/* Iterate a directory: returns malloc'd info per entry, NULL at end. */
EFI_FILE_PROTOCOL *dir_open(EFI_FILE_PROTOCOL *root, const char *path);
EFI_FILE_INFO *dir_next(EFI_FILE_PROTOCOL *dir);

/* Device paths */
size_t dp_size(const EFI_DEVICE_PATH *dp);    /* including end node */
EFI_DEVICE_PATH *dp_next(const EFI_DEVICE_PATH *dp);
bool dp_is_end(const EFI_DEVICE_PATH *dp);
EFI_DEVICE_PATH *dp_append_file(EFI_DEVICE_PATH *dev, const char *path); /* malloc'd */
EFI_DEVICE_PATH *dp_of(EFI_HANDLE h);

/* Variables */
void *var_get(const char *name, EFI_GUID *guid, size_t *size);   /* malloc'd */
bool  var_set(const char *name, EFI_GUID *guid, const void *data, size_t size, bool persist);

/* Monotonic milliseconds, calibrated against Stall() at init. */
void   time_init(void);
uint64_t time_ms(void);
double time_sec(void);

#endif

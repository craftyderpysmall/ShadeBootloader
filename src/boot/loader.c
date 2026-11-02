/* Starting loaders, firmware setup and power control. */
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"

static const char *status_text(EFI_STATUS st)
{
    switch (st) {
    case EFI_SECURITY_VIOLATION: return "Secure Boot rejected this loader";
    case EFI_NOT_FOUND: return "Loader file not found";
    case EFI_LOAD_ERROR: return "Not a valid EFI application";
    case EFI_UNSUPPORTED: return "Loader is not supported by this firmware";
    case EFI_OUT_OF_RESOURCES: return "Out of memory";
    case EFI_ACCESS_DENIED: return "Access denied";
    default: return "The loader could not be started";
    }
}

/* TEMPORARY: how far the last boots got, readable from Linux after a hang.
 * The recovery boot would overwrite the failed one, so keep one previous. */
static void trace_rotate(void)
{
    size_t n = 0;
    char *old = var_get("ShadeTrace", &gShadeVarGuid, &n);
    if (old) {
        var_set("ShadeTracePrev", &gShadeVarGuid, old, n, true);
        free(old);
    }
}

static void trace(const char *stage, const Entry *e)
{
    char s[96];
    str_fmt(s, sizeof(s), "%s %s", stage, e->path);
    var_set("ShadeTrace", &gShadeVarGuid, s, strlen(s) + 1, true);
}

bool boot_entry(const BootSet *b, const Entry *e, char *err, int cap)
{
    err[0] = 0;
    if (e->vol < 0) {
        if (e->boot_num < 0) {
            str_copy(err, "Nothing to boot", cap);
            return false;
        }
        /* firmware-only option: ask the firmware to boot it next */
        UINT16 next = (UINT16)e->boot_num;
        if (!var_set("BootNext", &gGlobalVarGuid, &next, sizeof(next), true)) {
            str_copy(err, "Could not set BootNext", cap);
            return false;
        }
        gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
        return false;
    }

    trace_rotate();
    trace("read", e);
    /* Read it ourselves and load from memory, as GRUB does. If that fails the
     * firmware gets to try the path and report why. */
    size_t size = 0;
    void *src = NULL;
    EFI_FILE_PROTOCOL *root = vol_open_root(b->vols[e->vol].handle);
    if (root) {
        src = file_read_all(root, e->path, &size);
        root->Close(root);
    }
    trace("load", e);
    EFI_DEVICE_PATH *dp = dp_append_file(dp_of(b->vols[e->vol].handle), e->path);
    EFI_HANDLE image = NULL;
    EFI_STATUS st = gBS->LoadImage(FALSE, gImage, dp, src, size, &image);
    free(src);
    LOG("boot: LoadImage %s (%llu bytes) -> %llx", e->path, (uint64_t)size, st);
    if (EFI_ERROR(st)) {
        free(dp);
        /* on a Secure Boot rejection the image is loaded anyway and must be freed */
        if (image)
            gBS->UnloadImage(image);
        str_fmt(err, cap, "%s (%s)", status_text(st), e->path);
        return false;
    }

    CHAR16 *opts = NULL;
    if (e->options[0]) {
        EFI_LOADED_IMAGE_PROTOCOL *li;
        if (gBS->HandleProtocol(image, &gLoadedImageGuid, (void **)&li) == EFI_SUCCESS) {
            /* not wstr_from: that rewrites '/' for paths, command lines need it */
            size_t len = strlen(e->options);
            opts = malloc((len + 1) * sizeof(CHAR16));
            for (size_t i = 0; i <= len; i++)
                opts[i] = (CHAR16)(uint8_t)e->options[i];
            li->LoadOptions = opts;
            li->LoadOptionsSize = (UINT32)((wstrlen(opts) + 1) * sizeof(CHAR16));
        }
    }

    trace("start", e);
    st = gBS->StartImage(image, NULL, NULL);
    LOG("boot: StartImage returned %llx", st);
    free(opts);
    free(dp);
    if (EFI_ERROR(st)) {
        str_fmt(err, cap, "%s exited with an error", e->title);
        return false;
    }
    return true;    /* loader returned cleanly (e.g. "exit" from a shell) */
}

bool firmware_setup_supported(void)
{
    size_t n = 0;
    UINT64 *v = var_get("OsIndicationsSupported", &gGlobalVarGuid, &n);
    bool ok = v && n >= sizeof(UINT64) && (*v & EFI_OS_INDICATIONS_BOOT_TO_FW_UI);
    free(v);
    return ok;
}

void reboot_to_firmware(void)
{
    size_t n = 0;
    UINT64 ind = 0;
    UINT64 *cur = var_get("OsIndications", &gGlobalVarGuid, &n);
    if (cur && n >= sizeof(UINT64))
        ind = *cur;
    free(cur);
    ind |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    var_set("OsIndications", &gGlobalVarGuid, &ind, sizeof(ind), true);
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

void power_off(void)
{
    gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

void power_restart(void)
{
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

void remember_last(const char *title)
{
    char cur[64];
    if (recall_last(cur, sizeof(cur)) && str_eq_ci(cur, title))
        return;     /* avoid needless NVRAM writes */
    var_set("ShadeLast", &gShadeVarGuid, title, strlen(title) + 1, true);
}

bool recall_last(char *title, int cap)
{
    size_t n = 0;
    char *v = var_get("ShadeLast", &gShadeVarGuid, &n);
    if (!v)
        return false;
    str_copy(title, v, cap);
    free(v);
    return true;
}

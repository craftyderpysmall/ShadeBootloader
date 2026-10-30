#include <string.h>
#include <stdlib.h>
#include "efi_util.h"

EFI_SYSTEM_TABLE  *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;
EFI_HANDLE gImage;
EFI_LOADED_IMAGE_PROTOCOL *gLoadedImage;

EFI_GUID gGopGuid           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gTextInExGuid      = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
EFI_GUID gSimplePointerGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
EFI_GUID gAbsPointerGuid    = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
EFI_GUID gLoadedImageGuid   = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gSimpleFsGuid      = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gFileInfoGuid      = EFI_FILE_INFO_GUID;
EFI_GUID gFsInfoGuid        = EFI_FILE_SYSTEM_INFO_GUID;
EFI_GUID gDevicePathGuid    = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gBlockIoGuid       = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gGlobalVarGuid     = EFI_GLOBAL_VARIABLE_GUID;
/* Shade's own vendor GUID for persisted state (last booted entry). */
EFI_GUID gShadeVarGuid      = { 0x5ade0b00, 0x7c1e, 0x4e2a, { 0x9b, 0x51, 0x2f, 0x0d, 0x5a, 0xde, 0xb0, 0x07 } };

void efi_init(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    gImage = image;
    gST = st;
    gBS = st->BootServices;
    gRT = st->RuntimeServices;
    gBS->HandleProtocol(image, &gLoadedImageGuid, (void **)&gLoadedImage);
    gBS->SetWatchdogTimer(0, 0, 0, NULL);
}

/* ---- strings --------------------------------------------------------- */

size_t wstrlen(const CHAR16 *w)
{
    size_t n = 0;
    while (w[n])
        n++;
    return n;
}

CHAR16 *wstr_from(const char *s)
{
    size_t n = strlen(s);
    CHAR16 *w = malloc((n + 1) * sizeof(CHAR16));
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = c;
        if (c >= 0xc0 && c < 0xe0 && i + 1 < n) {
            cp = ((c & 0x1f) << 6) | (s[i + 1] & 0x3f);
            i += 1;
        } else if (c >= 0xe0 && c < 0xf0 && i + 2 < n) {
            cp = ((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f);
            i += 2;
        }
        w[j++] = (CHAR16)(cp == '/' ? '\\' : cp);
    }
    w[j] = 0;
    return w;
}

char *str_from_w(const CHAR16 *w)
{
    size_t n = wstrlen(w);
    char *s = malloc(n * 3 + 1), *p = s;
    for (size_t i = 0; i < n; i++) {
        CHAR16 c = w[i];
        if (c < 0x80) {
            *p++ = (char)c;
        } else if (c < 0x800) {
            *p++ = (char)(0xc0 | (c >> 6));
            *p++ = (char)(0x80 | (c & 0x3f));
        } else {
            *p++ = (char)(0xe0 | (c >> 12));
            *p++ = (char)(0x80 | ((c >> 6) & 0x3f));
            *p++ = (char)(0x80 | (c & 0x3f));
        }
    }
    *p = 0;
    return s;
}

/* ---- files ----------------------------------------------------------- */

EFI_FILE_PROTOCOL *vol_open_root(EFI_HANDLE fs_handle)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root;
    if (gBS->HandleProtocol(fs_handle, &gSimpleFsGuid, (void **)&fs) != EFI_SUCCESS)
        return NULL;
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS)
        return NULL;
    return root;
}

static EFI_FILE_PROTOCOL *file_open(EFI_FILE_PROTOCOL *root, const char *path)
{
    EFI_FILE_PROTOCOL *f = NULL;
    CHAR16 *w = wstr_from(path);
    EFI_STATUS st = root->Open(root, &f, w, EFI_FILE_MODE_READ, 0);
    free(w);
    return st == EFI_SUCCESS ? f : NULL;
}

bool file_exists(EFI_FILE_PROTOCOL *root, const char *path)
{
    EFI_FILE_PROTOCOL *f = file_open(root, path);
    if (!f)
        return false;
    f->Close(f);
    return true;
}

EFI_FILE_INFO *file_info(EFI_FILE_PROTOCOL *f)
{
    UINTN size = 0;
    EFI_FILE_INFO *info = NULL;
    if (f->GetInfo(f, &gFileInfoGuid, &size, NULL) != EFI_BUFFER_TOO_SMALL)
        return NULL;
    info = malloc(size);
    if (f->GetInfo(f, &gFileInfoGuid, &size, info) != EFI_SUCCESS) {
        free(info);
        return NULL;
    }
    return info;
}

void *file_read_all(EFI_FILE_PROTOCOL *root, const char *path, size_t *size)
{
    EFI_FILE_PROTOCOL *f = file_open(root, path);
    if (!f)
        return NULL;
    EFI_FILE_INFO *info = file_info(f);
    if (!info || (info->Attribute & EFI_FILE_DIRECTORY)) {
        free(info);
        f->Close(f);
        return NULL;
    }
    UINTN n = info->FileSize;
    free(info);
    uint8_t *buf = malloc(n + 2);
    if (buf && f->Read(f, &n, buf) != EFI_SUCCESS) {
        free(buf);
        buf = NULL;
    }
    f->Close(f);
    if (buf) {
        buf[n] = buf[n + 1] = 0;
        if (size)
            *size = n;
    }
    return buf;
}

char *vol_label(EFI_FILE_PROTOCOL *root)
{
    UINTN size = 0;
    if (root->GetInfo(root, &gFsInfoGuid, &size, NULL) != EFI_BUFFER_TOO_SMALL)
        return NULL;
    EFI_FILE_SYSTEM_INFO *info = malloc(size);
    char *label = NULL;
    if (root->GetInfo(root, &gFsInfoGuid, &size, info) == EFI_SUCCESS)
        label = str_from_w(info->VolumeLabel);
    free(info);
    return label;
}

EFI_FILE_PROTOCOL *dir_open(EFI_FILE_PROTOCOL *root, const char *path)
{
    EFI_FILE_PROTOCOL *d = file_open(root, path);
    if (!d)
        return NULL;
    EFI_FILE_INFO *info = file_info(d);
    bool is_dir = info && (info->Attribute & EFI_FILE_DIRECTORY);
    free(info);
    if (!is_dir) {
        d->Close(d);
        return NULL;
    }
    return d;
}

EFI_FILE_INFO *dir_next(EFI_FILE_PROTOCOL *dir)
{
    UINTN size = 512;
    EFI_FILE_INFO *info = malloc(size);
    EFI_STATUS st = dir->Read(dir, &size, info);
    if (st == EFI_BUFFER_TOO_SMALL) {
        free(info);
        info = malloc(size);
        st = dir->Read(dir, &size, info);
    }
    if (st != EFI_SUCCESS || size == 0) {
        free(info);
        return NULL;
    }
    return info;
}

/* ---- device paths ---------------------------------------------------- */

static size_t dp_len(const EFI_DEVICE_PATH *dp)
{
    return dp->Length[0] | (dp->Length[1] << 8);
}

bool dp_is_end(const EFI_DEVICE_PATH *dp)
{
    return dp->Type == DP_TYPE_END && dp->SubType == DP_SUB_END_ENTIRE;
}

EFI_DEVICE_PATH *dp_next(const EFI_DEVICE_PATH *dp)
{
    return (EFI_DEVICE_PATH *)((uint8_t *)dp + dp_len(dp));
}

size_t dp_size(const EFI_DEVICE_PATH *dp)
{
    const EFI_DEVICE_PATH *p = dp;
    while (!dp_is_end(p) && dp_len(p) >= 4)
        p = dp_next(p);
    return (size_t)((uint8_t *)p - (uint8_t *)dp) + 4;
}

EFI_DEVICE_PATH *dp_of(EFI_HANDLE h)
{
    EFI_DEVICE_PATH *dp = NULL;
    gBS->HandleProtocol(h, &gDevicePathGuid, (void **)&dp);
    return dp;
}

EFI_DEVICE_PATH *dp_append_file(EFI_DEVICE_PATH *dev, const char *path)
{
    size_t dev_len = dev ? dp_size(dev) - 4 : 0;
    CHAR16 *w = wstr_from(path);
    size_t wbytes = (wstrlen(w) + 1) * sizeof(CHAR16);
    size_t node_len = 4 + wbytes;
    uint8_t *buf = malloc(dev_len + node_len + 4);
    if (dev_len)
        memcpy(buf, dev, dev_len);
    EFI_DEVICE_PATH *node = (EFI_DEVICE_PATH *)(buf + dev_len);
    node->Type = DP_TYPE_MEDIA;
    node->SubType = DP_SUB_MEDIA_FILE;
    node->Length[0] = (UINT8)(node_len & 0xff);
    node->Length[1] = (UINT8)(node_len >> 8);
    memcpy((uint8_t *)node + 4, w, wbytes);
    free(w);
    EFI_DEVICE_PATH *end = (EFI_DEVICE_PATH *)(buf + dev_len + node_len);
    end->Type = DP_TYPE_END;
    end->SubType = DP_SUB_END_ENTIRE;
    end->Length[0] = 4;
    end->Length[1] = 0;
    return (EFI_DEVICE_PATH *)buf;
}

/* ---- variables ------------------------------------------------------- */

void *var_get(const char *name, EFI_GUID *guid, size_t *size)
{
    CHAR16 *w = wstr_from(name);
    UINTN n = 0;
    void *data = NULL;
    if (gRT->GetVariable(w, guid, NULL, &n, NULL) == EFI_BUFFER_TOO_SMALL) {
        data = malloc(n + 2);
        if (gRT->GetVariable(w, guid, NULL, &n, data) != EFI_SUCCESS) {
            free(data);
            data = NULL;
        } else {
            ((uint8_t *)data)[n] = ((uint8_t *)data)[n + 1] = 0;
            if (size)
                *size = n;
        }
    }
    free(w);
    return data;
}

bool var_set(const char *name, EFI_GUID *guid, const void *data, size_t size, bool persist)
{
    CHAR16 *w = wstr_from(name);
    UINT32 attr = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
    if (persist)
        attr |= EFI_VARIABLE_NON_VOLATILE;
    EFI_STATUS st = gRT->SetVariable(w, guid, attr, size, (void *)data);
    free(w);
    return st == EFI_SUCCESS;
}

/* ---- time ------------------------------------------------------------ */

static uint64_t tsc_start, tsc_per_ms = 1000000;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void time_init(void)
{
    uint64_t a = rdtsc();
    gBS->Stall(5000);
    uint64_t b = rdtsc();
    tsc_per_ms = (b - a) / 5;
    if (tsc_per_ms == 0)
        tsc_per_ms = 1000000;
    tsc_start = rdtsc();
}

uint64_t time_ms(void)
{
    return (rdtsc() - tsc_start) / tsc_per_ms;
}

double time_sec(void)
{
    return (double)(rdtsc() - tsc_start) / (double)tsc_per_ms / 1000.0;
}

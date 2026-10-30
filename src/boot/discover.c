/* Walk every filesystem and the firmware's Boot#### list, then filter. */
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "core/efi_util.h"
#include "core/log.h"
#include "core/str.h"

typedef struct {
    Candidate *items;
    int count, cap;
} CandList;

static void cand_add(CandList *l, int vol, const char *path, const char *desc, int num)
{
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->items = realloc(l->items, sizeof(Candidate) * l->cap);
    }
    Candidate *c = &l->items[l->count++];
    c->vol = vol;
    c->path = path ? str_dup(path) : NULL;
    c->nvram_desc = desc ? str_dup(desc) : NULL;
    c->boot_num = num;
}

/* Firmware often connects only the boot device; connect everything so other
 * disks and USB sticks show up. */
static void connect_all(void)
{
    UINTN n = 0;
    EFI_HANDLE *handles = NULL;
    if (gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &n, &handles) != EFI_SUCCESS)
        return;
    for (UINTN i = 0; i < n; i++)
        gBS->ConnectController(handles[i], NULL, NULL, TRUE);
    gBS->FreePool(handles);
}

static void guid_str(const uint8_t *g, char *out)
{
    /* mixed-endian GUID layout, printed lower-case */
    str_fmt(out, 40, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
            g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

/* Pull partition GUID, media hints and the file path out of a device path. */
static void parse_dp(const EFI_DEVICE_PATH *dp, char *partuuid, MediaKind *media, char *path, int cap)
{
    if (partuuid) partuuid[0] = 0;
    if (path) path[0] = 0;
    for (const EFI_DEVICE_PATH *n = dp; n && !dp_is_end(n); n = dp_next(n)) {
        size_t len = n->Length[0] | (n->Length[1] << 8);
        if (len < 4)
            break;
        if (n->Type == DP_TYPE_MEDIA && n->SubType == DP_SUB_MEDIA_HD && partuuid) {
            const HARDDRIVE_DEVICE_PATH *hd = (const HARDDRIVE_DEVICE_PATH *)n;
            if (hd->SignatureType == 2)
                guid_str(hd->Signature, partuuid);
        } else if (n->Type == DP_TYPE_MEDIA && n->SubType == DP_SUB_MEDIA_CDROM && media) {
            *media = MEDIA_OPTICAL;
        } else if (n->Type == DP_TYPE_MESSAGING && n->SubType == DP_SUB_MSG_USB && media) {
            if (*media == MEDIA_INTERNAL)
                *media = MEDIA_USB;
        } else if (n->Type == DP_TYPE_MESSAGING && media &&
                   (n->SubType == DP_SUB_MSG_MAC || n->SubType == DP_SUB_MSG_IPV4 ||
                    n->SubType == DP_SUB_MSG_IPV6 || n->SubType == DP_SUB_MSG_URI)) {
            *media = MEDIA_NETWORK;
        } else if (n->Type == DP_TYPE_MEDIA && n->SubType == DP_SUB_MEDIA_FILE && path) {
            /* file paths may be split over several nodes */
            const CHAR16 *w = ((const FILEPATH_DEVICE_PATH *)n)->PathName;
            char *s = str_from_w(w);
            size_t have = strlen(path);
            if (have && path[have - 1] != '\\' && s[0] != '\\' && have + 1 < (size_t)cap)
                path[have++] = '\\', path[have] = 0;
            str_copy(path + have, s, cap - have);
            free(s);
        }
    }
}

/* Add every *.efi in dir; recurse into subdirectories up to depth levels. */
static void scan_dir(EFI_FILE_PROTOCOL *root, const char *dir_path, int vol, int depth, CandList *out)
{
    EFI_FILE_PROTOCOL *dir = dir_open(root, dir_path);
    if (!dir)
        return;
    EFI_FILE_INFO *f;
    while ((f = dir_next(dir)) != NULL) {
        char *name = str_from_w(f->FileName);
        bool is_dir = f->Attribute & EFI_FILE_DIRECTORY;
        free(f);
        char path[256];
        str_fmt(path, sizeof(path), "%s\\%s", dir_path, name);
        if (is_dir) {
            if (depth > 0 && name[0] != '.')
                scan_dir(root, path, vol, depth - 1, out);
        } else if (str_ends_ci(name, ".efi")) {
            cand_add(out, vol, path, NULL, -1);
        }
        free(name);
    }
    dir->Close(dir);
}

static void scan_volume(EFI_FILE_PROTOCOL *root, int vol, CandList *out)
{
    /* \EFI\<vendor>\*.efi plus one level deeper (\EFI\Microsoft\Boot\bootmgfw.efi) */
    EFI_FILE_PROTOCOL *efi = dir_open(root, "\\EFI");
    if (efi) {
        EFI_FILE_INFO *d;
        while ((d = dir_next(efi)) != NULL) {
            char *name = str_from_w(d->FileName);
            bool is_dir = d->Attribute & EFI_FILE_DIRECTORY;
            free(d);
            if (is_dir && name[0] != '.') {
                char sub[160];
                str_fmt(sub, sizeof(sub), "\\EFI\\%s", name);
                scan_dir(root, sub, vol, 1, out);
            }
            free(name);
        }
        efi->Close(efi);
    }
    if (file_exists(root, "\\System\\Library\\CoreServices\\boot.efi"))
        cand_add(out, vol, "\\System\\Library\\CoreServices\\boot.efi", NULL, -1);
}

/* A device path from NVRAM must stay inside its variable and end properly. */
static bool dp_within(const EFI_DEVICE_PATH *dp, size_t max)
{
    const uint8_t *p = (const uint8_t *)dp, *end = p + max;
    while (p + 4 <= end) {
        const EFI_DEVICE_PATH *n = (const EFI_DEVICE_PATH *)p;
        size_t len = n->Length[0] | (n->Length[1] << 8);
        if (len < 4 || p + len > end)
            return false;
        if (dp_is_end(n))
            return true;
        p += len;
    }
    return false;
}

static void scan_nvram(BootSet *b, CandList *out)
{
    size_t size = 0;
    UINT16 *order = var_get("BootOrder", &gGlobalVarGuid, &size);
    if (!order)
        return;
    for (size_t i = 0; i < size / 2; i++) {
        char name[16];
        str_fmt(name, sizeof(name), "Boot%04X", order[i]);
        size_t n = 0;
        uint8_t *opt = var_get(name, &gGlobalVarGuid, &n);
        if (!opt || n < 8) {
            free(opt);
            continue;
        }
        UINT32 attr = *(UINT32 *)opt;
        UINT16 dp_len = *(UINT16 *)(opt + 4);
        CHAR16 *desc_w = (CHAR16 *)(opt + 6);
        size_t desc_len = wstrlen(desc_w);      /* var_get NUL-pads, so this stops */
        EFI_DEVICE_PATH *dp = (EFI_DEVICE_PATH *)(opt + 6 + (desc_len + 1) * 2);
        if (!(attr & LOAD_OPTION_ACTIVE) || (attr & LOAD_OPTION_HIDDEN) ||
            6 + (desc_len + 1) * 2 + dp_len > n || !dp_within(dp, dp_len)) {
            free(opt);
            continue;
        }
        char *desc = str_from_w(desc_w);
        char partuuid[40], path[256];
        parse_dp(dp, partuuid, NULL, path, sizeof(path));
        int vol = -1;
        if (partuuid[0]) {
            for (int v = 0; v < b->nvols; v++)
                if (str_eq_ci(b->vols[v].partuuid, partuuid))
                    vol = v;
            if (vol < 0) {                  /* on a disk we can't read: skip */
                free(desc);
                free(opt);
                continue;
            }
        }
        LOG("nvram: %s '%s' vol=%d path=%s", name, desc, vol, path);
        cand_add(out, vol, (vol >= 0 && path[0]) ? path : NULL, desc, order[i]);
        free(desc);
        free(opt);
    }
    free(order);
}

void *boot_self_root(char *self_dir, int cap)
{
    if (self_dir) {
        char path[256];
        parse_dp(gLoadedImage->FilePath, NULL, NULL, path, sizeof(path));
        str_copy(self_dir, path, cap);
        char *slash = strrchr(self_dir, '\\');
        if (slash)
            *slash = 0;
    }
    return vol_open_root(gLoadedImage->DeviceHandle);
}

static void add_custom(const Settings *s, BootSet *b)
{
    for (int i = 0; i < s->ncustom; i++) {
        const CustomEntry *c = &s->custom[i];
        int vol = -1;
        for (int v = 0; v < b->nvols && vol < 0; v++) {
            const Volume *V = &b->vols[v];
            if (c->partuuid[0] ? str_eq_ci(c->partuuid, V->partuuid)
                : c->volume[0] ? (V->label && str_glob_ci(c->volume, V->label))
                : v == b->self_vol)
                vol = v;
        }
        if (vol < 0 || !c->path[0]) {
            LOG("config: entry.%s has no reachable volume/path", c->id);
            continue;
        }
        /* a custom entry for a loader we also found on our own replaces it */
        char norm[256];
        str_copy(norm, c->path, sizeof(norm));
        for (char *p = norm; *p; p++)
            if (*p == '/')
                *p = '\\';
        for (int k = 0; k < b->entries.count; k++) {
            Entry *o = &b->entries.items[k];
            if (!o->custom && o->vol == vol && str_eq_ci(o->path, norm) && !o->hidden) {
                o->hidden = true;
                str_copy(o->why, "replaced by custom entry", sizeof(o->why));
            }
        }
        Entry *e = entries_add(&b->entries);
        e->kind = KIND_OS;
        e->vol = vol;
        e->custom = true;
        e->priority = c->order;
        str_copy(e->title, c->title, sizeof(e->title));
        str_copy(e->subtitle, "Custom entry", sizeof(e->subtitle));
        str_copy(e->path, c->path, sizeof(e->path));
        str_copy(e->options, c->options, sizeof(e->options));
        str_copy(e->icon, c->icon[0] ? c->icon : "os-linux,os-generic", sizeof(e->icon));
        for (char *p = e->path; *p; p++)
            if (*p == '/')
                *p = '\\';
    }
}

void discover(const Settings *s, BootSet *b)
{
    memset(b, 0, sizeof(*b));
    b->self_vol = -1;
    parse_dp(gLoadedImage->FilePath, NULL, NULL, b->self_path, sizeof(b->self_path));

    if (s->connect_all)
        connect_all();

    UINTN n = 0;
    EFI_HANDLE *handles = NULL;
    gBS->LocateHandleBuffer(ByProtocol, &gSimpleFsGuid, NULL, &n, &handles);
    b->vols = calloc(n ? n : 1, sizeof(Volume));
    CandList cands = { 0 };

    for (UINTN i = 0; i < n; i++) {
        Volume *v = &b->vols[b->nvols];
        v->handle = handles[i];
        v->media = MEDIA_INTERNAL;
        parse_dp(dp_of(handles[i]), v->partuuid, &v->media, NULL, 0);
        EFI_BLOCK_IO_PROTOCOL *bio;
        if (v->media == MEDIA_INTERNAL &&
            gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) == EFI_SUCCESS &&
            bio->Media->RemovableMedia)
            v->media = MEDIA_USB;
        EFI_FILE_PROTOCOL *root = vol_open_root(handles[i]);
        if (!root)
            continue;
        v->label = vol_label(root);
        int idx = b->nvols++;
        if (handles[i] == gLoadedImage->DeviceHandle)
            b->self_vol = idx;
        bool ignored = filter_volume_ignored(&s->ignore, v);
        LOG("volume %d: '%s' %s media=%d%s", idx, v->label ? v->label : "", v->partuuid,
            v->media, ignored ? " (ignored, not scanned)" : "");
        if (!ignored)
            scan_volume(root, idx, &cands);
        root->Close(root);
    }
    if (handles)
        gBS->FreePool(handles);

    if (s->scan_nvram)
        scan_nvram(b, &cands);

    filter_build(b->vols, b->nvols, cands.items, cands.count, &s->ignore,
                 b->self_vol, b->self_path, &b->entries);
    add_custom(s, b);

    /* [options] title-glob = command line */
    for (int i = 0; i < b->entries.count; i++)
        for (int k = 0; k < s->nopts; k++)
            if (str_glob_ci(s->opt_match[k], b->entries.items[i].title))
                str_copy(b->entries.items[i].options, s->opt_value[k], sizeof(b->entries.items[i].options));

    filter_apply_rules(&b->entries, &s->ignore);
    entries_sort(&b->entries);

    for (int i = 0; i < b->entries.count; i++) {
        Entry *e = &b->entries.items[i];
        LOG("entry: %-20s %-22s %s%s%s", e->title, e->subtitle, e->path,
            e->hidden ? "  [hidden: " : "", e->hidden ? e->why : "");
    }
    for (int i = 0; i < cands.count; i++) {
        free(cands.items[i].path);
        free(cands.items[i].nvram_desc);
    }
    free(cands.items);
}

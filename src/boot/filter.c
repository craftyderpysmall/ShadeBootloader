/*
 * Classification and de-cluttering of boot candidates.
 *
 * Goal: one tile per operating system plus one per other bootable drive.
 * Everything else (utilities, alternate loaders, fallback copies, firmware
 * entries, ignored volumes) is kept but marked hidden so F3 can reveal it.
 */
#include <stdlib.h>
#include <string.h>
#include "entry.h"
#include "core/str.h"

/* ---- list helpers ---------------------------------------------------- */

Entry *entries_add(EntryList *l)
{
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        Entry *n = malloc(sizeof(Entry) * cap);
        if (l->items) {
            memcpy(n, l->items, sizeof(Entry) * l->count);
            free(l->items);
        }
        l->items = n;
        l->cap = cap;
    }
    Entry *e = &l->items[l->count++];
    memset(e, 0, sizeof(*e));
    e->vol = -1;
    e->boot_num = -1;
    return e;
}

void entries_free(EntryList *l)
{
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

static int entry_cmp(const Entry *a, const Entry *b)
{
    if (a->hidden != b->hidden)
        return a->hidden ? 1 : -1;
    if (a->priority != b->priority)
        return b->priority - a->priority;
    return str_casecmp(a->title, b->title);
}

void entries_sort(EntryList *l)
{
    /* insertion sort: lists are tiny and this keeps it stable */
    for (int i = 1; i < l->count; i++) {
        Entry tmp = l->items[i];
        int j = i - 1;
        while (j >= 0 && entry_cmp(&l->items[j], &tmp) > 0) {
            l->items[j + 1] = l->items[j];
            j--;
        }
        l->items[j + 1] = tmp;
    }
}

int entries_visible(const EntryList *l)
{
    int n = 0;
    for (int i = 0; i < l->count; i++)
        n += !l->items[i].hidden;
    return n;
}

static void hide(Entry *e, const char *why)
{
    if (!e->hidden) {
        e->hidden = true;
        str_copy(e->why, why, sizeof(e->why));
    }
}

/* ---- knowledge tables ------------------------------------------------ */

typedef struct {
    const char *dir;
    const char *title;
} Distro;

static const Distro distros[] = {
    { "ubuntu", "Ubuntu" },        { "debian", "Debian" },         { "fedora", "Fedora" },
    { "arch", "Arch Linux" },      { "manjaro", "Manjaro" },       { "opensuse", "openSUSE" },
    { "opensuse-secureboot", "openSUSE" }, { "sles", "SUSE Linux" }, { "pop", "Pop!_OS" },
    { "neon", "KDE neon" },        { "linuxmint", "Linux Mint" },  { "mint", "Linux Mint" },
    { "elementary", "elementary OS" }, { "zorin", "Zorin OS" },    { "kali", "Kali Linux" },
    { "centos", "CentOS" },        { "redhat", "Red Hat" },        { "rocky", "Rocky Linux" },
    { "almalinux", "AlmaLinux" },  { "gentoo", "Gentoo" },         { "nixos", "NixOS" },
    { "endeavouros", "EndeavourOS" }, { "void", "Void Linux" },    { "garuda", "Garuda Linux" },
    { "mx", "MX Linux" },          { "deepin", "deepin" },         { "cachyos", "CachyOS" },
    { "nobara", "Nobara" },        { "steamos", "SteamOS" },       { "proxmox", "Proxmox" },
    { "solus", "Solus" },          { "alpine", "Alpine Linux" },   { "slackware", "Slackware" },
    { "qubes", "Qubes OS" },       { "chromeos", "ChromeOS" },     { "tails", "Tails" },
};

/* Files that are never what a person means by "boot my OS". */
static const char *utility_globs[] = {
    "mm*.efi", "fb*.efi", "fwup*.efi", "memtest*", "*shell*.efi", "mokmanager*",
    "hashtool*", "keytool*", "lockdown*", "*recovery*", "gcd*.efi", "*diag*",
    "boot*ia32.efi", "*ia32*.efi", "*aa64*.efi", "*arm*.efi", "*riscv*.efi",
};

static bool is_utility(const char *base)
{
    for (size_t i = 0; i < sizeof(utility_globs) / sizeof(*utility_globs); i++)
        if (str_glob_ci(utility_globs[i], base))
            return true;
    return false;
}

static const Distro *find_distro(const char *vendor)
{
    for (size_t i = 0; i < sizeof(distros) / sizeof(*distros); i++)
        if (str_eq_ci(distros[i].dir, vendor))
            return &distros[i];
    return NULL;
}

/* Splits "\EFI\<vendor>\<file>"; vendor is "" for other layouts. */
static void split_path(const char *path, char *vendor, int cap)
{
    vendor[0] = 0;
    const char *p = path;
    if (*p == '\\' || *p == '/')
        p++;
    if (str_ncasecmp(p, "EFI", 3) != 0 || (p[3] != '\\' && p[3] != '/'))
        return;
    p += 4;
    int n = 0;
    while (p[n] && p[n] != '\\' && p[n] != '/')
        n++;
    if (!p[n])      /* file directly in \EFI */
        return;
    if (n >= cap)
        n = cap - 1;
    memcpy(vendor, p, n);
    vendor[n] = 0;
}

static const char *media_name(MediaKind m)
{
    switch (m) {
    case MEDIA_USB: return "USB drive";
    case MEDIA_OPTICAL: return "CD / DVD";
    case MEDIA_NETWORK: return "Network";
    case MEDIA_FIRMWARE: return "Firmware";
    default: return "Internal drive";
    }
}

static const char *media_icon(MediaKind m)
{
    switch (m) {
    case MEDIA_USB: return "drive-usb,drive-disk";
    case MEDIA_OPTICAL: return "drive-cd,drive-disk";
    case MEDIA_NETWORK: return "drive-net,os-generic";
    default: return "drive-disk";
    }
}

/* Loader preference inside a distro directory: lower is better, 0 = not a loader. */
static int distro_loader_rank(const char *base)
{
    if (str_eq_ci(base, "shimx64.efi")) return 1;
    if (str_eq_ci(base, "grubx64.efi")) return 2;
    if (str_eq_ci(base, "systemd-bootx64.efi")) return 3;
    if (str_eq_ci(base, "refind_x64.efi")) return 4;
    return 0;
}

/* Fill an entry from a loader path; unknown loaders come back hidden. */
static void classify(Entry *e, const Volume *v, const char *path)
{
    char vendor[64];
    split_path(path, vendor, sizeof(vendor));
    const char *base = str_basename(path);
    const Distro *d;

    str_copy(e->path, path, sizeof(e->path));
    e->kind = KIND_OS;
    e->priority = 60;
    str_copy(e->icon, "os-generic", sizeof(e->icon));

    if (str_eq_ci(path, "\\System\\Library\\CoreServices\\boot.efi")) {
        str_copy(e->title, v && v->label && *v->label ? v->label : "macOS", sizeof(e->title));
        str_copy(e->subtitle, "macOS", sizeof(e->subtitle));
        str_copy(e->icon, "os-macos,os-generic", sizeof(e->icon));
        e->priority = 95;
        return;
    }
    if (str_eq_ci(vendor, "BOOT") && str_eq_ci(base, "bootx64.efi")) {
        e->kind = KIND_DRIVE;
        const char *label = v && v->label && *v->label ? v->label : NULL;
        str_copy(e->title, label ? label : (v && v->media == MEDIA_USB ? "USB Drive" : "EFI Drive"), sizeof(e->title));
        str_copy(e->subtitle, media_name(v ? v->media : MEDIA_INTERNAL), sizeof(e->subtitle));
        str_copy(e->icon, media_icon(v ? v->media : MEDIA_INTERNAL), sizeof(e->icon));
        e->priority = v && v->media == MEDIA_INTERNAL ? 50 : 45;
        return;
    }
    if (is_utility(base)) {
        e->kind = KIND_TOOL;
        str_copy(e->title, base, sizeof(e->title));
        str_copy(e->subtitle, vendor[0] ? vendor : "Tool", sizeof(e->subtitle));
        e->priority = 10;
        hide(e, "utility");
        return;
    }
    if (str_eq_ci(vendor, "Microsoft")) {
        if (str_eq_ci(base, "bootmgfw.efi")) {
            str_copy(e->title, "Windows", sizeof(e->title));
            str_copy(e->subtitle, "Windows Boot Manager", sizeof(e->subtitle));
            str_copy(e->icon, "os-windows,os-generic", sizeof(e->icon));
            e->priority = 100;
        } else {
            e->kind = KIND_TOOL;
            str_copy(e->title, base, sizeof(e->title));
            str_copy(e->subtitle, "Microsoft", sizeof(e->subtitle));
            e->priority = 10;
            hide(e, "windows component");
        }
        return;
    }
    if (str_eq_ci(vendor, "Linux")) {           /* unified kernel images */
        str_copy(e->title, "Linux", sizeof(e->title));
        for (size_t i = 0; i < sizeof(distros) / sizeof(*distros); i++)
            if (str_starts_ci(base, distros[i].dir)) {
                str_copy(e->title, distros[i].title, sizeof(e->title));
                str_fmt(e->icon, sizeof(e->icon), "os-%s,os-linux,os-generic", distros[i].dir);
                break;
            }
        if (str_eq_ci(e->title, "Linux"))
            str_copy(e->icon, "os-linux,os-generic", sizeof(e->icon));
        str_copy(e->subtitle, "Unified kernel", sizeof(e->subtitle));
        e->priority = 85;
        return;
    }
    if (str_eq_ci(vendor, "systemd") && str_eq_ci(base, "systemd-bootx64.efi")) {
        str_copy(e->title, "Linux", sizeof(e->title));
        str_copy(e->subtitle, "systemd-boot", sizeof(e->subtitle));
        str_copy(e->icon, "os-linux,os-generic", sizeof(e->icon));
        e->priority = 88;
        return;
    }
    if ((d = find_distro(vendor)) != NULL) {
        int rank = distro_loader_rank(base);
        str_copy(e->title, d->title, sizeof(e->title));
        str_fmt(e->icon, sizeof(e->icon), "os-%s,os-linux,os-generic", d->dir);
        str_copy(e->subtitle, rank == 1 ? "Shim" : rank == 2 ? "GRUB" : rank == 3 ? "systemd-boot" : base,
                 sizeof(e->subtitle));
        e->priority = 90;
        if (!rank)
            hide(e, "distro tool");
        return;
    }
    if (str_eq_ci(vendor, "refind") && str_starts_ci(base, "refind_x64")) {
        str_copy(e->title, "rEFInd", sizeof(e->title));
        str_copy(e->subtitle, "Boot manager", sizeof(e->subtitle));
        e->priority = 70;
        return;
    }
    if (str_eq_ci(vendor, "OC") && str_eq_ci(base, "OpenCore.efi")) {
        str_copy(e->title, "OpenCore", sizeof(e->title));
        str_copy(e->subtitle, "Boot manager", sizeof(e->subtitle));
        e->priority = 70;
        return;
    }
    if (str_eq_ci(vendor, "CLOVER") && str_starts_ci(base, "CLOVERX64")) {
        str_copy(e->title, "Clover", sizeof(e->title));
        str_copy(e->subtitle, "Boot manager", sizeof(e->subtitle));
        e->priority = 70;
        return;
    }
    /* Unknown vendor directory: OEM tools, updaters, leftovers. */
    e->kind = KIND_TOOL;
    str_copy(e->title, base, sizeof(e->title));
    str_copy(e->subtitle, vendor[0] ? vendor : "EFI", sizeof(e->subtitle));
    e->priority = 20;
    hide(e, "unrecognized loader");
}

static bool same_target(const Entry *e, int vol, const char *path)
{
    return e->vol == vol && path && str_eq_ci(e->path, path);
}

static bool vendor_eq(const char *a, const char *b)
{
    char va[64], vb[64];
    split_path(a, va, sizeof(va));
    split_path(b, vb, sizeof(vb));
    return va[0] && str_eq_ci(va, vb);
}

bool filter_volume_ignored(const IgnoreRules *r, const Volume *v)
{
    if (!r)
        return false;
    for (int i = 0; i < r->nvolume; i++)
        if (v->label && str_glob_ci(r->volume[i], v->label))
            return true;
    for (int i = 0; i < r->npartuuid; i++)
        if (v->partuuid[0] && str_glob_ci(r->partuuid[i], v->partuuid))
            return true;
    return false;
}

void filter_apply_rules(EntryList *l, const IgnoreRules *r)
{
    if (!r)
        return;
    for (int i = 0; i < l->count; i++) {
        Entry *e = &l->items[i];
        for (int k = 0; k < r->npath; k++)
            if (e->path[0] && str_glob_ci(r->path[k], e->path))
                hide(e, "ignored (path)");
        for (int k = 0; k < r->nentry; k++)
            if (str_glob_ci(r->entry[k], e->title))
                hide(e, "ignored (entry)");
    }
}

void filter_build(const Volume *vols, int nvols,
                  const Candidate *cands, int ncands,
                  const IgnoreRules *rules,
                  int self_vol, const char *self_path,
                  EntryList *out)
{
    int first = out->count;

    /* 1. file-scanned candidates */
    for (int i = 0; i < ncands; i++) {
        const Candidate *c = &cands[i];
        if (c->boot_num >= 0 || !c->path || c->vol < 0 || c->vol >= nvols)
            continue;
        const Volume *v = &vols[c->vol];
        if (filter_volume_ignored(rules, v))
            continue;
        Entry *e = entries_add(out);
        e->vol = c->vol;
        classify(e, v, c->path);
        char vendor[64];
        split_path(c->path, vendor, sizeof(vendor));
        if ((c->vol == self_vol && self_path && str_eq_ci(c->path, self_path)) || str_eq_ci(vendor, "shade"))
            hide(e, "Shade itself");
    }
    int scanned_end = out->count;

    /* 2. one loader per distro directory: keep the best-ranked one */
    for (int i = first; i < scanned_end; i++) {
        Entry *a = &out->items[i];
        int ra = distro_loader_rank(str_basename(a->path));
        if (a->hidden || !ra || a->kind != KIND_OS)
            continue;
        for (int j = first; j < scanned_end; j++) {
            Entry *b = &out->items[j];
            int rb = distro_loader_rank(str_basename(b->path));
            if (i == j || b->hidden || !rb || b->vol != a->vol || !vendor_eq(a->path, b->path))
                continue;
            if (rb > ra || (rb == ra && j > i))
                hide(b, "alternate loader");
        }
    }

    /* 3. unified kernels: newest per volume; none if systemd-boot manages them */
    for (int i = first; i < scanned_end; i++) {
        Entry *a = &out->items[i];
        char va[64];
        split_path(a->path, va, sizeof(va));
        if (a->hidden || !str_eq_ci(va, "Linux"))
            continue;
        for (int j = first; j < scanned_end; j++) {
            Entry *b = &out->items[j];
            if (b->vol == a->vol && !b->hidden && str_eq_ci(b->subtitle, "systemd-boot")) {
                hide(a, "managed by systemd-boot");
                break;
            }
            char vb[64];
            split_path(b->path, vb, sizeof(vb));
            if (i != j && !b->hidden && b->vol == a->vol && str_eq_ci(vb, "Linux") &&
                str_eq_ci(b->title, a->title) && str_casecmp(b->path, a->path) > 0) {
                hide(a, "older kernel");
                break;
            }
        }
    }

    /* 4. NVRAM entries: merge with scanned ones, surface only genuinely new loaders */
    for (int i = 0; i < ncands; i++) {
        const Candidate *c = &cands[i];
        if (c->boot_num < 0)
            continue;
        if (c->vol >= nvols)
            continue;
        if (c->vol >= 0 && filter_volume_ignored(rules, &vols[c->vol]))
            continue;
        Entry *dup = NULL;
        for (int j = first; j < out->count && !dup; j++)
            if (c->path && same_target(&out->items[j], c->vol, c->path))
                dup = &out->items[j];
        if (dup) {
            /* The user (or an installer) registered this loader explicitly:
             * trust that for loaders we only hid for being unrecognized. */
            if (dup->hidden && (str_eq_ci(dup->why, "unrecognized loader") || str_eq_ci(dup->why, "distro tool"))) {
                dup->hidden = false;
                dup->why[0] = 0;
                dup->kind = KIND_OS;
                dup->priority = 60;
                if (c->nvram_desc && *c->nvram_desc)
                    str_copy(dup->title, c->nvram_desc, sizeof(dup->title));
            }
            continue;
        }
        Entry *e = entries_add(out);
        e->boot_num = c->boot_num;
        if (c->path && c->vol >= 0) {
            e->vol = c->vol;
            classify(e, &vols[c->vol], c->path);
            if (e->kind == KIND_TOOL && str_eq_ci(e->why, "unrecognized loader")) {
                e->hidden = false;
                e->why[0] = 0;
                e->kind = KIND_OS;
            }
            if (c->nvram_desc && *c->nvram_desc && e->kind == KIND_OS)
                str_copy(e->title, c->nvram_desc, sizeof(e->title));
        } else {
            e->kind = KIND_FIRMWARE;
            str_copy(e->title, c->nvram_desc && *c->nvram_desc ? c->nvram_desc : "Firmware entry", sizeof(e->title));
            str_copy(e->subtitle, "Firmware boot option", sizeof(e->subtitle));
            str_copy(e->icon, "os-generic", sizeof(e->icon));
            e->priority = 5;
            hide(e, "firmware entry");
        }
    }

    /* 5. fallback loaders only stand for drives that have nothing better */
    for (int i = first; i < out->count; i++) {
        Entry *a = &out->items[i];
        if (a->kind != KIND_DRIVE || a->hidden)
            continue;
        for (int j = first; j < out->count; j++) {
            Entry *b = &out->items[j];
            if (b != a && !b->hidden && b->vol == a->vol && b->kind == KIND_OS) {
                hide(a, "fallback copy");
                break;
            }
        }
    }

    /* 6. de-duplicate identical visible targets (e.g. NVRAM listing a fallback) */
    for (int i = first; i < out->count; i++)
        for (int j = i + 1; j < out->count; j++)
            if (!out->items[j].hidden && same_target(&out->items[i], out->items[j].vol, out->items[j].path))
                hide(&out->items[j], "duplicate");

    filter_apply_rules(out, rules);
    entries_sort(out);
}

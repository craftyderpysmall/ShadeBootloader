/*
 * Boot entry model. Discovery (discover.c, firmware side) produces volumes and
 * raw candidates; the filter (filter.c, pure C) turns them into the short list
 * of entries the picker shows, hiding utilities, duplicates and ignored items.
 */
#ifndef SHADE_ENTRY_H
#define SHADE_ENTRY_H

#include <stdbool.h>

typedef enum {
    MEDIA_INTERNAL,
    MEDIA_USB,
    MEDIA_OPTICAL,
    MEDIA_NETWORK,
    MEDIA_FIRMWARE,
} MediaKind;

typedef enum {
    KIND_OS,        /* an operating system loader */
    KIND_DRIVE,     /* "boot this drive" via its fallback loader */
    KIND_TOOL,      /* utilities: shells, MokManager, diagnostics */
    KIND_FIRMWARE,  /* firmware-only Boot#### entries (PXE, internal shell) */
} EntryKind;

typedef struct {
    char *label;            /* filesystem label, may be NULL */
    char  partuuid[40];     /* lower-case GPT partition GUID, "" if unknown */
    MediaKind media;
    void *handle;           /* EFI_HANDLE of the filesystem (opaque here) */
} Volume;

typedef struct {
    int   vol;              /* index into volumes, -1 = not on a known filesystem */
    char *path;             /* "\EFI\ubuntu\shimx64.efi", NULL for firmware entries */
    char *nvram_desc;       /* description when the candidate came from Boot#### */
    int   boot_num;         /* Boot#### number, -1 if file-scanned */
} Candidate;

typedef struct {
    char title[64];
    char subtitle[64];
    char icon[96];          /* comma-separated fallback chain */
    EntryKind kind;
    int  vol;
    char path[256];
    char options[512];      /* load options (kernel command line) */
    int  boot_num;
    int  priority;
    bool hidden;
    char why[48];           /* reason it is hidden, shown with F3 */
    bool custom;            /* defined in shade.conf */
} Entry;

typedef struct {
    Entry *items;
    int count, cap;
} EntryList;

#define MAX_RULES 32

/* [ignore] rules from shade.conf; values are case-insensitive globs. */
typedef struct {
    const char *volume[MAX_RULES];   int nvolume;
    const char *partuuid[MAX_RULES]; int npartuuid;
    const char *path[MAX_RULES];     int npath;
    const char *entry[MAX_RULES];    int nentry;
} IgnoreRules;

Entry *entries_add(EntryList *l);
void   entries_free(EntryList *l);
void   entries_sort(EntryList *l);
int    entries_visible(const EntryList *l);

bool filter_volume_ignored(const IgnoreRules *r, const Volume *v);

void filter_build(const Volume *vols, int nvols,
                  const Candidate *cands, int ncands,
                  const IgnoreRules *rules,
                  int self_vol, const char *self_path,
                  EntryList *out);

/* Re-applies path / entry ignore rules (used after custom entries are added). */
void filter_apply_rules(EntryList *l, const IgnoreRules *rules);

#endif

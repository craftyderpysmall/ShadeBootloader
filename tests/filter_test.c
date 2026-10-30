/* Host-side tests for boot entry filtering: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot/entry.h"
#include "core/config.h"
#include "core/str.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static Candidate file(int vol, const char *path)
{
    Candidate c = { vol, (char *)path, NULL, -1 };
    return c;
}

static Candidate nvram(int vol, const char *path, const char *desc, int num)
{
    Candidate c = { vol, (char *)path, (char *)desc, num };
    return c;
}

static void visible_titles(const EntryList *l, char *out, size_t cap)
{
    out[0] = 0;
    for (int i = 0; i < l->count; i++) {
        if (l->items[i].hidden)
            continue;
        if (out[0])
            strncat(out, ", ", cap - strlen(out) - 1);
        strncat(out, l->items[i].title, cap - strlen(out) - 1);
    }
}

static const Entry *find(const EntryList *l, const char *path)
{
    for (int i = 0; i < l->count; i++)
        if (str_eq_ci(l->items[i].path, path))
            return &l->items[i];
    return NULL;
}

/* Windows + Ubuntu dual boot with all the usual clutter, a USB installer
 * and a data drive that happens to carry an EFI folder. */
static void test_dual_boot(void)
{
    Volume vols[] = {
        { "ESP", "5ade0000-0000-4000-8000-000000000001", MEDIA_INTERNAL, NULL },
        { "INSTALLER", "5ade0000-0000-4000-8000-000000000002", MEDIA_USB, NULL },
        { "GAMES", "5ade0000-0000-4000-8000-000000000003", MEDIA_INTERNAL, NULL },
    };
    Candidate c[] = {
        file(0, "\\EFI\\BOOT\\BOOTX64.EFI"),
        file(0, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        file(0, "\\EFI\\Microsoft\\Boot\\bootmgr.efi"),
        file(0, "\\EFI\\Microsoft\\Boot\\memtest.efi"),
        file(0, "\\EFI\\ubuntu\\shimx64.efi"),
        file(0, "\\EFI\\ubuntu\\grubx64.efi"),
        file(0, "\\EFI\\ubuntu\\mmx64.efi"),
        file(0, "\\EFI\\HP\\hpdiags.efi"),
        file(0, "\\EFI\\tools\\shellx64.efi"),
        file(1, "\\EFI\\BOOT\\BOOTX64.EFI"),
        file(2, "\\EFI\\BOOT\\BOOTX64.EFI"),
        nvram(0, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi", "Windows Boot Manager", 0),
        nvram(0, "\\EFI\\ubuntu\\shimx64.efi", "ubuntu", 1),
        nvram(-1, NULL, "UEFI PXEv4 (MAC:525400123456)", 2),
        nvram(-1, NULL, "EFI Internal Shell", 3),
    };
    IgnoreRules r = { 0 };
    r.volume[r.nvolume++] = "games";
    EntryList l = { 0 };
    filter_build(vols, 3, c, sizeof(c) / sizeof(*c), &r, 0, "\\EFI\\BOOT\\BOOTX64.EFI", &l);

    char titles[256];
    visible_titles(&l, titles, sizeof(titles));
    CHECK(strcmp(titles, "Windows, Ubuntu, INSTALLER") == 0, "visible = [%s]", titles);

    const Entry *u = find(&l, "\\EFI\\ubuntu\\shimx64.efi");
    CHECK(u && !u->hidden && strcmp(u->subtitle, "Shim") == 0, "ubuntu boots through shim");
    CHECK(find(&l, "\\EFI\\ubuntu\\grubx64.efi")->hidden, "grub hidden behind shim");
    CHECK(find(&l, "\\EFI\\HP\\hpdiags.efi")->hidden, "OEM tool hidden");
    for (int i = 0; i < l.count; i++)
        CHECK(l.items[i].vol != 2, "ignored volume never produces entries (%s)", l.items[i].title);
    entries_free(&l);
}

static void test_fallback_only_drive(void)
{
    Volume vols[] = { { NULL, "", MEDIA_USB, NULL } };
    Candidate c[] = { file(0, "\\EFI\\BOOT\\BOOTX64.EFI") };
    EntryList l = { 0 };
    filter_build(vols, 1, c, 1, NULL, -1, NULL, &l);
    CHECK(l.count == 1 && !l.items[0].hidden && strcmp(l.items[0].title, "USB Drive") == 0,
          "unlabeled USB stick shows as 'USB Drive'");
    CHECK(l.items[0].kind == KIND_DRIVE, "kind drive");
    entries_free(&l);
}

static void test_systemd_boot_and_ukis(void)
{
    Volume vols[] = { { "ESP", "", MEDIA_INTERNAL, NULL } };
    Candidate c[] = {
        file(0, "\\EFI\\systemd\\systemd-bootx64.efi"),
        file(0, "\\EFI\\Linux\\arch-linux.efi"),
        file(0, "\\EFI\\BOOT\\BOOTX64.EFI"),
    };
    EntryList l = { 0 };
    filter_build(vols, 1, c, 3, NULL, -1, NULL, &l);
    char titles[256];
    visible_titles(&l, titles, sizeof(titles));
    CHECK(strcmp(titles, "Linux") == 0, "only systemd-boot shown, got [%s]", titles);
    entries_free(&l);

    Candidate c2[] = {
        file(0, "\\EFI\\Linux\\fedora-6.10.efi"),
        file(0, "\\EFI\\Linux\\fedora-6.11.efi"),
    };
    filter_build(vols, 1, c2, 2, NULL, -1, NULL, &l);
    visible_titles(&l, titles, sizeof(titles));
    CHECK(strcmp(titles, "Fedora") == 0, "one UKI tile, got [%s]", titles);
    CHECK(!find(&l, "\\EFI\\Linux\\fedora-6.11.efi")->hidden, "newest UKI kept");
    entries_free(&l);
}

static void test_nvram_custom_loader(void)
{
    Volume vols[] = { { "ESP", "", MEDIA_INTERNAL, NULL } };
    Candidate c[] = {
        file(0, "\\EFI\\custom\\kernel.efi"),
        nvram(0, "\\EFI\\custom\\kernel.efi", "My Kernel", 7),
    };
    EntryList l = { 0 };
    filter_build(vols, 1, c, 2, NULL, -1, NULL, &l);
    char titles[256];
    visible_titles(&l, titles, sizeof(titles));
    CHECK(strcmp(titles, "My Kernel") == 0, "registered loader surfaced, got [%s]", titles);
    entries_free(&l);
}

static void test_ignore_rules(void)
{
    Volume vols[] = { { "ESP", "abcd-1234", MEDIA_INTERNAL, NULL }, { "Data", "dead-beef", MEDIA_INTERNAL, NULL } };
    Candidate c[] = {
        file(0, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        file(0, "\\EFI\\fedora\\shimx64.efi"),
        file(1, "\\EFI\\BOOT\\BOOTX64.EFI"),
    };
    IgnoreRules r = { 0 };
    r.partuuid[r.npartuuid++] = "DEAD-*";
    r.entry[r.nentry++] = "fedora";
    EntryList l = { 0 };
    filter_build(vols, 2, c, 3, &r, -1, NULL, &l);
    char titles[256];
    visible_titles(&l, titles, sizeof(titles));
    CHECK(strcmp(titles, "Windows") == 0, "partuuid + entry rules, got [%s]", titles);
    entries_free(&l);
}

static void test_duplicate_titles(void)
{
    Volume vols[] = { { "EFI", "", MEDIA_INTERNAL, NULL }, { "EFI", "", MEDIA_INTERNAL, NULL },
                      { "Work", "", MEDIA_INTERNAL, NULL } };
    Candidate c[] = {
        file(0, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        file(1, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        file(2, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
    };
    EntryList l = { 0 };
    filter_build(vols, 3, c, 3, NULL, -1, NULL, &l);
    CHECK(l.count == 3 && entries_visible(&l) == 3, "all three Windows installs shown");
    CHECK(strcmp(l.items[0].subtitle, l.items[1].subtitle) != 0 &&
          strcmp(l.items[1].subtitle, l.items[2].subtitle) != 0 &&
          strcmp(l.items[0].subtitle, l.items[2].subtitle) != 0,
          "subtitles differ: [%s] [%s] [%s]", l.items[0].subtitle, l.items[1].subtitle, l.items[2].subtitle);
    int work = -1;
    for (int i = 0; i < 3; i++)
        if (l.items[i].vol == 2)
            work = i;
    CHECK(work >= 0 && strcmp(l.items[work].subtitle, "on Work") == 0, "unique label used");
    entries_free(&l);

    /* a single Windows keeps its normal subtitle */
    filter_build(vols, 1, c, 1, NULL, -1, NULL, &l);
    CHECK(strcmp(l.items[0].subtitle, "Windows Boot Manager") == 0, "lone entry untouched");
    entries_free(&l);
}

static void on_kv(void *user, const char *sec, const char *key, const char *val)
{
    char *out = user;
    char line[128];
    snprintf(line, sizeof(line), "%s.%s=%s;", sec, key, val);
    strcat(out, line);
}

static void test_config_parser(void)
{
    char out[512] = "";
    config_parse("# comment\ntimeout = 5   # seconds\n[ignore]\nvolume = \"My Games\"\nvolume=GAMES\r\n", on_kv, out);
    CHECK(strcmp(out, ".timeout=5;ignore.volume=My Games;ignore.volume=GAMES;") == 0, "parsed [%s]", out);
    CHECK(config_color("#0a0a0a", 0) == 0xff0a0a0a, "rgb color");
    CHECK(config_color("#00000099", 0) == 0x99000000, "rgba color");
    CHECK(str_glob_ci("\\efi\\*\\*.EFI", "\\EFI\\ubuntu\\shimx64.efi"), "glob");
    CHECK(!str_glob_ci("GAMES", "GAMES2"), "glob anchored");
}

int main(void)
{
    test_dual_boot();
    test_fallback_only_drive();
    test_systemd_boot_and_ukis();
    test_nvram_custom_loader();
    test_ignore_rules();
    test_duplicate_titles();
    test_config_parser();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("filter tests passed\n");
    return 0;
}

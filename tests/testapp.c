/*
 * Stand-in for real OS loaders on the QEMU test disks. Reports its own path
 * and load options on the debug console, shows them on screen, then powers
 * the VM off so automated runs terminate.
 */
#include "efi.h"

static void outb(UINT16 port, UINT8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void dbg(const CHAR16 *s)
{
    for (; *s; s++)
        outb(0xe9, (UINT8)(*s < 0x80 ? *s : '?'));
}

int _fltused = 1;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    st->BootServices->HandleProtocol(image, &li_guid, (void **)&li);

    CHAR16 *path = (CHAR16 *)u"?";
    if (li && li->FilePath && li->FilePath->Type == DP_TYPE_MEDIA &&
        li->FilePath->SubType == DP_SUB_MEDIA_FILE)
        path = ((FILEPATH_DEVICE_PATH *)li->FilePath)->PathName;

    dbg(u"[testapp] BOOTED ");
    dbg(path);
    if (li && li->LoadOptionsSize) {
        dbg(u" OPTIONS ");
        dbg((CHAR16 *)li->LoadOptions);
    }
    dbg(u"\n");

    st->ConOut->ClearScreen(st->ConOut);
    st->ConOut->OutputString(st->ConOut, (CHAR16 *)u"Shade test loader started: ");
    st->ConOut->OutputString(st->ConOut, path);
    st->ConOut->OutputString(st->ConOut, (CHAR16 *)u"\r\n");
    st->BootServices->Stall(1500000);
    st->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, 0);
    return EFI_SUCCESS;
}

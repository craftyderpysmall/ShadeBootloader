#!/bin/sh
# Install Shade onto the EFI System Partition and register it with the firmware.
#
#   sudo tools/install.sh [--dry-run] [--yes] [ESP_MOUNT]
#
# The ESP is found automatically (/boot/efi, /efi or /boot) unless given. The
# disk and partition number for the firmware entry are read from the ESP's
# mount, never guessed, and you are asked before anything is written to NVRAM.
set -eu

ESP_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/build/BOOTX64.EFI"
LOADER='\EFI\shade\shadex64.efi'
DRY=0
YES=0
ESP=
SYS=${SHADE_SYS:-/sys}     # overridable for tests/install_test.sh

die() { echo "error: $*" >&2; exit 1; }
run() { if [ "$DRY" = 1 ]; then echo "  would run: $*"; else "$@"; fi; }

for a in "$@"; do
    case $a in
        --dry-run) DRY=1 ;;
        --yes|-y) YES=1 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        -*) die "unknown option $a" ;;
        *) ESP=$a ;;
    esac
done

[ -d "$SYS/firmware/efi" ] || die "this system was not booted in UEFI mode; Shade needs UEFI"
[ -f "$BIN" ] || die "build first: make"
command -v findmnt >/dev/null && command -v lsblk >/dev/null || die "needs findmnt and lsblk (util-linux)"

# The partition behind a mount point, if it really is an ESP.
esp_source() {
    src=$(findmnt -no SOURCE --mountpoint "$1" 2>/dev/null) || return 1
    [ -n "$src" ] || return 1
    ptype=$(lsblk -no PARTTYPE "$src" 2>/dev/null | tr 'A-F' 'a-f')
    [ "$ptype" = "$ESP_GUID" ] || return 1
    echo "$src"
}

if [ -n "$ESP" ]; then
    ESP=${ESP%/}
    SRC=$(esp_source "$ESP") || die "$ESP is not a mounted EFI System Partition"
else
    for m in /boot/efi /efi /boot; do
        if SRC=$(esp_source "$m"); then ESP=$m; break; fi
    done
    [ -n "$ESP" ] || die "no mounted EFI System Partition at /boot/efi, /efi or /boot; mount it or pass its path"
fi

PART=$(cat "$SYS/class/block/$(basename "$SRC")/partition" 2>/dev/null) || die "can't tell which partition $SRC is"
PK=$(lsblk -no PKNAME "$SRC" | head -n1)
[ -n "$PK" ] || die "can't tell which disk $SRC is on"
DISK=/dev/$PK

echo "EFI System Partition: $ESP  ($SRC = $DISK, partition $PART)"

[ "$DRY" = 1 ] || [ "$(id -u)" = 0 ] || die "run with sudo"

run mkdir -p "$ESP/EFI/shade/themes"
run cp "$BIN" "$ESP/EFI/shade/shadex64.efi"
run cp -r "$ROOT/themes/." "$ESP/EFI/shade/themes/"
if [ -f "$ESP/EFI/shade/shade.conf" ]; then
    echo "Keeping your existing $ESP/EFI/shade/shade.conf"
else
    run cp "$ROOT/config/shade.conf" "$ESP/EFI/shade/shade.conf"
fi
echo "Files installed to $ESP/EFI/shade/"

if ! command -v efibootmgr >/dev/null; then
    echo "efibootmgr not installed; to register Shade later run:"
    echo "  sudo efibootmgr --create --disk $DISK --part $PART --label Shade --loader '$LOADER'"
    exit 0
fi

existing=$(efibootmgr 2>/dev/null | grep -i 'shadex64.efi' | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\).*/\1/p' | head -n1)
if [ -n "$existing" ]; then
    echo "A firmware entry for Shade already exists (Boot$existing); files were updated in place."
else
    echo
    echo "Next: add a firmware boot entry 'Shade' -> $DISK partition $PART, $LOADER"
    echo "efibootmgr puts new entries first in the boot order, so Shade starts at power-on."
    if [ "$YES" != 1 ] && [ "$DRY" != 1 ]; then
        printf "Create it? [y/N] "
        read -r answer
        case $answer in y|Y|yes|YES) ;; *) echo "Skipped. Nothing written to firmware."; exit 0 ;; esac
    fi
    run efibootmgr --create --disk "$DISK" --part "$PART" --label Shade --loader "$LOADER"
    echo "To undo: sudo efibootmgr --bootnum XXXX --delete-bootnum   (XXXX from 'efibootmgr')"
fi

# SecureBoot variable: 4 attribute bytes, then 1 = enabled
sb=$(od -An -t u1 -j4 -N1 "$SYS"/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null | tr -d ' ')
if [ "$sb" = 1 ]; then
    echo "Warning: Secure Boot is ON and Shade is unsigned, so the firmware will refuse to start it."
    echo "Sign it with your own key (sbsign) or enroll it via shim/MOK, or turn Secure Boot off."
fi

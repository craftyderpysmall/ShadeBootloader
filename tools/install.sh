#!/bin/sh
# Install Shade onto a mounted ESP and register it with the firmware.
#   sudo tools/install.sh [ESP_MOUNT] [DISK PARTNUM]
# e.g. sudo tools/install.sh /boot/efi /dev/nvme0n1 1
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ESP=${1:-/boot/efi}
BIN="$ROOT/build/BOOTX64.EFI"

[ -f "$BIN" ] || { echo "build first: make"; exit 1; }
[ -d "$ESP/EFI" ] || { echo "$ESP does not look like an ESP (no EFI/ directory)"; exit 1; }

mkdir -p "$ESP/EFI/shade/themes"
cp "$BIN" "$ESP/EFI/shade/shadex64.efi"
cp -r "$ROOT/themes/." "$ESP/EFI/shade/themes/"
[ -f "$ESP/EFI/shade/shade.conf" ] || cp "$ROOT/config/shade.conf" "$ESP/EFI/shade/shade.conf"
echo "Installed to $ESP/EFI/shade/"

if [ $# -ge 3 ] && command -v efibootmgr >/dev/null; then
    efibootmgr --create --disk "$2" --part "$3" --label "Shade" --loader '\EFI\shade\shadex64.efi'
    echo "Registered 'Shade' as the first boot option."
else
    echo "To make it the default, run e.g.:"
    echo "  sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label Shade --loader '\\EFI\\shade\\shadex64.efi'"
fi
echo "Note: Shade is unsigned. With Secure Boot on, sign it (sbsign) or enroll it via MOK."

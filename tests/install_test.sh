#!/bin/sh
# Exercises tools/install.sh against stubbed util-linux/efibootmgr: make test
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
mkdir -p "$T/bin" "$T/sys/firmware/efi/efivars"
[ -f "$ROOT/build/BOOTX64.EFI" ] || { echo "install test needs: make"; exit 1; }

# fake system description, rewritten per case
cat > "$T/bin/findmnt" <<'S'
#!/bin/sh
for a; do m=$a; done
grep "^$m " "$FAKE/mounts" | cut -d' ' -f2
S
cat > "$T/bin/lsblk" <<'S'
#!/bin/sh
col=$2; for a; do dev=$a; done
line=$(grep "^$dev " "$FAKE/devs")
case $col in PARTTYPE) echo "$line" | cut -d' ' -f2 ;; PKNAME) echo "$line" | cut -d' ' -f3 ;; esac
S
cat > "$T/bin/efibootmgr" <<'S'
#!/bin/sh
if [ $# -eq 0 ]; then cat "$FAKE/bootlist"; else echo "$*" >> "$FAKE/created"; fi
S
printf '#!/bin/sh\necho 0\n' > "$T/bin/id"
chmod +x "$T/bin/"*

setup() {   # mountpoint device partnum parttype parent
    rm -rf "$T/fake"; mkdir -p "$T/fake" "$T/sys/class/block/$(basename "$2")"
    echo "$1 $2" > "$T/fake/mounts"
    echo "$2 $4 $5" > "$T/fake/devs"
    echo "$3" > "$T/sys/class/block/$(basename "$2")/partition"
    : > "$T/fake/bootlist"
}
inst() { FAKE="$T/fake" SHADE_SYS="$T/sys" PATH="$T/bin:$PATH" sh "$ROOT/tools/install.sh" "$@" 2>&1; }
check() { if eval "$2"; then :; else fails=$((fails+1)); echo "FAIL: $1"; echo "$out" | sed 's/^/    /'; fi; }
ESPG=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Arch-style: ESP mounted at /boot on NVMe partition 1, found automatically
setup /boot /dev/nvme0n1p1 1 $ESPG nvme0n1
out=$(inst --dry-run)
check "nvme /boot detected" 'echo "$out" | grep -q -- "--disk /dev/nvme0n1 --part 1 "'

# SATA disk, ESP is partition 2 at /boot/efi
setup /boot/efi /dev/sda2 2 $ESPG sda
out=$(inst --dry-run)
check "sata partition 2" 'echo "$out" | grep -q -- "--disk /dev/sda --part 2 "'

# a FAT /boot that is not an ESP must be refused, not guessed around
setup /boot /dev/sdb1 1 0fc63daf-8483-4772-8e79-3d69d8477de4 sdb
out=$(inst --dry-run); rc=$?
check "non-ESP refused" '[ $rc -ne 0 ] && echo "$out" | grep -q "no mounted EFI System Partition"'

# real install into a temp ESP; user declines -> nothing written to NVRAM
mkdir -p "$T/esp/EFI"
setup "$T/esp" /dev/nvme1n1p1 1 $ESPG nvme1n1
out=$(echo n | inst "$T/esp")
check "files copied" '[ -f "$T/esp/EFI/shade/shadex64.efi" ] && [ -f "$T/esp/EFI/shade/shade.conf" ]'
check "decline writes nothing" '[ ! -f "$T/fake/created" ]'

# existing shade.conf is kept; existing firmware entry is not duplicated
echo "theme = daylight" > "$T/esp/EFI/shade/shade.conf"
echo 'Boot0007* Shade	HD(1,GPT,...)/File(\EFI\shade\shadex64.efi)' > "$T/fake/bootlist"
out=$(inst --yes "$T/esp")
check "config kept" 'grep -q daylight "$T/esp/EFI/shade/shade.conf"'
check "no duplicate entry" '[ ! -f "$T/fake/created" ] && echo "$out" | grep -q Boot0007'

# accepting creates exactly one entry with the detected disk/partition
: > "$T/fake/bootlist"
out=$(echo y | inst "$T/esp")
check "accept creates entry" 'grep -q -- "--disk /dev/nvme1n1 --part 1 --label Shade" "$T/fake/created"'

if [ $fails -ne 0 ]; then echo "$fails install check(s) failed"; exit 1; fi
echo "install tests passed"

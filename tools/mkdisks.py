#!/usr/bin/env python3
"""Build the QEMU test disks (GPT + FAT) used by `make run`.

usage: mkdisks.py BUILD_DIR SHADE.EFI TESTAPP.EFI

disk-main.img   internal ESP: Shade + Windows + Ubuntu + typical clutter
disk-usb.img    removable "INSTALLER" stick with only a fallback loader
disk-games.img  data drive "GAMES" that should be ignored via [ignore]
"""
import os
import struct
import subprocess
import sys
import uuid
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ESP_TYPE = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
DATA_TYPE = uuid.UUID('ebd0a0a2-b9e5-4433-87c0-68b6b72699c7')
SECTOR = 512


def make_fat(path, size_mb, label, files):
    if os.path.exists(path):
        os.remove(path)
    subprocess.run(['/sbin/mkfs.fat', '-C', '-F', '32' if size_mb >= 64 else '16',
                    '-n', label, path, str(size_mb * 1024)],
                   check=True, stdout=subprocess.DEVNULL)
    made = set()
    for dst, src in files:
        parts = dst.strip('/').split('/')
        for i in range(1, len(parts)):
            d = '/'.join(parts[:i])
            if d not in made:
                subprocess.run(['mmd', '-i', path, '::/' + d], check=True,
                               stderr=subprocess.DEVNULL)
                made.add(d)
        if isinstance(src, bytes):
            tmp = path + '.tmp'
            open(tmp, 'wb').write(src)
            src = tmp
        subprocess.run(['mcopy', '-o', '-i', path, src, '::/' + '/'.join(parts)], check=True)
    if os.path.exists(path + '.tmp'):
        os.remove(path + '.tmp')


def gpt_disk(out, fat_img, type_guid, part_guid, disk_guid, name):
    fat = open(fat_img, 'rb').read()
    first = 2048
    part_sectors = (len(fat) + SECTOR - 1) // SECTOR
    last = first + part_sectors - 1
    total = last + 1 + 33 + 2048
    entries = bytearray(128 * 128)
    entry = (type_guid.bytes_le + part_guid.bytes_le +
             struct.pack('<QQQ', first, last, 0) + name.encode('utf-16-le').ljust(72, b'\0'))
    entries[0:128] = entry
    ent_crc = zlib.crc32(entries) & 0xffffffff

    def header(cur, backup, ent_lba):
        h = struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0,
                        cur, backup, 34, total - 34,
                        disk_guid.bytes_le, ent_lba, 128, 128, ent_crc)
        crc = zlib.crc32(h) & 0xffffffff
        h = h[:16] + struct.pack('<I', crc) + h[20:]
        return h.ljust(SECTOR, b'\0')

    img = bytearray(total * SECTOR)
    # protective MBR
    mbr = bytearray(SECTOR)
    mbr[446:462] = struct.pack('<BBBBBBBBII', 0, 0, 2, 0, 0xee, 0xff, 0xff, 0xff, 1,
                               min(total - 1, 0xffffffff))
    mbr[510:512] = b'\x55\xaa'
    img[0:SECTOR] = mbr
    img[SECTOR:2 * SECTOR] = header(1, total - 1, 2)
    img[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    img[first * SECTOR:first * SECTOR + len(fat)] = fat
    back_ent = total - 33
    img[back_ent * SECTOR:back_ent * SECTOR + len(entries)] = entries
    img[(total - 1) * SECTOR:total * SECTOR] = header(total - 1, 1, back_ent)
    open(out, 'wb').write(img)


def main():
    build, shade, app = sys.argv[1:4]
    themes = []
    for dirpath, _, names in os.walk(os.path.join(ROOT, 'themes')):
        for n in names:
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, os.path.join(ROOT, 'themes'))
            themes.append(('EFI/shade/themes/' + rel, full))

    main_files = [
        ('EFI/BOOT/BOOTX64.EFI', shade),
        ('EFI/shade/shade.conf', os.path.join(ROOT, 'tests', 'test.conf')),
        ('EFI/Microsoft/Boot/bootmgfw.efi', app),
        ('EFI/Microsoft/Boot/bootmgr.efi', app),
        ('EFI/Microsoft/Boot/memtest.efi', app),
        ('EFI/ubuntu/shimx64.efi', app),
        ('EFI/ubuntu/grubx64.efi', app),
        ('EFI/ubuntu/mmx64.efi', app),
        ('EFI/ubuntu/BOOTX64.CSV', b'shimx64.efi,ubuntu,,This is the boot entry for ubuntu\n'),
        ('EFI/HP/hpdiags.efi', app),
        ('EFI/tools/shellx64.efi', app),
    ] + themes
    usb_files = [('EFI/BOOT/BOOTX64.EFI', app)]
    games_files = [('EFI/BOOT/BOOTX64.EFI', app),
                   ('Games/readme.txt', b'not a boot drive\n')]

    disks = [
        ('main', 96, 'EFI', main_files, ESP_TYPE, 'EFI System Partition'),
        ('usb', 32, 'INSTALLER', usb_files, DATA_TYPE, 'Installer'),
        ('games', 32, 'GAMES', games_files, DATA_TYPE, 'Games'),
    ]
    for i, (name, size, label, files, ptype, pname) in enumerate(disks):
        fat = os.path.join(build, f'fat-{name}.img')
        make_fat(fat, size, label, files)
        part = uuid.UUID(f'5ade0000-0000-4000-8000-00000000000{i + 1}')
        disk = uuid.UUID(f'5ade0000-0000-4000-8000-0000000000d{i + 1}')
        gpt_disk(os.path.join(build, f'disk-{name}.img'), fat, ptype, part, disk, pname)
        os.remove(fat)
    print('disks ready:', ', '.join(f'disk-{d[0]}.img' for d in disks))


if __name__ == '__main__':
    main()

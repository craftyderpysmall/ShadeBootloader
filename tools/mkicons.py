#!/usr/bin/env python3
"""Generate the NEUTRAL theme's monochrome icon set.

Icons are white glyphs on transparency; Shade tints them with theme colors at
runtime, so one set serves every palette. Drawn at 1024px and downsampled.

usage: mkicons.py OUT_DIR
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw

S = 1024
OUT = 256
W = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
STROKE = 64


def canvas():
    img = Image.new('RGBA', (S, S), CLEAR)
    return img, ImageDraw.Draw(img)


def cut(img, shape_fn):
    """Erase pixels covered by shape_fn(draw) from img."""
    mask = Image.new('L', (S, S), 0)
    shape_fn(ImageDraw.Draw(mask))
    alpha = ImageChops.subtract(img.getchannel('A'), mask)
    img.putalpha(alpha)


def rot_ellipse(draw, cx, cy, rx, ry, angle, fill):
    pts = []
    a = math.radians(angle)
    for i in range(96):
        t = 2 * math.pi * i / 96
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    draw.polygon(pts, fill=fill)


def thick_arc(draw, box, start, end, width):
    draw.arc(box, start, end, fill=W, width=width)
    # round caps
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    r = (box[2] - box[0]) / 2 - width / 2
    for ang in (start, end):
        t = math.radians(ang)
        x, y = cx + r * math.cos(t), cy + r * math.sin(t)
        draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=W)


def line(draw, a, b, width=STROKE):
    draw.line([a, b], fill=W, width=width)
    for p in (a, b):
        draw.ellipse((p[0] - width / 2, p[1] - width / 2, p[0] + width / 2, p[1] + width / 2), fill=W)


def os_windows():
    img, d = canvas()
    g, s, o = 36, 300, (S - 2 * 300 - 36) // 2
    for i in range(2):
        for j in range(2):
            x, y = o + i * (s + g), o + j * (s + g)
            d.rounded_rectangle((x, y, x + s, y + s), radius=28, fill=W)
    return img


def os_linux():
    img, d = canvas()
    d.ellipse((292, 120, 732, 900), fill=W)                     # body
    rot_ellipse(d, 290, 600, 70, 190, 20, W)                     # flippers
    rot_ellipse(d, 734, 600, 70, 190, -20, W)
    cut(img, lambda m: m.ellipse((372, 390, 652, 860), fill=255))   # belly
    cut(img, lambda m: (m.ellipse((404, 236, 496, 356), fill=255),
                        m.ellipse((528, 236, 620, 356), fill=255)))  # eye sockets
    d = ImageDraw.Draw(img)
    d.ellipse((440, 278, 482, 330), fill=W)                      # pupils
    d.ellipse((542, 278, 584, 330), fill=W)
    d.ellipse((430, 352, 594, 432), fill=W)                      # beak
    d.ellipse((236, 832, 476, 942), fill=W)                      # feet
    d.ellipse((548, 832, 788, 942), fill=W)
    return img


def os_macos():
    img, d = canvas()
    d.ellipse((232, 330, 552, 820), fill=W)
    d.ellipse((472, 330, 792, 820), fill=W)
    d.ellipse((300, 300, 724, 700), fill=W)
    d.ellipse((330, 600, 694, 900), fill=W)
    cut(img, lambda m: m.ellipse((452, 260, 572, 372), fill=255))     # stem notch
    cut(img, lambda m: m.ellipse((706, 400, 900, 610), fill=255))     # bite
    cut(img, lambda m: m.ellipse((470, 830, 554, 920), fill=255))     # bottom notch
    d = ImageDraw.Draw(img)
    rot_ellipse(d, 590, 200, 52, 118, 40, W)                          # leaf
    return img


def os_generic():
    img, d = canvas()
    d.rounded_rectangle((272, 272, 752, 752), radius=72, outline=W, width=STROKE)
    d.rounded_rectangle((412, 412, 612, 612), radius=24, fill=W)
    for k in (392, 512, 632):
        line(d, (k, 140), (k, 272))
        line(d, (k, 752), (k, 884))
        line(d, (140, k), (272, k))
        line(d, (752, k), (884, k))
    return img


def drive_usb():
    img, d = canvas()
    d.rounded_rectangle((322, 400, 702, 920), radius=72, fill=W)
    d.rounded_rectangle((392, 120, 632, 420), radius=24, outline=W, width=STROKE)
    d.rectangle((452, 210, 492, 270), fill=W)
    d.rectangle((532, 210, 572, 270), fill=W)
    return img


def drive_disk():
    img, d = canvas()
    d.rounded_rectangle((152, 300, 872, 724), radius=96, outline=W, width=STROKE)
    line(d, (152 + 32, 540), (872 - 32, 540))
    d.ellipse((640, 600, 704, 664), fill=W)
    d.ellipse((740, 600, 804, 664), fill=W)
    return img


def drive_cd():
    img, d = canvas()
    d.ellipse((152, 152, 872, 872), outline=W, width=STROKE)
    d.ellipse((432, 432, 592, 592), outline=W, width=STROKE)
    thick_arc(d, (272, 272, 752, 752), 200, 250, 40)
    return img


def drive_net():
    img, d = canvas()
    d.ellipse((152, 152, 872, 872), outline=W, width=STROKE)
    d.ellipse((352, 152, 672, 872), outline=W, width=STROKE)
    line(d, (180, 512), (844, 512))
    return img


def power():
    img, d = canvas()
    thick_arc(d, (192, 232, 832, 872), -50, 230, STROKE + 8)
    line(d, (512, 140), (512, 500), STROKE + 8)
    return img


def restart():
    img, d = canvas()
    w = STROKE + 8
    thick_arc(d, (192, 192, 832, 832), -20, 270, w)
    # arrow head at the top of the arc, pointing clockwise (to the right)
    x, y = 512, 192 + w / 2
    d.polygon([(x + 130, y), (x - 20, y - 120), (x - 20, y + 120)], fill=W)
    return img


def firmware():
    img, d = canvas()
    cx = cy = 512
    pts = []
    teeth = 8
    for i in range(teeth * 4):
        a = 2 * math.pi * i / (teeth * 4) - math.pi / 2
        r = 380 if (i % 4) in (0, 1) else 300
        pts.append((cx + r * math.cos(a + math.pi / (teeth * 4)), cy + r * math.sin(a + math.pi / (teeth * 4))))
    d.polygon(pts, fill=W)
    cut(img, lambda m: m.ellipse((392, 392, 632, 632), fill=255))
    return img


def arrow_right():
    img, d = canvas()
    w = STROKE + 24
    line(d, (232, 512), (792, 512), w)
    line(d, (552, 272), (792, 512), w)
    line(d, (552, 752), (792, 512), w)
    return img


def eye():
    img, d = canvas()
    d.ellipse((112, 272, 912, 752), fill=W)
    cut(img, lambda m: m.ellipse((176, 330, 848, 694), fill=255))
    d = ImageDraw.Draw(img)
    d.ellipse((392, 392, 632, 632), fill=W)
    return img


def cursor():
    """Classic arrow, white with a dark outline (not tinted)."""
    size = 1024
    img = Image.new('RGBA', (size, size), CLEAR)
    d = ImageDraw.Draw(img)
    pts = [(160, 80), (160, 860), (360, 680), (500, 980), (620, 925), (480, 630), (740, 630)]
    d.polygon(pts, fill=(10, 10, 10, 255))
    inner = [(215, 205), (215, 730), (375, 585), (520, 900), (565, 880), (420, 575), (610, 575)]
    d.polygon(inner, fill=W)
    return img


ICONS = {
    'os-windows': os_windows, 'os-linux': os_linux, 'os-macos': os_macos,
    'os-generic': os_generic, 'drive-usb': drive_usb, 'drive-disk': drive_disk,
    'drive-cd': drive_cd, 'drive-net': drive_net, 'power': power, 'restart': restart,
    'firmware': firmware, 'arrow-right': arrow_right, 'eye': eye,
}


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in ICONS.items():
        fn().resize((OUT, OUT), Image.LANCZOS).save(os.path.join(out, name + '.png'), optimize=True)
    cursor().resize((64, 64), Image.LANCZOS).save(os.path.join(out, 'cursor.png'), optimize=True)
    print('wrote', len(ICONS) + 1, 'icons to', out)


if __name__ == '__main__':
    main()

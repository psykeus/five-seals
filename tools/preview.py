#!/usr/bin/env python3
"""Render a contact sheet of all metatiles and sprites with their real palettes.
Usage: python3 tools/preview.py  -> build/preview.png
"""
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import build  # noqa: E402

# 2C02-style palette (RGB)
NES = [
    0x525252, 0x00006B, 0x00009B, 0x3D00B1, 0x690074, 0x5B0000, 0x5F0000, 0x401800, 0x102F00, 0x084A08, 0x006700,
    0x004212, 0x00286D, 0x000000, 0x000000, 0x000000, 0xE7D5C4, 0x0040FF, 0x220EDC, 0x6B47FF, 0x9F00D7, 0xD70A68,
    0xBC1900, 0xB15400, 0x5B6A00, 0x038C00, 0x00AB00, 0x00882C, 0x0072A4, 0x000000, 0x000000, 0x000000, 0xF8F8F8,
    0x3CABFF, 0x8179FF, 0xC55BFF, 0xF248FF, 0xFF49DF, 0xFF6D47, 0xF7B400, 0xFFE000, 0x75E300, 0x2BF403, 0x2EB878,
    0x18E2E5, 0x787878, 0x000000, 0x000000, 0xFFFFFF, 0xBEF2FF, 0xB8B8F8, 0xD8B8F8, 0xFFB6FF, 0xFFC3FF, 0xFFD1C7,
    0xFFDA9A, 0xF8ED88, 0xDDFF83, 0xB8F8B8, 0xACF8F5, 0xB0FFFF, 0xF8D8F8, 0x000000, 0x000000]


def rgb(i):
    c = NES[i & 0x3F]
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def decode(t):
    px = []
    for y in range(8):
        row = []
        for x in range(8):
            b0 = (t[y] >> (7 - x)) & 1
            b1 = (t[y + 8] >> (7 - x)) & 1
            row.append(b0 | (b1 << 1))
        px.append(row)
    return px


def draw_tile(img, t, ox, oy, pal, transparent=False):
    px = decode(t)
    for y in range(8):
        for x in range(8):
            v = px[y][x]
            if transparent and v == 0:
                continue
            img.putpixel((ox + x, oy + y), rgb(pal[v]))


def main():
    chrdata, defines, mt, boss = build.build_chr()
    bg = [chrdata[i * 16:(i + 1) * 16] for i in range(256)]
    sp = [chrdata[4096 + i * 16:4096 + (i + 1) * 16] for i in range(256)]
    sets = build.PALSETS
    W = 17 * 34
    img = Image.new("RGB", (W, 700), (40, 40, 40))
    y = 2
    for name, bgp, veil in sets:
        x = 2
        for (ch, mname, idxs, pal, f) in mt:
            p = bgp[pal * 4:pal * 4 + 4]
            for k, (dx, dy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
                draw_tile(img, bg[idxs[k]], x + dx, y + dy, p)
            x += 17
        # veil versions of palette-3 tiles
        for (ch, mname, idxs, pal, f) in mt:
            if pal == 3:
                for k, (dx, dy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
                    draw_tile(img, bg[idxs[k]], x + dx, y + dy, veil)
                x += 17
        y += 18
    # title logo + HUD tiles
    x = 2
    y += 4
    hud = [0x0F, 0x16, 0x21, 0x30]
    for i in list(range(1, 0x20)) + list(range(0xA4, 0x100)):
        draw_tile(img, bg[i], x, y, hud)
        x += 9
        if x > W - 10:
            x = 2
            y += 9
    y += 14
    # sprites
    spr_pals = build.SPRITE_PALS
    order = sorted((v, k) for k, v in defines.items() if k.startswith('SPR_'))
    x = 2
    for i, (start, name) in enumerate(order):
        end = order[i + 1][0] if i + 1 < len(order) else start + 4
        pal = spr_pals.get(name, [0x0F, 0x16, 0x27, 0x38])
        n = end - start
        bgc = Image.new('RGB', (34, 34), (0, 120, 0) if 'P_' in name else (60, 50, 40))
        img.paste(bgc, (x, y))
        if n >= 16 and 'BOSS' in name:
            for k in range(16):
                draw_tile(img, sp[start + k], x + (k % 4) * 8, y + (k // 4) * 8, pal, True)
        elif n == 4:
            for k, (dx, dy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
                draw_tile(img, sp[start + k], x + dx, y + dy, pal, True)
        elif n == 2:
            draw_tile(img, sp[start], x, y, pal, True)
            draw_tile(img, sp[start + 1], x + (0 if 'STAFF_V' in name else 8), y + (8 if 'STAFF_V' in name else 0), pal, True)
        else:
            draw_tile(img, sp[start], x, y, pal, True)
        x += 36
        if x > W - 36:
            x = 2
            y += 36
    # bosses
    x = 2
    y += 40
    bp = build.BOSS_PALS
    for b in range(len(bp)):
        for fr in range(2):
            base = (b * 2 + fr) * 16
            img.paste(Image.new('RGB', (34, 34), (60, 50, 40)), (x, y))
            for k in range(16):
                t = boss[(base + k) * 16:(base + k + 1) * 16]
                draw_tile(img, t, x + (k % 4) * 8, y + (k // 4) * 8, bp[b], True)
            x += 36
    img = img.crop((0, 0, W, y + 40))
    img = img.resize((img.width * 3, img.height * 3), Image.NEAREST)
    img.save(os.path.join(ROOT, 'build', 'preview.png'))
    print('wrote build/preview.png')


if __name__ == '__main__':
    main()

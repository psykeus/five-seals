#!/usr/bin/env python3
"""Asset builder for THE FIVE SEALS.

Generates:
  build/chr.bin       8 KB of tile data (uploaded to CHR-RAM at boot)
  src/gen_data.c/.h   metatiles, screens, messages, palettes
  src/gen_sound.s     note table, music, sound effects
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import art  # noqa: E402

# ----------------------------------------------------------------- helpers

def parse_art(rows, w=None, h=None):
    out = []
    for r in rows:
        line = []
        for ch in r:
            if ch in '.0 ':
                line.append(0)
            elif ch in '123':
                line.append(int(ch))
            elif ch == '#':
                line.append(3)
            else:
                raise ValueError('bad pixel %r in %r' % (ch, r))
        out.append(line)
    if w is not None:
        for r in out:
            assert len(r) == w, ('row width', len(r), w, rows)
    if h is not None:
        assert len(out) == h, ('height', len(out), h)
    return out


def tile_bytes(px, ox=0, oy=0):
    """Encode the 8x8 region at (ox,oy) of a pixel grid to 16 NES bytes."""
    p0, p1 = [], []
    for y in range(8):
        b0 = b1 = 0
        for x in range(8):
            v = px[oy + y][ox + x]
            b0 |= (v & 1) << (7 - x)
            b1 |= ((v >> 1) & 1) << (7 - x)
        p0.append(b0)
        p1.append(b1)
    return bytes(p0 + p1)


def split16(px):
    """16x16 -> [TL, TR, BL, BR] tile byte strings."""
    return [tile_bytes(px, 0, 0), tile_bytes(px, 8, 0), tile_bytes(px, 0, 8), tile_bytes(px, 8, 8)]


def blank(w, h):
    return [[0] * w for _ in range(h)]


def masked(base, mask, from_color, to_color=3):
    m = parse_art(mask, 16, 16)
    out = [row[:] for row in base]
    for y in range(16):
        for x in range(16):
            if m[y][x] and out[y][x] == from_color:
                out[y][x] = to_color
    return out

# ----------------------------------------------------------------- procedural sprites

def fill_tri(c, pts, col):
    (x0, y0), (x1, y1), (x2, y2) = pts
    h, w = len(c), len(c[0])
    for y in range(h):
        for x in range(w):
            px, py = x + 0.5, y + 0.5
            d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1)
            d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2)
            d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)
            neg = d1 < 0 or d2 < 0 or d3 < 0
            pos = d1 > 0 or d2 > 0 or d3 > 0
            if not (neg and pos):
                c[y][x] = col


def shrink(pts, f):
    cx = sum(p[0] for p in pts) / 3
    cy = sum(p[1] for p in pts) / 3
    return [(cx + (x - cx) * f, cy + (y - cy) * f) for x, y in pts]


def fill_ell(c, cx, cy, rx, ry, col):
    for y in range(len(c)):
        for x in range(len(c[0])):
            dx = (x + 0.5 - cx) / rx
            dy = (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                c[y][x] = col


def ring(c, cx, cy, r0, r1, col, gaps=0):
    for y in range(len(c)):
        for x in range(len(c[0])):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if r0 <= d <= r1:
                if gaps and ((x * 3 + y * 5) % gaps == 0):
                    continue
                c[y][x] = col


def boss_frame(up):
    c = blank(32, 32)
    for side in (0, 1):
        if up:
            pts = [(15, 15), (1, 1), (4, 24)]
        else:
            pts = [(15, 17), (1, 30), (5, 9)]
        if side:
            pts = [(31 - x, y) for x, y in pts]
        fill_tri(c, pts, 3)
        fill_tri(c, shrink(pts, 0.78), 1)
    fill_ell(c, 16, 17, 8.2, 12.2, 3)
    fill_ell(c, 16, 17, 7, 11, 2)
    # hood shadow
    fill_ell(c, 16, 12, 5, 4, 1)
    # eyes
    for ex in (13, 18):
        c[12][ex] = 3
        c[12][ex + 1] = 3
        c[13][ex] = 3
        c[13][ex + 1] = 3
    # swirls on lower body
    for x in range(10, 22):
        y = 20 + int(1.5 * math.sin((x + (0 if up else 3)) * 0.8))
        c[y][x] = 1
        y = 24 + int(1.5 * math.sin((x + (3 if up else 0)) * 0.8))
        c[y][x] = 1
    return c


def puff_frame(n):
    c = blank(16, 16)
    if n == 0:
        ring(c, 8, 8, 2.5, 5, 2)
        ring(c, 8, 8, 0, 2.5, 1)
    else:
        ring(c, 8, 8, 4.5, 7.5, 1, gaps=4)
        ring(c, 8, 8, 5.5, 6.5, 2, gaps=3)
    return c


def sigil():
    c = blank(16, 16)
    ring(c, 8, 8, 6, 7.6, 3)
    ring(c, 8, 8, 4.6, 6, 1)
    fill_ell(c, 8, 9, 2.6, 3.4, 2)
    fill_ell(c, 8, 10, 1.4, 1.8, 3)
    c[3][8] = 2
    c[4][7] = 2
    c[4][8] = 2
    return c


def seal():
    c = blank(16, 16)
    fill_ell(c, 8, 8, 7.6, 7.6, 3)
    fill_ell(c, 8, 8, 6.4, 6.4, 1)
    ring(c, 8, 8, 3.8, 4.8, 2)
    fill_ell(c, 8, 8, 2, 2, 3)
    for i in range(5):
        a = -math.pi / 2 + i * 2 * math.pi / 5
        x = int(8 + 5.6 * math.cos(a))
        y = int(8 + 5.6 * math.sin(a))
        c[y][x] = 2
    return c


def rot_ccw(px):
    h, w = len(px), len(px[0])
    return [[px[c][w - 1 - r] for c in range(h)] for r in range(w)]

# ----------------------------------------------------------------- CHR building

def build_chr():
    bg = [bytes(16)] * 256
    used = [False] * 256
    used[0] = True
    defines = {}

    def put_bg(idx, data, name=None):
        bg[idx] = data
        used[idx] = True
        if name:
            defines[name] = idx

    # HUD
    put_bg(0x01, tile_bytes(parse_art(art.HUD_HEART_FULL, 8, 8)), 'T_HEART_FULL')
    put_bg(0x02, tile_bytes(parse_art(art.HUD_HEART_HALF, 8, 8)), 'T_HEART_HALF')
    put_bg(0x03, tile_bytes(parse_art(art.HUD_HEART_EMPTY, 8, 8)), 'T_HEART_EMPTY')
    for k in range(9):
        px = blank(8, 8)
        for x in range(8):
            px[0][x] = 3
            px[7][x] = 3
            for y in range(1, 7):
                px[y][x] = 2 if x < k else 0
        put_bg(0x04 + k, tile_bytes(px))
    defines['T_MANA0'] = 0x04
    for i, t in enumerate(split16(parse_art(art.ICON_FIRE, 16, 16))):
        put_bg(0x0D + i, t)
    defines['T_ICON_FIRE'] = 0x0D
    for i, t in enumerate(split16(parse_art(art.ICON_VEIL, 16, 16))):
        put_bg(0x11 + i, t)
    defines['T_ICON_VEIL'] = 0x11

    hl = blank(8, 8)
    for x in range(8):
        hl[3][x] = 2
        hl[5][x] = 3
    put_bg(0x15, tile_bytes(hl), 'T_HUD_LINE')

    # font at ASCII codes
    for ch, rows in art.FONT.items():
        px = blank(8, 8)
        g = parse_art(rows, 5, 7)
        for y in range(7):
            for x in range(5):
                if g[y][x]:
                    px[y][x + 1] = 3
        put_bg(ord(ch), tile_bytes(px))

    # metatiles
    rock = parse_art(art.ROCK, 16, 16)
    water = parse_art(art.WATER, 16, 16)
    twall = parse_art(art.TWALL, 16, 16)
    bridge = [[1 if v == 3 else v for v in row] for row in water]
    pm = parse_art(art.PLANK_MASK, 16, 16)
    for y in range(16):
        for x in range(16):
            if pm[y][x] and water[y][x] == 3:
                bridge[y][x] = 3

    MF = dict(SOLID=1, VEIL=2, BURN=4, BRAZ=8, DOOR=16, WARP=32, SIGN=64, WATER=128)
    meta = [
        # char, name, pixels, palette, flags
        ('.', 'GRASS', parse_art(art.GRASS, 16, 16), 1, []),
        (',', 'PATH', parse_art(art.PATH, 16, 16), 1, []),
        ('T', 'TREE', parse_art(art.TREE, 16, 16), 1, ['SOLID']),
        ('#', 'BRAMBLE', parse_art(art.BRAMBLE, 16, 16), 1, ['SOLID', 'BURN']),
        ('R', 'ROCK', rock, 2, ['SOLID']),
        ('~', 'WATER', water, 2, ['SOLID', 'WATER']),
        ('C', 'CAVE', parse_art(art.CAVE, 16, 16), 2, ['WARP']),
        ('V', 'VEILROCK', masked(rock, art.RUNE_MASK, 1), 3, ['SOLID', 'VEIL']),
        ('=', 'VEILBRIDGE', bridge, 3, ['SOLID', 'VEIL', 'WATER']),
        ('A', 'STATUE', parse_art(art.STATUE, 16, 16), 0, ['SOLID', 'SIGN']),
        ('F', 'FLOOR', parse_art(art.FLOOR, 16, 16), 1, []),
        ('W', 'TWALL', twall, 1, ['SOLID']),
        ('D', 'DOOR', parse_art(art.DOOR, 16, 16), 1, ['SOLID', 'DOOR']),
        ('O', 'DOOROPEN', parse_art(art.DOOROPEN, 16, 16), 1, []),
        ('B', 'BRAZIER', parse_art(art.BRAZIER, 16, 16), 1, ['SOLID', 'BRAZ']),
        ('L', 'BRAZLIT', parse_art(art.BRAZLIT, 16, 16), 1, ['SOLID']),
        ('S', 'STAIRS', parse_art(art.STAIRS, 16, 16), 1, ['WARP']),
        ('v', 'VEILBRICK', masked(twall, art.RUNE_MASK, 2), 3, ['SOLID', 'VEIL']),
        ('X', 'VOID', parse_art(art.VOID, 16, 16), 0, ['SOLID']),
        ('"', 'FLOWERS', parse_art(art.FLOWERS, 16, 16), 1, []),
    ]
    tilemap = {bytes(16): 0}
    nxt = 0x60
    mt_tiles = []
    for ch, name, px, pal, flags in meta:
        idxs = []
        for t in split16(px):
            if t not in tilemap:
                assert nxt < 256, 'out of BG tiles'
                tilemap[t] = nxt
                put_bg(nxt, t)
                nxt += 1
            idxs.append(tilemap[t])
        f = 0
        for fl in flags:
            f |= MF[fl]
        mt_tiles.append((ch, name, idxs, pal, f))
    print('BG tiles used: metatiles up to $%02X' % (nxt - 1))

    # big title logo: 2x font, upper half colour 3, lower half colour 2, drop shadow colour 1
    big = {}
    for ch in sorted(set(TITLE_TEXT) - {' '}):
        g = parse_art(art.FONT[ch], 5, 7)
        px = blank(16, 16)
        for y in range(7):
            for x in range(5):
                if g[y][x]:
                    for dy in range(2):
                        for dx in range(2):
                            sx, sy = 3 + x * 2 + dx + 1, 1 + y * 2 + dy + 1
                            if px[sy][sx] == 0:
                                px[sy][sx] = 1
        for y in range(7):
            for x in range(5):
                if g[y][x]:
                    for dy in range(2):
                        for dx in range(2):
                            sy = 1 + y * 2 + dy
                            px[sy][3 + x * 2 + dx] = 3 if sy < 8 else 2
        idxs = []
        for t in split16(px):
            if t not in tilemap:
                assert nxt < 256, 'out of BG tiles'
                tilemap[t] = nxt
                put_bg(nxt, t)
                nxt += 1
            idxs.append(tilemap[t])
        big[ch] = idxs
    top, bot = [], []
    for ch in TITLE_TEXT:
        if ch == ' ':
            top += [0, 0]
            bot += [0, 0]
        else:
            top += big[ch][0:2]
            bot += big[ch][2:4]
    defines['_title_top'] = top
    defines['_title_bot'] = bot
    print('BG tiles used incl. title logo: up to $%02X' % (nxt - 1))

    # sprites
    spr = []
    sdef = {}

    def add16(name, px):
        sdef[name] = len(spr)
        spr.extend(split16(px))

    def add8(name, px):
        sdef[name] = len(spr)
        spr.append(tile_bytes(px))

    down = parse_art(art.PLAYER_DOWN, 16, 16)
    downb = [r[:] for r in down]
    for y in (14, 15):
        downb[y] = down[y][::-1]
    up = parse_art(art.PLAYER_UP, 16, 16)
    upb = [r[:] for r in up]
    for y in (14, 15):
        upb[y] = up[y][::-1]
    right = parse_art(art.PLAYER_RIGHT, 16, 16)
    rightb = [r[:] for r in right]
    lb = parse_art(art.PLAYER_RIGHT_LEGS_B, 16)
    rightb[14], rightb[15] = lb[0], lb[1]
    add16('SPR_P_DOWN0', down)
    add16('SPR_P_DOWN1', downb)
    add16('SPR_P_UP0', up)
    add16('SPR_P_UP1', upb)
    add16('SPR_P_RIGHT0', right)
    add16('SPR_P_RIGHT1', rightb)
    sh = parse_art(art.STAFF_H, 16, 8)
    sdef['SPR_STAFF_H'] = len(spr)
    spr.append(tile_bytes(sh, 0, 0))
    spr.append(tile_bytes(sh, 8, 0))
    sv = rot_ccw(sh)  # 8 wide, 16 tall, gem on top
    sdef['SPR_STAFF_V'] = len(spr)
    spr.append(tile_bytes(sv, 0, 0))
    spr.append(tile_bytes(sv, 0, 8))
    add8('SPR_BOLT0', parse_art(art.BOLT_A, 8, 8))
    add8('SPR_BOLT1', parse_art(art.BOLT_B, 8, 8))
    add16('SPR_BLOB0', parse_art(art.BLOB_A, 16, 16))
    add16('SPR_BLOB1', parse_art(art.BLOB_B, 16, 16))
    add16('SPR_STALK0', parse_art(art.STALKER_A, 16, 16))
    add16('SPR_STALK1', parse_art(art.STALKER_B, 16, 16))
    add16('SPR_SHADE0', parse_art(art.SHADE_A, 16, 16))
    add16('SPR_SHADE1', parse_art(art.SHADE_B, 16, 16))
    for n, upw in (('SPR_BOSS0', True), ('SPR_BOSS1', False)):
        px = boss_frame(upw)
        sdef[n] = len(spr)
        for ty in range(4):
            for tx in range(4):
                spr.append(tile_bytes(px, tx * 8, ty * 8))
    add16('SPR_PUFF0', puff_frame(0))
    add16('SPR_PUFF1', puff_frame(1))
    add8('SPR_HEART', parse_art(art.HEART_PICK, 8, 8))
    add8('SPR_MANA', parse_art(art.MANA_PICK, 8, 8))
    g = parse_art(art.GUST_A, 8, 8)
    add8('SPR_GUST0', g)
    add8('SPR_GUST1', rot_ccw(g))
    add16('SPR_HEARTC', parse_art(art.HEART_CONTAINER, 16, 16))
    add16('SPR_TOME', parse_art(art.TOME, 16, 16))
    add16('SPR_SIGIL', sigil())
    add16('SPR_SEAL', seal())
    assert len(spr) <= 256
    print('Sprite tiles used: %d' % len(spr))
    spr += [bytes(16)] * (256 - len(spr))

    chrdata = b''.join(bg) + b''.join(spr)
    assert len(chrdata) == 8192
    defines.update(sdef)
    return chrdata, defines, mt_tiles

# ----------------------------------------------------------------- world parsing

TITLE_TEXT = 'FIVE SEALS'

SP0 = [0x0F, 0x13, 0x37, 0x0F]   # player (robe colour swapped per Art at runtime)
SP1 = [0x0F, 0x11, 0x31, 0x0F]   # blobs
SP2 = [0x0F, 0x10, 0x30, 0x02]   # stalkers, shades, boss, mana, gusts, puffs
SP3 = [0x0F, 0x16, 0x27, 0x38]   # fire, hearts, relics
SPRITE_PALS = {}
for _n in ['SPR_P_DOWN0', 'SPR_P_DOWN1', 'SPR_P_UP0', 'SPR_P_UP1', 'SPR_P_RIGHT0', 'SPR_P_RIGHT1', 'SPR_STAFF_H', 'SPR_STAFF_V']:
    SPRITE_PALS[_n] = SP0
for _n in ['SPR_BLOB0', 'SPR_BLOB1']:
    SPRITE_PALS[_n] = SP1
for _n in ['SPR_STALK0', 'SPR_STALK1', 'SPR_SHADE0', 'SPR_SHADE1', 'SPR_BOSS0', 'SPR_BOSS1', 'SPR_PUFF0', 'SPR_PUFF1',
           'SPR_MANA', 'SPR_GUST0', 'SPR_GUST1']:
    SPRITE_PALS[_n] = SP2

AREAS = {'ow': (0, 4, 4), 'temple': (1, 3, 3), 'cave': (2, 1, 1), 'title': (3, 1, 1)}
AREA_BASE = {'ow': 0, 'temple': 16, 'cave': 25, 'title': 26}
ENEMY = {'blob': 1, 'stalker': 2, 'shade': 3, 'boss': 4}
ITEM = {'tome': 1, 'sigil': 2, 'heart': 3, 'seal': 4}
COND = {'none': 0, 'clear': 1, 'braziers': 2, 'flag': 3}

PALSETS = [
    ('OW_ROCK', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x0A, 0x1A, 0x37, 0x0F, 0x00, 0x10, 0x21, 0x0F, 0x00, 0x10, 0x00],
     [0x0F, 0x03, 0x13, 0x34]),
    ('OW_WATER', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x0A, 0x1A, 0x37, 0x0F, 0x00, 0x10, 0x21, 0x0F, 0x21, 0x10, 0x21],
     [0x0F, 0x01, 0x11, 0x37]),
    ('TEMPLE_WALL', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x07, 0x17, 0x27, 0x0F, 0x06, 0x27, 0x16, 0x0F, 0x07, 0x17, 0x17],
     [0x0F, 0x03, 0x13, 0x34]),
    ('TEMPLE_LAVA', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x07, 0x17, 0x27, 0x0F, 0x06, 0x27, 0x16, 0x0F, 0x16, 0x27, 0x16],
     [0x0F, 0x06, 0x16, 0x37]),
    ('TITLE', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x07, 0x17, 0x27, 0x0F, 0x06, 0x27, 0x38, 0x0F, 0x00, 0x10, 0x00],
     [0x0F, 0x03, 0x13, 0x34]),
    ('CAVE', [0x0F, 0x16, 0x21, 0x30, 0x0F, 0x08, 0x18, 0x28, 0x0F, 0x00, 0x10, 0x21, 0x0F, 0x00, 0x10, 0x00],
     [0x0F, 0x03, 0x13, 0x34]),
]
PALIDX = {n: i for i, (n, _, _) in enumerate(PALSETS)}


def scr_id(area, gx, gy):
    _, w, h = AREAS[area]
    assert 0 <= gx < w and 0 <= gy < h, (area, gx, gy)
    return AREA_BASE[area] + gy * w + gx


def parse_world(path, mt_chars):
    msgs = {}
    msg_order = []
    screens = {}
    lines = open(path).read().split('\n')
    i = 0
    while i < len(lines):
        ln = lines[i].rstrip()
        i += 1
        if not ln or ln.startswith('#'):
            continue
        if ln.startswith('@msg'):
            name = ln.split()[1]
            body = []
            while lines[i].strip() != '@end':
                t = lines[i].rstrip().upper()
                assert len(t) <= 28, 'message line too long: %r' % t
                for ch in t:
                    assert ch == ' ' or ch in art.FONT, 'no glyph for %r in %r' % (ch, t)
                body.append(t)
                i += 1
            i += 1
            assert len(body) <= 4, name
            msgs[name] = body
            msg_order.append(name)
            continue
        if ln.startswith('@screen'):
            _, area, gx, gy = ln.split()
            sid = scr_id(area, int(gx), int(gy))
            s = dict(area=area, gx=int(gx), gy=int(gy), pal='OW_ROCK', msg=None, onenter=False,
                     shrine=None, enemies=[], item=None, warp=None, cond=('none', 0), solvemsg=None, map=None)
            while True:
                ln = lines[i].strip()
                i += 1
                if ln == 'map':
                    rows = [lines[i + k].rstrip() for k in range(11)]
                    i += 11
                    for r in rows:
                        assert len(r) == 16, 'bad map row in %s %s,%s: %r' % (area, gx, gy, r)
                        for ch in r:
                            assert ch in mt_chars, 'unknown map char %r' % ch
                    s['map'] = rows
                    break
                p = ln.split()
                if p[0] == 'pal':
                    s['pal'] = p[1]
                elif p[0] == 'msg':
                    s['msg'] = p[1]
                    s['onenter'] = len(p) > 2 and p[2] == 'onenter'
                elif p[0] == 'shrine':
                    s['shrine'] = (int(p[1]), int(p[2]))
                elif p[0] == 'enemy':
                    s['enemies'].append((p[1], int(p[2]), int(p[3])))
                elif p[0] == 'item':
                    s['item'] = (p[1], int(p[2]), int(p[3]))
                elif p[0] == 'warp':
                    s['warp'] = (scr_id(p[1], int(p[2]), int(p[3])), int(p[4]), int(p[5]))
                elif p[0] == 'cond':
                    if p[1] == 'flag':
                        s['cond'] = ('flag', scr_id(p[2], int(p[3]), int(p[4])))
                    else:
                        s['cond'] = (p[1], 0)
                elif p[0] == 'solvemsg':
                    s['solvemsg'] = p[1]
                else:
                    raise ValueError('unknown screen keyword %r' % ln)
            assert sid not in screens, 'duplicate screen'
            screens[sid] = s
            continue
        raise ValueError('unexpected line %r' % ln)
    return msgs, msg_order, screens


def validate(screens, mt_by_char):
    def passable(ch):
        n, f = mt_by_char[ch]
        # doors and veil tiles can be passable in some state
        return (f & 1) == 0 or (f & (2 | 16)) != 0

    errors = []
    for sid, s in screens.items():
        if s['area'] == 'title':
            continue
        _, w, h = AREAS[s['area']]
        m = s['map']
        # neighbours
        if s['gx'] + 1 < w:
            nid = scr_id(s['area'], s['gx'] + 1, s['gy'])
            if nid in screens:
                o = screens[nid]['map']
                for y in range(11):
                    if passable(m[y][15]) != passable(o[y][0]):
                        errors.append('edge mismatch %d<->%d row %d' % (sid, nid, y))
        if s['gy'] + 1 < h:
            nid = scr_id(s['area'], s['gx'], s['gy'] + 1)
            if nid in screens:
                o = screens[nid]['map']
                for x in range(16):
                    if passable(m[10][x]) != passable(o[0][x]):
                        errors.append('edge mismatch %d<->%d col %d' % (sid, nid, x))
        for (t, x, y) in s['enemies']:
            if t in ('shade', 'boss'):
                continue
            if mt_by_char[m[y][x]][1] & 1:
                errors.append('enemy on solid tile in screen %d at %d,%d' % (sid, x, y))
        if s['item']:
            _, x, y = s['item']
            if mt_by_char[m[y][x]][1] & 1:
                errors.append('item on solid tile in screen %d' % sid)
        if s['warp']:
            t, x, y = s['warp']
            tm = screens[t]['map']
            if mt_by_char[tm[y][x]][1] & (1 | 32):
                errors.append('warp target from %d lands on bad tile %r' % (sid, tm[y][x]))
        if s['shrine']:
            x, y = s['shrine']
            if mt_by_char[m[y][x]][1] & 1:
                errors.append('shrine respawn on solid tile in screen %d' % sid)
    if errors:
        for e in errors:
            print('ERROR:', e)
        sys.exit(1)

# ----------------------------------------------------------------- sound

NOTE_NAMES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}
CPU = 1789773.0


def midi_of(name):
    oct_ = int(name[-1])
    return 12 * (oct_ + 1) + NOTE_NAMES[name[:-1]]


def note_idx(midi):
    i = midi - 35  # C2 (36) -> 1
    assert 1 <= i <= 63, midi
    return i


def period(i):
    midi = i + 35
    f = 440.0 * 2 ** ((midi - 69) / 12.0)
    p = int(round(CPU / (16 * f))) - 1
    return min(p, 0x7FF)


def song_stream(text, tri=False):
    out = []
    cur = None
    for tok in text.split():
        n, d = tok.split(':')
        d = int(d)
        while d > 0:
            dd = min(d, 127)
            if dd != cur:
                out.append(0x80 | dd)
                cur = dd
            if n == 'R':
                out.append(0)
            else:
                m = midi_of(n)
                if tri:
                    m += 12  # triangle sounds an octave below the pulse period
                out.append(note_idx(m))
            d -= dd
            n = 'R' if n == 'R' else n  # long notes re-strike; fine for our tunes
    out.append(0xFF)
    return out


SONGS = [
    ('SONG_OVERWORLD',
     'A4:24 E5:24 D5:12 C5:12 B4:24 C5:12 D5:12 E5:24 A4:48 '
     'F5:24 E5:12 D5:12 C5:24 D5:24 E5:36 D5:12 B4:48 '
     'A4:24 E5:24 D5:12 C5:12 B4:24 C5:12 D5:12 E5:24 G5:48 '
     'F5:24 E5:12 D5:12 C5:24 B4:24 A4:72 R:24',
     'A2:24 E3:24 A2:24 E3:24 A2:24 E3:24 A2:24 E3:24 '
     'F2:24 C3:24 F2:24 C3:24 E2:24 B2:24 E2:24 B2:24 '
     'A2:24 E3:24 A2:24 E3:24 C3:24 G3:24 C3:24 G3:24 '
     'F2:24 C3:24 G2:24 D3:24 A2:24 E3:24 A2:48'),
    ('SONG_TEMPLE',
     'D4:36 E4:12 F4:24 A4:24 G4:36 F4:12 E4:48 '
     'D4:36 E4:12 F4:24 C5:24 A#4:36 A4:12 A4:48 '
     'D5:24 C5:24 A#4:24 A4:24 G4:24 F4:24 E4:48 '
     'F4:24 E4:24 D4:24 C#4:24 D4:72 R:24',
     'D2:24 A2:24 D2:24 A2:24 C2:24 G2:24 C2:24 G2:24 '
     'A#1:24 F2:24 A#1:24 F2:24 A1:24 E2:24 A1:24 E2:24 '
     'A#1:24 F2:24 A#1:24 F2:24 C2:24 G2:24 C2:24 G2:24 '
     'A1:24 E2:24 A1:24 E2:24 D2:24 A2:24 D2:48'),
    ('SONG_BOSS',
     'E4:8 E4:8 G4:8 E4:8 A#4:8 A4:8 G4:8 F#4:8 E4:8 E4:8 G4:8 E4:8 B4:16 A4:16 '
     'E5:8 D5:8 B4:8 A4:8 G4:8 A4:8 B4:16 E4:8 F#4:8 G4:8 A4:8 B4:16 E4:16',
     'E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 E2:8 E3:8 '
     'C2:8 C3:8 C2:8 C3:8 C2:8 C3:8 C2:8 C3:8 B1:8 B2:8 B1:8 B2:8 B1:8 B2:8 B1:8 B2:8'),
    ('SONG_ENDING',
     'A4:24 C5:24 E5:24 A5:24 G5:36 E5:12 D5:48 '
     'F5:24 E5:24 D5:24 C5:24 B4:36 C5:12 D5:48 '
     'C5:24 E5:24 A5:48 G5:24 E5:24 D5:48 '
     'C5:24 B4:24 G#4:24 B4:24 A4:96',
     'A2:48 E3:48 C3:48 G2:48 F2:48 C3:48 G2:48 E2:48 '
     'A2:48 E3:48 C3:48 G2:48 F2:48 E2:48 A2:96'),
]


def sfx_pulse(frames):
    out = []
    for ctrl, per in frames:
        out += [ctrl, per & 0xFF, (per >> 8) | 0x08]
    out.append(0)
    return out


def sfx_noise(frames):
    out = []
    for ctrl, per in frames:
        out += [ctrl, per]
    out.append(0)
    return out


def lin(a, b, n):
    return [int(round(a + (b - a) * k / max(1, n - 1))) for k in range(n)]


SFX = [
    ('SFX_SWING', 1, sfx_noise([(0x30 | v, p) for v, p in zip([9, 8, 6, 4, 2], [3, 4, 5, 6, 7])])),
    ('SFX_FIRE', 1, sfx_noise([(0x30 | v, p) for v, p in zip(lin(12, 1, 12), lin(5, 11, 12))])),
    ('SFX_HIT', 0, sfx_pulse([(0xB0 | v, p) for v, p in zip(lin(12, 4, 7), lin(180, 420, 7))])),
    ('SFX_HURT', 0, sfx_pulse([(0x70 | v, p) for v, p in zip(lin(13, 4, 14), lin(280, 760, 14))])),
    ('SFX_PICKUP', 0, sfx_pulse([(0xB0 | 10, period(note_idx(midi_of(n)))) for n in
                                 ['E6', 'E6', 'E6', 'G6', 'G6', 'G6', 'B6', 'B6', 'B6', 'B6']])),
    ('SFX_VEILON', 0, sfx_pulse([(0x30 | v, p + (6 if k % 2 else -6)) for k, (v, p) in
                                 enumerate(zip(lin(6, 12, 18), lin(640, 180, 18)))])),
    ('SFX_VEILOFF', 0, sfx_pulse([(0x30 | v, p) for v, p in zip(lin(10, 3, 10), lin(180, 640, 10))])),
    ('SFX_DOOR', 1, sfx_noise([(0x30 | v, p) for v, p in zip(lin(13, 1, 26), [14, 15] * 13)])),
    ('SFX_BURN', 1, sfx_noise([(0x30 | v, p) for v, p in zip(lin(11, 1, 14), [3, 9] * 7)])),
    ('SFX_KILL', 1, sfx_noise([(0x30 | v, p) for v, p in zip(lin(14, 1, 16), lin(8, 14, 16))])),
    ('SFX_ERROR', 0, sfx_pulse([(0x30 | 9, 1100)] * 4 + [(0x30, 1100)] * 2 + [(0x30 | 9, 1100)] * 4)),
    ('SFX_ITEM', 0, sfx_pulse([(0xB0 | 11, period(note_idx(midi_of(n)))) for n in
                               ['C5'] * 5 + ['E5'] * 5 + ['G5'] * 5] +
                              [(0xB0 | v, period(note_idx(midi_of('C6')))) for v in lin(12, 2, 24)])),
    ('SFX_BLIP', 0, sfx_pulse([(0xB0 | 6, 160), (0xB0 | 4, 160)])),
    ('SFX_TINK', 0, sfx_pulse([(0x30 | v, 70) for v in lin(11, 3, 6)])),
    ('SFX_SELECT', 0, sfx_pulse([(0xB0 | 8, 220), (0xB0 | 8, 220), (0xB0 | 7, 160), (0xB0 | 5, 160)])),
]


def build_sound(path):
    L = ['; generated by tools/build.py - do not edit', '.export note_lo, note_hi',
         '.export song_p_lo, song_p_hi, song_t_lo, song_t_hi',
         '.export sfx_lo, sfx_hi, sfx_chan', '.segment "RODATA7"', '']
    lo = [0] + [period(i) & 0xFF for i in range(1, 64)]
    hi = [0] + [(period(i) >> 8) | 0x08 for i in range(1, 64)]
    L.append('note_lo: .byte ' + ','.join('$%02X' % v for v in lo))
    L.append('note_hi: .byte ' + ','.join('$%02X' % v for v in hi))
    for name, p, t in SONGS:
        L.append('%s_p: .byte %s' % (name.lower(), ','.join('$%02X' % v for v in song_stream(p))))
        L.append('%s_t: .byte %s' % (name.lower(), ','.join('$%02X' % v for v in song_stream(t, True))))
    L.append('song_p_lo: .byte ' + ','.join('<%s_p' % n.lower() for n, _, _ in SONGS))
    L.append('song_p_hi: .byte ' + ','.join('>%s_p' % n.lower() for n, _, _ in SONGS))
    L.append('song_t_lo: .byte ' + ','.join('<%s_t' % n.lower() for n, _, _ in SONGS))
    L.append('song_t_hi: .byte ' + ','.join('>%s_t' % n.lower() for n, _, _ in SONGS))
    for name, ch, data in SFX:
        L.append('%s: .byte %s' % (name.lower(), ','.join('$%02X' % v for v in data)))
    L.append('sfx_lo: .byte ' + ','.join('<%s' % n.lower() for n, _, _ in SFX))
    L.append('sfx_hi: .byte ' + ','.join('>%s' % n.lower() for n, _, _ in SFX))
    L.append('sfx_chan: .byte ' + ','.join(str(ch) for _, ch, _ in SFX))
    open(path, 'w').write('\n'.join(L) + '\n')

# ----------------------------------------------------------------- C output

def carr(name, vals, ctype='unsigned char'):
    body = ','.join(str(v) for v in vals)
    return 'const %s %s[%d] = {%s};' % (ctype, name, len(vals), body)


def main():
    chrdata, defines, mt = build_chr()
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    open(os.path.join(ROOT, 'build', 'chr.bin'), 'wb').write(chrdata)

    mt_index = {ch: i for i, (ch, _, _, _, _) in enumerate(mt)}
    mt_by_char = {ch: (name, f) for ch, name, _, _, f in mt}
    msgs, msg_order, screens = parse_world(os.path.join(ROOT, 'assets', 'world.txt'), mt_index)
    validate(screens, mt_by_char)
    msg_id = {n: i + 1 for i, n in enumerate(msg_order)}
    nscr = max(screens) + 1
    assert sorted(screens) == list(range(nscr)), 'screen ids must be contiguous'

    C = ['/* generated by tools/build.py - do not edit */', '#include "gen_data.h"', '']
    C.append(carr('mt_tl', [t[2][0] for t in mt]))
    C.append(carr('mt_tr', [t[2][1] for t in mt]))
    C.append(carr('mt_bl', [t[2][2] for t in mt]))
    C.append(carr('mt_br', [t[2][3] for t in mt]))
    C.append(carr('mt_pal', [t[3] for t in mt]))
    C.append(carr('mt_flags', [t[4] for t in mt]))
    for sid in range(nscr):
        s = screens[sid]
        vals = [mt_index[ch] for row in s['map'] for ch in row]
        C.append(carr('scr_map_%d' % sid, vals))
        ev = []
        for t, x, y in s['enemies']:
            ev += [ENEMY[t], x * 16, y * 16]
        ev.append(0)
        C.append(carr('scr_en_%d' % sid, ev))
    C.append('const unsigned char* const scr_maps[%d] = {%s};' % (nscr, ','.join('scr_map_%d' % i for i in range(nscr))))
    C.append('const unsigned char* const scr_enemies[%d] = {%s};' % (nscr, ','.join('scr_en_%d' % i for i in range(nscr))))

    def nb(s, dx, dy):
        if s['area'] == 'title':
            return 255
        _, w, h = AREAS[s['area']]
        x, y = s['gx'] + dx, s['gy'] + dy
        if 0 <= x < w and 0 <= y < h:
            n = scr_id(s['area'], x, y)
            return n if n in screens else 255
        return 255

    S = [screens[i] for i in range(nscr)]
    C.append(carr('scr_pal', [PALIDX[s['pal']] for s in S]))
    C.append(carr('scr_area', [AREAS[s['area']][0] for s in S]))
    C.append(carr('scr_up', [nb(s, 0, -1) for s in S]))
    C.append(carr('scr_down', [nb(s, 0, 1) for s in S]))
    C.append(carr('scr_left', [nb(s, -1, 0) for s in S]))
    C.append(carr('scr_right', [nb(s, 1, 0) for s in S]))
    C.append(carr('scr_cond', [COND[s['cond'][0]] for s in S]))
    C.append(carr('scr_condarg', [s['cond'][1] for s in S]))
    C.append(carr('scr_warp_s', [s['warp'][0] if s['warp'] else 255 for s in S]))
    C.append(carr('scr_warp_x', [s['warp'][1] * 16 if s['warp'] else 0 for s in S]))
    C.append(carr('scr_warp_y', [s['warp'][2] * 16 if s['warp'] else 0 for s in S]))
    C.append(carr('scr_msg', [msg_id[s['msg']] if s['msg'] else 0 for s in S]))
    C.append(carr('scr_mflags', [(1 if s['onenter'] else 0) | (2 if s['shrine'] else 0) for s in S]))
    C.append(carr('scr_shrine_x', [s['shrine'][0] * 16 if s['shrine'] else 0 for s in S]))
    C.append(carr('scr_shrine_y', [s['shrine'][1] * 16 if s['shrine'] else 0 for s in S]))
    C.append(carr('scr_item', [ITEM[s['item'][0]] if s['item'] else 0 for s in S]))
    C.append(carr('scr_item_x', [s['item'][1] * 16 if s['item'] else 0 for s in S]))
    C.append(carr('scr_item_y', [s['item'][2] * 16 if s['item'] else 0 for s in S]))
    C.append(carr('scr_solvemsg', [msg_id[s['solvemsg']] if s['solvemsg'] else 0 for s in S]))

    C.append('static const char msg_none[] = "";')
    for n in msg_order:
        txt = '\\n'.join(msgs[n])
        C.append('static const char msg_%s[] = "%s";' % (n.lower(), txt))
    C.append('const char* const msg_text[%d] = {msg_none,%s};' % (
        len(msg_order) + 1, ','.join('msg_%s' % n.lower() for n in msg_order)))
    pal = []
    for _, bg, veil in PALSETS:
        pal += bg + veil
    C.append(carr('palsets', pal))
    C.append(carr('title_top', defines['_title_top']))
    C.append(carr('title_bot', defines['_title_bot']))
    C.append(carr('spr_pals', SP0 + SP1 + SP2 + SP3))
    open(os.path.join(ROOT, 'src', 'gen_data.c'), 'w').write('\n'.join(C) + '\n')

    H = ['/* generated by tools/build.py - do not edit */', '#ifndef GEN_DATA_H', '#define GEN_DATA_H', '']
    for i, (ch, name, _, _, _) in enumerate(mt):
        H.append('#define MT_%s %d' % (name, i))
    H.append('#define NUM_MT %d' % len(mt))
    for k, v in sorted(defines.items()):
        if isinstance(v, int):
            H.append('#define %s %d' % (k, v))
    H.append('extern const unsigned char title_top[%d];' % len(defines['_title_top']))
    H.append('extern const unsigned char title_bot[%d];' % len(defines['_title_bot']))
    H.append('#define TITLE_W %d' % len(defines['_title_top']))
    H.append('extern const unsigned char spr_pals[16];')
    H.append('#define NUM_SCREENS %d' % nscr)
    start = scr_id('ow', 1, 2)
    H.append('#define SCR_START %d' % start)
    H.append('#define START_X %d' % (screens[start]['shrine'][0] * 16))
    H.append('#define START_Y %d' % (screens[start]['shrine'][1] * 16))
    H.append('#define SCR_TITLE %d' % AREA_BASE['title'])
    for n in msg_order:
        H.append('#define MSG_%s %d' % (n, msg_id[n]))
    for i, (n, _, _) in enumerate(SONGS):
        H.append('#define %s %d' % (n, i))
    for i, (n, _, _) in enumerate(SFX):
        H.append('#define %s %d' % (n, i))
    for i, (n, _, _) in enumerate(PALSETS):
        H.append('#define PAL_%s %d' % (n, i))
    for nm in ['mt_tl', 'mt_tr', 'mt_bl', 'mt_br', 'mt_pal', 'mt_flags', 'scr_pal', 'scr_area', 'scr_up',
               'scr_down', 'scr_left', 'scr_right', 'scr_cond', 'scr_condarg', 'scr_warp_s', 'scr_warp_x',
               'scr_warp_y', 'scr_msg', 'scr_mflags', 'scr_shrine_x', 'scr_shrine_y', 'scr_item', 'scr_item_x',
               'scr_item_y', 'scr_solvemsg', 'palsets']:
        H.append('extern const unsigned char %s[];' % nm)
    H.append('extern const unsigned char* const scr_maps[];')
    H.append('extern const unsigned char* const scr_enemies[];')
    H.append('extern const char* const msg_text[];')
    H.append('#endif')
    open(os.path.join(ROOT, 'src', 'gen_data.h'), 'w').write('\n'.join(H) + '\n')

    build_sound(os.path.join(ROOT, 'src', 'gen_sound.s'))
    print('Screens: %d  Messages: %d' % (nscr, len(msg_order)))


if __name__ == '__main__':
    main()

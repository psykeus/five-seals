#!/usr/bin/env python3
"""Reachability check: with a given set of Arts, which screens/items/warps can the player reach?
Doors ('D') are treated as openable (their puzzles are checked by hand / headless tests)."""
import os
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build  # noqa: E402


def passable(ch, arts):
    if ch in '.,"FOCSE':
        return True
    if ch in 'Vv=':
        return 'veil' in arts
    if ch == '#':
        return 'fire' in arts
    if ch in 'Kk':
        return 'earth' in arts
    if ch == 'P':
        return 'earth' in arts or 'wind' in arts
    if ch == 'p':
        return 'wind' in arts
    if ch == 'Z':
        return 'water' in arts
    if ch == 'D':
        return True
    if ch == 'G':
        return 'seals' in arts
    return False


def reach(screens, start, arts):
    sid0, x0, y0 = start
    seen = {(sid0, x0, y0)}
    q = deque([(sid0, x0, y0)])
    while q:
        sid, x, y = q.popleft()
        s = screens[sid]
        m = s['map']
        nxt = []
        if m[y][x] in 'CS' or (m[y][x] == 'G' and 'seals' in arts):
            if s['warp']:
                t, tx, ty = s['warp']
                nxt.append((t, tx // 1, ty // 1))
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 16 and 0 <= ny < 11:
                if passable(m[ny][nx], arts):
                    nxt.append((sid, nx, ny))
            else:
                key = (s['area'], s['gx'] + (1 if nx > 15 else -1 if nx < 0 else 0),
                       s['gy'] + (1 if ny > 10 else -1 if ny < 0 else 0))
                n2 = build.SCR_IDS.get(key)
                if n2 is not None:
                    nx2, ny2 = nx % 16, ny % 11
                    if passable(screens[n2]['map'][ny2][nx2], arts):
                        nxt.append((n2, nx2, ny2))
        for st in nxt:
            if st not in seen:
                seen.add(st)
                q.append(st)
    return seen


def main():
    chrdata, defines, mt, boss = build.build_chr()
    mt_index = {ch: i for i, (ch, _, _, _, _) in enumerate(mt)}
    msgs, order, screens = build.parse_world(os.path.join(ROOT, 'assets', 'world.txt'), mt_index)
    ids = build.SCR_IDS
    start = (ids[('ow', 1, 2)], 7, 6)

    def can(arts, area, gx, gy, what):
        sid = ids[(area, gx, gy)]
        s = screens[sid]
        seen = reach(screens, start, arts)
        if what == 'item':
            _, x, y = s['item']
            return (sid, x, y) in seen
        return any(p[0] == sid for p in seen)

    base = {'veil'}
    checks = [
        # (arts, area, gx, gy, what, expected)
        (base, 'ember', 1, 2, 'screen', True),
        (base, 'ember', 0, 0, 'item', True),          # Fire tome without fire
        (base, 'ember', 1, 0, 'screen', True),
        (base, 'stone', 1, 2, 'screen', False),       # needs fire
        (base | {'fire'}, 'stone', 0, 0, 'item', True),
        (base | {'fire'}, 'stone', 1, 0, 'screen', False),
        (base | {'fire', 'earth'}, 'stone', 1, 0, 'screen', True),
        (base | {'fire'}, 'sky', 1, 2, 'screen', False),
        (base | {'fire', 'earth'}, 'sky', 0, 0, 'item', True),
        (base | {'fire', 'earth'}, 'sky', 1, 0, 'screen', False),
        (base | {'fire', 'earth', 'wind'}, 'sky', 1, 0, 'screen', True),
        (base | {'fire', 'earth'}, 'tide', 1, 2, 'screen', False),
        (base | {'fire', 'earth', 'wind'}, 'tide', 0, 0, 'item', True),
        (base | {'fire', 'earth', 'wind'}, 'tide', 2, 0, 'screen', False),
        (base | {'fire', 'earth', 'wind', 'water'}, 'tide', 1, 0, 'screen', True),
        (base | {'fire', 'earth', 'wind', 'water'}, 'loom', 1, 2, 'screen', False),
        (base | {'fire', 'earth', 'wind', 'water', 'seals'}, 'loom', 1, 0, 'screen', True),
        (base | {'fire', 'earth', 'wind', 'water', 'seals'}, 'loom', 0, 0, 'item', True),
        (base | {'fire'}, 'trial', 0, 0, 'item', True),
        (base | {'fire', 'earth', 'wind', 'water'}, 'ow', 3, 3, 'item', True),
        (base | {'fire', 'earth'}, 'stone', 2, 2, 'item', True),
    ]
    ok = True
    for arts, area, gx, gy, what, exp in checks:
        got = can(arts, area, gx, gy, what)
        flag = 'ok ' if got == exp else 'BAD'
        ok &= got == exp
        print('%s %-34s %-6s %s %d,%d  expected %s' % (flag, ','.join(sorted(arts)), what, area, gx, gy, exp))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

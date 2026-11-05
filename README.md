# THE FIVE SEALS — Book I: Ember and Veil

An original top-down action-adventure for the NES. You're the last apprentice of a fallen order of mages. Five temples have been sealed and the elemental Arts are fading. You still have the Veil. Book I covers the first temple and the Art of Fire.

**Play:** open `dist/FiveSeals.nes` in MesenCE. Keep `FiveSeals.dbg` next to the ROM and Mesen's debugger will show source-level labels. Progress saves to battery RAM (Mesen writes a `.sav` file).

## Controls

| Button | Action |
|---|---|
| D-pad | Move |
| B | Staff strike (free, short range) |
| A | Cast the selected Art |
| SELECT | Switch Art (once you have more than one) |
| START | Pause / start / dismiss messages |

## The Arts (Book I)

- **Veil** (you start with it). Press A to toggle. It costs 4 mana to raise, then drains steadily.
  - Illusory walls and bridges show up and become passable.
  - Shades become visible and can be hurt.
  - Stalkers lose track of you.
  - Fire bolts pass through illusions.
  - The Veil can't drop while you're standing inside an illusion, so it can never trap you in a wall.
- **Fire** (learned in the Ember Temple). A bolt that burns brambles, lights braziers, and is the only thing that can hurt the boss.
  - **Mastery II** comes from the optional Trial Cave: double damage, cheaper casts, faster bolts.
- **Mana** refills slowly while the Veil is down. Enemies sometimes drop mana orbs. The staff costs nothing, so you can never soft-lock on mana.

## World

- **Ashen Vale:** a 4×4 overworld.
  - You start at the Sanctum in the middle.
  - The Ember Temple's gate is behind an illusory rock wall to the northeast.
  - Optional: the Trial Cave (west, behind brambles) and a secret heart vessel (southeast, across a hidden bridge).
- **Ember Temple:** 9 rooms with combat locks, a Veil maze, brazier puzzles, a lava bridge, and the Gale Warden boss.

## Art direction

All art is original, drawn to NES limits in a classic NES style:
- Sprites use 3 colors with a black outline.
- Light always comes from the top-left.
- Tiles are 16×16 with 3 colors plus a shared backdrop color.
- The title logo is the built-in font at double size with a fire gradient.

No third-party assets, so no attribution is required.

## Building

You need [cc65](https://cc65.github.io/) on `PATH`, plus `python3`.

```bash
make            # -> build/fiveseals.nes
```

## Changing the game

| Want to change… | Edit |
|---|---|
| Maps, enemies, items, doors, warps, all text | `assets/world.txt` (legend at the top of the file) |
| Pixel art: tiles, sprites, font | `tools/art.py` (ASCII art, colors 0–3). Run `python3 tools/preview.py` to render every tile and sprite to `build/preview.png` with its real palette |
| Palettes, music, sound effects | `tools/build.py` (`PALSETS`, `SONGS`, `SFX`) |
| Game logic | `src/main.c` |
| Hardware runtime: NMI, sprites, MMC1 | `src/crt0.s` |
| Sound driver | `src/sound.s` |

`tools/build.py` checks the world as it builds. It fails if neighbouring screen edges don't line up, an enemy or item sits inside a wall, or a warp lands on a bad tile.

### Tech notes

- **Mapper:** MMC1. 128 KB PRG (banks 2–6 are empty, room for Books II–V), 8 KB CHR-RAM, 8 KB battery WRAM.
- **Bank layout:**
  - Bank 7 (fixed): startup, runtime, sound, and C code.
  - Bank 0: data and overflow code.
  - Bank 1: tile data, copied into CHR-RAM at boot.
- **Screen:** one screen per room, Zelda-style 64 px HUD, 16×11 grid of 16×16 metatiles.
- **Veil effect:** illusion tiles use background palette 3. Normally their colors match what they imitate. Raising the Veil swaps that one palette, which shows the hidden pattern, at no extra map or tile cost.
- **Frame budget:** the NMI does OAM DMA, then palette, then a VRAM update buffer. Game logic decides what changes; HUD and tile updates are spread across frames to stay inside vblank.

### Headless tests

`test/run.mjs` runs the ROM in jsnes with scripted input. It can poke variables by symbol name (from the `.dbg` file) and save screenshots. The other files in `test/` are example scripts. `fuzz.json` mashes random input on every screen.

# THE FIVE SEALS

An original top-down action-adventure for the NES. You're the last apprentice of a fallen order of mages. The Unweaver has bound the five elemental Arts behind five seals. You still have the Veil. Break the four temple seals, then go down into the Loom beneath the Sanctum and undo the Unweaver.

**Play:** open `build/fiveseals.nes` (or `dist/FiveSeals.nes`) in MesenCE. Keep the `.dbg` file next to the ROM and Mesen's debugger shows source labels. Progress saves to battery RAM.

## Controls

| Button | Action |
|---|---|
| D-pad | Move |
| B | Staff strike (free, short range) |
| A | Cast the selected Art |
| SELECT | Cycle through the Arts you know |
| START | Pause / start / dismiss messages |

## The Arts

| Art | Where | Use in combat | Use in the world | Mana |
|---|---|---|---|---|
| **Veil** | You start with it | Shows shades and lets you hit them; stalkers lose track of you | Illusion walls and bridges become visible and passable | 4, then drains while up |
| **Fire** | Ember Temple | Bolt | Burns brambles, lights braziers | 8 (6 with mastery) |
| **Earth** | Stone Temple | Rolling boulder, 2 damage | Breaks cracked walls, fills pits | 10 |
| **Wind** | Sky Spire | Dash: you can't be hurt during it, and it hits everything you pass through | Flies over pits and chasms | 8 |
| **Water** | Tide Temple | Wave, 2 damage | Puts out walls of flame | 8 |

Bosses only take damage from one Art, following the cycle Fire → Wind → Earth → Water → Fire:

| Boss | Temple | Weak to |
|---|---|---|
| Gale Warden | Ember | Fire |
| Mire Serpent | Stone | Earth |
| Stone Colossus | Sky | Wind (dash into it) |
| Cinder Drake | Tide | Water |
| Unweaver | The Loom | The Art matching its current color, and only while the Veil is up |

Mana refills slowly while the Veil is down, enemies drop mana orbs, and bosses drop one each time you hit them while you're low. The staff is always free, so running out of mana can never soft-lock you.

## World

- **Ashen Vale:** a 6×4 overworld. The Sanctum is in the middle. The east half sits behind cracked rocks (Earth).
- **Five dungeons:**
  - Ember Temple (9 rooms)
  - Stone Temple (8)
  - Sky Spire (7)
  - Tide Temple (7)
  - The Loom (7)
- **Optional:** the Trial Cave (Fire mastery), two heart vessels, and Veil mastery in the Loom.
- **Order:** each temple needs the Art from the one before it. Fire opens the Stone Temple, Earth opens the east, Wind crosses the chasm to the Tide Temple, and all four seals open the Loom.

## Building

You need [cc65](https://cc65.github.io/) on `PATH`, plus `python3` with Pillow (Pillow is only for `tools/preview.py`).

```bash
make            # -> build/fiveseals.nes
```

Browser build (one HTML file with the emulator and ROM embedded):

```bash
python3 tools/build_web.py path/to/jsnes.min.js build/web.html
```

## Changing the game

| Want to change… | Edit |
|---|---|
| Maps, enemies, items, doors, warps, all text | `assets/world.txt` (legend at the top) |
| Pixel art: tiles, sprites, font, icons | `tools/art.py`. Run `python3 tools/preview.py` to render everything to `build/preview.png` |
| Boss sprites (drawn by code) | `tools/build.py` (`*_frame` functions, `BOSS_PALS`) |
| Palettes, music, sound effects | `tools/build.py` (`PALSETS`, `SONGS`, `SFX`) |
| Game logic, boss stats | `src/main.c` (`boss_hp`, `boss_move`, `art_cost`, …) |
| Hardware runtime: NMI, sprites, MMC1, bank copy | `src/crt0.s` |
| Sound driver | `src/sound.s` |

`tools/build.py` checks the world as it builds. The build fails if:
- neighbouring screen edges don't line up;
- an enemy or item sits in a wall;
- a warp lands on a bad tile.

The ID of every screen is written to `build/screens.json`.

### Tech notes

- **Mapper:** MMC1. 128 KB PRG, 8 KB CHR-RAM, 8 KB battery WRAM.
- **Bank layout:**

  | Bank | Contents |
  |---|---|
  | 0 | Most gameplay code and constant tables |
  | 1 | Tile data, plus five boss sprite sets. A boss's set is copied into a shared sprite slot when you enter its room |
  | 2 | World data: 64 screen maps, enemy lists, all text. Copied into RAM on demand with `bank_copy` |
  | 7 (fixed) | Startup, runtime, sound, the rest of the C code |
  | 3–6 | Free |

- **Screen:** one screen per room, a 64 px HUD (life, mana, Art, the four seals), 16×11 metatiles.
- **Veil effect:** illusion tiles use background palette 3. Normally its colors match what the tiles imitate; raising the Veil swaps that one palette. Each room picks one of 13 palette sets.
- **Frame budget:** the NMI does OAM DMA, then palette, then a VRAM update buffer. HUD and tile updates are spread across frames to stay inside vblank. The busiest rooms measure zero lag frames in testing.

### Headless tests

`test/run.mjs` runs the ROM in jsnes with scripted input. It can poke variables by symbol name (from the `.dbg` file) and save screenshots.

- `test/full/*.json`: each Art's world interaction, every boss (wrong Art does nothing, right Art kills it), the seal gate, and the ending.
- `test/fuzz.json`: random input on all 63 playable screens.

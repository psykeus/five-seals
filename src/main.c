/*
 * THE FIVE SEALS
 * Top-down action adventure for the NES (MMC1, cc65).
 *
 * Code placement: most gameplay lives in PRG bank 0 (segment CODE0, always mapped
 * during play); the rest sits in the fixed bank with the runtime. World data (maps,
 * enemy lists, text) is in bank 2 and is copied into RAM with bank_copy().
 */
#include <string.h>
#include "lib.h"
#include "gen_data.h"

/* ------------------------------------------------------------------ constants */
#define PF_Y       63          /* sprite Y offset of playfield (64 - 1) */
#define MANA_MAX   64
#define WORLD_BANK 2

#define DIR_DOWN  0
#define DIR_UP    1
#define DIR_LEFT  2
#define DIR_RIGHT 3

#define MF_SOLID  1
#define MF_VEIL   2
#define MF_BURN   4
#define MF_BRAZ   8
#define MF_DOOR   16
#define MF_WARP   32
#define MF_SIGN   64
#define MF_LOW    128        /* projectiles fly over it (water, lava, pits) */
#define MF2_CRACK 1
#define MF2_PIT   2
#define MF2_FILL  4
#define MF2_FLAME 8

#define ART_VEIL  0
#define ART_FIRE  1
#define ART_EARTH 2
#define ART_WIND  3
#define ART_WATER 4
#define ABIT(a)   (1 << (a))
#define ALL_SEALS 0x1E

#define BT_FIRE   1
#define BT_EARTH  2
#define BT_WATER  3

#define PK_HEART  1
#define PK_MANA   2
#define PK_ITEM   3

#define COND_NONE  0
#define COND_CLEAR 1
#define COND_BRAZ  2
#define COND_FLAG  3
#define COND_SEALS 4

#define ST_PLAY  0
#define ST_MSG   1
#define ST_DEAD  2
#define ST_END   3
#define ST_TITLE 4

#define MAX_EN   6
#define MAX_BOLT 2
#define MAX_SHOT 6
#define MAX_PK   4
#define MAX_PUFF 4

#define F_SEEN   64
#define F_TAKEN  128

#define NO_BOSS  255

/* ------------------------------------------------------------------ zero page */
#pragma bss-name(push, "ZEROPAGE")
unsigned char px, py, pdir, cur, veil_on;
unsigned char ax_, ay_, aw_, ah_;
#pragma bss-name(pop)
#pragma zpsym("px")
#pragma zpsym("py")
#pragma zpsym("pdir")
#pragma zpsym("cur")
#pragma zpsym("veil_on")
#pragma zpsym("ax_")
#pragma zpsym("ay_")
#pragma zpsym("aw_")
#pragma zpsym("ah_")

/* ------------------------------------------------------------------ battery save */
#pragma bss-name(push, "SAVE")
unsigned char sv[48];
#pragma bss-name(pop)

/* ------------------------------------------------------------------ state */
static unsigned char map[176];
static unsigned char flags[24];
static unsigned char attr[64];
static unsigned char rowbuf[32];
static unsigned char base_pal[32];
static unsigned char enbuf[32];
static char msgbuf[128];

static unsigned char php, pmaxhp, mana, arts, art_sel, fire_tier, veil_tier, seals;
static unsigned char pinv, pkb, pkbdir, patk, pcast, panim, pmoving;
static unsigned char pdash, dash_ext, dash_sx, dash_sy, dash_blocked;
static unsigned char cp_scr, cp_x, cp_y;
static unsigned char state, area, cur_song, palset;
static unsigned char hud_dirty, sign_latch, sign_touch, scr_changed;
static unsigned char end_after_msg, paused, rot;
static unsigned char boss_id, boss_weak, boss_slot;
unsigned char lag_frames, last_frame;   /* debug: frames where logic overran vblank */

static unsigned char e_type[MAX_EN], e_x[MAX_EN], e_y[MAX_EN], e_dir[MAX_EN];
static unsigned char e_hp[MAX_EN], e_t[MAX_EN], e_t2[MAX_EN], e_s[MAX_EN];
static unsigned char e_flash[MAX_EN], e_kb[MAX_EN], e_kbdir[MAX_EN];
static signed char e_dx[MAX_EN], e_dy[MAX_EN];

static unsigned char b_on[MAX_BOLT], b_type[MAX_BOLT], b_x[MAX_BOLT], b_y[MAX_BOLT], b_dir[MAX_BOLT], b_life[MAX_BOLT];
static unsigned char s_on[MAX_SHOT], s_x[MAX_SHOT], s_y[MAX_SHOT];
static signed char s_dx[MAX_SHOT], s_dy[MAX_SHOT];
static unsigned char k_type[MAX_PK], k_x[MAX_PK], k_y[MAX_PK], k_t[MAX_PK], k_item[MAX_PK];
static unsigned char f_t[MAX_PUFF], f_x[MAX_PUFF], f_y[MAX_PUFF];

static unsigned char mq[8], mq_n;
static const char* msg_p;
static unsigned char msg_row, msg_wait;

/* ------------------------------------------------------------------ tables */
static const unsigned char bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const unsigned char enemy_hp[6] = {0, 2, 3, 2, 1, 3};
static const signed char dir8_x[8] = {2, 1, 0, -1, -2, -1, 0, 1};
static const signed char dir8_y[8] = {0, 1, 2, 1, 0, -1, -2, -1};
static const char* const area_names[7] = {
    "ASHEN VALE", "EMBER TEMPLE", "TRIAL CAVE", "STONE TEMPLE", "SKY SPIRE", "TIDE TEMPLE", "THE LOOM"};
static const char* const art_name[5] = {"VEIL", "FIRE", "EARTH", "WIND", "WATER"};
static const unsigned char art_robe[5] = {0x13, 0x16, 0x17, 0x2A, 0x11};
static const unsigned char art_cost[5] = {4, 8, 10, 8, 8};

/* bosses: Gale Warden, Mire Serpent, Stone Colossus, Cinder Drake, Unweaver */
static const unsigned char boss_hp[5]   = {10, 10, 6, 12, 12};
static const unsigned char boss_weakt[5] = {ART_FIRE, ART_EARTH, ART_WIND, ART_WATER, ART_FIRE};
static const unsigned char boss_move[5] = {0, 1, 2, 0, 3};   /* bounce, chase, charge, bounce, teleport */
static const unsigned char boss_shot[5] = {0, 1, 2, 3, 1};   /* spread3, ring8, aimed, spread5 */
static const unsigned char boss_rate[5] = {110, 150, 80, 90, 120};
static const unsigned char tele_x[4] = {32, 192, 32, 192};
static const unsigned char tele_y[4] = {16, 16, 112, 112};

/* items: index = IT_* */
static const unsigned char item_tile[13] = {0, SPR_TOME, SPR_SIGIL, SPR_HEARTC, SPR_SEAL, SPR_TOME, SPR_TOME, SPR_TOME,
                                            SPR_SEAL, SPR_SEAL, SPR_SEAL, SPR_SEAL, SPR_SIGIL};
static const unsigned char item_attr[13] = {0, 3, 3, 3, 3, 2, 1, 1, 2, 1, 1, 0, 2};

/* ------------------------------------------------------------------ prototypes */
static void enter_screen(unsigned char s, unsigned char x, unsigned char y, unsigned char fade);
static void msg_start(unsigned char id);
static void room_solve(void);
static void pal_build(void);
static void pal_fade(unsigned char lvl);

#pragma code-name(push, "CODE0")

/* ------------------------------------------------------------------ small helpers */
static unsigned char flag_get(unsigned char n) { return flags[n >> 3] & bits[n & 7]; }
static void flag_set(unsigned char n) { flags[n >> 3] |= bits[n & 7]; }

static void wait_frames(unsigned char n) {
    while (n) { ppu_wait_nmi(); --n; }
}

static unsigned char is_boss(unsigned char t) { return t >= E_BOSS_GALE; }

static unsigned char song_for_area(void) {
    if (boss_id != NO_BOSS && e_type[boss_slot]) return SONG_BOSS;
    switch (area) {
    case AREA_OW:    return SONG_OVERWORLD;
    case AREA_EMBER:
    case AREA_TRIAL: return SONG_TEMPLE;
    case AREA_LOOM:  return SONG_FINAL;
    }
    return SONG_DUNGEON;
}

static void music_area(void) {
    unsigned char s;
    s = song_for_area();
    if (s != cur_song) { cur_song = s; mus_req = s; }
}

/* ------------------------------------------------------------------ VRAM update buffer */
static void vb_hdr(unsigned int adr, unsigned char len) {
    vbuf[vbuf_len] = adr >> 8;
    vbuf[vbuf_len + 1] = adr & 255;
    vbuf[vbuf_len + 2] = len;
    vbuf_len += 3;
}
#define VB(b) (vbuf[vbuf_len++] = (b))

static void vb_row(unsigned char row) {
    unsigned char n;
    vb_hdr(0x2000 + ((unsigned int)row << 5), 32);
    for (n = 0; n < 32; ++n) VB(rowbuf[n]);
}

static void row_clear(void) { memset(rowbuf, 0, 32); }

static void row_text(unsigned char col, const char* s) {
    while (*s && col < 32) rowbuf[col++] = *s++;
}

/* ------------------------------------------------------------------ HUD */
static unsigned char heart_tile(unsigned char n) {
    unsigned char t;
    if (n >= (pmaxhp >> 1)) return 0;
    t = n << 1;
    if (php >= t + 2) return T_HEART_FULL;
    if (php == t + 1) return T_HEART_HALF;
    return T_HEART_EMPTY;
}

static unsigned char mana_tile(unsigned char n) {
    unsigned char lo;
    lo = n << 3;
    if (mana <= lo) return T_MANA0;
    if (mana >= lo + 8) return T_MANA0 + 8;
    return T_MANA0 + (mana - lo);
}

static void hud_row(unsigned char r) {
    unsigned char n, t;
    row_clear();
    if (state == ST_TITLE) {
        if (r == 2) row_text(14, "THE");
        if (r == 3) memcpy(rowbuf + 16 - TITLE_W / 2, title_top, TITLE_W);
        if (r == 4) memcpy(rowbuf + 16 - TITLE_W / 2, title_bot, TITLE_W);
        if (r == 6) row_text(7, "A TALE OF FIVE ARTS");
        return;
    }
    t = art_icon[art_sel];
    switch (r) {
    case 2:
        row_text(2, "LIFE");
        row_text(13, "MANA");
        row_text(23, "ART");
        break;
    case 3:
        for (n = 0; n < 8; ++n) {
            rowbuf[2 + n] = heart_tile(n);
            rowbuf[13 + n] = mana_tile(n);
        }
        rowbuf[23] = t;
        rowbuf[24] = t + 1;
        row_text(26, art_name[art_sel]);
        break;
    case 4:
        rowbuf[23] = t + 2;
        rowbuf[24] = t + 3;
        if (art_sel == ART_FIRE) row_text(26, fire_tier > 1 ? "LV2" : "LV1");
        else if (art_sel == ART_VEIL) row_text(26, veil_on ? "ON" : "OFF");
        break;
    case 5:
        if (area < 7) row_text(2, area_names[area]);
        for (n = 1; n < 5; ++n) rowbuf[25 + n] = (seals & bits[n]) ? T_SEAL_ON : T_SEAL_OFF;
        break;
    case 7:
        memset(rowbuf, T_HUD_LINE, 32);
        break;
    }
}

/* one dirty part per frame keeps NMI work inside vblank */
static void hud_update(void) {
    unsigned char n;
    if (!hud_dirty) return;
    if (hud_dirty & 1) {
        vb_hdr(0x2000 + 3 * 32 + 2, 8);
        for (n = 0; n < 8; ++n) VB(heart_tile(n));
        hud_dirty &= ~1;
    } else if (hud_dirty & 2) {
        vb_hdr(0x2000 + 3 * 32 + 13, 8);
        for (n = 0; n < 8; ++n) VB(mana_tile(n));
        hud_dirty &= ~2;
    } else if (hud_dirty & 4) {
        hud_row(3);
        vb_hdr(0x2000 + 3 * 32 + 23, 8);
        for (n = 23; n < 31; ++n) VB(rowbuf[n]);
        hud_row(4);
        vb_hdr(0x2000 + 4 * 32 + 23, 8);
        for (n = 23; n < 31; ++n) VB(rowbuf[n]);
        hud_dirty &= ~4;
    }
}

/* ------------------------------------------------------------------ metatile redraw queue */
static void mq_push(unsigned char idx) {
    if (mq_n < 8) mq[mq_n++] = idx;
}

static void mq_flush(void) {
    unsigned char n, idx, m;
    unsigned int adr;
    for (n = 0; n < 2 && mq_n; ++n) {
        idx = mq[0];
        --mq_n;
        memmove(mq, mq + 1, mq_n);
        m = map[idx];
        adr = 0x2000 + ((unsigned int)(8 + ((idx >> 4) << 1)) << 5) + ((idx & 15) << 1);
        vb_hdr(adr, 2);
        VB(mt_tl[m]);
        VB(mt_tr[m]);
        vb_hdr(adr + 32, 2);
        VB(mt_bl[m]);
        VB(mt_br[m]);
    }
}

/* ------------------------------------------------------------------ save data (v2) */
static void save_game(void) {
    unsigned char n, sum;
    sv[0] = 'F'; sv[1] = 'S'; sv[2] = '0'; sv[3] = '2';
    sv[4] = pmaxhp;
    sv[5] = arts;
    sv[6] = fire_tier;
    sv[7] = cp_scr;
    sv[8] = cp_x;
    sv[9] = cp_y;
    sv[10] = seals;
    sv[11] = veil_tier;
    for (n = 0; n < 24; ++n) sv[12 + n] = flags[n];
    sum = 0x5A;
    for (n = 4; n < 36; ++n) sum += sv[n];
    sv[36] = sum;
}

static unsigned char save_valid(void) {
    unsigned char n, sum;
    if (sv[0] != 'F' || sv[1] != 'S' || sv[2] != '0' || sv[3] != '2') return 0;
    sum = 0x5A;
    for (n = 4; n < 36; ++n) sum += sv[n];
    return sum == sv[36];
}

static void load_game(void) {
    unsigned char n;
    pmaxhp = sv[4];
    arts = sv[5];
    fire_tier = sv[6];
    cp_scr = sv[7];
    cp_x = sv[8];
    cp_y = sv[9];
    seals = sv[10];
    veil_tier = sv[11];
    for (n = 0; n < 24; ++n) flags[n] = sv[12 + n];
    php = pmaxhp;
    mana = MANA_MAX;
    art_sel = ART_VEIL;
    for (n = 4; n; --n) if (arts & ABIT(n)) { art_sel = n; break; }
}

static void new_game(void) {
    pmaxhp = 6;
    php = 6;
    mana = MANA_MAX;
    arts = ABIT(ART_VEIL);
    art_sel = ART_VEIL;
    fire_tier = 1;
    veil_tier = 1;
    seals = 0;
    memset(flags, 0, 24);
    cp_scr = SCR_START;
    cp_x = START_X;
    cp_y = START_Y;
}

/* ------------------------------------------------------------------ map queries */
static unsigned char psolid(unsigned char x, unsigned char y) {
    unsigned char f;
    f = mt_flags[map[(y & 0xF0) | (x >> 4)]];
    if (!(f & MF_SOLID)) return 0;
    if ((f & MF_VEIL) && veil_on) return 0;
    return 1;
}

/* while dashing, pits and chasms do not block */
static unsigned char dsolid(unsigned char x, unsigned char y) {
    unsigned char m;
    m = map[(y & 0xF0) | (x >> 4)];
    if (mt_flags2[m] & MF2_PIT) return 0;
    if (!(mt_flags[m] & MF_SOLID)) return 0;
    if ((mt_flags[m] & MF_VEIL) && veil_on) return 0;
    return 1;
}

static unsigned char rsolid(unsigned char x, unsigned char y) {
    return mt_flags[map[(y & 0xF0) | (x >> 4)]] & MF_SOLID;
}

static unsigned char p_free(unsigned char x, unsigned char y) {
    if (psolid(x + 3, y + 8)) return 0;
    if (psolid(x + 12, y + 8)) return 0;
    if (psolid(x + 3, y + 15)) return 0;
    if (psolid(x + 12, y + 15)) return 0;
    return 1;
}

static unsigned char d_free(unsigned char x, unsigned char y) {
    if (dsolid(x + 3, y + 8)) return 0;
    if (dsolid(x + 12, y + 8)) return 0;
    if (dsolid(x + 3, y + 15)) return 0;
    if (dsolid(x + 12, y + 15)) return 0;
    return 1;
}

static void check_sign(unsigned char x, unsigned char y) {
    if (mt_flags[map[(y & 0xF0) | (x >> 4)]] & MF_SIGN) sign_touch = 1;
}

/* box A (ax_..ah_) against enemy n */
static unsigned char hit_enemy(unsigned char n) {
    unsigned char ex, ey, ew, eh;
    if (is_boss(e_type[n])) {
        ex = e_x[n] + 4; ey = e_y[n] + 4; ew = 24; eh = 24;
    } else {
        ex = e_x[n] + 2; ey = e_y[n] + 2; ew = 12; eh = 13;
    }
    return (ax_ < ex + ew) && (ex < ax_ + aw_) && (ay_ < ey + eh) && (ey < ay_ + ah_);
}

static unsigned char hit_player(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    return (px + 3 < x + w) && (x < px + 13) && (py + 4 < y + h) && (y < py + 16);
}

/* 8-way direction index from (x,y) toward the player's centre */
static unsigned char dir8_to_player(unsigned char x, unsigned char y) {
    unsigned char ax, ay, hx, hy;
    hx = px + 4 > x;
    ax = hx ? px + 4 - x : x - px - 4;
    hy = py + 4 > y;
    ay = hy ? py + 4 - y : y - py - 4;
    if (ax > (ay << 1)) return hx ? 0 : 4;
    if (ay > (ax << 1)) return hy ? 2 : 6;
    if (hx) return hy ? 1 : 7;
    return hy ? 3 : 5;
}

/* ------------------------------------------------------------------ spawners */
static unsigned char pk_spawn(unsigned char type, unsigned char x, unsigned char y, unsigned char t) {
    unsigned char n;
    for (n = 0; n < MAX_PK; ++n) {
        if (!k_type[n]) {
            k_type[n] = type; k_x[n] = x; k_y[n] = y; k_t[n] = t;
            return n;
        }
    }
    return 255;
}

static void puff_spawn(unsigned char x, unsigned char y) {
    unsigned char n;
    for (n = 0; n < MAX_PUFF; ++n) {
        if (!f_t[n]) { f_t[n] = 16; f_x[n] = x; f_y[n] = y; return; }
    }
}

static void shot_spawn(unsigned char x, unsigned char y, unsigned char d) {
    unsigned char n;
    for (n = 0; n < MAX_SHOT; ++n) {
        if (!s_on[n]) {
            s_on[n] = 1; s_x[n] = x; s_y[n] = y;
            s_dx[n] = dir8_x[d & 7]; s_dy[n] = dir8_y[d & 7];
            return;
        }
    }
}

static void spawn_item(void) {
    unsigned char it, n;
    it = scr_item[cur];
    if (!it || flag_get(F_TAKEN + cur)) return;
    if (scr_cond[cur] != COND_NONE && !flag_get(cur)) return;
    n = pk_spawn(PK_ITEM, scr_item_x[cur], scr_item_y[cur], 0);
    if (n != 255) k_item[n] = it;
}

/* ------------------------------------------------------------------ veil */
static void veil_set(unsigned char v) {
    veil_on = v;
    pal_build();
    pal_fade(0);
    sfx_req = v ? SFX_VEILON : SFX_VEILOFF;
    hud_dirty |= 4;
}

static unsigned char veil_can_drop(void) {
    unsigned char r;
    veil_on = 0;
    r = p_free(px, py);
    veil_on = 1;
    return r;
}

/* ------------------------------------------------------------------ player damage */
static void hurt_player(unsigned char dmg, unsigned char sx, unsigned char sy) {
    unsigned char ax, ay, cx, cy;
    if (pinv || pdash || !php) return;
    php = (php > dmg) ? php - dmg : 0;
    hud_dirty |= 1;
    pinv = 60;
    patk = 0;
    sfx_req = SFX_HURT;
    cx = px + 8; cy = py + 8;
    ax = (cx > sx) ? cx - sx : sx - cx;
    ay = (cy > sy) ? cy - sy : sy - cy;
    if (ax > ay) pkbdir = (cx > sx) ? DIR_RIGHT : DIR_LEFT;
    else pkbdir = (cy > sy) ? DIR_DOWN : DIR_UP;
    pkb = 8;
    if (!php) state = ST_DEAD;
}

/* ------------------------------------------------------------------ enemies */
static unsigned char e_step(unsigned char n, unsigned char d) {
    unsigned char x, y;
    x = e_x[n]; y = e_y[n];
    switch (d) {
    case DIR_DOWN:  if (y >= 160) return 0; ++y; break;
    case DIR_UP:    if (y == 0) return 0; --y; break;
    case DIR_LEFT:  if (x == 0) return 0; --x; break;
    default:        if (x >= 240) return 0; ++x; break;
    }
    if (rsolid(x + 2, y + 6) || rsolid(x + 13, y + 6) || rsolid(x + 2, y + 15) || rsolid(x + 13, y + 15))
        return 0;
    e_x[n] = x; e_y[n] = y;
    return 1;
}

static unsigned char dir_toward(unsigned char n, unsigned char alt) {
    unsigned char ax, ay, hx, hy, horiz;
    hx = px > e_x[n];
    ax = hx ? px - e_x[n] : e_x[n] - px;
    hy = py > e_y[n];
    ay = hy ? py - e_y[n] : e_y[n] - py;
    horiz = ax > ay;
    if (alt) horiz = !horiz;
    if (horiz) return hx ? DIR_RIGHT : DIR_LEFT;
    return hy ? DIR_DOWN : DIR_UP;
}

static void blob_ai(unsigned char n) {
    if (e_t[n]) --e_t[n];
    else { e_dir[n] = rand8() & 3; e_t[n] = 32 + (rand8() & 63); }
    if ((frame_count & 1) && !e_step(n, e_dir[n])) e_t[n] = 0;
}

static void stalker_ai(unsigned char n) {
    if (((frame_count + (n << 2)) & 15) == 0) e_dir[n] = dir_toward(n, 0);
    if (frame_count & 3) {
        if (!e_step(n, e_dir[n])) {
            e_dir[n] = dir_toward(n, 1);
            if (!e_step(n, e_dir[n])) e_dir[n] = rand8() & 3;
        }
    }
}

static void shade_ai(unsigned char n) {
    if (!(frame_count & 1)) return;
    if (e_x[n] < px) ++e_x[n]; else if (e_x[n] > px) --e_x[n];
    if (e_y[n] < py) ++e_y[n]; else if (e_y[n] > py) --e_y[n];
}

static void bat_ai(unsigned char n) {
    unsigned char r;
    if (!e_t[n]) {
        r = rand8();
        e_dx[n] = (r & 1) ? 1 : -1;
        e_dy[n] = (r & 2) ? 1 : -1;
        e_t[n] = 20 + (r & 31);
    }
    --e_t[n];
    if ((frame_count & 3) == 0) return;
    e_x[n] += e_dx[n];
    e_y[n] += e_dy[n];
    if (e_x[n] <= 2 || e_x[n] >= 238) { e_dx[n] = -e_dx[n]; e_x[n] += e_dx[n] << 1; }
    if (e_y[n] <= 2 || e_y[n] >= 158) { e_dy[n] = -e_dy[n]; e_y[n] += e_dy[n] << 1; }
}

static void eye_ai(unsigned char n) {
    if (--e_t[n]) return;
    e_t[n] = 110;
    shot_spawn(e_x[n] + 4, e_y[n] + 4, dir8_to_player(e_x[n] + 4, e_y[n] + 4));
}

static void boss_fire(unsigned char n) {
    unsigned char cx, cy, d, k;
    cx = e_x[n] + 12; cy = e_y[n] + 12;
    d = dir8_to_player(cx, cy);
    switch (boss_shot[boss_id]) {
    case 0:
        shot_spawn(cx, cy, d - 1); shot_spawn(cx, cy, d); shot_spawn(cx, cy, d + 1);
        break;
    case 1:
        for (k = 0; k < 8; k += 2) shot_spawn(cx, cy, k + (frame_count & 1));
        shot_spawn(cx, cy, d);
        break;
    case 2:
        shot_spawn(cx, cy, d);
        break;
    default:
        for (k = 0; k < 5; ++k) shot_spawn(cx, cy, d - 2 + k);
        break;
    }
    sfx_req = SFX_SWING;
}

static void boss_set_weak(unsigned char w) {
    boss_weak = w;
    pal_build();
    pal_fade(0);
}

static void boss_ai(unsigned char n) {
    unsigned char phase2, k;
    phase2 = e_hp[n] <= (boss_hp[boss_id] >> 1);
    switch (boss_move[boss_id]) {
    case 0:  /* bounce */
        k = phase2 ? 2 : 1;
        if (boss_id == 3) ++k;
        if (!(frame_count & 1)) --k;
        while (k) {
            --k;
            e_x[n] += e_dx[n];
            e_y[n] += e_dy[n];
            if (e_x[n] <= 16) e_dx[n] = 1;
            if (e_x[n] >= 208) e_dx[n] = -1;
            if (e_y[n] <= 8) e_dy[n] = 1;
            if (e_y[n] >= 136) e_dy[n] = -1;
        }
        break;
    case 1:  /* slow chase */
        if ((frame_count & 1) || phase2) {
            if (e_x[n] + 8 < px && e_x[n] < 208) ++e_x[n]; else if (e_x[n] + 8 > px && e_x[n] > 16) --e_x[n];
            if (e_y[n] + 8 < py && e_y[n] < 136) ++e_y[n]; else if (e_y[n] + 8 > py && e_y[n] > 8) --e_y[n];
        }
        break;
    case 2:  /* charge: wait, rush toward the player, stun on the wall */
        if (e_s[n] == 0) {
            if (!--e_t2[n]) {
                e_dx[n] = (px > e_x[n] + 8) ? 3 : -3;
                e_dy[n] = (py > e_y[n] + 8) ? 2 : -2;
                if (phase2) { e_dx[n] += (e_dx[n] > 0) ? 1 : -1; }
                e_s[n] = 1;
            }
        } else if (e_s[n] == 1) {
            e_x[n] += e_dx[n];
            e_y[n] += e_dy[n];
            if (e_x[n] <= 16 || e_x[n] >= 208 || e_y[n] <= 8 || e_y[n] >= 136) {
                if (e_x[n] < 16) e_x[n] = 16;
                if (e_x[n] > 208) e_x[n] = 208;
                if (e_y[n] < 8) e_y[n] = 8;
                if (e_y[n] > 136) e_y[n] = 136;
                e_s[n] = 2;
                e_t2[n] = 50;
                sfx_req = SFX_ROCK;
            }
        } else {
            if (!--e_t2[n]) { e_s[n] = 0; e_t2[n] = phase2 ? 30 : 50; }
        }
        break;
    default: /* teleport behind the veil, change weakness each time */
        if (!--e_t2[n]) {
            if (e_s[n] == 0) {
                e_s[n] = 1;
                e_t2[n] = 24;
            } else {
                k = rand8() & 3;
                e_x[n] = tele_x[k];
                e_y[n] = tele_y[k];
                e_s[n] = 0;
                e_t2[n] = phase2 ? 110 : 160;
                boss_set_weak(1 + (rand8() & 3));
                boss_fire(n);
            }
        }
        break;
    }
    if (e_s[n] == 1 && boss_move[boss_id] == 3) return;
    if (--e_t[n] == 0) {
        e_t[n] = boss_rate[boss_id] - (phase2 ? 30 : 0);
        boss_fire(n);
    }
}

static void kill_enemy(unsigned char n) {
    unsigned char x, y, r, k;
    x = e_x[n]; y = e_y[n];
    sfx_req = SFX_KILL;
    if (is_boss(e_type[n])) {
        puff_spawn(x, y); puff_spawn(x + 16, y);
        puff_spawn(x, y + 16); puff_spawn(x + 16, y + 16);
        memset(s_on, 0, MAX_SHOT);
        e_type[n] = 0;
        pal_build();
        pal_fade(0);
        music_area();
    } else {
        puff_spawn(x, y);
        r = rand8();
        if (r < 64) pk_spawn(PK_HEART, x + 4, y + 4, 255);
        else if (r < 128) pk_spawn(PK_MANA, x + 4, y + 4, 255);
        e_type[n] = 0;
    }
    if (scr_cond[cur] == COND_CLEAR) {
        for (k = 0; k < MAX_EN; ++k) if (e_type[k]) return;
        room_solve();
    }
}

static void damage_enemy(unsigned char n, unsigned char dmg, unsigned char dir) {
    if (e_flash[n]) return;
    if (e_hp[n] > dmg) {
        e_hp[n] -= dmg;
        sfx_req = SFX_HIT;
        if (is_boss(e_type[n])) {
            e_flash[n] = 30;
            if (mana < 40) pk_spawn(PK_MANA, 116, 140, 255);
        } else {
            e_flash[n] = 16;
            if (e_type[n] == E_BLOB || e_type[n] == E_STALKER) { e_kb[n] = 8; e_kbdir[n] = dir; }
        }
    } else {
        kill_enemy(n);
    }
}

/* can the boss be hit by art a right now? */
static unsigned char boss_takes(unsigned char n, unsigned char a) {
    if (boss_id == 4) {
        if (!veil_on || e_s[n]) return 0;
    }
    return a == boss_weak;
}

static void update_enemies(void) {
    unsigned char n, ty;
    for (n = 0; n < MAX_EN; ++n) {
        ty = e_type[n];
        if (!ty) continue;
        if (e_flash[n]) --e_flash[n];
        if (e_kb[n]) {
            --e_kb[n];
            e_step(n, e_kbdir[n]);
            e_step(n, e_kbdir[n]);
        } else {
            switch (ty) {
            case E_BLOB:    blob_ai(n); break;
            case E_STALKER: if (veil_on) blob_ai(n); else stalker_ai(n); break;
            case E_SHADE:   shade_ai(n); break;
            case E_BAT:     bat_ai(n); break;
            case E_EYE:     eye_ai(n); break;
            default:        boss_ai(n); break;
            }
        }
        if (!pinv && !pdash) {
            ax_ = px + 3; ay_ = py + 4; aw_ = 10; ah_ = 12;
            if (hit_enemy(n)) {
                if (is_boss(ty)) {
                    if (!(boss_id == 4 && e_s[n])) hurt_player(2, e_x[n] + 16, e_y[n] + 16);
                } else hurt_player(1, e_x[n] + 8, e_y[n] + 8);
            }
        }
    }
}

/* ------------------------------------------------------------------ room logic */
static void open_doors(unsigned char redraw) {
    unsigned char n, m, opened;
    opened = 0;
    for (n = 0; n < 176; ++n) {
        m = map[n];
        if (m == MT_DOOR) { map[n] = MT_DOOROPEN; opened = 1; }
        else if (m == MT_SEALGATE) { map[n] = MT_STAIRS; opened = 1; }
        else continue;
        if (redraw) mq_push(n);
    }
    if (opened && redraw) sfx_req = SFX_DOOR;
}

static void room_solve(void) {
    if (flag_get(cur)) return;
    flag_set(cur);
    open_doors(1);
    spawn_item();
    if (scr_solvemsg[cur]) msg_start(scr_solvemsg[cur]);
}

static void check_braziers(void) {
    unsigned char n;
    if (scr_cond[cur] != COND_BRAZ) return;
    for (n = 0; n < 176; ++n) if (map[n] == MT_BRAZIER) return;
    room_solve();
}

/* ------------------------------------------------------------------ casting */
static void spawn_bolt(unsigned char type) {
    unsigned char n, sz;
    for (n = 0; n < MAX_BOLT; ++n) if (!b_on[n]) break;
    if (n == MAX_BOLT) return;
    sz = (type == BT_FIRE) ? 4 : 12;
    b_on[n] = 1;
    b_type[n] = type;
    b_dir[n] = pdir;
    switch (type) {
    case BT_FIRE:  b_life[n] = (fire_tier > 1) ? 34 : 32; break;
    case BT_EARTH: b_life[n] = 80; break;
    default:       b_life[n] = 22; break;
    }
    if (type == BT_FIRE) {
        switch (pdir) {
        case DIR_DOWN:  b_x[n] = px + 4; b_y[n] = py + 12; break;
        case DIR_UP:    b_x[n] = px + 4; b_y[n] = (py >= 4) ? py - 4 : 0; break;
        case DIR_LEFT:  b_x[n] = (px >= 4) ? px - 4 : 0; b_y[n] = py + 6; break;
        default:        b_x[n] = px + 12; b_y[n] = py + 6; break;
        }
    } else {
        switch (pdir) {
        case DIR_DOWN:  b_x[n] = px; b_y[n] = (py < 148) ? py + sz : 160; break;
        case DIR_UP:    b_x[n] = px; b_y[n] = (py >= sz) ? py - sz : 0; break;
        case DIR_LEFT:  b_x[n] = (px >= sz) ? px - sz : 0; b_y[n] = py; break;
        default:        b_x[n] = (px < 228) ? px + sz : 240; b_y[n] = py; break;
        }
    }
}

static unsigned char bolt_of(unsigned char t) {
    unsigned char n;
    for (n = 0; n < MAX_BOLT; ++n) if (b_on[n] && b_type[n] == t) return 1;
    return 0;
}

static void cast(void) {
    unsigned char cost;
    cost = art_cost[art_sel];
    if (art_sel == ART_FIRE && fire_tier > 1) cost = 6;
    if (art_sel == ART_VEIL) {
        if (veil_on) {
            if (veil_can_drop()) veil_set(0);
            else sfx_req = SFX_ERROR;
        } else if (mana >= cost) {
            mana -= cost;
            hud_dirty |= 2;
            veil_set(1);
        } else sfx_req = SFX_ERROR;
        return;
    }
    if (pcast || mana < cost) { sfx_req = SFX_ERROR; return; }
    switch (art_sel) {
    case ART_FIRE:
        spawn_bolt(BT_FIRE);
        sfx_req = SFX_FIRE;
        break;
    case ART_EARTH:
        if (bolt_of(BT_EARTH)) { sfx_req = SFX_ERROR; return; }
        spawn_bolt(BT_EARTH);
        sfx_req = SFX_ROCK;
        break;
    case ART_WATER:
        spawn_bolt(BT_WATER);
        sfx_req = SFX_WAVE;
        break;
    default: /* wind dash */
        pdash = 14;
        dash_ext = 0;
        dash_sx = px;
        dash_sy = py;
        sfx_req = SFX_DASH;
        break;
    }
    mana -= cost;
    hud_dirty |= 2;
    pcast = 12;
}

static void tile_set(unsigned char idx, unsigned char m) {
    map[idx] = m;
    mq_push(idx);
}

static void update_bolts(void) {
    unsigned char n, k, t, sz, spd, cx, cy, idx, f, f2, dmg, art, stop;
    for (n = 0; n < MAX_BOLT; ++n) {
        if (!b_on[n]) continue;
        t = b_type[n];
        sz = (t == BT_FIRE) ? 8 : 16;
        spd = (t == BT_FIRE) ? ((fire_tier > 1) ? 4 : 3) : (t == BT_EARTH ? 2 : 3);
        switch (b_dir[n]) {
        case DIR_DOWN:  if (b_y[n] + sz + spd > 176) { b_on[n] = 0; continue; } b_y[n] += spd; break;
        case DIR_UP:    if (b_y[n] < spd) { b_on[n] = 0; continue; } b_y[n] -= spd; break;
        case DIR_LEFT:  if (b_x[n] < spd) { b_on[n] = 0; continue; } b_x[n] -= spd; break;
        default:        if (b_x[n] + sz + spd > 255) { b_on[n] = 0; continue; } b_x[n] += spd; break;
        }
        cx = b_x[n] + (sz >> 1); cy = b_y[n] + (sz >> 1);
        idx = (cy & 0xF0) | (cx >> 4);
        f = mt_flags[map[idx]];
        f2 = mt_flags2[map[idx]];
        stop = 0;
        if (t == BT_FIRE) {
            if (f & MF_BURN) {
                tile_set(idx, (area == AREA_OW) ? MT_GRASS : MT_FLOOR);
                puff_spawn(cx & 0xF0, cy & 0xF0);
                sfx_req = SFX_BURN;
                stop = 1;
            } else if (f & MF_BRAZ) {
                tile_set(idx, MT_BRAZLIT);
                sfx_req = SFX_BURN;
                b_on[n] = 0;
                check_braziers();
                continue;
            }
            art = ART_FIRE;
            dmg = fire_tier;
        } else if (t == BT_EARTH) {
            if (f2 & MF2_CRACK) {
                tile_set(idx, (area == AREA_OW) ? MT_GRASS : MT_FLOOR);
                puff_spawn(cx & 0xF0, cy & 0xF0);
                sfx_req = SFX_ROCK;
                stop = 1;
            } else if (f2 & MF2_FILL) {
                tile_set(idx, MT_FILLED);
                sfx_req = SFX_ROCK;
                stop = 1;
            } else if ((f & MF_SOLID) && !((f & MF_VEIL) && veil_on)) {
                puff_spawn(b_x[n], b_y[n]);
                stop = 1;
            }
            art = ART_EARTH;
            dmg = 2;
        } else {
            if (f2 & MF2_FLAME) {
                tile_set(idx, MT_FLOOR);
                puff_spawn(cx & 0xF0, cy & 0xF0);
                sfx_req = SFX_WAVE;
                stop = 1;
            }
            art = ART_WATER;
            dmg = 2;
        }
        if (!stop && (f & MF_SOLID) && !(f & MF_LOW) && !((f & MF_VEIL) && veil_on)) stop = 1;
        if (stop) { b_on[n] = 0; continue; }
        ax_ = b_x[n]; ay_ = b_y[n]; aw_ = sz; ah_ = sz;
        for (k = 0; k < MAX_EN; ++k) {
            if (!e_type[k]) continue;
            if (e_type[k] == E_SHADE && !veil_on) continue;
            if (!hit_enemy(k)) continue;
            if (is_boss(e_type[k])) {
                if (boss_id == 4 && (!veil_on || e_s[k])) continue;
                if (boss_takes(k, art)) damage_enemy(k, dmg, b_dir[n]);
                else sfx_req = SFX_TINK;
            } else {
                damage_enemy(k, dmg, b_dir[n]);
            }
            b_on[n] = 0;
            break;
        }
        if (b_on[n] && !--b_life[n]) b_on[n] = 0;
    }
}

/* ------------------------------------------------------------------ staff */
static void staff_box(void) {
    switch (pdir) {
    case DIR_RIGHT: ax_ = px + 13; ay_ = py + 5; aw_ = 14; ah_ = 8; break;
    case DIR_LEFT:  ax_ = (px >= 11) ? px - 11 : 0; ay_ = py + 5; aw_ = 14; ah_ = 8; break;
    case DIR_UP:    ax_ = px + 4; ay_ = (py >= 12) ? py - 12 : 0; aw_ = 8; ah_ = 14; break;
    default:        ax_ = px + 4; ay_ = py + 14; aw_ = 8; ah_ = 14; break;
    }
}

static void staff_hits(void) {
    unsigned char n;
    if (patk < 3 || patk > 12) return;
    staff_box();
    for (n = 0; n < MAX_EN; ++n) {
        if (!e_type[n]) continue;
        if (e_type[n] == E_SHADE && !veil_on) continue;
        if (!hit_enemy(n)) continue;
        if (is_boss(e_type[n])) {
            if (patk == 10) sfx_req = SFX_TINK;
            continue;
        }
        damage_enemy(n, 1, pdir);
    }
}

/* ------------------------------------------------------------------ wind dash */
static void dash_update(void) {
    unsigned char k, nx, ny, n;
    dash_blocked = 0;
    for (k = 0; k < 4; ++k) {
        nx = px; ny = py;
        switch (pdir) {
        case DIR_DOWN:  if (py >= 160) dash_blocked = 1; else ++ny; break;
        case DIR_UP:    if (py == 0) dash_blocked = 1; else --ny; break;
        case DIR_LEFT:  if (px == 0) dash_blocked = 1; else --nx; break;
        default:        if (px >= 240) dash_blocked = 1; else ++nx; break;
        }
        if (dash_blocked || !d_free(nx, ny)) { dash_blocked = 1; break; }
        px = nx; py = ny;
    }
    ax_ = px + 2; ay_ = py + 2; aw_ = 12; ah_ = 13;
    for (n = 0; n < MAX_EN; ++n) {
        if (!e_type[n] || !hit_enemy(n)) continue;
        if (e_type[n] == E_SHADE && !veil_on) continue;
        if (is_boss(e_type[n])) {
            if (boss_takes(n, ART_WIND)) damage_enemy(n, 1, pdir);
            else if (!e_flash[n]) { sfx_req = SFX_TINK; e_flash[n] = 1; }
        } else damage_enemy(n, 1, pdir);
    }
    if (dash_blocked) pdash = 1;
    if (--pdash) return;
    if (p_free(px, py)) return;
    /* still over a pit: keep flying if possible, otherwise fall back to the start */
    if (!dash_blocked && dash_ext < 12) { ++dash_ext; pdash = 1; return; }
    px = dash_sx;
    py = dash_sy;
    if (php > 1) --php; else php = 1;
    hud_dirty |= 1;
    pinv = 60;
    sfx_req = SFX_HURT;
}

/* ------------------------------------------------------------------ enemy shots, pickups, puffs */
static void update_shots(void) {
    unsigned char n;
    for (n = 0; n < MAX_SHOT; ++n) {
        if (!s_on[n]) continue;
        s_x[n] += s_dx[n];
        s_y[n] += s_dy[n];
        if (s_x[n] > 248 || s_y[n] > 168) { s_on[n] = 0; continue; }
        if (!pinv && !pdash && hit_player(s_x[n] + 1, s_y[n] + 1, 6, 6)) {
            hurt_player(1, s_x[n] + 4, s_y[n] + 4);
            s_on[n] = 0;
        }
    }
}

static void learn(unsigned char a) {
    arts |= ABIT(a);
    art_sel = a;
    pal_build();
    pal_fade(0);
}

static void grow_life(void) {
    if (pmaxhp < 16) pmaxhp += 2;
    php = pmaxhp;
}

static void get_item(unsigned char it) {
    unsigned char m;
    flag_set(F_TAKEN + cur);
    sfx_req = SFX_ITEM;
    m = 0;
    switch (it) {
    case IT_TOME:       learn(ART_FIRE);  m = MSG_TOME; break;
    case IT_TOME_EARTH: learn(ART_EARTH); m = MSG_TOME_EARTH; break;
    case IT_TOME_WIND:  learn(ART_WIND);  m = MSG_TOME_WIND; break;
    case IT_TOME_WATER: learn(ART_WATER); m = MSG_TOME_WATER; break;
    case IT_SIGIL:      fire_tier = 2;    m = MSG_SIGIL; break;
    case IT_VSIGIL:     veil_tier = 2;    m = MSG_VSIGIL; break;
    case IT_HEART:      grow_life();      m = MSG_HEART; break;
    case IT_SEAL:       seals |= ABIT(ART_FIRE);  grow_life(); m = MSG_SEAL; break;
    case IT_SEAL_EARTH: seals |= ABIT(ART_EARTH); grow_life(); m = MSG_SEAL_EARTH; break;
    case IT_SEAL_WIND:  seals |= ABIT(ART_WIND);  grow_life(); m = MSG_SEAL_WIND; break;
    case IT_SEAL_WATER: seals |= ABIT(ART_WATER); grow_life(); m = MSG_SEAL_WATER; break;
    case IT_SEAL_VEIL:  m = MSG_FINAL; end_after_msg = 1; break;
    }
    save_game();
    msg_start(m);
}

static void update_pickups(void) {
    unsigned char n, sz;
    for (n = 0; n < MAX_PK; ++n) {
        if (!k_type[n]) continue;
        if (k_t[n] && (frame_count & 1)) {
            if (!--k_t[n]) { k_type[n] = 0; continue; }
        }
        sz = (k_type[n] == PK_ITEM) ? 16 : 8;
        if (!hit_player(k_x[n], k_y[n], sz, sz)) continue;
        switch (k_type[n]) {
        case PK_HEART:
            php += 2;
            if (php > pmaxhp) php = pmaxhp;
            hud_dirty |= 1;
            sfx_req = SFX_PICKUP;
            break;
        case PK_MANA:
            mana += 16;
            if (mana > MANA_MAX) mana = MANA_MAX;
            hud_dirty |= 2;
            sfx_req = SFX_PICKUP;
            break;
        default:
            k_type[n] = 0;
            get_item(k_item[n]);
            return;
        }
        k_type[n] = 0;
    }
}

static void update_puffs(void) {
    unsigned char n;
    for (n = 0; n < MAX_PUFF; ++n) if (f_t[n]) --f_t[n];
}

/* ------------------------------------------------------------------ messages */
static void msg_start(unsigned char id) {
    if (!id) return;
    cp_src = (const unsigned char*)msg_text[id];
    cp_dst = (unsigned char*)msgbuf;
    cp_len = 127;
    bank_copy(WORLD_BANK);
    msgbuf[127] = 0;
    msg_p = msgbuf;
    msg_row = 0;
    msg_wait = 0;
    state = ST_MSG;
}

static void update_msg(void) {
    unsigned char c;
    if (msg_row < 4) {
        row_clear();
        c = 2;
        while (*msg_p && *msg_p != '\n') rowbuf[c++] = *msg_p++;
        if (*msg_p == '\n') ++msg_p;
        vb_row(2 + msg_row);
        ++msg_row;
    } else if (msg_row == 4) {
        if (msg_wait < 255) ++msg_wait;
        if (msg_wait > 24 && (pad_new & (PAD_A | PAD_B | PAD_START))) {
            msg_row = 5;
            sfx_req = SFX_BLIP;
        }
    } else {
        hud_row(msg_row - 3);
        vb_row(msg_row - 3);
        ++msg_row;
        if (msg_row == 9) {
            hud_dirty = 0;
            state = end_after_msg ? ST_END : ST_PLAY;
        }
    }
}

#pragma code-name(pop)

/* ------------------------------------------------------------------ player (fixed bank from here on) */
static void edge_exit(unsigned char ns, unsigned char nx, unsigned char ny) {
    if (ns == 255) return;
    enter_screen(ns, nx, ny, 1);
}

static void p_try(unsigned char d) {
    unsigned char nx, ny, a, b, t;
    nx = px; ny = py;
    pdir = d;
    switch (d) {
    case DIR_UP:
        if (py == 0) { edge_exit(scr_up[cur], px, 160); return; }
        ny = py - 1;
        break;
    case DIR_DOWN:
        if (py >= 160) { edge_exit(scr_down[cur], px, 0); return; }
        ny = py + 1;
        break;
    case DIR_LEFT:
        if (px == 0) { edge_exit(scr_left[cur], 240, py); return; }
        nx = px - 1;
        break;
    default:
        if (px >= 240) { edge_exit(scr_right[cur], 0, py); return; }
        nx = px + 1;
        break;
    }
    if (p_free(nx, ny)) { px = nx; py = ny; pmoving = 1; return; }
    if (d == DIR_UP || d == DIR_DOWN) {
        t = (d == DIR_UP) ? ny + 8 : ny + 15;
        a = psolid(px + 3, t);
        b = psolid(px + 12, t);
        check_sign(px + 3, t);
        check_sign(px + 12, t);
        if (a && !b && ((px + 3) & 15) >= 10) { if (p_free(px + 1, py)) { ++px; pmoving = 1; } }
        else if (b && !a && ((px + 12) & 15) <= 5) { if (p_free(px - 1, py)) { --px; pmoving = 1; } }
    } else {
        t = (d == DIR_LEFT) ? nx + 3 : nx + 12;
        a = psolid(t, py + 8);
        b = psolid(t, py + 15);
        check_sign(t, py + 8);
        check_sign(t, py + 15);
        if (a && !b && ((py + 8) & 15) >= 10) { if (p_free(px, py + 1)) { ++py; pmoving = 1; } }
        else if (b && !a && ((py + 15) & 15) <= 5) { if (p_free(px, py - 1)) { --py; pmoving = 1; } }
    }
}

static void p_knock(void) {
    unsigned char k, nx, ny;
    for (k = 0; k < 2; ++k) {
        nx = px; ny = py;
        switch (pkbdir) {
        case DIR_DOWN:  if (py < 160) ++ny; break;
        case DIR_UP:    if (py > 0) --ny; break;
        case DIR_LEFT:  if (px > 0) --nx; break;
        default:        if (px < 240) ++nx; break;
        }
        if (p_free(nx, ny)) { px = nx; py = ny; }
    }
}

static void pause_toggle(void) {
    paused ^= 1;
    sfx_req = SFX_SELECT;
    if (paused) { row_clear(); row_text(2, "- PAUSED -"); }
    else hud_row(5);
    vb_row(5);
}

static void next_art(void) {
    unsigned char k, a;
    a = art_sel;
    for (k = 0; k < 5; ++k) {
        a = (a == 4) ? 0 : a + 1;
        if (arts & ABIT(a)) break;
    }
    if (a == art_sel) return;
    art_sel = a;
    pal_build();
    pal_fade(0);
    hud_dirty |= 4;
    sfx_req = SFX_SELECT;
}

static void update_play(void) {
    unsigned char n, m;
    scr_changed = 0;
    if (pad_new & PAD_START) { pause_toggle(); return; }
    if (paused) return;

    if (pinv) --pinv;
    if (pcast) --pcast;

    if (veil_on) {
        if (!(frame_count & (veil_tier > 1 ? 15 : 7)) && mana) { --mana; hud_dirty |= 2; }
        if (!mana && veil_can_drop()) veil_set(0);
    } else if (!(frame_count & 31) && mana < MANA_MAX) {
        ++mana;
        hud_dirty |= 2;
    }

    if (pad_new & PAD_SELECT) next_art();

    sign_touch = 0;
    pmoving = 0;
    if (pdash) {
        dash_update();
    } else if (pkb) {
        --pkb;
        p_knock();
    } else if (patk) {
        --patk;
        staff_hits();
    } else if (pad_new & PAD_B) {
        patk = 14;
        sfx_req = SFX_SWING;
    } else if (pad_new & PAD_A) {
        cast();
    } else {
        n = ((frame_count & 3) == 0) ? 2 : 1;
        while (n) {
            --n;
            if (pad & PAD_UP) p_try(DIR_UP);
            else if (pad & PAD_DOWN) p_try(DIR_DOWN);
            else if (pad & PAD_LEFT) p_try(DIR_LEFT);
            else if (pad & PAD_RIGHT) p_try(DIR_RIGHT);
            if (scr_changed) return;
        }
        if (pmoving) ++panim;
    }

    if (sign_touch) {
        if (!sign_latch && scr_msg[cur]) {
            sign_latch = 1;
            msg_start(scr_msg[cur]);
            return;
        }
    } else sign_latch = 0;

    m = map[((py + 12) & 0xF0) | ((px + 8) >> 4)];
    if ((mt_flags[m] & MF_WARP) && scr_warp_s[cur] != 255) {
        pdash = 0;
        enter_screen(scr_warp_s[cur], scr_warp_x[cur], scr_warp_y[cur], 1);
        return;
    }

    update_bolts();
    if (state != ST_PLAY) return;
    update_enemies();
    if (state != ST_PLAY) return;
    update_shots();
    update_pickups();
    update_puffs();
}

/* ------------------------------------------------------------------ sprites */
static void draw_player(void) {
    unsigned char base, at, sx, sy;
    if (pinv && !pdash && (frame_count & 2)) return;
    if (veil_on && (frame_count & 1)) return;
    at = 0;
    switch (pdir) {
    case DIR_DOWN: base = SPR_P_DOWN0; break;
    case DIR_UP:   base = SPR_P_UP0; break;
    case DIR_LEFT: base = SPR_P_RIGHT0; at = 0x40; break;
    default:       base = SPR_P_RIGHT0; break;
    }
    if ((panim & 8) || (pdash && (frame_count & 2))) base += 4;
    spr_x = px; spr_y = py + PF_Y; spr_tile = base; spr_attr = at;
    oam_meta16();
    if (pdash) {
        /* wind trail */
        spr_tile = SPR_GUST0 + (frame_count & 1);
        spr_attr = 2;
        spr_x = (unsigned char)(dash_sx + px) >> 1;
        spr_x = ((unsigned int)dash_sx + px) >> 1;
        spr_y = ((((unsigned int)dash_sy + py) >> 1)) + PF_Y + 4;
        spr_x += 4;
        oam_spr();
        return;
    }
    if (!patk) return;
    sy = py + PF_Y;
    switch (pdir) {
    case DIR_RIGHT:
        spr_y = sy + 5; spr_attr = 0;
        spr_x = px + 12; spr_tile = SPR_STAFF_H; oam_spr();
        spr_x = px + 20; spr_tile = SPR_STAFF_H + 1; oam_spr();
        break;
    case DIR_LEFT:
        if (px < 12) break;
        sx = px - 12;
        spr_y = sy + 5; spr_attr = 0x40;
        spr_x = sx; spr_tile = SPR_STAFF_H + 1; oam_spr();
        spr_x = sx + 8; spr_tile = SPR_STAFF_H; oam_spr();
        break;
    case DIR_UP:
        spr_x = px + 4; spr_attr = 0;
        spr_y = sy - 12; spr_tile = SPR_STAFF_V; oam_spr();
        spr_y = sy - 4; spr_tile = SPR_STAFF_V + 1; oam_spr();
        break;
    default:
        spr_x = px + 4; spr_attr = 0x80;
        spr_y = sy + 12; spr_tile = SPR_STAFF_V + 1; oam_spr();
        spr_y = sy + 20; spr_tile = SPR_STAFF_V; oam_spr();
        break;
    }
}

static void draw_enemy(unsigned char n) {
    unsigned char r, c, base;
    if (!e_type[n]) return;
    if (e_flash[n] && (frame_count & 2)) return;
    spr_x = e_x[n];
    spr_y = e_y[n] + PF_Y;
    switch (e_type[n]) {
    case E_BLOB:
        spr_tile = SPR_BLOB0 + ((frame_count & 16) ? 4 : 0);
        spr_attr = 1;
        break;
    case E_STALKER:
        spr_tile = SPR_STALK0 + ((frame_count & 8) ? 4 : 0);
        spr_attr = 2 | ((e_dir[n] == DIR_LEFT) ? 0x40 : 0);
        break;
    case E_SHADE:
        if (!veil_on && (frame_count & 15)) return;
        spr_tile = SPR_SHADE0 + ((frame_count & 16) ? 4 : 0);
        spr_attr = 2;
        break;
    case E_BAT:
        spr_tile = SPR_BAT0 + ((frame_count & 4) ? 4 : 0);
        spr_attr = 1;
        break;
    case E_EYE:
        spr_tile = (e_t[n] < 40) ? SPR_EYE0 : SPR_EYE1;
        spr_attr = 2;
        break;
    default:
        if (boss_id == 4) {
            if (e_s[n]) return;
            if (!veil_on && (frame_count & 7)) return;
        }
        base = SPR_BOSS + ((frame_count & 8) ? 16 : 0);
        spr_attr = 2;
        for (r = 0; r < 4; ++r) {
            spr_y = e_y[n] + PF_Y + (r << 3);
            for (c = 0; c < 4; ++c) {
                spr_x = e_x[n] + (c << 3);
                spr_tile = base + (r << 2) + c;
                oam_spr();
            }
        }
        return;
    }
    oam_meta16();
}

static void draw_bolt(unsigned char n) {
    spr_x = b_x[n];
    spr_y = b_y[n] + PF_Y;
    switch (b_type[n]) {
    case BT_FIRE:
        spr_tile = SPR_BOLT0 + ((frame_count & 4) ? 1 : 0);
        spr_attr = 3;
        oam_spr();
        return;
    case BT_EARTH:
        spr_tile = SPR_BOULDER;
        spr_attr = 2 | ((frame_count & 8) ? 0x40 : 0);
        break;
    default:
        if (b_dir[n] >= DIR_LEFT) {
            spr_tile = SPR_WAVE_H;
            spr_attr = 1 | ((b_dir[n] == DIR_LEFT) ? 0x40 : 0);
        } else {
            spr_tile = SPR_WAVE_V;
            spr_attr = 1 | ((b_dir[n] == DIR_DOWN) ? 0x80 : 0);
        }
        break;
    }
    oam_meta16();
}

static void draw_sprites(void) {
    unsigned char n, k;
    oam_begin();
    if (state != ST_DEAD) draw_player();
    for (n = 0; n < MAX_BOLT; ++n) if (b_on[n]) draw_bolt(n);
    if (++rot >= MAX_EN) rot = 0;
    for (k = 0; k < MAX_EN; ++k) {
        n = rot + k;
        if (n >= MAX_EN) n -= MAX_EN;
        draw_enemy(n);
    }
    for (n = 0; n < MAX_SHOT; ++n) {
        if (!s_on[n]) continue;
        spr_x = s_x[n]; spr_y = s_y[n] + PF_Y;
        spr_tile = SPR_GUST0 + ((frame_count & 4) ? 1 : 0);
        spr_attr = (boss_id == 3) ? 3 : 2;
        oam_spr();
    }
    for (n = 0; n < MAX_PK; ++n) {
        if (!k_type[n]) continue;
        if (k_t[n] && k_t[n] < 50 && (frame_count & 2)) continue;
        spr_x = k_x[n]; spr_y = k_y[n] + PF_Y;
        switch (k_type[n]) {
        case PK_HEART: spr_tile = SPR_HEART; spr_attr = 3; oam_spr(); break;
        case PK_MANA:  spr_tile = SPR_MANA; spr_attr = 2; oam_spr(); break;
        default:
            spr_tile = item_tile[k_item[n]];
            spr_attr = item_attr[k_item[n]];
            if (frame_count & 32) --spr_y;
            oam_meta16();
            break;
        }
    }
    for (n = 0; n < MAX_PUFF; ++n) {
        if (!f_t[n]) continue;
        spr_x = f_x[n]; spr_y = f_y[n] + PF_Y;
        spr_tile = (f_t[n] > 8) ? SPR_PUFF0 : SPR_PUFF1;
        spr_attr = 2;
        oam_meta16();
    }
    oam_finish();
}

/* ------------------------------------------------------------------ screens */
static void load_screen(unsigned char s) {
    unsigned char n, k, t, solved, c;
    cur = s;
    area = scr_area[s];
    palset = scr_pal[s];
    cp_src = scr_maps[s];
    cp_dst = map;
    cp_len = 176;
    bank_copy(WORLD_BANK);
    c = scr_cond[s];
    solved = flag_get(s);
    if (c == COND_FLAG && flag_get(scr_condarg[s])) solved = 1;
    if (c == COND_SEALS && (seals & ALL_SEALS) == ALL_SEALS) solved = 1;
    if (solved) {
        open_doors(0);
        for (n = 0; n < 176; ++n) if (map[n] == MT_BRAZIER) map[n] = MT_BRAZLIT;
    }
    memset(e_type, 0, MAX_EN);
    memset(b_on, 0, MAX_BOLT);
    memset(s_on, 0, MAX_SHOT);
    memset(k_type, 0, MAX_PK);
    memset(f_t, 0, MAX_PUFF);
    mq_n = 0;
    boss_id = NO_BOSS;
    cp_src = scr_enemies[s];
    cp_dst = enbuf;
    cp_len = 32;
    bank_copy(WORLD_BANK);
    n = 0;
    for (k = 0; enbuf[k] && n < MAX_EN && k < 30; k += 3) {
        t = enbuf[k];
        if (is_boss(t)) {
            if (flag_get(s)) continue;
            boss_id = t - E_BOSS_GALE;
            boss_slot = n;
            boss_weak = boss_weakt[boss_id];
            cp_dst = (unsigned char*)(0x1000 + SPR_BOSS * 16);
            upload_boss(boss_id);
            e_hp[n] = boss_hp[boss_id];
            e_dx[n] = 1; e_dy[n] = 1;
            e_t[n] = 120;
            e_t2[n] = 60;
        } else {
            e_hp[n] = enemy_hp[t];
            e_t[n] = (n << 3) + 1 + (t == E_EYE ? 40 : 0);
            e_t2[n] = 0;
        }
        e_type[n] = t;
        e_x[n] = enbuf[k + 1];
        e_y[n] = enbuf[k + 2];
        e_dir[n] = rand8() & 3;
        e_s[n] = 0;
        e_flash[n] = 0;
        e_kb[n] = 0;
        ++n;
    }
    spawn_item();
}


/* ------------------------------------------------------------------ fixed-bank code: palette, drawing, flow */
static void pal_build(void) {
    const unsigned char* p;
    unsigned char n;
    p = palsets + (unsigned int)palset * 20;
    for (n = 0; n < 16; ++n) base_pal[n] = p[n];
    if (veil_on) {
        for (n = 0; n < 4; ++n) base_pal[12 + n] = p[16 + n];
    }
    for (n = 0; n < 16; ++n) base_pal[16 + n] = spr_pals[n];
    base_pal[17] = art_robe[art_sel];
    if (boss_id != NO_BOSS && e_type[boss_slot]) {
        for (n = 0; n < 4; ++n) base_pal[24 + n] = boss_pals[(boss_id << 2) + n];
        if (boss_id == 4) base_pal[25] = art_robe[boss_weak];
    }
}

static void pal_fade(unsigned char lvl) {
    unsigned char n, c, sub;
    sub = lvl << 4;
    for (n = 0; n < 32; ++n) {
        c = base_pal[n];
        if ((c & 0x0F) >= 0x0D || c < sub) c = 0x0F;
        else c -= sub;
        pal_buf[n] = c;
    }
    pal_dirty = 1;
}

static void fade_out(void) {
    unsigned char l;
    for (l = 1; l < 5; ++l) { pal_fade(l); wait_frames(3); }
}

static void fade_in(void) {
    unsigned char l;
    l = 4;
    while (l) { --l; pal_fade(l); wait_frames(3); }
}

static void put_text(unsigned char col, unsigned char row, const char* s) {
    vram_adr(0x2000 + ((unsigned int)row << 5) + col);
    while (*s) vram_put(*s++);
}

static void draw_screen_full(void) {
    unsigned char r, c, m, my, a, sh;
    vram_adr(0x2000);
    for (r = 0; r < 8; ++r) {
        hud_row(r);
        for (c = 0; c < 32; ++c) vram_put(rowbuf[c]);
    }
    memset(attr, 0, 64);
    for (my = 0; my < 11; ++my) {
        for (c = 0; c < 16; ++c) {
            m = map[(my << 4) + c];
            vram_put(mt_tl[m]);
            vram_put(mt_tr[m]);
            a = ((2 + (my >> 1)) << 3) + (c >> 1);
            sh = ((my & 1) ? 4 : 0) + ((c & 1) ? 2 : 0);
            attr[a] |= mt_pal[m] << sh;
        }
        for (c = 0; c < 16; ++c) {
            m = map[(my << 4) + c];
            vram_put(mt_bl[m]);
            vram_put(mt_br[m]);
        }
    }
    if (state == ST_TITLE) memset(attr, 0xAA, 16);
    for (c = 0; c < 64; ++c) vram_put(attr[c]);
}

static void clear_screen(void) {
    vram_adr(0x2000);
    spr_tile = 0;
    vram_fill(1024);
    oam_begin();
    oam_finish();
}

static void enter_screen(unsigned char s, unsigned char x, unsigned char y, unsigned char fade) {
    if (fade) fade_out();
    ppu_off();
    load_screen(s);
    px = x;
    py = y;
    pkb = 0;
    patk = 0;
    pdash = 0;
    draw_screen_full();
    pal_build();
    pal_fade(4);
    draw_sprites();
    ppu_on();
    music_area();
    if (scr_mflags[s] & 2) {
        cp_scr = s;
        cp_x = scr_shrine_x[s];
        cp_y = scr_shrine_y[s];
        save_game();
    }
    fade_in();
    hud_dirty = 0;
    scr_changed = 1;
    sign_latch = 1;
    if (scr_cond[s] == COND_SEALS && (seals & ALL_SEALS) == ALL_SEALS && !flag_get(s)) {
        room_solve();
        save_game();
    } else if ((scr_mflags[s] & 1) && !flag_get(F_SEEN + s)) {
        flag_set(F_SEEN + s);
        msg_start(scr_msg[s]);
    }
}

static void wait_start(void) {
    while (1) {
        ppu_wait_nmi();
        pad_poll();
        if (pad_new & (PAD_START | PAD_A)) return;
    }
}

static void text_screen(const char* l1, const char* l2, const char* l3, const char* l4, unsigned char song) {
    unsigned char c;
    fade_out();
    ppu_off();
    clear_screen();
    c = 16 - (strlen(l1) >> 1);
    put_text(c, 9, l1);
    c = 16 - (strlen(l2) >> 1);
    put_text(c, 12, l2);
    c = 16 - (strlen(l3) >> 1);
    put_text(c, 15, l3);
    c = 16 - (strlen(l4) >> 1);
    put_text(c, 20, l4);
    palset = PAL_OW_ROCK;
    veil_on = 0;
    boss_id = NO_BOSS;
    pal_build();
    pal_fade(4);
    ppu_on();
    if (song != cur_song) { cur_song = song; mus_req = song; }
    fade_in();
    wait_start();
    sfx_req = SFX_SELECT;
    fade_out();
}

static void do_death(void) {
    mus_req = 0xFE;
    cur_song = 255;
    wait_frames(40);
    text_screen("YOU HAVE FALLEN...", "THE VEIL GROWS THIN,", "BUT NOT YET CLOSED.", "PRESS START", 0xFE);
    php = pmaxhp;
    mana = MANA_MAX;
    veil_on = 0;
    pinv = 0;
    pcast = 0;
    pdash = 0;
    paused = 0;
    state = ST_PLAY;
    cur_song = 255;
    enter_screen(cp_scr, cp_x, cp_y, 0);
}

static void do_ending(void) {
    wait_frames(30);
    text_screen("THE UNWEAVER IS UNDONE.", "FIRE, EARTH, WIND, WATER", "AND VEIL ARE FREE AGAIN.", "PRESS START", SONG_ENDING);
    text_screen("THE FIVE SEALS", "A TALE OF FIVE ARTS", "THE END", "THANK YOU FOR PLAYING", SONG_ENDING);
}

/* returns 1 = continue saved game */
static unsigned char do_title(void) {
    unsigned char has, sel, old;
    state = ST_TITLE;
    veil_on = 0;
    art_sel = ART_VEIL;
    ppu_off();
    load_screen(SCR_TITLE);
    draw_screen_full();
    has = save_valid();
    if (has) {
        put_text(12, 17, "CONTINUE");
        put_text(12, 19, "NEW GAME");
    } else {
        put_text(11, 18, "PRESS START");
    }
    put_text(4, 23, "A:CAST B:STAFF SELECT:ART");
    oam_begin();
    oam_finish();
    pal_build();
    pal_fade(4);
    ppu_on();
    cur_song = SONG_OVERWORLD;
    mus_req = SONG_OVERWORLD;
    fade_in();
    sel = 0;
    old = 255;
    while (1) {
        if (has && sel != old) {
            vb_hdr(0x2000 + 17 * 32 + 10, 1); VB(sel == 0 ? '>' : ' ');
            vb_hdr(0x2000 + 19 * 32 + 10, 1); VB(sel == 1 ? '>' : ' ');
            old = sel;
        }
        ppu_wait_nmi();
        pad_poll();
        if (has && (pad_new & (PAD_UP | PAD_DOWN | PAD_SELECT))) { sel ^= 1; sfx_req = SFX_BLIP; }
        if (pad_new & (PAD_START | PAD_A)) break;
        rand8();
    }
    sfx_req = SFX_SELECT;
    fade_out();
    return has && sel == 0;
}

/* ------------------------------------------------------------------ main */
void main(void) {
    ppu_off();
    upload_chr();
    while (1) {
        if (do_title()) load_game();
        else new_game();
        state = ST_PLAY;
        cur_song = 255;
        veil_on = 0;
        pinv = 0; pkb = 0; patk = 0; pcast = 0; pdash = 0;
        paused = 0;
        end_after_msg = 0;
        enter_screen(cp_scr, cp_x, cp_y, 0);
        while (1) {
            ppu_wait_nmi();
            if ((unsigned char)(frame_count - last_frame) > 1 && state == ST_PLAY) ++lag_frames;
            last_frame = frame_count;
            pad_poll();
            if (state == ST_PLAY) update_play();
            else if (state == ST_MSG) update_msg();
            if (state == ST_DEAD) { do_death(); continue; }
            if (state == ST_END) { do_ending(); break; }
            mq_flush();
            if (state == ST_PLAY) hud_update();
            draw_sprites();
        }
    }
}

/*
 * THE FIVE SEALS - Book I: Ember and Veil
 * Top-down action adventure for the NES (MMC1, cc65).
 */
#include <string.h>
#include "lib.h"
#include "gen_data.h"

/* ------------------------------------------------------------------ constants */
#define PF_Y      63          /* sprite Y offset of playfield (64 - 1) */
#define MANA_MAX  64

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
#define MF_WATER  128

#define ART_VEIL  0
#define ART_FIRE  1
#define HAS_VEIL  1
#define HAS_FIRE  2

#define E_BLOB    1
#define E_STALKER 2
#define E_SHADE   3
#define E_BOSS    4

#define IT_TOME   1
#define IT_SIGIL  2
#define IT_HEART  3
#define IT_SEAL   4

#define PK_HEART  1
#define PK_MANA   2
#define PK_ITEM   3

#define COND_NONE  0
#define COND_CLEAR 1
#define COND_BRAZ  2
#define COND_FLAG  3

#define AREA_OW     0
#define AREA_TEMPLE 1

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

#define F_SEEN   32
#define F_TAKEN  64

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
unsigned char sv[32];
#pragma bss-name(pop)

/* ------------------------------------------------------------------ state */
static unsigned char map[176];
static unsigned char flags[12];
static unsigned char attr[64];
static unsigned char rowbuf[32];
static unsigned char base_pal[32];

static unsigned char php, pmaxhp, mana, arts, art_sel, fire_tier;
static unsigned char pinv, pkb, pkbdir, patk, pcast, panim, pmoving;
static unsigned char cp_scr, cp_x, cp_y;
static unsigned char state, area, cur_song, palset;
static unsigned char hud_dirty, sign_latch, sign_touch, scr_changed;
static unsigned char end_after_msg, paused, rot;
unsigned char lag_frames, last_frame;   /* debug: frames where logic overran vblank */

static unsigned char e_type[MAX_EN], e_x[MAX_EN], e_y[MAX_EN], e_dir[MAX_EN];
static unsigned char e_hp[MAX_EN], e_t[MAX_EN], e_flash[MAX_EN], e_kb[MAX_EN], e_kbdir[MAX_EN];
static signed char e_dx[MAX_EN], e_dy[MAX_EN];

static unsigned char b_on[MAX_BOLT], b_x[MAX_BOLT], b_y[MAX_BOLT], b_dir[MAX_BOLT], b_life[MAX_BOLT];
static unsigned char s_on[MAX_SHOT], s_x[MAX_SHOT], s_y[MAX_SHOT];
static signed char s_dx[MAX_SHOT], s_dy[MAX_SHOT];
static unsigned char k_type[MAX_PK], k_x[MAX_PK], k_y[MAX_PK], k_t[MAX_PK], k_item[MAX_PK];
static unsigned char f_t[MAX_PUFF], f_x[MAX_PUFF], f_y[MAX_PUFF];

static unsigned char mq[8], mq_n;
static const char* msg_p;
static unsigned char msg_row, msg_wait;

static const unsigned char bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const unsigned char enemy_hp[5] = {0, 2, 3, 2, 10};
static const signed char dir8_x[8] = {2, 1, 0, -1, -2, -1, 0, 1};
static const signed char dir8_y[8] = {0, 1, 2, 1, 0, -1, -2, -1};
static const char* const area_names[3] = {"ASHEN VALE", "EMBER TEMPLE", "TRIAL CAVE"};

/* ------------------------------------------------------------------ prototypes */
static void enter_screen(unsigned char s, unsigned char x, unsigned char y, unsigned char fade);
static void msg_start(unsigned char id);
static void room_solve(void);

/* ------------------------------------------------------------------ small helpers */
static unsigned char flag_get(unsigned char n) { return flags[n >> 3] & bits[n & 7]; }
static void flag_set(unsigned char n) { flags[n >> 3] |= bits[n & 7]; }

static void wait_frames(unsigned char n) {
    while (n) { ppu_wait_nmi(); --n; }
}

/* ------------------------------------------------------------------ palette */
static void pal_build(void) {
    const unsigned char* p;
    unsigned char n;
    p = palsets + (unsigned int)palset * 20;
    for (n = 0; n < 16; ++n) base_pal[n] = p[n];
    if (veil_on) {
        for (n = 0; n < 4; ++n) base_pal[12 + n] = p[16 + n];
    }
    for (n = 0; n < 16; ++n) base_pal[16 + n] = spr_pals[n];
    base_pal[17] = (art_sel == ART_FIRE) ? 0x16 : 0x13;
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

static void put_text(unsigned char col, unsigned char row, const char* s) {
    vram_adr(0x2000 + ((unsigned int)row << 5) + col);
    while (*s) vram_put(*s++);
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
        if (r == 6) row_text(5, "BOOK I: EMBER AND VEIL");
        return;
    }
    t = (art_sel == ART_FIRE) ? T_ICON_FIRE : T_ICON_VEIL;
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
        row_text(26, art_sel == ART_FIRE ? "FIRE" : "VEIL");
        break;
    case 4:
        rowbuf[23] = t + 2;
        rowbuf[24] = t + 3;
        if (art_sel == ART_FIRE) row_text(26, fire_tier > 1 ? "LV2" : "LV1");
        else row_text(26, veil_on ? "ON" : "OFF");
        break;
    case 5:
        if (area < 3) row_text(2, area_names[area]);
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
        vb_hdr(0x2000 + 3 * 32 + 23, 7);
        for (n = 23; n < 30; ++n) VB(rowbuf[n]);
        hud_row(4);
        vb_hdr(0x2000 + 4 * 32 + 23, 7);
        for (n = 23; n < 30; ++n) VB(rowbuf[n]);
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

/* ------------------------------------------------------------------ full screen draw (rendering off) */
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
    if (state == ST_TITLE) memset(attr, 0xAA, 16);   /* logo rows use palette 2 */
    for (c = 0; c < 64; ++c) vram_put(attr[c]);
}

static void clear_screen(void) {
    vram_adr(0x2000);
    spr_tile = 0;
    vram_fill(1024);
    oam_begin();
    oam_finish();
}

/* ------------------------------------------------------------------ save data */
static void save_game(void) {
    unsigned char n, sum;
    sv[0] = 'F'; sv[1] = 'S'; sv[2] = '0'; sv[3] = '1';
    sv[4] = pmaxhp;
    sv[5] = arts;
    sv[6] = fire_tier;
    sv[7] = cp_scr;
    sv[8] = cp_x;
    sv[9] = cp_y;
    for (n = 0; n < 12; ++n) sv[10 + n] = flags[n];
    sum = 0x5A;
    for (n = 4; n < 22; ++n) sum += sv[n];
    sv[22] = sum;
}

static unsigned char save_valid(void) {
    unsigned char n, sum;
    if (sv[0] != 'F' || sv[1] != 'S' || sv[2] != '0' || sv[3] != '1') return 0;
    sum = 0x5A;
    for (n = 4; n < 22; ++n) sum += sv[n];
    return sum == sv[22];
}

static void load_game(void) {
    unsigned char n;
    pmaxhp = sv[4];
    arts = sv[5];
    fire_tier = sv[6];
    cp_scr = sv[7];
    cp_x = sv[8];
    cp_y = sv[9];
    for (n = 0; n < 12; ++n) flags[n] = sv[10 + n];
    php = pmaxhp;
    mana = MANA_MAX;
    art_sel = (arts & HAS_FIRE) ? ART_FIRE : ART_VEIL;
}

static void new_game(void) {
    pmaxhp = 6;
    php = 6;
    mana = MANA_MAX;
    arts = HAS_VEIL;
    art_sel = ART_VEIL;
    fire_tier = 1;
    memset(flags, 0, 12);
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

static void check_sign(unsigned char x, unsigned char y) {
    if (mt_flags[map[(y & 0xF0) | (x >> 4)]] & MF_SIGN) sign_touch = 1;
}

/* box A (ax_..ah_) against enemy n */
static unsigned char hit_enemy(unsigned char n) {
    unsigned char ex, ey, ew, eh;
    if (e_type[n] == E_BOSS) {
        ex = e_x[n] + 4; ey = e_y[n] + 4; ew = 24; eh = 24;
    } else {
        ex = e_x[n] + 2; ey = e_y[n] + 2; ew = 12; eh = 13;
    }
    return (ax_ < ex + ew) && (ex < ax_ + aw_) && (ay_ < ey + eh) && (ey < ay_ + ah_);
}

static unsigned char hit_player(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    return (px + 3 < x + w) && (x < px + 13) && (py + 4 < y + h) && (y < py + 16);
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

/* can the veil drop without trapping the player inside an illusion? */
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
    if (pinv || !php) return;
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

static void boss_ai(unsigned char n) {
    unsigned char cx, cy, ax, ay, d, hx, hy;
    if (e_hp[n] <= 5 || (frame_count & 1)) {
        e_x[n] += e_dx[n];
        e_y[n] += e_dy[n];
        if (e_x[n] <= 16) e_dx[n] = 1;
        if (e_x[n] >= 208) e_dx[n] = -1;
        if (e_y[n] <= 8) e_dy[n] = 1;
        if (e_y[n] >= 136) e_dy[n] = -1;
    }
    if (--e_t[n]) return;
    e_t[n] = (e_hp[n] <= 5) ? 70 : 110;
    cx = e_x[n] + 12; cy = e_y[n] + 12;
    hx = px + 4 > cx;
    ax = hx ? px + 4 - cx : cx - px - 4;
    hy = py + 4 > cy;
    ay = hy ? py + 4 - cy : cy - py - 4;
    if (ax > (ay << 1)) d = hx ? 0 : 4;
    else if (ay > (ax << 1)) d = hy ? 2 : 6;
    else if (hx) d = hy ? 1 : 7;
    else d = hy ? 3 : 5;
    shot_spawn(cx, cy, d - 1);
    shot_spawn(cx, cy, d);
    shot_spawn(cx, cy, d + 1);
    sfx_req = SFX_SWING;
}

static void kill_enemy(unsigned char n) {
    unsigned char x, y, r, k;
    x = e_x[n]; y = e_y[n];
    sfx_req = SFX_KILL;
    if (e_type[n] == E_BOSS) {
        puff_spawn(x, y); puff_spawn(x + 16, y);
        puff_spawn(x, y + 16); puff_spawn(x + 16, y + 16);
        cur_song = SONG_TEMPLE;
        mus_req = SONG_TEMPLE;
        memset(s_on, 0, MAX_SHOT);
    } else {
        puff_spawn(x, y);
        r = rand8();
        if (r < 64) pk_spawn(PK_HEART, x + 4, y + 4, 255);
        else if (r < 128) pk_spawn(PK_MANA, x + 4, y + 4, 255);
    }
    e_type[n] = 0;
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
        if (e_type[n] == E_BOSS) {
            e_flash[n] = 30;
            if (e_hp[n] == 6 || e_hp[n] == 3 || e_hp[n] == 5 || e_hp[n] == 2)
                pk_spawn(PK_MANA, 116, 140, 255);
        } else {
            e_flash[n] = 16;
            if (e_type[n] != E_SHADE) { e_kb[n] = 8; e_kbdir[n] = dir; }
        }
    } else {
        kill_enemy(n);
    }
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
            case E_BOSS:    boss_ai(n); break;
            }
        }
        if (!pinv) {
            ax_ = px + 3; ay_ = py + 4; aw_ = 10; ah_ = 12;
            if (hit_enemy(n)) {
                if (ty == E_BOSS) hurt_player(2, e_x[n] + 16, e_y[n] + 16);
                else hurt_player(1, e_x[n] + 8, e_y[n] + 8);
            }
        }
    }
}

/* ------------------------------------------------------------------ room logic */
static void room_solve(void) {
    unsigned char n, opened;
    if (flag_get(cur)) return;
    flag_set(cur);
    opened = 0;
    for (n = 0; n < 176; ++n) {
        if (map[n] == MT_DOOR) { map[n] = MT_DOOROPEN; mq_push(n); opened = 1; }
    }
    if (opened) sfx_req = SFX_DOOR;
    spawn_item();
    if (scr_solvemsg[cur]) msg_start(scr_solvemsg[cur]);
}

static void check_braziers(void) {
    unsigned char n;
    if (scr_cond[cur] != COND_BRAZ) return;
    for (n = 0; n < 176; ++n) if (map[n] == MT_BRAZIER) return;
    room_solve();
}

/* ------------------------------------------------------------------ fire bolts */
static void cast(void) {
    unsigned char n, cost;
    if (art_sel == ART_VEIL) {
        if (veil_on) {
            if (veil_can_drop()) veil_set(0);
            else sfx_req = SFX_ERROR;
        } else if (mana >= 4) {
            mana -= 4;
            hud_dirty |= 2;
            veil_set(1);
        } else sfx_req = SFX_ERROR;
        return;
    }
    cost = (fire_tier > 1) ? 6 : 8;
    if (pcast || mana < cost) { sfx_req = SFX_ERROR; return; }
    for (n = 0; n < MAX_BOLT; ++n) if (!b_on[n]) break;
    if (n == MAX_BOLT) return;
    b_on[n] = 1;
    b_dir[n] = pdir;
    b_life[n] = (fire_tier > 1) ? 34 : 32;
    switch (pdir) {
    case DIR_DOWN:  b_x[n] = px + 4; b_y[n] = py + 12; break;
    case DIR_UP:    b_x[n] = px + 4; b_y[n] = (py >= 4) ? py - 4 : 0; break;
    case DIR_LEFT:  b_x[n] = (px >= 4) ? px - 4 : 0; b_y[n] = py + 6; break;
    default:        b_x[n] = px + 12; b_y[n] = py + 6; break;
    }
    mana -= cost;
    hud_dirty |= 2;
    pcast = 12;
    sfx_req = SFX_FIRE;
}

static void update_bolts(void) {
    unsigned char n, k, spd, cx, cy, idx, f;
    spd = (fire_tier > 1) ? 4 : 3;
    for (n = 0; n < MAX_BOLT; ++n) {
        if (!b_on[n]) continue;
        switch (b_dir[n]) {
        case DIR_DOWN:  if (b_y[n] >= 168 - spd) { b_on[n] = 0; continue; } b_y[n] += spd; break;
        case DIR_UP:    if (b_y[n] < spd) { b_on[n] = 0; continue; } b_y[n] -= spd; break;
        case DIR_LEFT:  if (b_x[n] < spd) { b_on[n] = 0; continue; } b_x[n] -= spd; break;
        default:        if (b_x[n] >= 248 - spd) { b_on[n] = 0; continue; } b_x[n] += spd; break;
        }
        cx = b_x[n] + 4; cy = b_y[n] + 4;
        idx = (cy & 0xF0) | (cx >> 4);
        f = mt_flags[map[idx]];
        if (f & MF_BURN) {
            map[idx] = (area == AREA_OW) ? MT_GRASS : MT_FLOOR;
            mq_push(idx);
            puff_spawn(cx & 0xF0, cy & 0xF0);
            sfx_req = SFX_BURN;
            b_on[n] = 0;
            continue;
        }
        if (f & MF_BRAZ) {
            map[idx] = MT_BRAZLIT;
            mq_push(idx);
            sfx_req = SFX_BURN;
            b_on[n] = 0;
            check_braziers();
            continue;
        }
        if ((f & MF_SOLID) && !(f & MF_WATER) && !((f & MF_VEIL) && veil_on)) {
            b_on[n] = 0;
            continue;
        }
        ax_ = b_x[n]; ay_ = b_y[n]; aw_ = 8; ah_ = 8;
        for (k = 0; k < MAX_EN; ++k) {
            if (!e_type[k]) continue;
            if (e_type[k] == E_SHADE && !veil_on) continue;
            if (hit_enemy(k)) {
                damage_enemy(k, fire_tier, b_dir[n]);
                b_on[n] = 0;
                break;
            }
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
        if (e_type[n] == E_BOSS) {
            if (patk == 10) sfx_req = SFX_TINK;
            continue;
        }
        damage_enemy(n, 1, pdir);
    }
}

/* ------------------------------------------------------------------ enemy shots, pickups, puffs */
static void update_shots(void) {
    unsigned char n;
    for (n = 0; n < MAX_SHOT; ++n) {
        if (!s_on[n]) continue;
        s_x[n] += s_dx[n];
        s_y[n] += s_dy[n];
        if (s_x[n] > 248 || s_y[n] > 168) { s_on[n] = 0; continue; }
        if (!pinv && hit_player(s_x[n] + 1, s_y[n] + 1, 6, 6)) {
            hurt_player(1, s_x[n] + 4, s_y[n] + 4);
            s_on[n] = 0;
        }
    }
}

static void get_item(unsigned char it) {
    unsigned char m;
    flag_set(F_TAKEN + cur);
    sfx_req = SFX_ITEM;
    m = 0;
    switch (it) {
    case IT_TOME:
        arts |= HAS_FIRE;
        art_sel = ART_FIRE;
        pal_build();
        pal_fade(0);
        m = MSG_TOME;
        break;
    case IT_SIGIL:
        fire_tier = 2;
        m = MSG_SIGIL;
        break;
    case IT_HEART:
        pmaxhp += 2;
        php = pmaxhp;
        m = MSG_HEART;
        break;
    case IT_SEAL:
        pmaxhp += 2;
        php = pmaxhp;
        m = MSG_SEAL;
        end_after_msg = 1;
        break;
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
    msg_p = msg_text[id];
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

/* ------------------------------------------------------------------ player */
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
    /* blocked: signs and corner nudging */
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

static void update_play(void) {
    unsigned char n, m;
    scr_changed = 0;
    if (pad_new & PAD_START) { pause_toggle(); return; }
    if (paused) return;

    if (pinv) --pinv;
    if (pcast) --pcast;

    /* mana: veil drains, otherwise slow regeneration */
    if (veil_on) {
        if (!(frame_count & 7) && mana) { --mana; hud_dirty |= 2; }
        if (!mana && veil_can_drop()) veil_set(0);
    } else if (!(frame_count & 31) && mana < MANA_MAX) {
        ++mana;
        hud_dirty |= 2;
    }

    if ((pad_new & PAD_SELECT) && (arts & HAS_FIRE)) {
        art_sel ^= 1;
        pal_build();
        pal_fade(0);
        hud_dirty |= 4;
        sfx_req = SFX_SELECT;
    }

    sign_touch = 0;
    pmoving = 0;
    if (pkb) {
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
    if (pinv && (frame_count & 2)) return;
    if (veil_on && (frame_count & 1)) return;
    at = 0;
    switch (pdir) {
    case DIR_DOWN: base = SPR_P_DOWN0; break;
    case DIR_UP:   base = SPR_P_UP0; break;
    case DIR_LEFT: base = SPR_P_RIGHT0; at = 0x40; break;
    default:       base = SPR_P_RIGHT0; break;
    }
    if (panim & 8) base += 4;
    spr_x = px; spr_y = py + PF_Y; spr_tile = base; spr_attr = at;
    oam_meta16();
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
    default:
        base = SPR_BOSS0 + ((frame_count & 8) ? 16 : 0);
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

static void draw_sprites(void) {
    unsigned char n, k;
    oam_begin();
    if (state != ST_DEAD) draw_player();
    for (n = 0; n < MAX_BOLT; ++n) {
        if (!b_on[n]) continue;
        spr_x = b_x[n]; spr_y = b_y[n] + PF_Y;
        spr_tile = SPR_BOLT0 + ((frame_count & 4) ? 1 : 0);
        spr_attr = 3;
        oam_spr();
    }
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
        spr_attr = 2;
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
            spr_attr = 3;
            switch (k_item[n]) {
            case IT_TOME:  spr_tile = SPR_TOME; break;
            case IT_SIGIL: spr_tile = SPR_SIGIL; break;
            case IT_HEART: spr_tile = SPR_HEARTC; break;
            default:       spr_tile = SPR_SEAL; break;
            }
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
    unsigned char n, m, t, solved;
    const unsigned char* src;
    cur = s;
    area = scr_area[s];
    palset = scr_pal[s];
    memcpy(map, scr_maps[s], 176);
    solved = flag_get(s);
    if (scr_cond[s] == COND_FLAG && flag_get(scr_condarg[s])) solved = 1;
    if (solved) {
        for (n = 0; n < 176; ++n) {
            m = map[n];
            if (m == MT_DOOR) map[n] = MT_DOOROPEN;
            else if (m == MT_BRAZIER) map[n] = MT_BRAZLIT;
        }
    }
    memset(e_type, 0, MAX_EN);
    memset(b_on, 0, MAX_BOLT);
    memset(s_on, 0, MAX_SHOT);
    memset(k_type, 0, MAX_PK);
    memset(f_t, 0, MAX_PUFF);
    mq_n = 0;
    src = scr_enemies[s];
    n = 0;
    while (src[0] && n < MAX_EN) {
        t = src[0];
        if (!(t == E_BOSS && flag_get(s))) {
            e_type[n] = t;
            e_x[n] = src[1];
            e_y[n] = src[2];
            e_hp[n] = enemy_hp[t];
            e_t[n] = (n << 3) + 1;
            e_dir[n] = rand8() & 3;
            e_flash[n] = 0;
            e_kb[n] = 0;
            if (t == E_BOSS) { e_dx[n] = 1; e_dy[n] = 1; e_t[n] = 120; }
            ++n;
        }
        src += 3;
    }
    spawn_item();
}

static void enter_screen(unsigned char s, unsigned char x, unsigned char y, unsigned char fade) {
    unsigned char song;
    if (fade) fade_out();
    ppu_off();
    load_screen(s);
    px = x;
    py = y;
    pkb = 0;
    patk = 0;
    draw_screen_full();
    pal_build();
    pal_fade(4);
    draw_sprites();
    ppu_on();

    song = (area == AREA_OW) ? SONG_OVERWORLD : SONG_TEMPLE;
    if (e_type[0] == E_BOSS) song = SONG_BOSS;
    if (song != cur_song) { cur_song = song; mus_req = song; }

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
    if ((scr_mflags[s] & 1) && !flag_get(F_SEEN + s)) {
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
    pal_build();
    pal_fade(4);
    ppu_on();
    cur_song = song;
    mus_req = song;
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
    paused = 0;
    state = ST_PLAY;
    cur_song = 255;
    enter_screen(cp_scr, cp_x, cp_y, 0);
}

static void do_ending(void) {
    wait_frames(30);
    text_screen("THE EMBER SEAL IS BROKEN.", "FOUR SEALS REMAIN...", "BOOK I COMPLETE", "THANK YOU FOR PLAYING", SONG_ENDING);
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
        pinv = 0; pkb = 0; patk = 0; pcast = 0;
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

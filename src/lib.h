/* Hardware runtime interface (implemented in crt0.s / sound.s) */
#ifndef LIB_H
#define LIB_H

extern unsigned char frame_count, pad, pad_new;
extern unsigned char spr_x, spr_y, spr_tile, spr_attr, oam_idx;
extern unsigned char vbuf_len, pal_dirty;
extern unsigned char sfx_req, mus_req;
#pragma zpsym("frame_count")
#pragma zpsym("pad")
#pragma zpsym("pad_new")
#pragma zpsym("spr_x")
#pragma zpsym("spr_y")
#pragma zpsym("spr_tile")
#pragma zpsym("spr_attr")
#pragma zpsym("oam_idx")
#pragma zpsym("vbuf_len")
#pragma zpsym("pal_dirty")
#pragma zpsym("sfx_req")
#pragma zpsym("mus_req")

extern unsigned char pal_buf[32];
extern unsigned char vbuf[160];

void __fastcall__ ppu_wait_nmi(void);   /* hand OAM/palette/vbuf to NMI and wait one frame */
void __fastcall__ ppu_off(void);
void __fastcall__ ppu_on(void);
void __fastcall__ pad_poll(void);
void __fastcall__ vram_adr(unsigned int adr);   /* rendering off only */
void __fastcall__ vram_put(unsigned char v);
void __fastcall__ vram_fill(unsigned int count); /* writes spr_tile 'count' times */
void __fastcall__ oam_begin(void);
void __fastcall__ oam_spr(void);       /* spr_x, spr_y, spr_tile, spr_attr */
void __fastcall__ oam_meta16(void);    /* 2x2 tiles starting at spr_tile */
void __fastcall__ oam_finish(void);
unsigned char __fastcall__ rand8(void);
void __fastcall__ upload_chr(void);
void __fastcall__ mmc1_prg(unsigned char bank);

#define PAD_A      0x80
#define PAD_B      0x40
#define PAD_SELECT 0x20
#define PAD_START  0x10
#define PAD_UP     0x08
#define PAD_DOWN   0x04
#define PAD_LEFT   0x02
#define PAD_RIGHT  0x01

#endif

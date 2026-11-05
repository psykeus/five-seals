; THE FIVE SEALS - startup + hardware runtime
; Mapper: MMC1 (SNROM-style) 128 KB PRG, 8 KB CHR-RAM, 8 KB battery WRAM.
; Bank layout: bank 0 at $8000 (code/data) during play, bank 1 = CHR source,
; bank 7 fixed at $C000 (startup, runtime, sound).

.export __STARTUP__ : absolute = 1
.import copydata, zerobss, initlib, _main
.import __STACK_START__, __STACK_SIZE__
.importzp c_sp
.import sound_init, sound_update

.export _ppu_wait_nmi, _ppu_off, _ppu_on, _pad_poll
.export _vram_adr, _vram_put, _vram_fill
.export _oam_begin, _oam_spr, _oam_meta16, _oam_finish
.export _rand8, _upload_chr, _mmc1_prg
.export _pal_buf, _vbuf
.exportzp _frame_count, _pad, _pad_new, _spr_x, _spr_y, _spr_tile, _spr_attr
.exportzp _oam_idx, _vbuf_len, _pal_dirty

PPUCTRL   = $2000
PPUMASK   = $2001
PPUSTATUS = $2002
OAMADDR   = $2003
PPUSCROLL = $2005
PPUADDR   = $2006
PPUDATA   = $2007
OAMDMA    = $4014
OAM       = $0200

; ------------------------------------------------------------------ header (NES 2.0)
.segment "HEADER"
    .byte "NES", $1A
    .byte 8                 ; 8 x 16 KB PRG = 128 KB
    .byte 0                 ; CHR-RAM
    .byte $13               ; mapper 1 low nibble, battery, vertical mirroring
    .byte $08               ; NES 2.0
    .byte $00
    .byte $00
    .byte $70               ; PRG-NVRAM 64<<7 = 8 KB (battery)
    .byte $07               ; CHR-RAM 64<<7 = 8 KB
    .byte $00, $00, $00, $00

; ------------------------------------------------------------------ zero page
.segment "ZEROPAGE"
nmi_ready:    .res 1
_frame_count: .res 1
ppuctrl_sh:   .res 1
ppumask_sh:   .res 1
_pal_dirty:   .res 1
_vbuf_len:    .res 1
_pad:         .res 1
_pad_new:     .res 1
pad_prev:     .res 1
_spr_x:       .res 1
_spr_y:       .res 1
_spr_tile:    .res 1
_spr_attr:    .res 1
_oam_idx:     .res 1
oam_full:     .res 1
rng:          .res 2
zp_ptr:       .res 2
zp_tmp:       .res 1

.segment "BSS"
_pal_buf:     .res 32
_vbuf:        .res 160

; ------------------------------------------------------------------ reset
.segment "STARTUP"

reset:
    sei
    cld
    ldx #$40
    stx $4017
    ldx #$FF
    txs
    inx
    stx PPUCTRL
    stx PPUMASK
    stx $4010
    bit PPUSTATUS
@vw1:
    bit PPUSTATUS
    bpl @vw1

    lda #0
    tax
@clr:
    sta $0000,x
    sta $0100,x
    sta $0300,x
    sta $0400,x
    sta $0500,x
    sta $0600,x
    sta $0700,x
    inx
    bne @clr
    lda #$FF
@clr2:
    sta OAM,x
    inx
    bne @clr2

@vw2:
    bit PPUSTATUS
    bpl @vw2

    ; ---- MMC1 init
    lda #$80
    sta $8000               ; reset shift register
    lda #$0E                ; vertical mirroring, PRG mode 3 (fix last), CHR 8K
    sta $8000
    lsr a
    sta $8000
    lsr a
    sta $8000
    lsr a
    sta $8000
    lsr a
    sta $8000
    lda #0                  ; CHR bank 0
    sta $A000
    sta $A000
    sta $A000
    sta $A000
    sta $A000
    lda #0
    jsr _mmc1_prg           ; PRG bank 0 at $8000, WRAM enabled

    ; ---- C runtime
    lda #<(__STACK_START__ + __STACK_SIZE__)
    sta c_sp
    lda #>(__STACK_START__ + __STACK_SIZE__)
    sta c_sp+1
    jsr zerobss
    jsr copydata
    jsr initlib

    lda #$AC
    sta rng
    lda #$E1
    sta rng+1

    jsr sound_init

    lda #$88                ; NMI on, sprites from $1000, BG from $0000
    sta ppuctrl_sh
    sta PPUCTRL
    lda #0
    sta ppumask_sh
    jmp _main

; ------------------------------------------------------------------ runtime (fixed bank)
.segment "CODE"

; A = bank number (0..7)
_mmc1_prg:
    sta $E000
    lsr a
    sta $E000
    lsr a
    sta $E000
    lsr a
    sta $E000
    lsr a
    sta $E000
    rts

nmi:
    pha
    txa
    pha
    tya
    pha

    inc _frame_count
    lda nmi_ready
    beq @skip

    lda #0
    sta OAMADDR
    lda #>OAM
    sta OAMDMA

    lda _pal_dirty
    beq @nopal
    bit PPUSTATUS
    lda #$3F
    sta PPUADDR
    lda #$00
    sta PPUADDR
    ldx #0
@pl:
    lda _pal_buf,x
    sta PPUDATA
    inx
    cpx #32
    bne @pl
    lda #0
    sta _pal_dirty
@nopal:

    bit PPUSTATUS
    ldx #0
@vb:
    cpx _vbuf_len
    bcs @vbdone
    lda _vbuf,x
    sta PPUADDR
    inx
    lda _vbuf,x
    sta PPUADDR
    inx
    ldy _vbuf,x
    inx
@vbd:
    lda _vbuf,x
    sta PPUDATA
    inx
    dey
    bne @vbd
    jmp @vb
@vbdone:
    lda #0
    sta _vbuf_len

    lda ppuctrl_sh
    sta PPUCTRL
    lda #0
    sta PPUSCROLL
    sta PPUSCROLL
    lda ppumask_sh
    sta PPUMASK
    lda #0
    sta nmi_ready
@skip:
    jsr sound_update
    pla
    tay
    pla
    tax
    pla
irq:
    rti

_ppu_wait_nmi:
    lda #1
    sta nmi_ready
    lda _frame_count
@w:
    cmp _frame_count
    beq @w
    rts

_ppu_off:
    lda #0
    sta ppumask_sh
    jmp _ppu_wait_nmi

_ppu_on:
    lda #$1E
    sta ppumask_sh
    jmp _ppu_wait_nmi

; A=lo X=hi
_vram_adr:
    bit PPUSTATUS
    stx PPUADDR
    sta PPUADDR
    rts

_vram_put:
    sta PPUDATA
    rts

; fill: value in zp_tmp is set via _spr_tile; count A(lo) X(hi)
; void vram_fill(unsigned int count) - writes _spr_tile count times
_vram_fill:
    sta zp_ptr
    stx zp_ptr+1
    lda _spr_tile
    ldy zp_ptr
    beq @hi
@lo:
    sta PPUDATA
    dey
    bne @lo
@hi:
    ldx zp_ptr+1
    beq @done
@pg:
    ldy #0
@pgl:
    sta PPUDATA
    dey
    bne @pgl
    dex
    bne @pg
@done:
    rts

_pad_poll:
    lda #1
    sta $4016
    lda #0
    sta $4016
    ldx #8
@l:
    lda $4016
    and #3
    cmp #1
    rol zp_tmp
    dex
    bne @l
    lda pad_prev
    eor #$FF
    and zp_tmp
    sta _pad_new
    lda zp_tmp
    sta _pad
    sta pad_prev
    rts

_oam_begin:
    lda #0
    sta _oam_idx
    sta oam_full
    rts

; one 8x8 sprite from spr_x/spr_y/spr_tile/spr_attr
_oam_spr:
    lda oam_full
    bne @full
    ldx _oam_idx
    lda _spr_y
    sta OAM,x
    lda _spr_tile
    sta OAM+1,x
    lda _spr_attr
    sta OAM+2,x
    lda _spr_x
    sta OAM+3,x
    txa
    clc
    adc #4
    sta _oam_idx
    bne @full
    inc oam_full
@full:
    rts

; 16x16 from 4 consecutive tiles (TL,TR,BL,BR); honours hflip (bit 6) and vflip (bit 7)
_oam_meta16:
    lda _spr_tile
    sta zp_tmp
    ; choose tile order based on flips: index = t0..t3 for positions TL,TR,BL,BR
    lda _spr_attr
    and #$C0
    sta zp_ptr              ; flip bits
    ; position TL
    ldy #0
    jsr meta_pick
    jsr _oam_spr
    ; TR
    lda _spr_x
    clc
    adc #8
    sta _spr_x
    ldy #1
    jsr meta_pick
    jsr _oam_spr
    ; BR
    lda _spr_y
    clc
    adc #8
    sta _spr_y
    ldy #3
    jsr meta_pick
    jsr _oam_spr
    ; BL
    lda _spr_x
    sec
    sbc #8
    sta _spr_x
    ldy #2
    jsr meta_pick
    jsr _oam_spr
    lda _spr_y
    sec
    sbc #8
    sta _spr_y
    rts

; Y = screen position (0 TL,1 TR,2 BL,3 BR) -> sets _spr_tile
meta_pick:
    tya
    bit zp_ptr
    bvc @noh
    eor #1                  ; hflip swaps columns
@noh:
    bit zp_ptr
    bpl @nov
    eor #2                  ; vflip swaps rows
@nov:
    clc
    adc zp_tmp
    sta _spr_tile
    rts

_oam_finish:
    lda oam_full
    bne @done
    ldx _oam_idx
    lda #$FF
@l:
    sta OAM,x
    inx
    inx
    inx
    inx
    bne @l
@done:
    rts

_rand8:
    lda rng
    asl a
    rol rng+1
    bcc @n
    eor #$2D
@n:
    sta rng
    adc rng+1
    ldx #0
    rts

; copy 8 KB of tiles from PRG bank 1 into CHR-RAM. Rendering must be off.
_upload_chr:
    lda #1
    jsr _mmc1_prg
    bit PPUSTATUS
    lda #0
    sta PPUADDR
    sta PPUADDR
    sta zp_ptr
    lda #$80
    sta zp_ptr+1
    ldx #32
    ldy #0
@l:
    lda (zp_ptr),y
    sta PPUDATA
    iny
    bne @l
    inc zp_ptr+1
    dex
    bne @l
    lda #0
    jmp _mmc1_prg

; ------------------------------------------------------------------ CHR source
.segment "CHRDATA"
    .incbin "../build/chr.bin"

.segment "VECTORS"
    .word nmi
    .word reset
    .word irq

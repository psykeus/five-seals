; THE FIVE SEALS - tiny sound engine (runs from NMI, lives in the fixed bank)
; Music: pulse 1 (melody) + triangle (bass).  SFX: pulse 2 + noise.
; Stream format: $00 rest, $01-$3F note index, $80|n set duration n frames, $FF loop.

.export sound_init, sound_update
.exportzp _sfx_req, _mus_req
.import note_lo, note_hi, song_p_lo, song_p_hi, song_t_lo, song_t_hi
.import sfx_lo, sfx_hi, sfx_chan

.segment "ZEROPAGE"
_sfx_req:   .res 1
_mus_req:   .res 1
ptr_p:      .res 2
ptr_t:      .res 2
loop_p:     .res 2
loop_t:     .res 2
tm_p:       .res 1
tm_t:       .res 1
dur_p:      .res 1
dur_t:      .res 1
vol_p:      .res 1
mus_on:     .res 1
sptr_a:     .res 2
sptr_n:     .res 2
son_a:      .res 1
son_n:      .res 1
lasthi_a:   .res 1

.segment "CODE"

sound_init:
    lda #$0F
    sta $4015
    lda #$40
    sta $4017
    lda #$08
    sta $4001
    sta $4005
    lda #$30
    sta $4000
    sta $4004
    sta $400C
    lda #$80
    sta $4008
    lda #$FF
    sta _sfx_req
    sta _mus_req
    lda #0
    sta mus_on
    sta son_a
    sta son_n
    rts

sound_update:
    ; ---------------- music request
    lda _mus_req
    cmp #$FF
    beq @no_mreq
    ldx #$FF
    stx _mus_req
    cmp #$FE
    bne @mstart
    lda #0
    sta mus_on
    lda #$30
    sta $4000
    lda #$80
    sta $4008
    jmp @no_mreq
@mstart:
    tax
    lda song_p_lo,x
    sta ptr_p
    sta loop_p
    lda song_p_hi,x
    sta ptr_p+1
    sta loop_p+1
    lda song_t_lo,x
    sta ptr_t
    sta loop_t
    lda song_t_hi,x
    sta ptr_t+1
    sta loop_t+1
    lda #1
    sta tm_p
    sta tm_t
    sta mus_on
    lda #12
    sta dur_p
    sta dur_t
@no_mreq:
    ; ---------------- sfx request
    lda _sfx_req
    cmp #$FF
    beq @no_sreq
    ldx #$FF
    stx _sfx_req
    tax
    lda sfx_chan,x
    bne @snoise
    lda sfx_lo,x
    sta sptr_a
    lda sfx_hi,x
    sta sptr_a+1
    lda #1
    sta son_a
    lda #$FF
    sta lasthi_a
    jmp @no_sreq
@snoise:
    lda sfx_lo,x
    sta sptr_n
    lda sfx_hi,x
    sta sptr_n+1
    lda #1
    sta son_n
    lda #$08
    sta $400F
@no_sreq:

    ; ---------------- music
    lda mus_on
    beq @sfx
    dec tm_p
    bne @pdecay
    jsr step_p
    jmp @tri
@pdecay:
    lda tm_p
    and #3
    bne @tri
    lda vol_p
    cmp #5
    bcc @tri
    dec vol_p
    lda vol_p
    ora #$70
    sta $4000
@tri:
    dec tm_t
    bne @sfx
    jsr step_t

    ; ---------------- sfx pulse 2
@sfx:
    lda son_a
    beq @noise
    ldy #0
    lda (sptr_a),y
    bne @aplay
    sta son_a
    lda #$30
    sta $4004
    jmp @noise
@aplay:
    sta $4004
    iny
    lda (sptr_a),y
    sta $4006
    iny
    lda (sptr_a),y
    cmp lasthi_a
    beq @asame
    sta lasthi_a
    sta $4007
@asame:
    lda sptr_a
    clc
    adc #3
    sta sptr_a
    bcc @noise
    inc sptr_a+1

    ; ---------------- sfx noise
@noise:
    lda son_n
    beq @done
    ldy #0
    lda (sptr_n),y
    bne @nplay
    sta son_n
    lda #$30
    sta $400C
    rts
@nplay:
    sta $400C
    iny
    lda (sptr_n),y
    sta $400E
    lda sptr_n
    clc
    adc #2
    sta sptr_n
    bcc @done
    inc sptr_n+1
@done:
    rts

; read pulse stream until a note/rest
step_p:
    ldy #0
@rd:
    lda (ptr_p),y
    inc ptr_p
    bne @i
    inc ptr_p+1
@i:
    cmp #$FF
    bne @nl
    lda loop_p
    sta ptr_p
    lda loop_p+1
    sta ptr_p+1
    jmp @rd
@nl:
    cmp #$80
    bcc @note
    and #$7F
    sta dur_p
    jmp @rd
@note:
    tax
    beq @rest
    lda #10
    sta vol_p
    ora #$70
    sta $4000
    lda note_lo,x
    sta $4002
    lda note_hi,x
    sta $4003
    jmp @d
@rest:
    lda #0
    sta vol_p
    lda #$30
    sta $4000
@d:
    lda dur_p
    sta tm_p
    rts

step_t:
    ldy #0
@rd:
    lda (ptr_t),y
    inc ptr_t
    bne @i
    inc ptr_t+1
@i:
    cmp #$FF
    bne @nl
    lda loop_t
    sta ptr_t
    lda loop_t+1
    sta ptr_t+1
    jmp @rd
@nl:
    cmp #$80
    bcc @note
    and #$7F
    sta dur_t
    jmp @rd
@note:
    tax
    beq @rest
    lda #$FF
    sta $4008
    lda note_lo,x
    sta $400A
    lda note_hi,x
    sta $400B
    jmp @d
@rest:
    lda #$80
    sta $4008
@d:
    lda dur_t
    sta tm_t
    rts

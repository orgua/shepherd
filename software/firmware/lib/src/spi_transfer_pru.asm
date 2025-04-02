; HW_REV == 2.0, TODO: there can be less NOPs, ICs are faster
SCLK .set 0 ; ti specific code, gcc can use: ".equ SCLK, 0"
MOSI .set 1
MISO .set 2

.macro NOP ; TODO: cgt assembler understands a simple NOP, without macro
   MOV r23, r23
.endm

; ADS8691 - SPI-Mode-00
; -> MSB begins with falling CS
; -> begin with low CLK
; -> Reads on rising CLK-Edge
; -> transfer frame must contain 32 capture edges for writing (READING can be shorter)
; (datasheet says: "shorter frame can result in erroneous device configuration")
; (datasheet also: "host can use the short data transfer frame to read only the required number of MSB bits")

    .global adc_readwrite ; code performs with 20-22 MHz, ~ 1450 - 1600 ns CS low
adc_readwrite:
    MOV r24, r14 ; Save input arg (CS pin)
    LDI r20, 32 ; Load Counter for outloop
    LDI r14, 0 ; Clear return reg
    CLR r30, r30, MOSI ; Set MOSI low
    CLR r30, r30, SCLK ; Set SCLK low
    CLR r21, r30, r24 ; r21 = r30 with SCLK Low, CS Low
    CLR r30, r30, r24 ; Set CS low

adc_io_loop_head:
    loop adc_io_loop_end, r20 ; start hw-assisted loop (zero overhead)
    SUB r20, r20, 1 ; decrement shiftloop counter
    NOP ; TODO: only temporary - due to trouble to receive data
    QBBC adc_io_mosi_clr, r15, r20
adc_io_mosi_set:
    SET r30, r21, MOSI ; Set SCLK Low, MOSI high
    JMP adc_io_loop_mid
adc_io_mosi_clr:
    CLR r30, r21, MOSI ; Set SCLK Low, MOSI low
    NOP
adc_io_loop_mid:
    NOP
    NOP
    NOP ; TODO: only temporary - due to trouble to receive data
    SET r30, r30, SCLK ; Set SCLK high
    QBBC adc_io_miso_clr, r31, MISO
adc_io_miso_set:
    SET r14, r14, r20
    JMP adc_io_loop_end
adc_io_miso_clr:
    NOP
adc_io_loop_end:
    SET r30, r30, r24 ; set CS high
    CLR r30, r30, MOSI ; Set MOSI low
    JMP r3.w2



    .global adc_fastread
    ; 1 x NOP: code performs with 33-40 MHz (18 bit read), ~ 450-550 ns CS low
    ; 3 x NOP: 200 MHz PRU / 7 Ops = 28.6 MHz, ~ 650 ns CS low
    ; 5 x NOP: 22 MHz for readloop, ~ 836 ns CS Low
    ; NOTE: currently only the 5NOP-Version works reliably
adc_fastread:
    MOV r24, r14 ; Save input arg (CS pin)
    LDI r20, 18 ; Load Counter for loop
    LDI r14, 0 ; Clear return reg
    CLR r30, r30, MOSI ; Set MOSI low
    CLR r30, r30, r24 ; Set CS low

adc_readloop_head: ; 5 - 6 ticks, depending on input
    loop adc_loop_end, r20 ; start hw-assisted loop (zero overhead)
    NOP ; spare
    NOP ; spare
    CLR r30, r30, SCLK ; Set SCLK low
    SUB r20, r20, 1 ; decrement shiftloop counter
    NOP
    NOP ; spare
    NOP ; spare
    SET r30, r30, SCLK ; Set SCLK High
    QBBC adc_loop_end, r31, MISO
adc_miso_set:
    SET r14, r14, r20
adc_loop_end:
    SET r30, r30, r24 ; set CS high
;    CLR r30, r30, SCLK ; Set SCLK low
    JMP r3.w2



; DAC8562
; -> MSB begins with falling CS
; -> begin with high CLK
; -> Reads on falling CLK-Edge
; -> transfer frame must contain 24 capture edges for writing

    .global dac_write  ; code performs with 33.33 MHz, ~ 740 ns CS low
dac_write:
    LDI r20, 24 ; Load Counter for outloop
    SET r30, r30, SCLK ; Set SCLK high
    CLR r21, r30, r14 ; r21 = r30 with SCLK High, CS Low
    CLR r30, r30, r14 ; Set CS low

dac_loop_head:
    loop dac_loop_end, r20 ; start hw-assisted loop (zero overhead)
    SUB r20, r20, 1 ; Decrement counter (there is no way to get hw-loop-counter)
    QBBS dac_mosi_set, r15, r20 ; If bit number [r20] is set in value [r15]
dac_mosi_clr:
    CLR r30, r21, MOSI ; copy prepared register, Set SCLK High, MOSI low
    JMP dac_loop_mid
dac_mosi_set:
    SET r30, r21, MOSI ; copy prepared register, Set SCLK High, MOSI high
    NOP
dac_loop_mid:
    NOP
    CLR r30, r30, SCLK ; Set SCLK low
dac_loop_end:
    NOP
    NOP
    CLR r30, r30, MOSI ; clear MOSI
    SET r30, r30, r14 ; set CS high
    JMP r3.w2

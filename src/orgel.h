/* ORGEL – Klangchip des MERIDIAN 816: 4 Synthesestimmen, 8 Samplekanaele, Filter, Echo, Stereo.
   Rechnet im 1-MHz-Takt wie rtl/orgel.sv. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct Orgel {
    /* Register */
    uint16_t s_freq[4], s_puls[4]; uint8_t s_st[4], s_ad[4], s_sr[4], s_laut[4], s_pan[4];
    uint16_t f_ecke; uint8_t f_rf, f_modus, k_pegel, f_send, f_drive;
    uint8_t x_bits[8], x_rate[8], x_drive[8], x_send[8], x_filt[8];
    uint16_t e_zeit; uint8_t e_rueck, e_anteil, e_bank;
    uint32_t k_start[8], k_len[8], k_loop[8]; uint16_t k_schr[8]; uint8_t k_laut[8], k_pan[8], k_schl[8];
    /* Zustand */
    uint32_t acc[4], lfsr[4]; uint8_t env[4], hst[4], ecnt[4], gate_a[4], msb_a[4], b19_a[4]; uint16_t rcnt[4], welle[4];
    uint32_t k_pos[8]; uint16_t k_frac[8]; uint8_t k_an[8], x_cnt[8], x_hold[8]; int8_t k_wert[8], k_fx[8];
    int64_t f_tief, f_band, f_hoch;
    uint8_t e_teiler; bool e_vorn; uint16_t e_pos, e_fill; int32_t e_wet, e_sum;
    int16_t links, rechts;
} Orgel;
void orgel_reset(Orgel *o);
void orgel_wr(Orgel *o, int seite, uint8_t addr, uint8_t v);
uint8_t orgel_rd(const Orgel *o, int seite, uint8_t addr);
void orgel_tick(Orgel *o, const uint8_t *chip, uint8_t *sdram, const volatile uint8_t *fenster);   /* 1 us; fenster = Bank $FD */

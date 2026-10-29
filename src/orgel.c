#include "orgel.h"
#include <string.h>
#define S28(x) ((int64_t)(((uint64_t)(x) << 36)) >> 36)

void orgel_reset(Orgel *o) {
    memset(o, 0, sizeof *o);
    for (int i = 0; i < 4; i++) { o->s_laut[i] = 15; o->s_pan[i] = 0xFF; o->hst[i] = 2; o->lfsr[i] = 0x7FFFF8; }
    for (int i = 0; i < 8; i++) { o->k_laut[i] = 63; o->k_pan[i] = 0xFF; }
    o->f_modus = 0x0F; o->k_pegel = 4; o->e_vorn = true;
}
void orgel_wr(Orgel *o, int seite, uint8_t a, uint8_t v) {
    int rn = (a >> 4) & 3, kn = (seite << 2) | rn, hi = a >> 6;
    if (hi == 0 && !seite) {
        switch (a & 15) {
        case 0: o->s_freq[rn] = (o->s_freq[rn] & 0xFF00) | v; break;
        case 1: o->s_freq[rn] = (o->s_freq[rn] & 0x00FF) | (v << 8); break;
        case 2: o->s_puls[rn] = (o->s_puls[rn] & 0x0F00) | v; break;
        case 3: o->s_puls[rn] = (o->s_puls[rn] & 0x00FF) | ((v & 15) << 8); break;
        case 4: o->s_st[rn] = v; break; case 5: o->s_ad[rn] = v; break; case 6: o->s_sr[rn] = v; break;
        case 7: o->s_laut[rn] = v & 15; break; case 8: o->s_pan[rn] = v; break;
        }
    } else if ((a >> 4) == 4 && !seite) {
        switch (a & 15) {
        case 0: o->f_ecke = (o->f_ecke & 0x700) | v; break; case 1: o->f_ecke = (o->f_ecke & 0xFF) | ((v & 7) << 8); break;
        case 2: o->f_rf = v; break; case 3: o->f_modus = v; break; case 4: o->k_pegel = v & 15; break;
        case 5: o->f_send = v & 15; break; case 6: o->f_drive = v & 15; break;
        }
    } else if ((a >> 4) == 5 && !seite) {
        switch (a & 15) {
        case 0: o->e_zeit = (o->e_zeit & 0x7F00) | v; o->e_vorn = true; break;
        case 1: o->e_zeit = (o->e_zeit & 0x00FF) | ((v & 127) << 8); o->e_vorn = true; break;
        case 2: o->e_rueck = v & 15; break; case 3: o->e_anteil = v & 15; break;
        case 4: o->e_bank = v; o->e_vorn = true; break;
        }
    } else if (hi == 3) {
        switch (a & 15) {
        case 0: o->x_bits[kn] = (v >> 3) ? 0 : (v & 7); break; case 1: o->x_rate[kn] = v; break;
        case 2: o->x_drive[kn] = v & 15; break; case 3: o->x_send[kn] = v & 15; break; case 4: o->x_filt[kn] = v & 1; break;
        }
    } else if (hi == 2) {
        uint32_t *st = &o->k_start[kn], *ln = &o->k_len[kn], *lp = &o->k_loop[kn];
        switch (a & 15) {
        case 0: *st = (*st & 0xFFFF00) | v; break; case 1: *st = (*st & 0xFF00FF) | (v << 8); break; case 2: *st = (*st & 0x00FFFF) | ((uint32_t)v << 16); break;
        case 3: *ln = v; break;                                   /* setzt Bits 16-23 auf 0 */
        case 4: *ln = (*ln & 0xFF00FF) | (v << 8); break;
        case 5: *lp = v; break;
        case 6: *lp = (*lp & 0xFF00FF) | (v << 8); break;
        case 7: o->k_schr[kn] = (o->k_schr[kn] & 0xFF00) | v; break; case 8: o->k_schr[kn] = (o->k_schr[kn] & 0x00FF) | (v << 8); break;
        case 9: o->k_laut[kn] = v & 63; break; case 10: o->k_pan[kn] = v; break;
        case 11: o->k_schl[kn] = (v >> 1) & 1;
                 if (v & 1) { o->k_an[kn] = (*ln != 0); o->k_pos[kn] = 0; o->k_frac[kn] = 0; } else o->k_an[kn] = 0; break;
        case 14: *ln = (*ln & 0x00FFFF) | ((uint32_t)v << 16); break;
        case 15: *lp = (*lp & 0x00FFFF) | ((uint32_t)v << 16); break;
        }
    }
}
uint8_t orgel_rd(const Orgel *o, int seite, uint8_t a) {
    int rn = (a >> 4) & 3, kn = (seite << 2) | rn;
    if ((a >> 6) == 0 && !seite) { if ((a & 15) == 9) return o->env[rn]; if ((a & 15) == 10) return o->welle[rn] >> 4; }
    else if ((a >> 6) == 2) switch (a & 15) {
        case 11: return o->k_an[kn]; case 12: return o->k_pos[kn]; case 13: return o->k_pos[kn] >> 8; case 14: return o->k_pos[kn] >> 16;
    }
    return 0xFF;
}

static const uint16_t PER[16] = { 9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251 };
static int exp_per(int e) { return e > 93 ? 1 : e > 54 ? 2 : e > 26 ? 4 : e > 14 ? 8 : e > 6 ? 16 : 30; }
static int8_t bits_weg(int8_t w, int b) { return b == 0 ? w : (int8_t)((uint8_t)w & (0xFF << (8 - b))); }
static int8_t zerren(int8_t w, int d) {
    if (!d) return w;
    int t = (w * (2 * d + 4)) >> 2, a = t < 0 ? -t : t, y = a <= 64 ? a : 64 + ((a - 64) >> 2);
    if (y > 127) y = 127; return (int8_t)(t < 0 ? -y : y);
}
static int32_t f_zerren(int32_t x, int d) {
    if (!d) return x;
    int64_t t = ((int64_t)x * (2 * d + 4)) >> 2, a = t < 0 ? -t : t, y = a <= 1536 ? a : 1536 + ((a - 1536) >> 2);
    if (y > 2560) y = 2560; return (int32_t)(t < 0 ? -y : y);
}
static int16_t begrenzen(int64_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : (int16_t)x; }
static uint8_t membyte(const uint8_t *chip, const uint8_t *sdram, const volatile uint8_t *fenster, uint32_t a) {
    if ((a >> 16) < 4) return chip[a & 0x3FFFF];
    if ((a >> 16) == 0xFD) return fenster ? fenster[a & 0xFFFF] : 0;           /* DRAHT-Fenster (Radio, SID JAM) */
    return sdram ? sdram[a & 0xFFFFFF] : 0;
}

void orgel_tick(Orgel *o, const uint8_t *chip, uint8_t *sdram, const volatile uint8_t *fenster) {
    /* ---- Synthesestimmen ---- */
    uint32_t old[4]; memcpy(old, o->acc, sizeof old);
    for (int v = 0; v < 4; v++) {
        int p = (v - 1) & 3; uint8_t st = o->s_st[v];
        o->msb_a[v] = (old[v] >> 23) & 1; uint8_t b19_old = o->b19_a[v]; o->b19_a[v] = (old[v] >> 19) & 1;
        if (st & 8) o->acc[v] = 0;
        else if ((st & 2) && ((old[p] >> 23) & 1) && !o->msb_a[p]) o->acc[v] = 0;
        else o->acc[v] = (old[v] + o->s_freq[v]) & 0xFFFFFF;
        if (st & 8) o->lfsr[v] = 0x7FFFF8;
        else if (((old[v] >> 19) & 1) && !b19_old) o->lfsr[v] = ((o->lfsr[v] << 1) | (((o->lfsr[v] >> 22) ^ (o->lfsr[v] >> 17)) & 1)) & 0x7FFFFF;
        unsigned saw = old[v] >> 12, tri = ((old[v] >> 23) ^ ((st & 4) && ((old[p] >> 23) & 1))) & 1 ? (~(old[v] >> 11)) & 0xFFF : (old[v] >> 11) & 0xFFF;
        unsigned pul = (saw >= o->s_puls[v]) ? 0xFFF : 0, l = o->lfsr[v];
        unsigned noi = (((l >> 20) & 1) << 11) | (((l >> 18) & 1) << 10) | (((l >> 14) & 1) << 9) | (((l >> 11) & 1) << 8) |
                       (((l >> 9) & 1) << 7) | (((l >> 5) & 1) << 6) | (((l >> 2) & 1) << 5) | (((l >> 0) & 1) << 4), w = 0xFFF;
        if (st & 16) w &= tri; if (st & 32) w &= saw; if (st & 64) w &= pul; if (st & 128) w &= noi;
        o->welle[v] = (st & 0xF0) ? w : 0x800;
        /* Huellkurve */
        int hst = o->hst[v], per = PER[hst == 0 ? o->s_ad[v] >> 4 : hst == 1 ? o->s_ad[v] & 15 : o->s_sr[v] & 15];
        uint8_t gate = st & 1, ga = o->gate_a[v]; o->gate_a[v] = gate;
        if (gate && !ga) { o->hst[v] = 0; o->rcnt[v] = 0; }
        else if (!gate && ga) { o->hst[v] = 2; o->rcnt[v] = 0; o->ecnt[v] = 0; }
        else if (o->rcnt[v] < per) o->rcnt[v]++;
        else {
            o->rcnt[v] = 0;
            if (hst == 0) { if (o->env[v] == 0xFF) o->hst[v] = 1; else { o->env[v]++; if (o->env[v] == 0xFF) o->hst[v] = 1; } o->ecnt[v] = 0; }
            else if (o->ecnt[v] + 1 >= exp_per(o->env[v])) {
                o->ecnt[v] = 0;
                if (hst == 1) { if (o->env[v] > (o->s_sr[v] >> 4) * 17) o->env[v]--; }
                else if (o->env[v]) o->env[v]--;
            } else o->ecnt[v]++;
        }
    }
    /* ---- Samplekanaele ---- */
    for (int k = 0; k < 8; k++) {
        if (o->k_an[k]) {
            uint32_t kf = o->k_frac[k] + o->k_schr[k]; o->k_frac[k] = kf & 0xFFFF;
            if (kf >> 16) {
                uint32_t kp = o->k_pos[k] + 1;
                if (kp >= o->k_len[k]) { if (o->k_schl[k]) o->k_pos[k] = o->k_loop[k]; else o->k_an[k] = 0; }
                else o->k_pos[k] = kp;
            }
            o->k_wert[k] = (int8_t)membyte(chip, sdram, fenster, o->k_start[k] + o->k_pos[k]);
        }
        /* Effekte: Crusher, Halten, Verzerrung */
        if (o->x_rate[k] == 0 || o->x_cnt[k] == 0) { o->x_hold[k] = (uint8_t)bits_weg(o->k_wert[k], o->x_bits[k]); o->x_cnt[k] = o->x_rate[k] ? o->x_rate[k] - 1 : 0; }
        else o->x_cnt[k]--;
        o->k_fx[k] = zerren((int8_t)o->x_hold[k], o->x_drive[k]);
    }
    /* ---- Echo (alle 32 us ein Wort) ---- */
    if (o->e_bank == 0) o->e_wet = 0;
    if (o->e_teiler == 0 && o->e_bank != 0 && o->e_zeit != 0 && sdram) {
        if (o->e_vorn) { o->e_pos = 0; o->e_fill = 0; o->e_vorn = false; }
        uint32_t ad = ((uint32_t)o->e_bank << 16) | ((uint32_t)o->e_pos << 1);
        int32_t rd = (int16_t)(sdram[ad] | (sdram[ad + 1] << 8)), n = (o->e_fill >= o->e_zeit) ? rd : 0;
        o->e_wet = (int16_t)n;
        int64_t w = o->e_sum + (((int64_t)n * o->e_rueck) >> 4); int16_t wv = begrenzen(w);
        sdram[ad] = (uint8_t)wv; sdram[ad + 1] = (uint8_t)((uint16_t)wv >> 8);
        o->e_pos = (o->e_pos + 1 >= o->e_zeit) ? 0 : o->e_pos + 1; if (o->e_fill < o->e_zeit) o->e_fill++;
    }
    o->e_teiler = (o->e_teiler + 1) & 31;
    /* ---- Mischpult ---- */
    int32_t stimme[4];
    for (int j = 0; j < 4; j++) {
        int64_t a = ((int64_t)o->welle[j] - 2048) * o->env[j], b = (a >> 8) * o->s_laut[j]; stimme[j] = (int32_t)(b >> 4);
    }
    int64_t zf = 0, es = 0, sl = 0, sr = 0, kl = 0, kr = 0;
    for (int g = 0; g < 4; g++) if (o->f_rf & (1 << g)) zf += stimme[g];
    for (int g = 0; g < 8; g++) {
        int32_t kv = o->k_fx[g] * (int32_t)o->k_laut[g];
        bool an = o->k_an[g], trocken = an && !o->x_filt[g];
        if (an && o->x_filt[g]) zf += (kv * o->k_pegel) >> 4;
        if (trocken) { kl += kv * (o->k_pan[g] >> 4); kr += kv * (o->k_pan[g] & 15); }
        if (an) es += kv * o->x_send[g];
    }
    int64_t f_F = 12 + (((uint32_t)o->f_ecke * 632) >> 8 & 0xFFF), f_q = 92680 - (int64_t)(o->f_rf >> 4) * 5570;
    int64_t ein = S28(zf << 8);
    o->f_tief = S28(o->f_tief + ((f_F * o->f_band) >> 16));
    o->f_hoch = S28(ein - o->f_tief - ((f_q * o->f_band) >> 16));
    o->f_band = S28(o->f_band + ((f_F * o->f_hoch) >> 16));
    for (int g = 0; g < 4; g++) { int32_t sv = (o->f_rf & (1 << g)) ? 0 : stimme[g]; sl += sv * (o->s_pan[g] >> 4); sr += sv * (o->s_pan[g] & 15); }
    sl >>= 4; sr >>= 4;
    int64_t roh = (((o->f_modus & 16) ? o->f_tief : 0) + ((o->f_modus & 32) ? o->f_band : 0) + ((o->f_modus & 64) ? o->f_hoch : 0)) >> 8;
    kl = (kl * o->k_pegel) >> 8; kr = (kr * o->k_pegel) >> 8;
    int32_t roh_m = f_zerren((int32_t)roh, o->f_drive);
    int64_t e_mix = ((int64_t)o->e_wet * o->e_anteil * o->k_pegel) >> 6;
    int64_t suml = sl + roh_m + kl + e_mix, sumr = sr + roh_m + kr + e_mix;
    o->e_sum = (int16_t)((es + (int64_t)roh_m * o->f_send * 4) >> 6);
    o->links = begrenzen((suml * (o->f_modus & 15) * 3) >> 4);
    o->rechts = begrenzen((sumr * (o->f_modus & 15) * 3) >> 4);
}

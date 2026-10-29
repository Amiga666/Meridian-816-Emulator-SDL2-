#define _FILE_OFFSET_BITS 64
#include "meridian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHIPMASK 0x3FFFF

/* ------------------------------------------------------------ PFORTE --- */
static void pf_update_irq(Machine *m) { (void)m; }
static uint8_t pf_rd(Machine *m, int r) {
    switch (r) {
    case 0x00: return m->kq_n ? m->keyq[0].code : 0;
    case 0x01: return m->kq_n ? m->keyq[0].info : 0;
    case 0x04: return m->pf_irq_st;
    case 0x05: return m->pf_irq_en;
    case 0x08: return m->joy1; case 0x09: return m->joy1 >> 8;
    case 0x0A: return m->joy2; case 0x0B: return m->joy2 >> 8;
    case 0x0C: return m->layout;
    case 0x10: case 0x11: case 0x12: case 0x13: {
        uint32_t u = m->us_frz ? m->us_frozen : (uint32_t)m->us; return u >> (8 * (r - 0x10)); }
    case 0x14: return m->ta_cnt; case 0x15: return m->ta_cnt >> 8; case 0x16: return m->ta_ctl;
    case 0x18: return m->tb_cnt; case 0x19: return m->tb_cnt >> 8; case 0x1A: return m->tb_ctl;
    case 0x20: return m->mx_f; case 0x21: return m->mx_f >> 8;
    case 0x22: return m->my_f; case 0x23: return m->my_f >> 8;
    case 0x24: return m->mbtn_f; case 0x25: { int8_t w = m->mw_f; m->mw_f = 0; return w; }
    case 0x26: return m->mxmax; case 0x27: return m->mxmax >> 8;
    case 0x28: return m->mymax; case 0x29: return m->mymax >> 8;
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: return m->rtc[r - 0x30];
    case 0x37: return 1;
    }
    return 0;
}
static void pf_wr(Machine *m, int r, uint8_t v) {
    switch (r) {
    case 0x02: if (m->kq_n) { memmove(&m->keyq[0], &m->keyq[1], sizeof(KeyEv) * (--m->kq_n)); } break;
    case 0x04: m->pf_irq_st &= ~v; break;
    case 0x05: m->pf_irq_en = v; break;
    case 0x10: m->us_frozen = (uint32_t)m->us; m->us_frz = true; break;
    case 0x14: m->ta_start = (m->ta_start & 0xFF00) | v; m->ta_cnt = m->ta_start; break;
    case 0x15: m->ta_start = (m->ta_start & 0x00FF) | (v << 8); m->ta_cnt = m->ta_start; break;
    case 0x16: m->ta_ctl = v; break;
    case 0x18: m->tb_start = (m->tb_start & 0xFF00) | v; m->tb_cnt = m->tb_start; break;
    case 0x19: m->tb_start = (m->tb_start & 0x00FF) | (v << 8); m->tb_cnt = m->tb_start; break;
    case 0x1A: m->tb_ctl = v; break;
    case 0x20: m->mx = (m->mx & 0xFF00) | v; break; case 0x21: m->mx = (m->mx & 0xFF) | (v << 8); break;
    case 0x22: m->my = (m->my & 0xFF00) | v; break; case 0x23: m->my = (m->my & 0xFF) | (v << 8); break;
    case 0x26: m->mxmax = (m->mxmax & 0xFF00) | v; break; case 0x27: m->mxmax = (m->mxmax & 0xFF) | (v << 8); break;
    case 0x28: m->mymax = (m->mymax & 0xFF00) | v; break; case 0x29: m->mymax = (m->mymax & 0xFF) | (v << 8); break;
    case 0x2A: m->mx_f = m->mx; m->my_f = m->my; m->mbtn_f = m->mbtn; m->mw_f += m->mwheel; m->mwheel = 0; break;
    }
    pf_update_irq(m);
}
static void audio_push(Machine *m) {
    m->asum_l += m->org.links; m->asum_r += m->org.rechts; m->acnt++;
    m->aphase += 48000;
    if (m->aphase >= 1000000) {
        m->aphase -= 1000000;
        unsigned w = m->aw & 16383; m->aring[w * 2] = (int16_t)(m->asum_l / m->acnt); m->aring[w * 2 + 1] = (int16_t)(m->asum_r / m->acnt);
        m->aw++; if (m->aw - m->ar > 16384) m->ar = m->aw - 16384;
        m->asum_l = m->asum_r = 0; m->acnt = 0;
    }
}
size_t m_audio_read(Machine *m, int16_t *out, size_t max) {
    size_t n = 0;
    while (n < max && m->ar != m->aw) { unsigned r = m->ar & 16383; out[n * 2] = m->aring[r * 2]; out[n * 2 + 1] = m->aring[r * 2 + 1]; m->ar++; n++; }
    return n;
}
static void tick_us(Machine *m) {
    m->us++;
    orgel_tick(&m->org, m->ram, m->sdram, m->dr);
    if (m->audio_on) audio_push(m);
    if (m->ta_ctl & 1) { if (m->ta_cnt == 0) { m->pf_irq_st |= 2; m->ta_cnt = m->ta_start; if (m->ta_ctl & 2) m->ta_ctl &= ~1; } else m->ta_cnt--; }
    if (m->tb_ctl & 1) { if (m->tb_cnt == 0) { m->pf_irq_st |= 4; m->tb_cnt = m->tb_start; if (m->tb_ctl & 2) m->tb_ctl &= ~1; } else m->tb_cnt--; }
}
void m_key(Machine *m, bool ext, uint8_t code, bool release) {
    if (m->kq_n >= 16) return;
    m->keyq[m->kq_n++] = (KeyEv){ code, 0x80 | (ext ? 2 : 0) | (release ? 1 : 0) };
    m->pf_irq_st |= 1;
}
void m_mouse(Machine *m, int x, int y, uint8_t b) {
    if (x < 0) x = 0; if (y < 0) y = 0; if (x > m->mxmax) x = m->mxmax; if (y > m->mymax) y = m->mymax;
    m->mx = m->mx_f = x; m->my = m->my_f = y; m->mbtn = m->mbtn_f = b;
}
void m_set_joy(Machine *m, int n, uint16_t bits) { if (n == 1) m->joy1 = bits; else m->joy2 = bits; }

/* ------------------------------------------------------------ PINSEL --- */
static void conv_sync(Machine *m);
static uint8_t pin_rd(Machine *m, int r) {
    int k = m->gl_nr;
    switch (r) {
    case 0x0C: return m->irq_st_pin;
    case 0x0D: return m->line; case 0x0E: return m->line >> 8;
    case 0x0F: return m->frame_count;
    case 0x40: return m->gl_nr;
    case 0x41: return m->gl_idx[k]; case 0x42: return m->gl_von[k]; case 0x43: return m->gl_bis[k];
    case 0x44: return m->gl_r0[k]; case 0x45: return m->gl_g0[k]; case 0x46: return m->gl_b0[k];
    case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C:
        return m->gl_d[(r - 0x47) >> 1][k] >> (8 * ((r - 0x47) & 1));
    case 0x4D: return m->gl_an; case 0x4E: return m->gl_an >> 8;
    case 0x50: return m->lm_idx; case 0x51: return m->lmask[m->lm_idx];
    }
    return m->pin[r];
}
static void pal_commit(Machine *m, uint8_t idx, uint8_t lo, uint8_t hi) {
    int r = hi & 15, g = lo >> 4, b = lo & 15;
    conv_sync(m);
    m->pal[idx] = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
}
static void pin_wr(Machine *m, int r, uint8_t v) {
    int k = m->gl_nr;
    switch (r) {
    case 0x08: m->pal_idx = v; return;
    case 0x09: m->pal_lo = v; return;
    case 0x0A: pal_commit(m, m->pal_idx, m->pal_lo, v); m->pal_idx++; return;
    case 0x28: m->lpal_idx = v; return;
    case 0x29: m->lpal_lo = v; return;
    case 0x2A: pal_commit(m, m->lpal_idx, m->lpal_lo, v); m->lpal_idx++; return;
    case 0x0B: m->irq_en_pin = v; return;
    case 0x0C: m->irq_st_pin &= ~v; return;
    case 0x0F: return;
    case 0x36: case 0x37: m->pin[r] = v & 7; return;
    case 0x40: m->gl_nr = v & 15; return;
    case 0x41: m->gl_idx[k] = v; return; case 0x42: m->gl_von[k] = v; return; case 0x43: m->gl_bis[k] = v; return;
    case 0x44: m->gl_r0[k] = v; return; case 0x45: m->gl_g0[k] = v; return; case 0x46: m->gl_b0[k] = v; return;
    case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: {
        uint16_t *d = &m->gl_d[(r - 0x47) >> 1][k]; int hi = (r - 0x47) & 1;
        *d = hi ? (*d & 0x00FF) | (v << 8) : (*d & 0xFF00) | v; return; }
    case 0x4D: m->gl_an = (m->gl_an & 0xFF00) | v; return;
    case 0x4E: m->gl_an = (m->gl_an & 0x00FF) | (v << 8); return;
    case 0x50: m->lm_idx = v & 31; return;
    case 0x51: conv_sync(m); m->lmask[m->lm_idx] = v; m->lm_idx = (m->lm_idx + 1) & 31; return;
    case 0x52: conv_sync(m); m->pin[r] = v & 15; return;
    }
    m->pin[r] = v;
}

/* ------------------------------------------------------------- KRAN ---- */
static uint8_t bus_rd(void *ctx, uint32_t a);
static void bus_wr(void *ctx, uint32_t a, uint8_t v);
static void kran_job(Machine *m, const uint8_t *j) {
    uint32_t src = j[0] | (j[1] << 8) | (j[2] << 16), dst = j[3] | (j[4] << 8) | (j[5] << 16);
    int w = j[6] | (j[7] << 8), h = j[8] | (j[9] << 8);
    int sa = (int16_t)(j[10] | (j[11] << 8)), da = (int16_t)(j[12] | (j[13] << 8));
    uint8_t val = j[14], mode = j[15];
    for (int r = 0; r < h; r++) {
        int row = (mode & 4) ? h - 1 - r : r;
        uint32_t s = src + (int32_t)row * sa, d = dst + (int32_t)row * da;
        for (int i = 0; i < w; i++) {
            uint32_t sp = (s + i) & 0xFFFFFF, dp = (d + i) & 0xFFFFFF; uint8_t b;
            switch (mode & 3) {
            case 0: bus_wr(m, dp, bus_rd(m, sp)); break;
            case 1: bus_wr(m, dp, val); break;
            case 2: b = bus_rd(m, sp); if (b != val) bus_wr(m, dp, b); break;
            default: b = bus_rd(m, sp); { uint8_t o = bus_rd(m, dp);
                if ((b >> 4) != (val & 15)) o = (o & 0x0F) | (b & 0xF0);
                if ((b & 15) != (val & 15)) o = (o & 0xF0) | (b & 15);
                bus_wr(m, dp, o); }
            }
        }
    }
}
static void kran_cmd(Machine *m, uint8_t v) {
    uint8_t *R = &m->io[0x600];
    if (v & 8) m->kran_st &= ~2;
    if (v & 1) { kran_job(m, R); m->kran_st |= 2; }
    if (v & 2) {
        uint32_t p = R[0x11] | (R[0x12] << 8) | (R[0x13] << 16);
        for (int n = 0; n < 4096; n++) {
            uint8_t j[16]; for (int i = 0; i < 16; i++) j[i] = bus_rd(m, p + i);
            kran_job(m, j); p += 16; if (j[15] & 0x80) break;
        }
        m->kran_st |= 2;
    }
}

/* ------------------------------------------------------------ TRUHE ---- */
bool m_mount(Machine *m, int n, const char *path) {
    if (n < 0 || n > 2) return false;
    FILE *f = fopen(path, "r+b"); bool ro = false;
    if (!f) { f = fopen(path, "rb"); ro = true; }
    if (!f) return false;
    fseeko(f, 0, SEEK_END); off_t len = ftello(f);
    m_eject(m, n);
    m->drv[n] = f; m->drv_ro[n] = ro; m->drv_grow[n] = false; m->drv_blocks[n] = (uint32_t)(len / 512);
    m->truhe_in |= 1 << n; if (ro) m->truhe_in |= 0x10 << n; m->truhe_chg |= 1 << n; return true;
}
void m_eject(Machine *m, int n) {
    if (n < 0 || n > 2) return;
    if (m->drv[n]) { fclose(m->drv[n]); m->drv[n] = NULL; m->truhe_chg |= 1 << n; }
    m->drv_blocks[n] = 0; m->drv_ro[n] = false; m->truhe_in &= ~((1 << n) | (0x10 << n));
}
static uint8_t truhe_rd(Machine *m, int r) {
    switch (r) {
    case 0x00: return m->truhe_err ? 2 : 0;
    case 0x06: return m->truhe_in;
    case 0x07: return m->truhe_chg;
    case 0x08: case 0x09: case 0x0A: case 0x0B: { int d = m->io[0x901]; return d < 3 ? m->drv_blocks[d] >> (8 * (r - 8)) : 0; }
    }
    return m->io[0x900 + r];
}
static void truhe_cmd(Machine *m, uint8_t cmd) {
    int d = m->io[0x901]; uint32_t blk = m->io[0x902] | (m->io[0x903] << 8) | (m->io[0x904] << 16) | ((uint32_t)m->io[0x905] << 24);
    int page = m->io[0x90C] & 15, n = m->io[0x90D] ? m->io[0x90D] : 1; if (page + n > 16) n = 16 - page;
    m->truhe_err = 1;
    if (d > 2 || !m->drv[d] || (cmd != 1 && cmd != 2)) return;
    bool grow = m->drv_grow[d];                               /* Speicherstand: waechst beim Schreiben, ungeschrieben = Nullen */
    if (!grow && (uint64_t)blk + n > m->drv_blocks[d]) return;
    if (cmd == 2 && m->drv_ro[d]) return;
    FILE *f = m->drv[d]; uint8_t *p = &m->truhe_buf[page * 512];
    if (cmd == 1) {
        memset(p, 0, (size_t)n * 512);
        for (int i = 0; i < n; i++) {
            if (blk + i >= m->drv_blocks[d]) break;
            if (fseeko(f, (off_t)(blk + i) * 512, SEEK_SET) || fread(p + i * 512, 512, 1, f) != 1) return;
        }
    } else {
        if (fseeko(f, (off_t)blk * 512, SEEK_SET) || fwrite(p, 512, n, f) != (size_t)n) return;
        fflush(f); if (blk + n > m->drv_blocks[d]) m->drv_blocks[d] = blk + n;
    }
    m->truhe_err = 0;
}

/* ------------------------------------------------------------ KOBOLD ---- */
static uint8_t kob_rd(Machine *m, int r) {
    switch (r) {
    case 0x00: return m->spr_madr; case 0x01: return m->spr_madr >> 8; case 0x03: return m->spr_an;
    case 0x04: case 0x05: case 0x06: case 0x07: return m->koll >> (8 * (r - 4));
    }
    return 0xFF;
}
static void kob_wr(Machine *m, int r, uint8_t v) {
    switch (r) {
    case 0x00: m->spr_madr = (m->spr_madr & 0x3F00) | v; break;
    case 0x01: m->spr_madr = (m->spr_madr & 0x00FF) | ((v & 0x3F) << 8); break;
    case 0x02: m->spr_mem[m->spr_madr & 0x3FFF] = v; m->spr_madr = (m->spr_madr + 1) & 0x3FFF; break;
    case 0x03: m->spr_an = v & 1; break;
    case 0x04: m->koll = 0; break;
    }
}
/* ------------------------------------------------------------ LOTSE ---- */
static uint8_t lot_rd(Machine *m, int r) {
    switch (r) {
    case 0x00: return m->cop_liste; case 0x01: return m->cop_liste >> 8; case 0x02: return m->cop_liste >> 16;
    case 0x03: return m->cop_steuer;
    case 0x04: return ((m->cop_st == 2) << 2) | ((m->cop_st == 1 || m->cop_st == 2) << 1) | m->cop_irq;
    case 0x05: return m->cop_cmds_last; case 0x06: return m->cop_cmds_last >> 8;
    }
    return 0xFF;
}
static void lot_wr(Machine *m, int r, uint8_t v) {
    switch (r) {
    case 0x00: m->cop_liste = (m->cop_liste & 0xFFFF00) | v; break;
    case 0x01: m->cop_liste = (m->cop_liste & 0xFF00FF) | (v << 8); break;
    case 0x02: m->cop_liste = (m->cop_liste & 0x00FFFF) | ((uint32_t)v << 16); break;
    case 0x03: m->cop_steuer = v & 3; if (!(v & 1)) m->cop_st = 0; break;
    case 0x04: if (v & 1) m->cop_irq = 0; break;
    }
}

/* ------------------------------------------------------------- BOTE ----- */
static uint8_t bote_rd(Machine *m, int r) {
    const uint8_t *k = m->bote_kopf;
    switch (r) {
    case 0x00: return (m->module_off << 3) | (m->module_in << 2) | (m->bote_start << 1) | m->bote_geladen;
    case 0x01: case 0x02: case 0x03: return k[r + 3];
    case 0x04: case 0x05: case 0x06: return k[r + 4];
    case 0x07: return k[7];
    case 0x08: case 0x09: case 0x0A: return k[r + 4];
    case 0x0B: return m->bote_quelle;
    }
    return 0xFF;
}
static void bote_wr(Machine *m, int r, uint8_t v) {
    if (r == 0) { if (v & 1) m->bote_geladen = 0; if (v & 2) m->bote_start = 0; }
}
static uint8_t *fread_all(const char *path, long *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = *n > 0 ? malloc(*n) : NULL;
    if (b && fread(b, 1, *n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}
bool m_load_program(Machine *m, const char *path) {
    long n; uint8_t *b = fread_all(path, &n);
    if (!b) return false;
    if (n < 16 || memcmp(b, "MER", 3) || b[3] != 1) { free(b); return false; }
    uint32_t lade = b[4] | (b[5] << 8) | (b[6] << 16), len = b[12] | (b[13] << 8) | (b[14] << 16) | ((uint32_t)b[15] << 24);
    if (len > (uint32_t)(n - 16)) len = n - 16;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = (lade + i) & 0xFFFFFF; int bank = a >> 16;
        if (bank < 4) m->ram[a] = b[16 + i];
        else if (bank == 0xFF || !m->sdram) continue;
        else if (m->module_in && bank >= 0x40 && bank < 0x80) continue;      /* steckendes Modul bleibt heil */
        else m->sdram[a] = b[16 + i];
    }
    memcpy(m->bote_kopf, b, 16); m->bote_kopf[12] = len; m->bote_kopf[13] = len >> 8; m->bote_kopf[14] = len >> 16; m->bote_kopf[15] = 0;
    m->bote_geladen = 1; m->bote_quelle = 1;
    if (b[7] & 1) { m->bote_start = 1; m->cpu.nmi_edge = true; }              /* Autostart: NMI */
    free(b); return true;
}
void m_eject_module(Machine *m) {
    if (!m->module_in) return;
    m->module_in = false; free(m->module); m->module = NULL; m->module_len = 0; m->module_name[0] = 0;
    m_eject(m, 0); m_reset(m, false);
}
bool m_insert_module(Machine *m, const char *path) {
    long n; uint8_t *b = fread_all(path, &n);
    if (!b || n > 0x400000 || !m->sdram) { free(b); return false; }
    free(m->module); m->module = b; m->module_len = n; m->module_in = true;
    memcpy(m->sdram + 0x400000, b, n);
    const char *s = strrchr(path, '/'), *t = strrchr(path, '\\'); if (t > s) s = t; snprintf(m->module_name, sizeof m->module_name, "%s", s ? s + 1 : path);
    char sav[1100]; snprintf(sav, sizeof sav, "%s", path);                    /* Speicherstand neben dem Modul */
    char *dot = strrchr(sav, '.'); if (dot && dot > sav + (s ? s - path : 0)) *dot = 0;
    strncat(sav, ".sav", sizeof sav - strlen(sav) - 1);
    FILE *f = fopen(sav, "ab"); if (f) fclose(f);
    if (m_mount(m, 0, sav)) m->drv_grow[0] = true;
    m_reset(m, false); return true;
}
static uint8_t io_rd(Machine *m, uint16_t a) {
    if (a >> 8 == 0xC5 || a >> 8 == 0xCC) return orgel_rd(&m->org, (a >> 8) == 0xCC, a & 0xFF);
    if (a >> 8 == 0xC2) return bote_rd(m, a & 0xFF);
    if (a >> 8 == 0xC4) return kob_rd(m, a & 0xFF);
    if (a >> 8 == 0xC7) return lot_rd(m, a & 0xFF);
    if (a >= 0xCA00 && a < 0xCC00) return m->truhe_buf[(m->io[0x90C] & 15) * 512 + (a & 0x1FF)];
    if (a >> 8 == 0xC9) return truhe_rd(m, a & 0xFF);
    if (a == 0xC610) return m->kran_st;
    switch (a >> 8) {
    case 0xC0: return pin_rd(m, a & 0xFF);
    case 0xC1: return pf_rd(m, a & 0xFF);
    case 0xC8: return a == 0xC800 ? m->system : 0xFF;
    case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
    case 0xC9: case 0xCA: case 0xCB: case 0xCC: return m->io[a & 0xFFF];
    }
    return 0xFF;
}
static void io_wr(Machine *m, uint16_t a, uint8_t v) {
    if (a >> 8 == 0xC5 || a >> 8 == 0xCC) { orgel_wr(&m->org, (a >> 8) == 0xCC, a & 0xFF, v); return; }
    if (a >> 8 == 0xC2) { bote_wr(m, a & 0xFF, v); return; }
    if (a >> 8 == 0xC4) { kob_wr(m, a & 0xFF, v); return; }
    if (a >> 8 == 0xC7) { lot_wr(m, a & 0xFF, v); return; }
    if (a >= 0xCA00 && a < 0xCC00) { m->truhe_buf[(m->io[0x90C] & 15) * 512 + (a & 0x1FF)] = v; return; }
    if (a == 0xC900) { truhe_cmd(m, v); return; }
    if (a == 0xC907) { m->truhe_chg &= ~v; return; }
    if (a == 0xC906) return;
    if (a == 0xC610) { kran_cmd(m, v); return; }
    switch (a >> 8) {
    case 0xC0: pin_wr(m, a & 0xFF, v); return;
    case 0xC1: pf_wr(m, a & 0xFF, v); return;
    case 0xC8: if (a == 0xC800) m->system = v; return;
    case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
    case 0xC9: case 0xCA: case 0xCB: case 0xCC: m->io[a & 0xFFF] = v; return;
    }
}
static uint8_t bus_rd(void *ctx, uint32_t a) {
    Machine *m = ctx; uint8_t bank = a >> 16; uint16_t o = a;
    if (bank == 0) { if (o < 0xC000) return m->ram[o]; if (o < 0xD000) return io_rd(m, o); return m->rom_kern[o & 0x3FFF]; }
    if (bank == 1 && (m->system & 1) && o < 0x2000) return m->ram[o];
    if (bank < 4) return m->ram[a];
    if (bank == 0xFF) { switch (o >> 14) { case 0: return m->rom_basic[o & 0x3FFF]; case 1: return m->rom_dos[o & 0x3FFF];
                                           case 2: return m->rom_song[o & 0x3FFF]; default: return m->rom_werk[o & 0x3FFF]; } }
    if (bank == 0xFD) return m->dr[o];
    if (!m->sdram) return 0xFF;
    return m->sdram[a];
}
static void bus_wr(void *ctx, uint32_t a, uint8_t v) {
    Machine *m = ctx; uint8_t bank = a >> 16; uint16_t o = a;
    if (bank == 0) { if (o < 0xC000) m->ram[o] = v; else if (o < 0xD000) io_wr(m, o, v); return; }
    if (bank == 1 && (m->system & 1) && o < 0x2000) { m->ram[o] = v; return; }
    if (bank < 4) { m->ram[a] = v; return; }
    if (bank == 0xFD) { m->dr[o] = v; return; }
    if (bank == 0xFF || !m->sdram) return;
    if (m->module_in && bank >= 0x40 && bank < 0x80) return;
    m->sdram[a] = v;
}

/* ---------------------------------------------------------- Bildaufbau -- */
static void layer(Machine *m, int L, int y, uint8_t *px, uint8_t *fromB) {
    const uint8_t *r = &m->pin[L ? 0x20 : 0x00];
    int typ = r[0] & 7, opt = r[1];
    uint32_t data = r[2] | (r[3] << 8) | (r[4] << 16), pat = r[5] | (r[6] << 8) | (r[7] << 16);
    int sx = (m->pin[L ? 0x30 : 0x10] | (m->pin[L ? 0x31 : 0x11] << 8)) & 511, sy = m->pin[L ? 0x32 : 0x12];
    uint8_t line[SCR_W]; memset(line, 0, sizeof line);
    const uint8_t *ram = m->ram;
    switch (typ) {
    case 1: {
        int cols = (opt & 1) ? 80 : 40, fy = y & 7; uint32_t base = data + (y >> 3) * cols * 2;
        for (int cx = 0; cx < cols; cx++) {
            uint8_t ch = ram[(base + cx * 2) & CHIPMASK], col = ram[(base + cx * 2 + 1) & CHIPMASK];
            uint8_t bits = ram[(pat + ch * 8 + fy) & CHIPMASK];
            for (int b = 0; b < 8; b++) {
                uint8_t p = (bits & (0x80 >> b)) ? (col & 15) : (col >> 4);
                if (cols == 40) { line[cx * 16 + b * 2] = p; line[cx * 16 + b * 2 + 1] = p; } else line[cx * 8 + b] = p;
            }
        }
    } break;
    case 2: {
        int wy = (y + sy) & 255;
        for (int x = 0; x < 320; x++) {
            int wx = (x + sx) & 511;
            uint32_t ma = data + ((wy >> 3) * 64 + (wx >> 3)) * 2;
            uint16_t e = ram[ma & CHIPMASK] | (ram[(ma + 1) & CHIPMASK] << 8);
            int tx = wx & 7, ty = wy & 7; if (e & 0x400) tx = 7 - tx; if (e & 0x800) ty = 7 - ty;
            uint8_t byte = ram[(pat + (e & 0x3FF) * 32 + ty * 4 + (tx >> 1)) & CHIPMASK];
            uint8_t v = (tx & 1) ? (byte & 15) : (byte >> 4);
            uint8_t p = v ? ((e >> 12) * 16 + v) : 0;
            line[x * 2] = line[x * 2 + 1] = p;
        }
    } break;
    case 3: {
        uint32_t base = data + y * 160; int bank = opt >> 4;
        for (int x = 0; x < 320; x++) {
            uint8_t byte = ram[(base + (x >> 1)) & CHIPMASK], v = (x & 1) ? (byte & 15) : (byte >> 4);
            line[x * 2] = line[x * 2 + 1] = bank * 16 + v;
        }
    } break;
    case 4: {
        uint32_t base = data + y * 320;
        for (int x = 0; x < 320; x++) line[x * 2] = line[x * 2 + 1] = ram[(base + x) & CHIPMASK];
    } break;
    default: break;
    }
    if (L == 0) memcpy(px, line, SCR_W);
    else if (typ) for (int x = 0; x < SCR_W; x++) if (line[x]) { px[x] = line[x]; if (fromB) fromB[x] = 1; }
}

/* KOBOLD: ein Musterpunkt (0 = durchsichtig / ausserhalb) des Sprites s, Zeile sl, Spalte sc auf dem Schirm */
static int spr_nib(Machine *m, int s, int sl, int sc) {
    const uint8_t *t = &m->io[0x300 + s * 8]; uint8_t att = t[5]; bool gross = att & 0x80; int w = gross ? 32 : 16;
    if (sl < 0 || sl >= w || sc < 0 || sc >= w) return 0;
    int r = gross ? sl >> 1 : sl; if (att & 0x20) r = 15 - r;
    int c = gross ? sc >> 1 : sc; if (att & 0x10) c = 15 - c;
    uint8_t b = m->spr_mem[((t[4] & 127) * 128 + r * 8 + (c >> 1)) & 0x3FFF];
    return (c & 1) ? (b & 15) : (b >> 4);
}
typedef struct { uint8_t valid[320], col[320], back[320], sh[320], tint[320], who[320]; } SprLine;
static void spr_place(Machine *m, SprLine *L, int s, int x, int col, bool tint) {
    const uint8_t *t = &m->io[0x300 + s * 8];
    if (!L->valid[x]) {
        L->valid[x] = 1; L->col[x] = col; L->back[x] = (t[5] >> 6) & 1; L->sh[x] = (t[6] >> 2) & 1; L->tint[x] = tint; L->who[x] = s;
    } else if (!tint && !L->tint[x]) { m->koll |= (1u << s) | (1u << L->who[x]); }
}
static void sprites(Machine *m, int y, SprLine *L) {
    bool kon = m->pin[0x34] & 1;
    for (int s = 0; s < 32; s++) {
        const uint8_t *t = &m->io[0x300 + s * 8];
        if (!(t[6] & 1)) continue;
        int sx = t[0] | ((t[1] & 1) << 8), sy = t[2] | ((t[3] & 1) << 8), att = t[5], w = (att & 0x80) ? 32 : 16;
        if (kon && (t[6] & 2)) {
            int q = (y + 33 - sy) & 0x3FF; if (q >= w + 2) continue;
            int sl = q - 1;
            for (int c = -1; c <= w; c++) {
                int x = (sx + c - 32) & 0x3FF; if (x >= 320) continue;
                int own = spr_nib(m, s, sl, c);
                if (own) spr_place(m, L, s, x, ((att & 15) << 4) | own, false);
                else if ((spr_nib(m, s, sl, c - 1) || spr_nib(m, s, sl, c + 1) || spr_nib(m, s, sl - 1, c) || spr_nib(m, s, sl + 1, c)) && !L->valid[x])
                    spr_place(m, L, s, x, m->pin[0x35], true);
            }
        } else {
            int yy = (y + 32 - sy) & 0x1FF; if (yy >= w) continue;
            for (int c = 0; c < w; c++) {
                int x = (sx + c - 32) & 0x3FF, n = spr_nib(m, s, yy, c);
                if (x < 320 && n) spr_place(m, L, s, x, ((att & 15) << 4) | n, false);
            }
        }
    }
}
/* Zeile y vorzeichnen: Ebenen, Sprites, Schlagschatten (Tusche) -> Palettenindizes */
static void prerender(Machine *m, int y, uint8_t *idx, uint8_t *shd) {
    uint8_t fromB[SCR_W], vis[SCR_W]; memset(fromB, 0, sizeof fromB);
    layer(m, 0, y, idx, NULL); layer(m, 1, y, idx, fromB);
    SprLine L; memset(&L, 0, sizeof L);
    if (m->spr_an) sprites(m, y, &L);
    uint8_t t1 = m->pin[0x34] & 2, t2 = m->pin[0x34] & 4; uint8_t *H = m->sch_hist[y & 7];
    for (int d = 0; d < SCR_W; d++) { int x = d >> 1; vis[d] = L.valid[x] && !(L.back[x] && fromB[d]); if (vis[d]) idx[d] = L.col[x]; }
    for (int x = 0; x < 320; x++) H[x] = ((L.valid[x] && L.sh[x] && t1) ? 1 : 0) | ((fromB[2 * x] && t2) ? 2 : 0);
    int dx = m->pin[0x36] & 7, dy = m->pin[0x37] & 7; if (!dy) dy = 1;
    for (int d = 0; d < SCR_W; d++) {
        shd[d] = 0; int x = d >> 1;
        if ((t1 || t2) && x >= dx && y >= dy) { uint8_t q = m->sch_hist[(y - dy) & 7][x - dx];
            shd[d] = !vis[d] && (((q & 1) && t1) || ((q & 2) && t2 && !fromB[d])); }
    }
}
/* Palette anwenden, Punkt fuer Punkt (Palettenwechsel mitten in der Zeile wirken ab dort) */
static void conv_to(Machine *m, int upto) {
    if (upto > SCR_W) upto = SCR_W;
    int lv = m->pin[0x52] & 15;
    for (; m->conv_x < upto; m->conv_x++) {
        int x = m->conv_x; uint8_t i = m->cur_idx[x]; uint32_t c = m->pal[i];
        m->bild[x] = m->cur_sh[x] ? ((c >> 1) & 0x7F7F7F) : c;
        bool g = (m->lmask[i >> 3] >> (i & 7)) & 1;
        m->glw[x] = g; m->src[x] = (g && lv) ? c : 0;
    }
}
static void conv_sync(Machine *m) {
    if (m->cur_line < 0 || m->cur_line >= SCR_H) return;
    uint64_t clk = m->clk24 + 3 * (m->cpu.cyc - m->cyc_ref);
    int dot = (int)(clk / 2) - 124; if (dot < 0) dot = 0;
    conv_to(m, dot);
}
/* Leuchten: waagrechter Hof = Dreieck ueber +-15 Punkte (zwei Kastenfilter zu 16), Summe / 256 */
static void hof_line(const uint32_t *s, uint32_t *h) {
    static int P[SCR_W + 1], b1[SCR_W + 15], R[SCR_W + 16];
    for (int sh = 16, ch = 0; ch < 3; ch++, sh -= 8) {
        P[0] = 0; for (int i = 0; i < SCR_W; i++) P[i + 1] = P[i] + ((s[i] >> sh) & 255);
        for (int k = 0; k < SCR_W + 15; k++) { int hi = k + 1 > SCR_W ? SCR_W : k + 1, lo = k - 15 < 0 ? 0 : k - 15; b1[k] = P[hi] - P[lo]; }
        R[0] = 0; for (int k = 0; k < SCR_W + 15; k++) R[k + 1] = R[k] + b1[k];
        for (int x = 0; x < SCR_W; x++) { int v = (R[x + 16] - R[x]) >> 8; if (v > 255) v = 255; if (ch == 0) h[x] = 0; h[x] |= (uint32_t)v << sh; }
    }
}
static void finish_line(Machine *m, int y) {
    conv_to(m, SCR_W);
    int lv = m->pin[0x52] & 15; uint32_t h0[SCR_W];
    if (lv) hof_line(m->src, h0); else memset(h0, 0, sizeof h0);
    uint32_t *o = &m->fb[y * SCR_W];
    for (int x = 0; x < SCR_W; x++) {
        uint32_t c = m->bild[x], r = 0; int st = m->glw[x] ? 0 : lv;
        for (int sh = 16; sh >= 0; sh -= 8) {
            int v = (((m->hz[1][x] >> sh) & 255) + 2 * ((m->hz[0][x] >> sh) & 255) + ((h0[x] >> sh) & 255)) >> 2;
            int t = ((c >> sh) & 255) + ((v * st) >> 3); if (t > 255) t = 255; r |= (uint32_t)t << sh;
        }
        o[x] = 0xFF000000u | r;
    }
    memcpy(m->hz[1], m->hz[0], sizeof m->hz[0]); memcpy(m->hz[0], h0, sizeof h0);
}
/* Glanz: in der Austastluecke vor Zeile y die Verlaufsfarben in die Palette schreiben */
static void glanz(Machine *m, int y) {
    for (int k = 0; k < 16; k++) {
        if (!((m->gl_an >> k) & 1) || y < m->gl_von[k] || y > m->gl_bis[k]) continue;
        const uint8_t st[3] = { m->gl_r0[k], m->gl_g0[k], m->gl_b0[k] }; uint32_t rgb = 0;
        for (int c = 0; c < 3; c++) {
            uint16_t a = m->gl_a[c][k];
            if (y == m->gl_von[k]) a = (st[c] << 8) | 0x80;
            else { int32_t t = (int32_t)a + (int16_t)m->gl_d[c][k]; a = t < 0 ? 0 : t > 0xFFFF ? 0xFFFF : t; }
            m->gl_a[c][k] = a; rgb = (rgb << 8) | (a >> 8);
        }
        m->pal[m->gl_idx[k]] = rgb;
    }
}
/* ---------------------------------------------------------------- LOTSE -- */
static int ord_of(Machine *m, int z) { return z >= 240 ? z - 240 : z + m->nlines - 240; }
static void copper_run(Machine *m, int ln, uint64_t clk) {
    if (!(m->cop_steuer & 1)) { m->cop_st = 0; return; }
    int cp = (int)(clk / 2), o = ord_of(m, ln);
    for (int budget = 0; budget < 256; budget++) {
        if (m->cop_st == 0 || m->cop_st == 3) return;
        if (m->cop_st == 2) { if (o > m->cop_wz || (o == m->cop_wz && cp >= m->cop_wx)) m->cop_st = 1; else return; }
        uint8_t b[4]; for (int i = 0; i < 4; i++) b[i] = m->ram[(m->cop_fp + i) & CHIPMASK];
        m->cop_fp = (m->cop_fp + 4) & CHIPMASK; m->cop_cmds++;
        switch (b[0]) {
        case 0x00: m->cop_st = 3; break;
        case 0x01: { int z = b[1] | ((b[2] & 1) << 8), x = b[3] | ((b[2] & 2) << 7);
                     m->cop_wz = ord_of(m, z); m->cop_wx = x == 0x1FF ? 0 : 124 + 2 * x; m->cop_st = 2; } break;
        case 0x02: if ((b[2] >> 3) == 0x18 && (b[2] & 7) != 2) io_wr(m, (b[2] << 8) | b[1], b[3]); break;
        case 0x03: m->cop_fp = (b[1] | (b[2] << 8) | (b[3] << 16)) & CHIPMASK; break;
        case 0x04: if (m->cop_steuer & 2) m->cop_irq = 1; break;
        case 0x06: pin_wr(m, 0x28, b[1]); pin_wr(m, 0x29, b[2]); pin_wr(m, 0x2A, b[3]); break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------- Ablauf ------ */
static void update_rtc(Machine *m) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
#define BCD(n) ((uint8_t)((((n) / 10) << 4) | ((n) % 10)))
    m->rtc[0] = BCD(tm->tm_sec); m->rtc[1] = BCD(tm->tm_min); m->rtc[2] = BCD(tm->tm_hour); m->rtc[3] = BCD(tm->tm_mday);
    m->rtc[4] = BCD(tm->tm_mon + 1); m->rtc[5] = BCD(tm->tm_year % 100); m->rtc[6] = tm->tm_wday;
}
static bool read_file(const char *dir, const char *name, uint8_t *dst, size_t n) {
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return false;
    size_t r = fread(dst, 1, n, f); fclose(f); return r == n;
}
bool m_load_rom(Machine *m, const char *dir) {
    return read_file(dir, "kern.rom", m->rom_kern, 0x4000) && read_file(dir, "basic.rom", m->rom_basic, 0x4000) &&
           read_file(dir, "dos.rom", m->rom_dos, 0x4000) && read_file(dir, "song.rom", m->rom_song, 0x4000) &&
           read_file(dir, "werkstatt.rom", m->rom_werk, 0x4000);
}
void m_set_pal(Machine *m, bool pal) { m->pal_mode = pal; m->nlines = pal ? 312 : 262; }
Machine *m_new(const char *romdir, bool sdram) {
    Machine *m = calloc(1, sizeof *m);
    if (!m_load_rom(m, romdir)) { free(m); return NULL; }
    if (sdram) m->sdram = calloc(1, 0x1000000);
    m->cpu.ctx = m; m->cpu.rd = bus_rd; m->cpu.wr = bus_wr;
    m->dr = m->draht;
    m->module_fix = true;                                     /* nur fuer genau dieses Kern-ROM */
    m->fix_ok = !memcmp(&m->rom_kern[0x1DBB], "\x4C\x89\xE4", 3) && !memcmp(&m->rom_kern[0x1E82], "\xE2\x20\x9C\x03\xC7", 5);
    m_set_pal(m, false); m_reset(m, true); return m;
}
void m_reset(Machine *m, bool power) {
    if (power) { memset(m->ram, 0, sizeof m->ram); if (m->sdram) { memset(m->sdram, 0, 0x1000000); if (m->module_in) memcpy(m->sdram + 0x400000, m->module, m->module_len); } }
    m->bote_geladen = m->bote_start = 0;
    memset(m->pin, 0, sizeof m->pin); memset(m->pal, 0, sizeof m->pal); memset(m->io, 0, sizeof m->io);
    orgel_reset(&m->org); m->aw = m->ar = 0; m->asum_l = m->asum_r = 0; m->acnt = 0; m->aphase = 0;
    m->system = 0; m->kran_st = 0; m->kq_n = 0; m->pf_irq_st = m->pf_irq_en = 0; m->irq_en_pin = m->irq_st_pin = 0;
    m->ta_ctl = m->tb_ctl = 0; m->us = 0; m->us_frz = false; m->us_acc = 0; m->line = 0; m->clk24 = 0;
    m->mxmax = 319; m->mymax = 239; m->pin[0x36] = 2; m->pin[0x37] = 2;
    m->spr_madr = 0; m->spr_an = 0; m->koll = 0; m->cop_liste = 0; m->cop_steuer = 0; m->cop_st = 0; m->cop_irq = 0; m->cop_cmds = m->cop_cmds_last = 0;
    memset(m->spr_mem, 0, sizeof m->spr_mem); memset(m->nxt_idx, 0, sizeof m->nxt_idx); memset(m->nxt_sh, 0, sizeof m->nxt_sh);
    memset(m->hz, 0, sizeof m->hz); memset(m->sch_hist, 0, sizeof m->sch_hist); memset(m->lmask, 0, sizeof m->lmask); m->lm_idx = 0;
    m->gl_nr = 0; m->gl_an = 0; memset(m->gl_idx, 0, sizeof m->gl_idx); memset(m->gl_von, 0, sizeof m->gl_von); memset(m->gl_bis, 0, sizeof m->gl_bis);
    memset(m->gl_d, 0, sizeof m->gl_d); memset(m->gl_a, 0, sizeof m->gl_a); m->cur_line = -1; m->cyc_ref = 0;
    update_rtc(m); cpu_reset(&m->cpu);
}
static void run_cpu_until(Machine *m, int clk) {
    Cpu *c = &m->cpu;
    while (m->clk24 < clk) {
        c->irq = ((m->pf_irq_st & m->pf_irq_en) | (m->irq_st_pin & m->irq_en_pin) | ((m->kran_st & 2) && (m->io[0x614] & 1)) | m->cop_irq) != 0;
        m->cyc_ref = c->cyc;
        /* Ausgleich fuer Module: Kehrt ein Modul mit RTL zurueck, ruft der Kern anzeige_zuruecksetzen NICHT auf (nur beim
           Einschalten und beim Fernstart). Ebenen, Copper und Glanz des Moduls blieben stehen, BASIC laeuft unsichtbar.
           Hier wird vor dem "JMP basic_kalt" (Kern-ROM $DDBB) ein JSR auf $DE82 eingeschoben, wie es der Kern selbst tun sollte. */
        if (m->module_fix && m->fix_ok && !c->e && c->pbr == 0 && c->pc == 0xDDBB && !m->fix_done) {
            m->fix_done = true;
            bus_wr(m, c->s, 0xDD); c->s--; bus_wr(m, c->s, 0xBA); c->s--;       /* Ruecksprung auf $DDBA+1 */
            c->pc = 0xDE82;
        } else if (c->pc == 0xE489) m->fix_done = false;
        int cy = cpu_step(c);
        if (m->trace) { m->trace--; fprintf(stderr, "%02X:%04X A=%04X X=%04X Y=%04X S=%04X P=%02X\n", c->pbr, c->pc, c->a, c->x, c->y, c->s, c->p); }
        m->clk24 += cy * 3; m->cyc_ref = c->cyc;
        m->us_acc += cy; while (m->us_acc >= 8) { m->us_acc -= 8; tick_us(m); }
        copper_run(m, m->line, m->clk24);
    }
}
static void begin_line(Machine *m, int ln) {
    if (m->cur_line >= 0 && m->cur_line < SCR_H) finish_line(m, m->cur_line);
    m->line = ln; m->cur_line = ln; m->conv_x = 0;
    if (ln == 240) {
        if (m->irq_en_pin & 1) m->irq_st_pin |= 1;
        m->frame_count++;
        memset(m->hz, 0, sizeof m->hz);
        m->cop_cmds_last = m->cop_cmds; m->cop_cmds = 0;                       /* LOTSE: neues Bild von vorn */
        if (m->cop_steuer & 1) { m->cop_fp = m->cop_liste & CHIPMASK; m->cop_st = 1; }
    }
    if (ln == (m->pin[0x0D] | (m->pin[0x0E] << 8)) && (m->irq_en_pin & 2)) m->irq_st_pin |= 2;
    if (ln < SCR_H) { memcpy(m->cur_idx, m->nxt_idx, SCR_W); memcpy(m->cur_sh, m->nxt_sh, SCR_W); glanz(m, ln); }
    copper_run(m, ln, m->clk24);
}
void m_frame(Machine *m) {
    for (int ln = 0; ln < m->nlines; ln++) {
        begin_line(m, ln);
        run_cpu_until(m, 248);                                     /* Austastluecke: 124 Punkte */
        int nx = (ln + 1) % m->nlines;
        if (nx < SCR_H) prerender(m, nx, m->nxt_idx, m->nxt_sh);  /* PINSEL zeichnet eine Zeile voraus */
        run_cpu_until(m, 1528);
        m->clk24 -= 1528;
    }
}

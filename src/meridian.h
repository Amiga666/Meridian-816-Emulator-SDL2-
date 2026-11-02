/* MERIDIAN 816 – Maschine (Speicherkarte, PINSEL, PFORTE, ...). GPL-3.0-or-later */
#pragma once
#include <stdio.h>
#include "cpu65816.h"
#include "orgel.h"
#define SCR_W 640
#define SCR_H 240
typedef struct { uint8_t code, info; } KeyEv;
typedef struct Machine {
    Cpu cpu;
    uint8_t ram[0x40000];                 /* Chip-RAM: Baenke 0-3 */
    uint8_t *sdram;                       /* 16 MB, Baenke 04-FE (NULL = ohne Zusatzspeicher) */
    uint8_t draht[0x10000];               /* Bank $FD (ohne Dienst) */
    volatile uint8_t *dr;                 /* Bank $FD: draht[] oder die gemeinsame Fensterdatei des Netzdienstes */
    uint8_t rom_kern[0x4000], rom_basic[0x4000], rom_dos[0x4000], rom_song[0x4000], rom_werk[0x4000];
    uint8_t *module; uint32_t module_len; bool module_in, module_off; char module_name[64]; /* Modul-ROM $40-$7F */
    uint8_t bote_kopf[16], bote_geladen, bote_start, bote_quelle;
    bool module_fix, fix_ok, fix_done;      /* Modul-Rueckkehr: Anzeige zuruecksetzen (siehe run_cpu_until) */
    uint8_t io[0x1000];                   /* Registerfelder der noch nicht emulierten Chips */
    uint8_t system, kran_st;
    /* PINSEL */
    uint8_t pin[0x100]; uint32_t pal[256]; uint8_t pal_idx, pal_lo, lpal_idx, lpal_lo;
    uint8_t irq_en_pin, irq_st_pin; int line, nlines; uint8_t frame_count; bool pal_mode;
    /* PFORTE */
    KeyEv keyq[16]; int kq_n; uint8_t pf_irq_st, pf_irq_en, layout;
    uint16_t joy1, joy2;
    uint64_t us; uint32_t us_frozen; bool us_frz; int us_acc;
    uint16_t ta_start, ta_cnt, tb_start, tb_cnt; uint8_t ta_ctl, tb_ctl;
    uint16_t mx, my, mxmax, mymax; uint8_t mbtn; int8_t mwheel; uint16_t mx_f, my_f; uint8_t mbtn_f; int8_t mw_f;
    uint8_t rtc[8], rtc_fest[8]; long rtc_versatz;   /* Kalenderuhr: gestellter Stand, festgehaltener Stand ($37), Versatz zur Rechneruhr */
    /* TRUHE: drei Laufwerke, Puffer 8 KB = 16 Seiten */
    FILE *drv[3]; uint32_t drv_blocks[3]; bool drv_ro[3], drv_grow[3]; uint8_t truhe_in, truhe_chg, truhe_err; uint8_t truhe_buf[8192];
    /* KOBOLD */
    uint8_t spr_mem[16384]; uint16_t spr_madr; uint8_t spr_an; uint32_t koll;
    /* LOTSE */
    uint32_t cop_liste, cop_fp; uint8_t cop_steuer, cop_st, cop_irq; uint16_t cop_cmds, cop_cmds_last;
    int cop_wz, cop_wx;                   /* Wartepunkt: Zeilenfolge, Punkt ab Austastluecke */
    /* PINSEL-Zeilenpipeline: Zeile y+1 wird beim Beginn der Zeile y vorgezeichnet */
    uint8_t nxt_idx[SCR_W], cur_idx[SCR_W], nxt_sh[SCR_W], cur_sh[SCR_W]; uint8_t sch_hist[8][320];
    uint32_t bild[SCR_W], src[SCR_W]; uint8_t glw[SCR_W]; int conv_x; uint32_t hz[3][SCR_W]; int cur_line;
    uint8_t lmask[32], lm_idx;            /* Leuchten */
    uint8_t gl_nr, gl_idx[16], gl_von[16], gl_bis[16], gl_r0[16], gl_g0[16], gl_b0[16]; uint16_t gl_d[3][16], gl_a[3][16], gl_an;
    uint64_t cyc_ref;
    /* ORGEL und Audio-Ausgabe (48 kHz Stereo aus dem 1-MHz-Mischer) */
    Orgel org; bool audio_on; int64_t asum_l, asum_r; int acnt; int64_t aphase; int16_t aring[16384 * 2]; unsigned aw, ar;
    /* Takt */
    int clk24;                            /* 24-MHz-Takte innerhalb der Zeile */
    uint32_t fb[SCR_W * SCR_H];
    uint64_t trace;
} Machine;

Machine *m_new(const char *romdir, bool sdram);
bool m_load_rom(Machine *m, const char *dir);
void m_reset(Machine *m, bool power);     /* power = Kaltstart (RAM loeschen) */
void m_frame(Machine *m);                 /* ein Bild rechnen */
void m_key(Machine *m, bool ext, uint8_t code, bool release);
void m_mouse(Machine *m, int x, int y, uint8_t buttons);
void m_set_joy(Machine *m, int n, uint16_t bits);
void m_set_pal(Machine *m, bool pal);
bool m_load_program(Machine *m, const char *path);   /* .MER wie MiSTer-Menue "Load program" */
bool m_insert_module(Machine *m, const char *path);  /* .MOD, startet neu; Speicherstand .sav = Laufwerk 0 */
void m_eject_module(Machine *m);
bool m_mount(Machine *m, int drive, const char *path);   /* Laufwerk 0-2; Image-Datei einlegen */
void m_eject(Machine *m, int drive);
size_t m_audio_read(Machine *m, int16_t *out, size_t max_frames);   /* Stereo-Frames abholen */

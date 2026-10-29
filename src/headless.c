/* Testlauf ohne Fenster: meridian_headless <roms> <bilder> [out.ppm] [trace] */
#include "meridian.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    Machine *m = m_new(argv[1], true); if (!m) { fprintf(stderr, "ROMs fehlen\n"); return 1; }
    if (argc > 4) m->trace = strtoull(argv[4], 0, 10);
    for (int i = 0, n = atoi(argv[2]); i < n; i++) m_frame(m);
    if (argc > 3) { FILE *f = fopen(argv[3], "wb"); fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
        for (int i = 0; i < SCR_W * SCR_H; i++) { uint32_t p = m->fb[i]; fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); } fclose(f); }
    fprintf(stderr, "PC=%02X:%04X P=%02X e=%d frames=%d stopped=%d\n", m->cpu.pbr, m->cpu.pc, m->cpu.p, m->cpu.e, atoi(argv[2]), m->cpu.stopped);
    for (int y = 0; y < 30; y++) { for (int x = 0; x < 40; x++) { int ch = m->ram[0x400 + y*80 + x*2]; putchar(ch >= 32 && ch < 127 ? ch : '.'); } putchar('\n'); }
    return 0;
}

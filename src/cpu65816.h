/* WDC 65C816 – kompakter, bus-zyklen-zaehlender Interpreter. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { F_C=1, F_Z=2, F_I=4, F_D=8, F_X=0x10, F_M=0x20, F_V=0x40, F_N=0x80 };
typedef struct Cpu {
    uint16_t a, x, y, s, d, pc;
    uint8_t  dbr, pbr, p;
    bool     e, stopped, waiting, nmi_edge, irq;
    uint64_t cyc;                       /* Buszyklen seit Reset */
    void *ctx;
    uint8_t (*rd)(void *, uint32_t);
    void    (*wr)(void *, uint32_t, uint8_t);
} Cpu;
void cpu_reset(Cpu *c);
int  cpu_step(Cpu *c);                  /* liefert verbrauchte Zyklen */

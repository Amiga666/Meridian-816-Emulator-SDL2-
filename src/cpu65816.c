#include "cpu65816.h"
#include <stddef.h>
#define M8(c) ((c)->e || ((c)->p & F_M))
#define X8(c) ((c)->e || ((c)->p & F_X))
#define SETF(f, cond) (c->p = (cond) ? (c->p | (f)) : (c->p & ~(f)))
enum { M_DP, M_DPX, M_DPY, M_DPI, M_DPLI, M_DPXI, M_DPIY, M_DPLIY, M_ABS, M_ABSX,
       M_ABSY, M_LONG, M_LONGX, M_SR, M_SRIY, M_IMM, M_NONE };

static inline uint8_t rd(Cpu *c, uint32_t a) { c->cyc++; return c->rd(c->ctx, a & 0xFFFFFF); }
static inline void wr(Cpu *c, uint32_t a, uint8_t v) { c->cyc++; c->wr(c->ctx, a & 0xFFFFFF, v); }
static inline uint8_t f8(Cpu *c) { return rd(c, ((uint32_t)c->pbr << 16) | c->pc++); }
static inline uint16_t f16(Cpu *c) { uint16_t l = f8(c); return l | (f8(c) << 8); }
static inline uint32_t f24(Cpu *c) { uint32_t l = f16(c); return l | ((uint32_t)f8(c) << 16); }
static inline uint16_t rd16(Cpu *c, uint32_t a) { uint16_t l = rd(c, a); return l | (rd(c, a + 1) << 8); }
static inline void wr16(Cpu *c, uint32_t a, uint16_t v) { wr(c, a, v); wr(c, a + 1, v >> 8); }
static inline uint16_t rdp16(Cpu *c, uint32_t a) { uint16_t l = rd(c, a & 0xFFFF); return l | (rd(c, (a + 1) & 0xFFFF) << 8); }
static inline uint32_t rdp24(Cpu *c, uint32_t a) { uint32_t l = rdp16(c, a); return l | ((uint32_t)rd(c, (a + 2) & 0xFFFF) << 16); }

static void push8(Cpu *c, uint8_t v) { wr(c, c->s, v); c->s--; if (c->e) c->s = 0x100 | (c->s & 0xFF); }
static uint8_t pull8(Cpu *c) { c->s++; if (c->e) c->s = 0x100 | (c->s & 0xFF); return rd(c, c->s); }
static void push16(Cpu *c, uint16_t v) { push8(c, v >> 8); push8(c, v); }
static uint16_t pull16(Cpu *c) { uint16_t l = pull8(c); return l | (pull8(c) << 8); }

static uint32_t dpa(Cpu *c, uint8_t off, uint16_t idx) {
    if (c->e && !(c->d & 0xFF)) return (c->d & 0xFF00) | ((off + idx) & 0xFF);
    return (uint16_t)(c->d + off + idx);
}
static uint32_t ea(Cpu *c, int m) {
    uint32_t t; uint16_t w; uint32_t db = (uint32_t)c->dbr << 16;
    switch (m) {
    case M_DP:   return dpa(c, f8(c), 0);
    case M_DPX:  return dpa(c, f8(c), c->x);
    case M_DPY:  return dpa(c, f8(c), c->y);
    case M_DPI:  t = dpa(c, f8(c), 0); return db | rdp16(c, t);
    case M_DPLI: t = dpa(c, f8(c), 0); return rdp24(c, t);
    case M_DPXI: t = dpa(c, f8(c), c->x); return db | rdp16(c, t);
    case M_DPIY: t = dpa(c, f8(c), 0); w = rdp16(c, t); return (db + w + c->y) & 0xFFFFFF;
    case M_DPLIY:t = dpa(c, f8(c), 0); return (rdp24(c, t) + c->y) & 0xFFFFFF;
    case M_ABS:  return db | f16(c);
    case M_ABSX: return (db + f16(c) + c->x) & 0xFFFFFF;
    case M_ABSY: return (db + f16(c) + c->y) & 0xFFFFFF;
    case M_LONG: return f24(c);
    case M_LONGX:return (f24(c) + c->x) & 0xFFFFFF;
    case M_SR:   return (uint16_t)(c->s + f8(c));
    case M_SRIY: t = (uint16_t)(c->s + f8(c)); w = rdp16(c, t); return (db + w + c->y) & 0xFFFFFF;
    }
    return 0;
}
static uint16_t rdm(Cpu *c, int m, bool w8, uint32_t *at) {
    if (m == M_IMM) return w8 ? f8(c) : f16(c);
    uint32_t t = ea(c, m); if (at) *at = t;
    return w8 ? rd(c, t) : rd16(c, t);
}
static void wrm(Cpu *c, uint32_t t, uint16_t v, bool w8) { if (w8) wr(c, t, v); else wr16(c, t, v); }
static void nz(Cpu *c, uint16_t v, bool w8) {
    c->p &= ~(F_N | F_Z);
    if (w8) { v &= 0xFF; if (!v) c->p |= F_Z; if (v & 0x80) c->p |= F_N; }
    else { if (!v) c->p |= F_Z; if (v & 0x8000) c->p |= F_N; }
}
static void setA(Cpu *c, uint16_t v) { if (M8(c)) c->a = (c->a & 0xFF00) | (v & 0xFF); else c->a = v; nz(c, v, M8(c)); }

static void adc(Cpu *c, uint16_t v) {
    bool w8 = M8(c); uint32_t a = w8 ? (c->a & 0xFF) : c->a, sign = w8 ? 0x80 : 0x8000, mask = w8 ? 0xFF : 0xFFFF;
    uint32_t cy = c->p & F_C, t = a + v + cy, r;
    SETF(F_V, (~(a ^ v) & (a ^ t) & sign));
    if (c->p & F_D) {
        int n = w8 ? 2 : 4; r = 0;
        for (int i = 0; i < n; i++) { int d = ((a >> 4*i) & 15) + ((v >> 4*i) & 15) + cy; if (d > 9) d += 6; cy = d > 15; r |= (uint32_t)(d & 15) << 4*i; }
        SETF(F_C, cy);
    } else { r = t & mask; SETF(F_C, t > mask); }
    setA(c, r);
}
static void sbc(Cpu *c, uint16_t v) {
    bool w8 = M8(c); uint32_t a = w8 ? (c->a & 0xFF) : c->a, sign = w8 ? 0x80 : 0x8000, mask = w8 ? 0xFF : 0xFFFF;
    uint32_t cy = c->p & F_C, nv = ~v & mask, t = a + nv + cy, r;
    SETF(F_V, (~(a ^ nv) & (a ^ t) & sign));
    if (c->p & F_D) {
        int n = w8 ? 2 : 4, bw = !cy; r = 0;
        for (int i = 0; i < n; i++) { int d = ((a >> 4*i) & 15) - ((v >> 4*i) & 15) - bw; if (d < 0) { d -= 6; bw = 1; } else bw = 0; r |= (uint32_t)(d & 15) << 4*i; }
        SETF(F_C, !bw);
    } else { r = t & mask; SETF(F_C, t > mask); }
    setA(c, r);
}
static void cmp(Cpu *c, uint16_t reg, uint16_t v, bool w8) {
    uint32_t mask = w8 ? 0xFF : 0xFFFF, a = reg & mask, b = v & mask, r = (a - b) & mask;
    SETF(F_C, a >= b); nz(c, r, w8);
}
/* Schieben/Drehen/INC/DEC: op 0 ASL,1 ROL,2 LSR,3 ROR,6 DEC,7 INC */
static uint16_t shf(Cpu *c, int op, uint16_t v, bool w8) {
    uint32_t top = w8 ? 0x80 : 0x8000, mask = w8 ? 0xFF : 0xFFFF, cy = c->p & F_C, r; v &= mask;
    switch (op) {
    case 0: SETF(F_C, v & top); r = (v << 1) & mask; break;
    case 1: SETF(F_C, v & top); r = ((v << 1) | cy) & mask; break;
    case 2: SETF(F_C, v & 1);   r = v >> 1; break;
    case 3: SETF(F_C, v & 1);   r = (v >> 1) | (cy ? top : 0); break;
    case 6: r = (v - 1) & mask; break;
    default:r = (v + 1) & mask; break;
    }
    nz(c, r, w8); return r;
}
static void bit(Cpu *c, uint16_t v, bool imm, bool w8) {
    uint32_t a = w8 ? (c->a & 0xFF) : c->a; SETF(F_Z, !(a & v & (w8 ? 0xFF : 0xFFFF)));
    if (!imm) { SETF(F_N, v & (w8 ? 0x80 : 0x8000)); SETF(F_V, v & (w8 ? 0x40 : 0x4000)); }
}
static void idx_fix(Cpu *c) { if (X8(c)) { c->x &= 0xFF; c->y &= 0xFF; } }
static void intr(Cpu *c, uint16_t vn, uint16_t ve, bool brk) {
    if (!c->e) push8(c, c->pbr);
    push16(c, c->pc);
    push8(c, c->e ? ((c->p & ~0x10) | 0x20 | (brk ? 0x10 : 0)) : c->p);
    c->p |= F_I; c->p &= ~F_D; c->pbr = 0;
    c->pc = rd16(c, c->e ? ve : vn);
}
void cpu_reset(Cpu *c) {
    c->e = true; c->p = 0x34; c->s = 0x1FF; c->d = 0; c->dbr = c->pbr = 0; c->a = c->x = c->y = 0;
    c->stopped = c->waiting = c->nmi_edge = false; c->cyc = 0;
    c->pc = rd16(c, 0xFFFC);
}
static void branch(Cpu *c, bool cond) { int8_t o = f8(c); if (cond) { c->pc += o; c->cyc++; } }
static void trans(Cpu *c, uint16_t *dst, uint16_t v, bool w8) { if (w8) *dst = v & 0xFF; else *dst = v; nz(c, v, w8); }

int cpu_step(Cpu *c) {
    uint64_t start = c->cyc;
    if (c->stopped) { c->cyc += 1; return 1; }
    if (c->nmi_edge) { c->nmi_edge = false; c->waiting = false; intr(c, 0xFFEA, 0xFFFA, false); return (int)(c->cyc - start); }
    if (c->irq) {
        if (c->waiting && (c->p & F_I)) c->waiting = false;
        else if (!(c->p & F_I)) { c->waiting = false; intr(c, 0xFFEE, 0xFFFE, false); return (int)(c->cyc - start); }
    }
    if (c->waiting) { c->cyc += 1; return 1; }

    uint8_t op = f8(c); int cc = op & 3, bbb = (op >> 2) & 7, aaa = op >> 5;
    bool w8 = M8(c), x8 = X8(c); uint32_t t; uint16_t v;

    if (op == 0x89) { bit(c, f8(c) | (w8 ? 0 : (uint16_t)f8(c) << 8), true, w8); goto done; }
    if (cc == 1 || (cc == 3 && bbb != 2 && bbb != 6) || (cc == 2 && bbb == 4)) {
        static const int m1[8] = { M_DPXI, M_DP, M_IMM, M_ABS, M_DPIY, M_DPX, M_ABSY, M_ABSX };
        static const int m3[8] = { M_SR, M_DPLI, M_NONE, M_LONG, M_SRIY, M_DPLIY, M_NONE, M_LONGX };
        int m = cc == 1 ? m1[bbb] : cc == 3 ? m3[bbb] : M_DPI;
        if (aaa == 4) { t = ea(c, m); wrm(c, t, c->a, w8); goto done; }
        v = rdm(c, m, w8, NULL);
        switch (aaa) {
        case 0: setA(c, c->a | v); break;
        case 1: setA(c, c->a & v); break;
        case 2: setA(c, c->a ^ v); break;
        case 3: adc(c, v); break;
        case 5: setA(c, v); break;
        case 6: cmp(c, c->a, v, w8); break;
        case 7: sbc(c, v); break;
        }
        goto done;
    }
    if (cc == 2) {
        static const int mm[8] = { M_NONE, M_DP, M_NONE, M_ABS, M_NONE, M_DPX, M_NONE, M_ABSX };
        switch (bbb) {
        case 0:
            if (op == 0xA2) { v = x8 ? f8(c) : f16(c); trans(c, &c->x, v, x8); }
            switch (op) {
            case 0x02: f8(c); intr(c, 0xFFE4, 0xFFF4, false); break;                   /* COP */
            case 0x22: t = f24(c); push8(c, c->pbr); push16(c, c->pc - 1); c->pbr = t >> 16; c->pc = t; break; /* JSL */
            case 0x42: f8(c); break;                                                    /* WDM */
            case 0x62: v = f16(c); push16(c, c->pc + (int16_t)v); break;                /* PER */
            case 0x82: v = f16(c); c->pc += (int16_t)v; break;                          /* BRL */
            case 0xC2: c->p &= ~f8(c); if (c->e) c->p |= 0x30; break;                   /* REP */
            case 0xE2: c->p |= f8(c); if (c->e) c->p |= 0x30; idx_fix(c); break;        /* SEP */
            }
            break;
        case 2:
            if (aaa < 4) { c->a = M8(c) ? ((c->a & 0xFF00) | (shf(c, aaa, c->a, w8) & 0xFF)) : shf(c, aaa, c->a, w8); }
            else if (aaa == 4) { if (w8) c->a = (c->a & 0xFF00) | (c->x & 0xFF); else c->a = c->x; nz(c, c->a, w8); }  /* TXA */
            else if (aaa == 5) { trans(c, &c->x, c->a, x8); }                          /* TAX */
            else if (aaa == 6) { c->x = x8 ? (c->x - 1) & 0xFF : c->x - 1; nz(c, c->x, x8); } /* DEX */
            break;                                                                      /* 7: NOP */
        case 6:
            switch (aaa) {
            case 0: c->a = w8 ? ((c->a & 0xFF00) | (shf(c, 7, c->a, 1) & 0xFF)) : shf(c, 7, c->a, 0); break; /* INC A */
            case 1: c->a = w8 ? ((c->a & 0xFF00) | (shf(c, 6, c->a, 1) & 0xFF)) : shf(c, 6, c->a, 0); break; /* DEC A */
            case 2: if (x8) push8(c, c->y); else push16(c, c->y); break;               /* PHY */
            case 3: v = x8 ? pull8(c) : pull16(c); c->y = v; nz(c, v, x8); break;      /* PLY */
            case 4: c->s = c->e ? (0x100 | (c->x & 0xFF)) : c->x; break;               /* TXS */
            case 5: v = c->s; trans(c, &c->x, v, x8); break;                           /* TSX */
            case 6: if (x8) push8(c, c->x); else push16(c, c->x); break;               /* PHX */
            case 7: v = x8 ? pull8(c) : pull16(c); c->x = v; nz(c, v, x8); break;      /* PLX */
            }
            break;
        default: /* 1,3,5,7 */
            if (aaa == 4) {
                if (bbb == 7) { t = ea(c, M_ABSX); wrm(c, t, 0, w8); }                 /* STZ abs,X */
                else { t = ea(c, bbb == 5 ? M_DPY : mm[bbb]); wrm(c, t, c->x, x8); }  /* STX */
            } else if (aaa == 5) {
                int m = bbb == 5 ? M_DPY : bbb == 7 ? M_ABSY : mm[bbb];
                v = rdm(c, m, x8, NULL); trans(c, &c->x, v, x8);                       /* LDX */
            } else {
                t = ea(c, mm[bbb]); v = w8 ? rd(c, t) : rd16(c, t);
                wrm(c, t, shf(c, aaa, v, w8), w8);
            }
        }
        goto done;
    }
    if (cc == 3) {
        if (bbb == 2) switch (aaa) {
            case 0: push16(c, c->d); break;                                             /* PHD */
            case 1: c->d = pull16(c); nz(c, c->d, 0); break;                            /* PLD */
            case 2: push8(c, c->pbr); break;                                            /* PHK */
            case 3: c->pc = pull16(c) + 1; c->pbr = pull8(c); break;                    /* RTL */
            case 4: push8(c, c->dbr); break;                                            /* PHB */
            case 5: c->dbr = pull8(c); nz(c, c->dbr, 1); break;                         /* PLB */
            case 6: c->waiting = true; break;                                           /* WAI */
            case 7: c->a = (c->a << 8) | (c->a >> 8); nz(c, c->a, 1); break;           /* XBA */
        } else switch (aaa) {
            case 0: c->s = c->e ? (0x100 | (c->a & 0xFF)) : c->a; break;               /* TCS */
            case 1: c->a = c->s; nz(c, c->a, 0); break;                                 /* TSC */
            case 2: c->d = c->a; nz(c, c->d, 0); break;                                 /* TCD */
            case 3: c->a = c->d; nz(c, c->a, 0); break;                                 /* TDC */
            case 4: trans(c, &c->y, c->x, x8); break;                                   /* TXY */
            case 5: trans(c, &c->x, c->y, x8); break;                                   /* TYX */
            case 6: c->stopped = true; break;                                           /* STP */
            case 7: { bool oe = c->e; c->e = c->p & F_C; SETF(F_C, oe);                 /* XCE */
                      if (c->e) { c->p |= 0x30; c->s = 0x100 | (c->s & 0xFF); c->x &= 0xFF; c->y &= 0xFF; } } break;
        }
        goto done;
    }
    /* cc == 0 */
    if (bbb == 4) {
        static const uint8_t fl[4] = { F_N, F_V, F_C, F_Z };
        bool set = c->p & fl[aaa >> 1]; branch(c, set == (bool)(aaa & 1)); goto done;
    }
    if (bbb == 6) {
        switch (aaa) {
        case 0: c->p &= ~F_C; break; case 1: c->p |= F_C; break; case 2: c->p &= ~F_I; break; case 3: c->p |= F_I; break;
        case 4: if (w8) c->a = (c->a & 0xFF00) | (c->y & 0xFF); else c->a = c->y; nz(c, c->a, w8); break;
        case 5: c->p &= ~F_V; break; case 6: c->p &= ~F_D; break; case 7: c->p |= F_D; break;
        } goto done;
    }
    if (bbb == 2) {
        switch (aaa) {
        case 0: push8(c, c->e ? (c->p | 0x30) : c->p); break;                           /* PHP */
        case 1: c->p = pull8(c); if (c->e) c->p |= 0x30; idx_fix(c); break;             /* PLP */
        case 2: if (w8) push8(c, c->a); else push16(c, c->a); break;                    /* PHA */
        case 3: v = w8 ? pull8(c) : pull16(c); setA(c, v); break;                       /* PLA */
        case 4: c->y = x8 ? (c->y - 1) & 0xFF : c->y - 1; nz(c, c->y, x8); break;       /* DEY */
        case 5: trans(c, &c->y, c->a, x8); break;                                       /* TAY */
        case 6: c->y = x8 ? (c->y + 1) & 0xFF : c->y + 1; nz(c, c->y, x8); break;       /* INY */
        case 7: c->x = x8 ? (c->x + 1) & 0xFF : c->x + 1; nz(c, c->x, x8); break;       /* INX */
        } goto done;
    }
    switch (op) {
    case 0x00: f8(c); intr(c, 0xFFE6, 0xFFFE, true); goto done;                         /* BRK */
    case 0x20: t = f16(c); push16(c, c->pc - 1); c->pc = t; goto done;                  /* JSR */
    case 0x40: c->p = pull8(c); c->pc = pull16(c); if (c->e) c->p |= 0x30; else c->pbr = pull8(c); idx_fix(c); goto done; /* RTI */
    case 0x60: c->pc = pull16(c) + 1; goto done;                                        /* RTS */
    case 0x80: branch(c, true); goto done;                                              /* BRA */
    case 0x4C: c->pc = f16(c); goto done;                                               /* JMP */
    case 0x5C: t = f24(c); c->pc = t; c->pbr = t >> 16; goto done;                      /* JML */
    case 0x6C: t = f16(c); c->pc = rd16(c, t); goto done;                               /* JMP (a) */
    case 0x7C: t = (f16(c) + c->x) & 0xFFFF; c->pc = rd16(c, ((uint32_t)c->pbr << 16) | t); goto done;
    case 0xDC: t = f16(c); { uint32_t p = rd16(c, t); p |= (uint32_t)rd(c, t + 2) << 16; c->pc = p; c->pbr = p >> 16; } goto done;
    case 0xFC: t = (f16(c) + c->x) & 0xFFFF; push16(c, c->pc - 1); c->pc = rd16(c, ((uint32_t)c->pbr << 16) | t); goto done;
    case 0xF4: push16(c, f16(c)); goto done;                                            /* PEA */
    case 0xD4: t = dpa(c, f8(c), 0); push16(c, rdp16(c, t)); goto done;                 /* PEI */
    case 0x44: case 0x54: {                                                             /* MVP / MVN */
        uint8_t dst = f8(c), src = f8(c); uint8_t b = rd(c, ((uint32_t)src << 16) | c->x);
        wr(c, ((uint32_t)dst << 16) | c->y, b); c->dbr = dst;
        int dl = op == 0x54 ? 1 : -1;
        c->x += dl; c->y += dl; if (x8) { c->x &= 0xFF; c->y &= 0xFF; }
        c->a--; if (c->a != 0xFFFF) c->pc -= 3; c->cyc += 2; goto done; }
    }
    {   static const int m0[8] = { M_IMM, M_DP, M_NONE, M_ABS, M_NONE, M_DPX, M_NONE, M_ABSX };
        int m = m0[bbb];
        switch (aaa) {
        case 0: t = ea(c, bbb & 2 ? M_ABS : M_DP); v = w8 ? rd(c, t) : rd16(c, t);      /* TSB / TRB */
                SETF(F_Z, !(c->a & v & (w8 ? 0xFF : 0xFFFF)));
                wrm(c, t, bbb >= 5 ? (v & ~c->a) : (v | c->a), w8); break;
        case 1: v = rdm(c, m, w8, NULL); bit(c, v, false, w8); break;                   /* BIT */
        case 3: t = ea(c, m); wrm(c, t, 0, w8); break;                                  /* STZ */
        case 4: if (bbb == 7) { t = ea(c, M_ABS); wrm(c, t, 0, w8); }                   /* STZ abs */
                else { t = ea(c, m); wrm(c, t, c->y, x8); } break;                      /* STY */
        case 5: v = rdm(c, m, x8, NULL); trans(c, &c->y, v, x8); break;                 /* LDY */
        case 6: v = rdm(c, m, x8, NULL); cmp(c, c->y, v, x8); break;                    /* CPY */
        case 7: v = rdm(c, m, x8, NULL); cmp(c, c->x, v, x8); break;                    /* CPX */
        }
    }
done:
    if (c->cyc - start < 2) c->cyc = start + 2;
    return (int)(c->cyc - start);
}

#include "ht4bit_cpu.h"
#include <string.h>

void ht4bit_init(cpu_state_t *cpu) {
    memset(cpu, 0, sizeof(cpu_state_t));
}

int ht4bit_check_state(cpu_state_t *s) {
    unsigned i, x = 0;
    for (i = 0; i < 256; i++) x |= s->mem[i], s->mem[i] &= 15;
    x |= s->pc >> 8; s->pc &= 0xfff;
    x |= s->stack >> 9; s->stack &= 0x1fff;
    x |= s->a; s->a &= 15;
    for (i = 0; i < 5; i++) x |= s->r[i], s->r[i] &= 15;
    x |= (s->cf | s->tf | s->timer_en) << 3;
    s->cf &= 1; s->tf &= 1; s->timer_en &= 1;
    return x >> 4;
}

void ht4bit_run(cpu_state_t *cpu, uint8_t *rom, unsigned ticks) {
    unsigned pc = cpu->pc;
    unsigned a = cpu->a, cf = cpu->cf;
    unsigned tickcount = 0;
    unsigned tmr_frac = 0;

#define R1R0 (cpu->r[1] << 4 | cpu->r[0])
#define R3R2 (cpu->r[3] << 4 | cpu->r[2])

    while (tickcount < ticks) {
        unsigned x, op;
        op = rom[pc];

        switch (op) {

        case 0x00: cf = a & 1; a = (a << 4 | a) >> 1 & 15; break;
        case 0x01: cf = a >> 3; a = (a << 4 | a) >> 3 & 15; break;
        case 0x02: a = cf << 4 | a; cf = a & 1; a >>= 1; break;
        case 0x03: a = a << 1 | cf; cf = a >> 4; a &= 15; break;

        case 0x04: case 0x06:
            x = op & 2; x = cpu->r[x + 1] << 4 | cpu->r[x]; a = cpu->mem[x]; break;
        case 0x05: case 0x07:
            x = op & 2; x = cpu->r[x + 1] << 4 | cpu->r[x]; cpu->mem[x] = a; break;

        case 0x08: case 0x09:
            cf &= ~op;
            a += cpu->mem[R1R0] + cf; cf = a >> 4; a &= 15;
            break;

        case 0x0a: case 0x0b:
            cf |= op & 1;
            a += 15 - cpu->mem[R1R0] + cf; cf = a >> 4; a &= 15;
            break;

        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
            x = op & 2; x = cpu->r[x + 1] << 4 | cpu->r[x];
            cpu->mem[x] = (cpu->mem[x] + (op & 1 ? -1 : 1)) & 15;
            break;

        case 0x10: case 0x12: case 0x14: case 0x16: case 0x18:
            x = op >> 1 & 7; cpu->r[x] = (cpu->r[x] + 1) & 15; break;
        case 0x11: case 0x13: case 0x15: case 0x17: case 0x19:
            x = op >> 1 & 7; cpu->r[x] = (cpu->r[x] - 1) & 15; break;

        case 0x1a: a &= cpu->mem[R1R0]; break;
        case 0x1b: a ^= cpu->mem[R1R0]; break;
        case 0x1c: a |= cpu->mem[R1R0]; break;
        case 0x1d: cpu->mem[R1R0] &= a; break;
        case 0x1e: cpu->mem[R1R0] ^= a; break;
        case 0x1f: cpu->mem[R1R0] |= a; break;

        case 0x20: case 0x22: case 0x24: case 0x26: case 0x28:
            cpu->r[op >> 1 & 7] = a; break;
        case 0x21: case 0x23: case 0x25: case 0x27: case 0x29:
            a = cpu->r[op >> 1 & 7]; break;

        case 0x2a: cf = 0; break;
        case 0x2b: cf = 1; break;
        case 0x2c: break;
        case 0x2d: break;
        case 0x2e: pc = cpu->stack; pc--; break;
        case 0x2f: pc = cpu->stack; cf = pc >> 12; pc--; break;

        case 0x31: a = (a + 1) & 15; break;
        case 0x36:
            if (a >= 10 || cf) a = (a + 6) & 15, cf = 1;
            break;
        case 0x37: break;
        case 0x38: cpu->timer_en = 1; break;
        case 0x39: cpu->timer_en = 0; break;
        case 0x3a: a = cpu->tmr & 15; break;
        case 0x3b: a = cpu->tmr >> 4; break;
        case 0x3c: cpu->tmr = (cpu->tmr & 0xf0) | a; break;
        case 0x3d: cpu->tmr = a << 4 | (cpu->tmr & 15); break;
        case 0x3e: break;
        case 0x3f: a = (a - 1) & 15; break;

        case 0x40: a += rom[++pc & 0xfff] & 15; cf = a >> 4; a &= 15; break;
        case 0x41: a += 16 - (rom[++pc & 0xfff] & 15); cf = a >> 4; a &= 15; break;
        case 0x42: a &= rom[++pc & 0xfff]; break;
        case 0x43: a ^= rom[++pc & 0xfff] & 15; break;
        case 0x44: a |= rom[++pc & 0xfff] & 15; break;
        case 0x45: pc++; break;
        case 0x46: cpu->r[4] = rom[++pc & 0xfff] & 15; break;
        case 0x47: cpu->tmr = rom[++pc & 0xfff]; break;
        case 0x48: case 0x49: case 0x4a: case 0x4b: break;

        case 0x4c:
            a = rom[(pc & 0xf00) | a << 4 | cpu->mem[R1R0]];
            cpu->r[4] = a >> 4; a &= 15; break;
        case 0x4d:
            a = rom[0xf00 | a << 4 | cpu->mem[R1R0]];
            cpu->r[4] = a >> 4; a &= 15; break;
        case 0x4e:
            a = rom[(pc & 0xf00) | a << 4 | cpu->r[4]];
            cpu->mem[R1R0] = a >> 4; a &= 15; break;
        case 0x4f:
            a = rom[0xf00 | a << 4 | cpu->r[4]];
            cpu->mem[R1R0] = a >> 4; a &= 15; break;

        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5a: case 0x5b:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            cpu->r[0] = op & 0xf; cpu->r[1] = rom[++pc & 0xfff] & 15; break;

        case 0x60: case 0x61: case 0x62: case 0x63:
        case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6a: case 0x6b:
        case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            cpu->r[2] = op & 0xf; cpu->r[3] = rom[++pc & 0xfff] & 15; break;

        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b:
        case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            a = op & 15; break;

        #define CASE8(x) case x: case x+1: case x+2: case x+3: \
                          case x+4: case x+5: case x+6: case x+7:

        CASE8(0x80) CASE8(0x88)
        CASE8(0x90) CASE8(0x98)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (a >> (op >> 3 & 3) & 1) pc = x - 1;
            break;

        CASE8(0xa0)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (cpu->r[0]) pc = x - 1;
            break;
        CASE8(0xa8)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (cpu->r[1]) pc = x - 1;
            break;

        CASE8(0xb0)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (!a) pc = x - 1;
            break;
        CASE8(0xb8)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (a) pc = x - 1;
            break;

        CASE8(0xc0)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (cf) pc = x - 1;
            break;
        CASE8(0xc8)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (!cf) pc = x - 1;
            break;

        CASE8(0xd0)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (cpu->tf) pc = x - 1;
            cpu->tf = 0;
            break;

        CASE8(0xd8)
            x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
            if (cpu->r[4]) pc = x - 1;
            break;

        CASE8(0xe0) CASE8(0xe8)
            pc = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
            pc--; break;

        CASE8(0xf0) CASE8(0xf8)
            cpu->stack = (pc + 2) & 0xfff;
            pc = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
            pc--; break;

        default:
            break;
        }

        pc = (pc + 1) & 0xfff;
        tickcount++;

        if (cpu->timer_en) {
            tmr_frac += 32;
            if (tmr_frac >= 0x10000) {
                tmr_frac -= 0x10000;
                if (!++cpu->tmr) cpu->tf = 1;
            }
        }
    }

    cpu->pc = pc;
    cpu->a = a;
    cpu->cf = cf;
}

#ifndef HT4BIT_CPU_H
#define HT4BIT_CPU_H

#include <stdint.h>

typedef struct {
    uint8_t mem[256];
    uint16_t pc;
    uint16_t stack;
    uint8_t a;
    uint8_t r[5];
    uint8_t cf;
    uint8_t tmr;
    uint8_t tf;
    uint8_t timer_en;
} cpu_state_t;

void ht4bit_init(cpu_state_t *cpu);
int  ht4bit_check_state(cpu_state_t *s);
void ht4bit_run(cpu_state_t *cpu, uint8_t *rom, unsigned ticks);

#endif

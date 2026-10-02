/*
 * FrogUI Calculator
 * v73: Windows 10 Standard style calculator
 * Integer-only arithmetic (no FPU)
 */

#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <stdint.h>

// Initialize calculator
void calc_init(void);

// Open calculator
void calc_open(void);

// Close calculator
void calc_close(void);

// Check if calculator is active
int calc_is_active(void);

// Handle input - returns 1 if should close
int calc_handle_input(int up, int down, int left, int right, int a, int b, int x, int y);

// Render calculator
void calc_render(uint16_t *framebuffer);

#endif // CALCULATOR_H

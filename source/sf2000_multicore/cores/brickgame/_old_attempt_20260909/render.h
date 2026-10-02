#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define GRID_X 60
#define GRID_Y 30
#define CELL_SIZE 10
#define GRID_COLS 20
#define GRID_ROWS 10

void render_frame(uint16_t *fb, uint8_t *mem, int pitch);

#endif

#include "render.h"
#include <string.h>

#define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) >> 3)))

static const uint16_t color_table[16] = {
    0x0000,
    RGB565(0, 0, 255),
    RGB565(0, 255, 0),
    RGB565(0, 255, 255),
    RGB565(255, 0, 0),
    RGB565(255, 0, 255),
    RGB565(255, 255, 0),
    RGB565(255, 255, 255),
    RGB565(128, 128, 128),
    RGB565(64, 64, 255),
    RGB565(64, 255, 64),
    RGB565(64, 255, 255),
    RGB565(255, 64, 64),
    RGB565(255, 64, 255),
    RGB565(255, 255, 64),
    RGB565(192, 192, 192),
};

static void fill_rect(uint16_t *fb, int pitch, int x, int y, int w, int h, uint16_t color) {
    for (int row = 0; row < h; row++) {
        uint16_t *dst = fb + (y + row) * (pitch / 2) + x;
        for (int col = 0; col < w; col++) {
            dst[col] = color;
        }
    }
}

static void draw_char(uint16_t *fb, int pitch, int x, int y, char c, uint16_t color) {
    static const uint8_t font5x7[][5] = {
        {0x7e,0x81,0x81,0x81,0x7e},
        {0x00,0x42,0xff,0x02,0x00},
        {0x42,0x81,0x89,0x89,0x76},
        {0x44,0x82,0x89,0x89,0x76},
        {0x18,0x14,0x12,0xff,0x10},
        {0x8f,0x89,0x89,0x89,0x71},
        {0x7e,0x89,0x89,0x89,0x71},
        {0x01,0xe1,0x19,0x05,0x03},
        {0x76,0x89,0x89,0x89,0x76},
        {0x76,0x89,0x89,0x89,0x7e},
        {0x00,0x36,0x36,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00},
    };
    int idx = 0;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == ':') idx = 10;
    else idx = 11;

    for (int row = 0; row < 7; row++) {
        uint8_t bits = font5x7[idx][row < 5 ? row : 0];
        if (row >= 5) bits = 0;
        for (int col = 0; col < 5; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                if (px < SCREEN_W && py < SCREEN_H) {
                    fb[py * (pitch / 2) + px] = color;
                }
            }
        }
    }
}

static void draw_text(uint16_t *fb, int pitch, int x, int y, const char *text, uint16_t color) {
    while (*text) {
        draw_char(fb, pitch, x, y, *text, color);
        x += 6;
        text++;
    }
}

static void draw_number(uint16_t *fb, int pitch, int x, int y, int num, int digits, uint16_t color) {
    char buf[8];
    int i;
    for (i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (num % 10);
        num /= 10;
    }
    buf[digits] = 0;
    draw_text(fb, pitch, x, y, buf, color);
}

void render_frame(uint16_t *fb, uint8_t *mem, int pitch) {
    int x, y;
    uint16_t white = RGB565(255, 255, 255);
    uint16_t gray = RGB565(100, 100, 100);
    uint16_t dark = RGB565(20, 20, 30);
    uint16_t grid_bg = RGB565(0, 0, 0);
    uint16_t border = RGB565(60, 60, 80);

    fill_rect(fb, pitch, 0, 0, SCREEN_W, SCREEN_H, dark);

    fill_rect(fb, pitch, GRID_X - 2, GRID_Y - 2,
              GRID_COLS * CELL_SIZE + 4, GRID_ROWS * CELL_SIZE + 4, border);
    fill_rect(fb, pitch, GRID_X, GRID_Y,
              GRID_COLS * CELL_SIZE, GRID_ROWS * CELL_SIZE, grid_bg);

    for (y = 0; y < GRID_ROWS; y++) {
        unsigned a = mem[217 + y * 2] << 4 | mem[216 + y * 2];
        for (x = 0; x < GRID_COLS; x++) {
            int bit = (a >> (9 - x)) & 1;
            if (bit) {
                int cx = GRID_X + x * CELL_SIZE;
                int cy = GRID_Y + y * CELL_SIZE;
                fill_rect(fb, pitch, cx + 1, cy + 1, CELL_SIZE - 2, CELL_SIZE - 2, white);
            }
        }
    }

    {
        int a = mem[184] | mem[186] << 4 | mem[188] << 8 | mem[190] << 12;
        int nx = GRID_X + GRID_COLS * CELL_SIZE + 16;
        int ny = GRID_Y + 20;
        draw_text(fb, pitch, nx, ny - 14, "NEXT", white);
        for (y = 0; y < 4; y++) {
            int row = (a >> (y * 4)) & 0xf;
            for (x = 0; x < 4; x++) {
                if (row & (1 << x)) {
                    fill_rect(fb, pitch, nx + x * 8, ny + y * 8, 6, 6, white);
                }
            }
        }
    }

    {
        char buf[8];
        int val;
        static const uint8_t digit4[] = {
            0xe7, 0xa0, 0xcb, 0xe9, 0xac, 0x6d, 0x6f, 0xe0, 0xef, 0xed
        };
        int a = (mem[179] | mem[199] << 4) << 24;
        a |= (mem[185] | mem[201] << 4) << 16;
        a |= (mem[189] | mem[187] << 4) << 8;
        a |= mem[191] | mem[203] << 4;
        a &= 0xefefefef;
        int score = 0;
        for (int i = 0; i < 4; i++, a >>= 8) {
            int x2 = a & 0xff;
            int d = 0;
            for (int j = 0; j < 10; j++) if (x2 == digit4[j]) { d = j; break; }
            score = score * 10 + d;
        }
        draw_text(fb, pitch, GRID_X, GRID_Y + GRID_ROWS * CELL_SIZE + 8, "SCORE", gray);
        draw_number(fb, pitch, GRID_X + 36, GRID_Y + GRID_ROWS * CELL_SIZE + 8, score, 4, white);
    }

    {
        int speed = 0;
        int a = mem[196] | mem[198] << 4 | mem[200] << 8 | mem[202] << 12;
        a &= 0x8ccc;
        static const uint16_t digit1[] = {
            0x8c8c, 0x0880, 0x84c8, 0x88c8, 0x08c4,
            0x884c, 0x8c4c, 0x0888, 0x8ccc, 0x88cc
        };
        for (int j = 0; j < 10; j++) if (a == digit1[j]) { speed = j; break; }
        draw_text(fb, pitch, GRID_X + 80, GRID_Y + GRID_ROWS * CELL_SIZE + 8, "SPD", gray);
        draw_number(fb, pitch, GRID_X + 102, GRID_Y + GRID_ROWS * CELL_SIZE + 8, speed, 1, white);
    }

    {
        int level = 0;
        int a = mem[204] | mem[206] << 4 | mem[208] << 8 | mem[210] << 12;
        a &= 0x8ccc;
        static const uint16_t digit1[] = {
            0x8c8c, 0x0880, 0x84c8, 0x88c8, 0x08c4,
            0x884c, 0x8c4c, 0x0888, 0x8ccc, 0x88cc
        };
        for (int j = 0; j < 10; j++) if (a == digit1[j]) { level = j; break; }
        draw_text(fb, pitch, GRID_X + 130, GRID_Y + GRID_ROWS * CELL_SIZE + 8, "LVL", gray);
        draw_number(fb, pitch, GRID_X + 152, GRID_Y + GRID_ROWS * CELL_SIZE + 8, level, 1, white);
    }

    {
        int lines = 0;
        int a = mem[192] | mem[194] << 4 | mem[196] << 8 | mem[198] << 12;
        a &= 0x8ccc;
        static const uint16_t digit1[] = {
            0x8c8c, 0x0880, 0x84c8, 0x88c8, 0x08c4,
            0x884c, 0x8c4c, 0x0888, 0x8ccc, 0x88cc
        };
        for (int j = 0; j < 10; j++) if (a == digit1[j]) { lines = j; break; }
        draw_text(fb, pitch, GRID_X + 180, GRID_Y + GRID_ROWS * CELL_SIZE + 8, "LIN", gray);
        draw_number(fb, pitch, GRID_X + 202, GRID_Y + GRID_ROWS * CELL_SIZE + 8, lines, 1, white);
    }
}

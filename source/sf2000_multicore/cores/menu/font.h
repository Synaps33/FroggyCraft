#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// Initialize font system
void font_init(void);

// Load font from settings (call when font setting changes)
void font_load_from_settings(const char *font_name);

// v23: Set font smoothing/antialiasing
void font_set_smooth(int enabled);

// v32: Set extra spacing between letters (0-3 pixels)
void font_set_spacing(int pixels);

// v32: Get current extra spacing
int font_get_spacing(void);

// Draw a single character at position (x, y) with given color
void font_draw_char(uint16_t *framebuffer, int screen_width, int screen_height, 
                   int x, int y, char c, uint16_t color);

// Draw a text string at position (x, y) with given color
void font_draw_text(uint16_t *framebuffer, int screen_width, int screen_height,
                   int x, int y, const char *text, uint16_t color);

// Measure text width in pixels
int font_measure_text(const char *text);

// Get font character width/height
#define FONT_CHAR_WIDTH 18
#define FONT_CHAR_HEIGHT 16
#define FONT_CHAR_SPACING 13

// v59: Built-in bitmap font (5x7, no TTF needed)
// Character dimensions for built-in font
#define BUILTIN_CHAR_WIDTH 5
#define BUILTIN_CHAR_HEIGHT 7
#define BUILTIN_CHAR_SPACING 6

// Draw text with built-in bitmap font (no outline)
void builtin_draw_text(uint16_t *framebuffer, int screen_width, int screen_height,
                       int x, int y, const char *text, uint16_t color);

// Draw text with built-in bitmap font (with black outline)
void builtin_draw_text_outlined(uint16_t *framebuffer, int screen_width, int screen_height,
                                int x, int y, const char *text, uint16_t color);

// Measure text width in pixels using built-in font
int builtin_measure_text(const char *text);

#endif // FONT_H
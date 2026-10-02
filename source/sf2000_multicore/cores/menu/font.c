#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo font_info;
static unsigned char *font_buffer = NULL;
static int font_loaded = 0;
static int font_y_offset = 0;  // v22: Per-font vertical offset adjustment
static int font_smooth = 0;    // v23: Font antialiasing/smoothing
static int font_extra_spacing = 0;  // v32: Extra spacing between letters (0-2px)

// v28: Fixed-point scale (scale * 1024) to avoid FPU on no-FPU device
static int font_scale_fp = 0;
static int font_baseline_fp = 0;  // Pre-computed baseline

// v28: Glyph cache to avoid repeated stbtt calls (ASCII 32-126)
#define GLYPH_CACHE_START 32
#define GLYPH_CACHE_END 127
#define GLYPH_CACHE_SIZE (GLYPH_CACHE_END - GLYPH_CACHE_START)

typedef struct {
    int glyph_index;
    int advance_width_fp;  // Fixed-point (value * 1024)
    int left_bearing_fp;
    unsigned char *bitmap;
    int bm_width;
    int bm_height;
    int bm_xoff;
    int bm_yoff;
} GlyphCacheEntry;

static GlyphCacheEntry glyph_cache[GLYPH_CACHE_SIZE];
static int glyph_cache_initialized = 0;

#define FONT_SIZE 20

// v23: Set font smoothing from settings
void font_set_smooth(int enabled) {
    font_smooth = enabled;
}

// v32: Set extra spacing between letters
void font_set_spacing(int pixels) {
    font_extra_spacing = (pixels < 0) ? 0 : (pixels > 3) ? 3 : pixels;
}

// v32: Get current extra spacing
int font_get_spacing(void) {
    return font_extra_spacing;
}

// v28: Free glyph cache bitmaps
static void free_glyph_cache(void) {
    if (!glyph_cache_initialized) return;
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if (glyph_cache[i].bitmap) {
            stbtt_FreeBitmap(glyph_cache[i].bitmap, NULL);
            glyph_cache[i].bitmap = NULL;
        }
    }
    glyph_cache_initialized = 0;
}

// v28: Initialize glyph cache - pre-compute all metrics and bitmaps
static void init_glyph_cache(float scale) {
    free_glyph_cache();

    // Convert float scale to fixed-point (scale * 1024)
    font_scale_fp = (int)(scale * 1024.0f);

    // Pre-compute baseline
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    font_baseline_fp = (ascent * font_scale_fp) >> 10;  // Back to integer

    // Cache ASCII characters 32-126 (space to ~)
    for (int c = GLYPH_CACHE_START; c < GLYPH_CACHE_END; c++) {
        int idx = c - GLYPH_CACHE_START;
        char ch = (char)c;

        // Convert to uppercase for cache lookup
        if (ch >= 'a' && ch <= 'z') {
            ch = ch - 'a' + 'A';
        }

        int glyph_index = stbtt_FindGlyphIndex(&font_info, ch);
        glyph_cache[idx].glyph_index = glyph_index;

        if (glyph_index != 0) {
            int advance_width, left_bearing;
            stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance_width, &left_bearing);

            // Store as fixed-point
            glyph_cache[idx].advance_width_fp = (advance_width * font_scale_fp) >> 10;
            glyph_cache[idx].left_bearing_fp = (left_bearing * font_scale_fp) >> 10;

            // Pre-render bitmap
            glyph_cache[idx].bitmap = stbtt_GetGlyphBitmap(
                &font_info, 0, scale, glyph_index,
                &glyph_cache[idx].bm_width,
                &glyph_cache[idx].bm_height,
                &glyph_cache[idx].bm_xoff,
                &glyph_cache[idx].bm_yoff
            );
        } else {
            glyph_cache[idx].advance_width_fp = FONT_CHAR_SPACING;
            glyph_cache[idx].left_bearing_fp = 0;
            glyph_cache[idx].bitmap = NULL;
        }
    }

    glyph_cache_initialized = 1;
}

// Internal function to load a font file
static int load_font_file(const char *font_filename, float custom_size) {
    // Free previous font if loaded
    if (font_buffer) {
        free_glyph_cache();
        free(font_buffer);
        font_buffer = NULL;
        font_loaded = 0;
    }

    // Build search paths for the font
    char font_paths[2][256];
    snprintf(font_paths[0], sizeof(font_paths[0]), "/mnt/sda1/frogui/fonts/%s", font_filename);
    snprintf(font_paths[1], sizeof(font_paths[1]), "fonts/%s", font_filename);

    FILE *fp = NULL;
    for (int i = 0; i < 2; i++) {
        fp = fopen(font_paths[i], "rb");
        if (fp) break;
    }

    if (!fp) {
        return 0;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long font_size_bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Allocate buffer and read font
    font_buffer = (unsigned char*)malloc(font_size_bytes);
    if (!font_buffer) {
        fclose(fp);
        return 0;
    }

    fread(font_buffer, 1, font_size_bytes, fp);
    fclose(fp);

    // Initialize font
    if (!stbtt_InitFont(&font_info, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0))) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    // v28: Calculate scale and initialize glyph cache (only float ops here, at load time)
    float scale = stbtt_ScaleForPixelHeight(&font_info, custom_size);
    init_glyph_cache(scale);

    font_loaded = 1;
    return 1;
}

void font_load_from_settings(const char *font_name) {
    const char *font_filename = NULL;
    float custom_size = (float)FONT_SIZE;
    int y_offset = 0;  // v22: Per-font vertical offset

    // Map font names to font files
    if (strcmp(font_name, "GamePocket") == 0) {
        font_filename = "GamePocket-Regular-ZeroKern.ttf";
        custom_size = 18.0f;
    } else if (strcmp(font_name, "Monogram") == 0) {
        font_filename = "monogram.ttf";
        custom_size = 16.0f;
    } else if (strcmp(font_name, "Minikaliber") == 0) {
        font_filename = "minikaliber.ttf";
        custom_size = 19.0f;
    } else if (strcmp(font_name, "Orbitron") == 0) {
        font_filename = "orbitron.ttf";
        custom_size = 16.0f;
    } else if (strcmp(font_name, "Setback") == 0) {
        font_filename = "setback.ttf";
        custom_size = 15.0f;
    } else if (strcmp(font_name, "Upheaval") == 0) {
        font_filename = "upheaval.ttf";
        custom_size = 12.0f;
        y_offset = 3;  // Upheaval renders 3px too high
    } else {
        // Default to GamePocket
        font_filename = "GamePocket-Regular-ZeroKern.ttf";
        custom_size = 18.0f;
    }

    font_y_offset = y_offset;
    // v28: Pass custom_size to load_font_file - glyph cache initialized there
    load_font_file(font_filename, custom_size);
}

void font_init(void) {
    // Load default font initially
    font_load_from_settings("GamePocket");
}

// v28: Get cached glyph entry (returns NULL if not in cache)
static GlyphCacheEntry* get_cached_glyph(char c) {
    // Convert to uppercase
    if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }
    if (c < GLYPH_CACHE_START || c >= GLYPH_CACHE_END) {
        return NULL;
    }
    return &glyph_cache[c - GLYPH_CACHE_START];
}

void font_draw_char(uint16_t *framebuffer, int screen_width, int screen_height,
                   int x, int y, char c, uint16_t color) {
    if (!font_loaded || !framebuffer || !glyph_cache_initialized) return;

    // v28: Use cached glyph instead of stbtt calls
    GlyphCacheEntry *entry = get_cached_glyph(c);
    if (!entry || entry->glyph_index == 0 || !entry->bitmap) return;

    unsigned char *bitmap = entry->bitmap;
    int width = entry->bm_width;
    int height = entry->bm_height;
    int xoff = entry->bm_xoff;
    int yoff = entry->bm_yoff;

    // v28: Use pre-computed baseline (no float math)
    int baseline = font_baseline_fp;

    // Pre-extract foreground color components for blending
    uint8_t fg_r = (color >> 11) & 0x1F;
    uint8_t fg_g = (color >> 5) & 0x3F;
    uint8_t fg_b = color & 0x1F;

    for (int row = 0; row < height; row++) {
        int py = y + baseline + yoff + row + font_y_offset;
        if (py < 0 || py >= screen_height) continue;

        for (int col = 0; col < width; col++) {
            unsigned char alpha = bitmap[row * width + col];
            if (alpha > 0) {
                int px = x + xoff + col;

                if (px >= 0 && px < screen_width) {
                    int idx = py * screen_width + px;

                    if (font_smooth && alpha < 250) {
                        // v23: Full alpha blending for antialiased text
                        uint16_t bg = framebuffer[idx];
                        uint8_t bg_r = (bg >> 11) & 0x1F;
                        uint8_t bg_g = (bg >> 5) & 0x3F;
                        uint8_t bg_b = bg & 0x1F;

                        // Blend: result = bg + (fg - bg) * alpha / 255
                        uint8_t r = bg_r + (((fg_r - bg_r) * alpha) >> 8);
                        uint8_t g = bg_g + (((fg_g - bg_g) * alpha) >> 8);
                        uint8_t b = bg_b + (((fg_b - bg_b) * alpha) >> 8);

                        framebuffer[idx] = (r << 11) | (g << 5) | b;
                    } else if (alpha > 127) {
                        // No smoothing or nearly opaque - direct write
                        framebuffer[idx] = color;
                    }
                }
            }
        }
    }
    // v28: No stbtt_FreeBitmap - bitmap is cached
}

void font_draw_text(uint16_t *framebuffer, int screen_width, int screen_height,
                   int x, int y, const char *text, uint16_t color) {
    if (!font_loaded || !framebuffer || !text || !glyph_cache_initialized) return;

    int start_x = x;

    while (*text) {
        if (*text == '\n') {
            y += FONT_SIZE + 4;  // Line spacing
            x = start_x;
            text++;
            continue;
        }

        char c = *text;

        // v28: Use cached glyph - no stbtt calls, no float math
        GlyphCacheEntry *entry = get_cached_glyph(c);

        if (entry && entry->glyph_index != 0) {
            // Draw the character
            font_draw_char(framebuffer, screen_width, screen_height, x, y, c, color);

            // Advance cursor using cached fixed-point value + extra spacing
            x += entry->advance_width_fp + font_extra_spacing;
        } else {
            // Space or unknown character
            x += FONT_CHAR_SPACING + font_extra_spacing;
        }

        text++;
    }
}

int font_measure_text(const char *text) {
    if (!text || !font_loaded || !glyph_cache_initialized) return 0;

    int width = 0;

    while (*text) {
        // Skip newlines
        if (*text == '\n') {
            text++;
            continue;
        }

        char c = *text;

        // v28: Use cached glyph - no stbtt calls, no float math
        GlyphCacheEntry *entry = get_cached_glyph(c);

        if (entry && entry->glyph_index != 0) {
            // Add character width from cache + extra spacing
            width += entry->advance_width_fp + font_extra_spacing;
        } else {
            // Space or unknown character
            width += FONT_CHAR_SPACING + font_extra_spacing;
        }

        text++;
    }

    return width;
}

// ============== v59: BUILT-IN BITMAP FONT (5x7) ==============
static const unsigned char builtin_font_data[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},{0x08,0x1C,0x2A,0x08,0x08},
};

// Draw single character with built-in bitmap font (no outline)
static void builtin_draw_char(uint16_t *pixels, int sw, int sh, int x, int y, char c, uint16_t col) {
    if (c < 32 || c > 127) c = '?';
    const unsigned char *g = builtin_font_data[c - 32];

    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            if (g[cx] & (1 << cy)) {
                int px = x + cx, py = y + cy;
                if (px >= 0 && px < sw && py >= 0 && py < sh) {
                    pixels[py * sw + px] = col;
                }
            }
        }
    }
}

// Draw single character with built-in bitmap font (with black outline)
static void builtin_draw_char_outlined(uint16_t *pixels, int sw, int sh, int x, int y, char c, uint16_t col) {
    if (c < 32 || c > 127) c = '?';
    const unsigned char *g = builtin_font_data[c - 32];
    const uint16_t outline = 0x0000;

    // First pass: draw black outline
    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            if (g[cx] & (1 << cy)) {
                static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                for (int d = 0; d < 8; d++) {
                    int px = x + cx + dx[d];
                    int py = y + cy + dy[d];
                    if (px >= 0 && px < sw && py >= 0 && py < sh) {
                        int ox = cx + dx[d], oy = cy + dy[d];
                        if (ox < 0 || ox >= 5 || oy < 0 || oy >= 7 || !(g[ox] & (1 << oy))) {
                            pixels[py * sw + px] = outline;
                        }
                    }
                }
            }
        }
    }

    // Second pass: draw font pixels
    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            if (g[cx] & (1 << cy)) {
                int px = x + cx, py = y + cy;
                if (px >= 0 && px < sw && py >= 0 && py < sh) {
                    pixels[py * sw + px] = col;
                }
            }
        }
    }
}

void builtin_draw_text(uint16_t *framebuffer, int screen_width, int screen_height,
                       int x, int y, const char *text, uint16_t color) {
    if (!framebuffer || !text) return;
    int start_x = x;
    while (*text) {
        if (*text == '\n') {
            y += BUILTIN_CHAR_HEIGHT + 2;
            x = start_x;
            text++;
            continue;
        }
        builtin_draw_char(framebuffer, screen_width, screen_height, x, y, *text, color);
        x += BUILTIN_CHAR_SPACING;
        text++;
    }
}

void builtin_draw_text_outlined(uint16_t *framebuffer, int screen_width, int screen_height,
                                int x, int y, const char *text, uint16_t color) {
    if (!framebuffer || !text) return;
    int start_x = x;
    while (*text) {
        if (*text == '\n') {
            y += BUILTIN_CHAR_HEIGHT + 2;
            x = start_x;
            text++;
            continue;
        }
        builtin_draw_char_outlined(framebuffer, screen_width, screen_height, x, y, *text, color);
        x += BUILTIN_CHAR_SPACING;
        text++;
    }
}

int builtin_measure_text(const char *text) {
    if (!text) return 0;
    int width = 0;
    int max_width = 0;
    while (*text) {
        if (*text == '\n') {
            if (width > max_width) max_width = width;
            width = 0;
        } else {
            width += BUILTIN_CHAR_SPACING;
        }
        text++;
    }
    return (width > max_width) ? width : max_width;
}

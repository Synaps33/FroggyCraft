#include "render.h"
#include "theme.h"
#include "gfx_theme.h"
#include "font.h"
#include "settings.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>

#include "lodepng.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Buffer for image operations (512KB is enough for 320x240 RGB565 and decode buffers)
#define UNIVERSAL_BUFFER_BYTES (512 * 1024)  // 512KB
static uint8_t universal_buffer[UNIVERSAL_BUFFER_BYTES];

// Helper macros for buffer access
#define universal_buffer_u16 ((uint16_t*)universal_buffer)
#define UNIVERSAL_MAX_PIXELS_RGB565 (UNIVERSAL_BUFFER_BYTES / sizeof(uint16_t))  // 3,145,728 pixels

// Track if we're in platform menu or game list
static bool in_platform_menu = true;

void render_set_in_platform_menu(bool is_platform_menu) {
    in_platform_menu = is_platform_menu;
}

bool render_is_in_platform_menu(void) {
    return in_platform_menu;
}

// Draw text with drop shadow (for GFX themes) - OPTIMIZED: only 2 draws instead of 9
void font_draw_text_outlined(uint16_t *framebuffer, int fb_width, int fb_height,
                             int x, int y, const char *text, uint16_t color) {
    // Simple drop shadow - much faster than full outline (2 draws vs 9)
    uint16_t shadow_color = 0x0000; // Black shadow
    font_draw_text(framebuffer, fb_width, fb_height, x+2, y+2, text, shadow_color);
    // Draw main text on top
    font_draw_text(framebuffer, fb_width, fb_height, x, y, text, color);
}

void render_init(uint16_t *framebuffer) {
    if (framebuffer) {
        render_clear_screen(framebuffer);
    }
}

void render_clear_screen(uint16_t *framebuffer) {
    if (!framebuffer) return;
    
    // Fill with background color
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        framebuffer[i] = COLOR_BG;
    }
}

void render_fill_rect(uint16_t *framebuffer, int x, int y, int width, int height, uint16_t color) {
    if (!framebuffer) return;
    
    for (int py = y; py < y + height && py < SCREEN_HEIGHT; py++) {
        for (int px = x; px < x + width && px < SCREEN_WIDTH; px++) {
            if (px >= 0 && py >= 0) {
                framebuffer[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
}

// v24: Alias for render_fill_rect
void render_filled_rect(uint16_t *framebuffer, int x, int y, int width, int height, uint16_t color) {
    render_fill_rect(framebuffer, x, y, width, height, color);
}

// v24: Draw rectangle outline (border only)
void render_rect(uint16_t *framebuffer, int x, int y, int width, int height, uint16_t color) {
    if (!framebuffer) return;

    // Top edge
    for (int px = x; px < x + width && px < SCREEN_WIDTH; px++) {
        if (px >= 0 && y >= 0 && y < SCREEN_HEIGHT) {
            framebuffer[y * SCREEN_WIDTH + px] = color;
        }
    }
    // Bottom edge
    int bottom_y = y + height - 1;
    for (int px = x; px < x + width && px < SCREEN_WIDTH; px++) {
        if (px >= 0 && bottom_y >= 0 && bottom_y < SCREEN_HEIGHT) {
            framebuffer[bottom_y * SCREEN_WIDTH + px] = color;
        }
    }
    // Left edge
    for (int py = y; py < y + height && py < SCREEN_HEIGHT; py++) {
        if (x >= 0 && x < SCREEN_WIDTH && py >= 0) {
            framebuffer[py * SCREEN_WIDTH + x] = color;
        }
    }
    // Right edge
    int right_x = x + width - 1;
    for (int py = y; py < y + height && py < SCREEN_HEIGHT; py++) {
        if (right_x >= 0 && right_x < SCREEN_WIDTH && py >= 0) {
            framebuffer[py * SCREEN_WIDTH + right_x] = color;
        }
    }
}

void render_rounded_rect(uint16_t *framebuffer, int x, int y, int width, int height, int radius, uint16_t color) {
    if (!framebuffer) return;
    
    // Draw main body (excluding corners)
    render_fill_rect(framebuffer, x + radius, y, width - 2 * radius, height, color);
    render_fill_rect(framebuffer, x, y + radius, width, height - 2 * radius, color);
    
    // Draw rounded corners using circle approximation
    for (int corner_y = 0; corner_y < radius; corner_y++) {
        for (int corner_x = 0; corner_x < radius; corner_x++) {
            int dx = radius - corner_x;
            int dy = radius - corner_y;
            int dist_sq = dx * dx + dy * dy;
            int radius_sq = radius * radius;
            
            if (dist_sq <= radius_sq) {
                // Top-left corner
                int px = x + corner_x;
                int py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    framebuffer[py * SCREEN_WIDTH + px] = color;
                }
                
                // Top-right corner
                px = x + width - 1 - corner_x;
                py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    framebuffer[py * SCREEN_WIDTH + px] = color;
                }
                
                // Bottom-left corner
                px = x + corner_x;
                py = y + height - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    framebuffer[py * SCREEN_WIDTH + px] = color;
                }
                
                // Bottom-right corner
                px = x + width - 1 - corner_x;
                py = y + height - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    framebuffer[py * SCREEN_WIDTH + px] = color;
                }
            }
        }
    }
}

void render_text_pillbox(uint16_t *framebuffer, int x, int y, const char *text,
                        uint16_t bg_color, uint16_t text_color, int padding) {
    if (!framebuffer || !text) return;
    // Draw background rectangle only if bg_color is non-black
    if (bg_color != 0x0000) {
        int text_w = font_measure_text(text);
        if (text_w > 0 && text_w < SCREEN_WIDTH) {
            int rect_x = x - padding;
            if (rect_x < 0) rect_x = 0;
            int rect_y = y - 2;
            if (rect_y < 0) rect_y = 0;
            int rect_w = text_w + padding * 2;
            int rect_h = 16;
            int radius = 4;
            // Clamp radius so it doesn't exceed half of smallest dimension
            if (radius > rect_w / 2) radius = rect_w / 2;
            if (radius > rect_h / 2) radius = rect_h / 2;
            if (radius < 1) radius = 1;
            render_rounded_rect(framebuffer, rect_x, rect_y, rect_w, rect_h, radius, bg_color);
        }
    }
    font_draw_text_outlined(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, x, y, text, text_color);
}




void render_header(uint16_t *framebuffer, const char *title) {
    (void)framebuffer;
    (void)title;
    return; // Title completely removed
}

void render_legend(uint16_t *framebuffer, int x_button_mode) {
    if (!framebuffer) return;

    char legend_str[128];
    if (in_platform_menu) {
        snprintf(legend_str, sizeof(legend_str), " A-OPEN   B-BACK   X-SEARCH   Y-HIDE ");
    } else if (x_button_mode == LEGEND_X_FAVOURITE) {
        snprintf(legend_str, sizeof(legend_str), " A-OPEN   B-BACK   X-SEARCH   Y-FAV ");
    } else if (x_button_mode == LEGEND_X_REMOVE) {
        snprintf(legend_str, sizeof(legend_str), " A-OPEN   B-BACK   X-SEARCH   Y-REM ");
    } else {
        snprintf(legend_str, sizeof(legend_str), " A-OPEN   B-BACK   X-SEARCH ");
    }

    int legend_width = font_measure_text(legend_str);
    int legend_x = PADDING;
    int legend_y = SCREEN_HEIGHT - 24;

    if (gfx_theme_is_active()) {
        const GfxThemeLayout* layout = gfx_theme_get_layout();
        if (layout) {
            legend_x = layout->legend_x;
            legend_y = layout->legend_y;
        }
    }

    render_rounded_rect(framebuffer, legend_x - 4, legend_y - 2, legend_width + 8, 20, 10, COLOR_LEGEND_BG);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, legend_x, legend_y, legend_str, COLOR_LEGEND);
}

// Draw a sized logo image at (start_x, start_y) with optional alpha blending
void render_draw_image_sized(uint16_t *framebuffer, int start_x, int start_y, int target_w, int target_h,
                            const uint16_t *pixels, const uint8_t *alpha,
                            int src_w, int src_h, uint8_t alpha_multiplier) {
    if (!framebuffer || !pixels || src_w <= 0 || src_h <= 0 || target_w <= 0 || target_h <= 0) return;

    for (int dy = 0; dy < target_h; dy++) {
        int py = start_y + dy;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;

        int sy = (dy * src_h) / target_h;

        for (int dx = 0; dx < target_w; dx++) {
            int px = start_x + dx;
            if (px < 0 || px >= SCREEN_WIDTH) continue;

            int sx = (dx * src_w) / target_w;
            int src_idx = sy * src_w + sx;

            uint8_t a = alpha ? alpha[src_idx] : 255;
            if (alpha_multiplier != 255) {
                a = (uint8_t)(((uint16_t)a * alpha_multiplier) / 255);
            }
            if (a == 0) continue;

            uint16_t color = pixels[src_idx];

            if (a == 255) {
                framebuffer[py * SCREEN_WIDTH + px] = color;
            } else {
                uint16_t bg = framebuffer[py * SCREEN_WIDTH + px];
                uint8_t r_src = ((color >> 11) & 0x1F) << 3;
                uint8_t g_src = ((color >> 5) & 0x3F) << 2;
                uint8_t b_src = (color & 0x1F) << 3;

                uint8_t r_bg = ((bg >> 11) & 0x1F) << 3;
                uint8_t g_bg = ((bg >> 5) & 0x3F) << 2;
                uint8_t b_bg = (bg & 0x1F) << 3;

                uint8_t r = (r_src * a + r_bg * (255 - a)) / 255;
                uint8_t g = (g_src * a + g_bg * (255 - a)) / 255;
                uint8_t b = (b_src * a + b_bg * (255 - a)) / 255;

                framebuffer[py * SCREEN_WIDTH + px] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    }
}

void render_draw_image_50x50(uint16_t *framebuffer, int start_x, int start_y,
                            const uint16_t *pixels, const uint8_t *alpha,
                            int src_w, int src_h) {
    render_draw_image_sized(framebuffer, start_x, start_y, 50, 50, pixels, alpha, src_w, src_h, 255);
}

void render_draw_image_60x60(uint16_t *framebuffer, int start_x, int start_y,
                            const uint16_t *pixels, const uint8_t *alpha,
                            int src_w, int src_h) {
    render_draw_image_sized(framebuffer, start_x, start_y, 60, 60, pixels, alpha, src_w, src_h, 255);
}


static int load_raw_horiz_rgb565(const char *path, uint16_t **pixels, int *width, int *height) {
    if (access(path, F_OK) != 0) return 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int w = 0, h = 0;
    if (file_size == 144 * 208 * 2) { w = 144; h = 208; }
    else if (file_size == 160 * 160 * 2) { w = 160; h = 160; }
    else if (file_size == 128 * 128 * 2) { w = 128; h = 128; }
    else if (file_size == 200 * 200 * 2) { w = 200; h = 200; }
    else if (file_size == 250 * 200 * 2) { w = 250; h = 200; }
    else if (file_size == 320 * 240 * 2) { w = 320; h = 240; }
    else if (file_size == 64 * 64 * 2) { w = 64; h = 64; }
    else {
        fclose(fp);
        return 0;
    }

    uint16_t *data = (uint16_t*)malloc(file_size);
    if (!data) {
        fclose(fp);
        return 0;
    }

    if (fread(data, 1, file_size, fp) == (size_t)file_size) {
        fclose(fp);
        *pixels = data;
        *width = w;
        *height = h;
        return 1;
    }

    free(data);
    fclose(fp);
    return 0;
}

#define HORIZ_THUMB_CACHE_SIZE 12
typedef struct {
    char path[256];
    uint16_t *pixels;
    int w, h;
    uint32_t last_used;
} HorizThumbCache;

static HorizThumbCache horiz_thumb_cache[HORIZ_THUMB_CACHE_SIZE];
static uint32_t horiz_thumb_ticks = 0;

static bool get_horiz_cached_thumb(const char *path, uint16_t **pixels, int *w, int *h) {
    horiz_thumb_ticks++;
    for (int i = 0; i < HORIZ_THUMB_CACHE_SIZE; i++) {
        if (horiz_thumb_cache[i].path[0] != '\0' && strcmp(horiz_thumb_cache[i].path, path) == 0) {
            *pixels = horiz_thumb_cache[i].pixels;
            *w = horiz_thumb_cache[i].w;
            *h = horiz_thumb_cache[i].h;
            horiz_thumb_cache[i].last_used = horiz_thumb_ticks;
            return true;
        }
    }
    return false;
}

static void add_horiz_cached_thumb(const char *path, uint16_t *pixels, int w, int h) {
    if (!path || !path[0]) return;
    
    int best_slot = 0;
    uint32_t oldest = 0xFFFFFFFF;
    
    for (int i = 0; i < HORIZ_THUMB_CACHE_SIZE; i++) {
        if (horiz_thumb_cache[i].path[0] == '\0') {
            best_slot = i;
            break;
        }
        if (horiz_thumb_cache[i].last_used < oldest) {
            oldest = horiz_thumb_cache[i].last_used;
            best_slot = i;
        }
    }
    
    if (horiz_thumb_cache[best_slot].pixels) {
        free(horiz_thumb_cache[best_slot].pixels);
        horiz_thumb_cache[best_slot].pixels = NULL;
    }
    
    strncpy(horiz_thumb_cache[best_slot].path, path, 255);
    horiz_thumb_cache[best_slot].path[255] = '\0';
    
    if (pixels && w > 0 && h > 0) {
        size_t bytes = w * h * sizeof(uint16_t);
        horiz_thumb_cache[best_slot].pixels = malloc(bytes);
        if (horiz_thumb_cache[best_slot].pixels) {
            memcpy(horiz_thumb_cache[best_slot].pixels, pixels, bytes);
            horiz_thumb_cache[best_slot].w = w;
            horiz_thumb_cache[best_slot].h = h;
        }
    } else {
        horiz_thumb_cache[best_slot].pixels = NULL;
        horiz_thumb_cache[best_slot].w = 0;
        horiz_thumb_cache[best_slot].h = 0;
    }
    horiz_thumb_cache[best_slot].last_used = horiz_thumb_ticks;
}

void render_menu_item(uint16_t *framebuffer, int index, const char *name, const char *game_path, int is_dir,
                     int is_selected, float scroll_offset, int is_favorited) {
    if (!framebuffer || !name) return;

    // Get layout from GFX theme if active, otherwise use defaults
    int list_x = PADDING;
    int list_y = START_Y;
    int item_height = ITEM_HEIGHT;
    int visible_items = VISIBLE_ENTRIES;
    bool use_outline = false;

    if (gfx_theme_is_active()) {
        const GfxThemeLayout* layout = gfx_theme_get_layout();
        if (layout) {
            if (in_platform_menu) {
                list_x = layout->platform_list_x;
                list_y = layout->platform_list_y_start;
                item_height = layout->platform_item_height;
                visible_items = layout->platform_visible_items;
            } else {
                list_x = layout->game_list_x;
                list_y = layout->game_list_y_start;
                item_height = layout->game_item_height;
                visible_items = layout->game_visible_items;
            }
        }
        use_outline = true;
    }

    // Check grid columns setting / horizontal carousel mode / theme.ini layout default
    const char *grid_setting = NULL;
    if (in_platform_menu) {
        grid_setting = settings_get_value("frogui_menu_layout");
        if (!grid_setting) grid_setting = settings_get_value("frogui_grid_navigation");

        if (!grid_setting || strcmp(grid_setting, "theme_default") == 0 || strcmp(grid_setting, "disabled") == 0) {
            const char *theme_layout = gfx_theme_get_menu_layout();
            if (theme_layout && theme_layout[0] != '\0') {
                grid_setting = theme_layout;
            }
        }
    } else {
        grid_setting = settings_get_value("frogui_game_list_layout");

        if (!grid_setting || strcmp(grid_setting, "theme_default") == 0) {
            const char *theme_layout = gfx_theme_get_game_list_layout();
            if (theme_layout && theme_layout[0] != '\0') {
                grid_setting = theme_layout;
            }
        }
    }
    bool is_horizontal = (grid_setting && strcmp(grid_setting, "horizontal") == 0);

    // Helper declaration
    extern int gfx_theme_load_platform_logo(const char *platform_name, uint16_t **pixels, uint8_t **alpha, int *width, int *height);

    // Get color setting
    uint16_t game_name_color = COLOR_SELECT_TEXT;
    const char *color_opt = settings_get_value("frogui_game_name_color");
    if (color_opt && strcmp(color_opt, "theme_default") != 0) {
        if (strcmp(color_opt, "white") == 0) game_name_color = 0xFFFF;
        else if (strcmp(color_opt, "black") == 0) game_name_color = 0x0000;
        else if (strcmp(color_opt, "red") == 0) game_name_color = 0xF800;
        else if (strcmp(color_opt, "green") == 0) game_name_color = 0x07E0;
        else if (strcmp(color_opt, "blue") == 0) game_name_color = 0x001F;
        else if (strcmp(color_opt, "yellow") == 0) game_name_color = 0xFFE0;
        else if (strcmp(color_opt, "cyan") == 0) game_name_color = 0x07FF;
        else if (strcmp(color_opt, "magenta") == 0) game_name_color = 0xF81F;
        else if (strcmp(color_opt, "gray") == 0) game_name_color = 0x8410;
        else if (strcmp(color_opt, "orange") == 0) game_name_color = 0xFC00;
        else if (strcmp(color_opt, "purple") == 0) game_name_color = 0x8010;
    } else {
        if (gfx_theme_has_custom_game_name_color()) {
            game_name_color = gfx_theme_get_game_name_color();
        }
    }

    // Check icon display settings for platform menu vs game list
    bool show_icons = true;
    if (in_platform_menu) {
        const char *show_icons_setting = settings_get_value("frogui_show_icons");
        if (show_icons_setting && strcmp(show_icons_setting, "true") == 0) {
            show_icons = true;
        } else if (show_icons_setting && strcmp(show_icons_setting, "false") == 0) {
            show_icons = false;
        } else {
            show_icons = gfx_theme_get_show_icons();
        }
    } else {
        const char *show_game_icons_setting = settings_get_value("frogui_show_game_icons");
        if (show_game_icons_setting && strcmp(show_game_icons_setting, "true") == 0) {
            show_icons = true;
        } else if (show_game_icons_setting && strcmp(show_game_icons_setting, "false") == 0) {
            show_icons = false;
        } else {
            show_icons = gfx_theme_get_show_game_icons();
        }
    }

    // Check if system/game names (labels) should be hidden
    bool hide_system_name = false;
    if (in_platform_menu) {
        const char *hide_sys = settings_get_value("frogui_hide_system_names");
        if (hide_sys && strcmp(hide_sys, "true") == 0) {
            hide_system_name = true;
        } else if (hide_sys && strcmp(hide_sys, "false") == 0) {
            hide_system_name = false;
        } else {
            hide_system_name = gfx_theme_get_hide_system_names();
        }
    } else {
        const char *hide_game = settings_get_value("frogui_hide_game_names");
        if (hide_game && strcmp(hide_game, "true") == 0) {
            hide_system_name = true;
        } else if (hide_game && strcmp(hide_game, "false") == 0) {
            hide_system_name = false;
        } else {
            hide_system_name = gfx_theme_get_hide_game_names();
        }
    }

    // Helper declaration
    extern int gfx_theme_load_entry_logo(const char *name, bool is_platform, uint16_t **pixels, uint8_t **alpha, int *width, int *height);

    if (is_horizontal) {
        float rel_index = (float)index - scroll_offset;
        if (rel_index < -3.5f || rel_index > 3.5f) {
            return;
        }
    }

    // Try loading logo for this entry (only in horizontal carousel mode)
    uint16_t *logo_pixels = NULL;
    uint8_t *logo_alpha = NULL;
    int logo_w = 0, logo_h = 0;
    bool local_thumb_allocated = false;
    bool local_alpha_allocated = false;
    bool has_logo = is_horizontal && gfx_theme_load_entry_logo(name, in_platform_menu, &logo_pixels, &logo_alpha, &logo_w, &logo_h);

    if (is_horizontal) {
        // Horizontal Nintendo Switch style carousel menu
        float rel_index = (float)index - scroll_offset;

        extern int gfx_theme_get_horizontal_y(void);
        extern int gfx_theme_get_horizontal_tile_w(void);
        extern int gfx_theme_get_horizontal_tile_h(void);
        extern int gfx_theme_get_horizontal_item_spacing(void);

        int center_x = SCREEN_WIDTH / 2;
        int center_y = gfx_theme_get_horizontal_y();
        int tile_w = gfx_theme_get_horizontal_tile_w();
        int tile_h = gfx_theme_get_horizontal_tile_h();
        int item_spacing = gfx_theme_get_horizontal_item_spacing();

        int item_center_x = center_x + (int)(rel_index * (float)item_spacing);
        int logo_x = item_center_x - (tile_w / 2);
        int logo_y = center_y - (tile_h / 2);

        if (show_icons) {
            bool sel_bg = true;
            const char *sel_bg_setting = settings_get_value("frogui_show_selected_icon_bg");
            if (gfx_theme_has_custom_show_selected_icon_bg() && (!sel_bg_setting || strcmp(sel_bg_setting, "theme_default") == 0)) {
                sel_bg = gfx_theme_get_show_selected_icon_bg();
            } else if (sel_bg_setting && strcmp(sel_bg_setting, "false") == 0) {
                sel_bg = false;
            }

            bool dim_unsel = false;
            const char *dim_setting = settings_get_value("frogui_dim_unselected_icons");
            if (gfx_theme_has_custom_dim_unselected_icons() && (!dim_setting || strcmp(dim_setting, "theme_default") == 0)) {
                dim_unsel = gfx_theme_get_dim_unselected_icons();
            } else if (dim_setting && strcmp(dim_setting, "true") == 0) {
                dim_unsel = true;
            }

            bool empty_bg = true;
            const char *empty_bg_setting = settings_get_value("frogui_show_empty_icon_bg");
            if (gfx_theme_has_custom_show_empty_icon_bg() && (!empty_bg_setting || strcmp(empty_bg_setting, "theme_default") == 0)) {
                empty_bg = gfx_theme_get_show_empty_icon_bg();
            } else if (empty_bg_setting && strcmp(empty_bg_setting, "false") == 0) {
                empty_bg = false;
            }

            const char *text_empty_opt = NULL;
            int text_in_empty = 1;
            if (!in_platform_menu) {
                text_empty_opt = settings_get_value("frogui_game_text_in_empty");
                if (!text_empty_opt || strcmp(text_empty_opt, "theme_default") == 0) {
                    text_empty_opt = settings_get_value("frogui_text_in_empty_icon");
                }
            } else {
                text_empty_opt = settings_get_value("frogui_text_in_empty_icon");
            }

            if (text_empty_opt && strcmp(text_empty_opt, "false") == 0) {
                text_in_empty = 0;
            } else if (text_empty_opt && strcmp(text_empty_opt, "true") == 0) {
                text_in_empty = 1;
            } else if (gfx_theme_has_custom_text_in_empty_icon()) {
                text_in_empty = gfx_theme_get_text_in_empty_icon();
            }
            if (!in_platform_menu && game_path && game_path[0] != '\0') {
                char path1[256];
                get_thumbnail_path(game_path, path1, sizeof(path1));
                char *d1 = strrchr(path1, '.');
                if (d1) {
                    strcpy(d1, "-icon.rgb565");
                }
                
                char path2[256];
                strncpy(path2, game_path, sizeof(path2) - 1);
                path2[sizeof(path2) - 1] = '\0';
                char *d2 = strrchr(path2, '.');
                if (d2) {
                    strcpy(d2, "-icon.rgb565");
                } else {
                    strncat(path2, "-icon.rgb565", sizeof(path2) - strlen(path2) - 1);
                }
                
                uint16_t *cached_px = NULL;
                int cached_w = 0, cached_h = 0;
                
                if (get_horiz_cached_thumb(path1, &cached_px, &cached_w, &cached_h)) {
                    if (cached_px) {
                        logo_pixels = cached_px;
                        logo_alpha = NULL;
                        logo_w = cached_w;
                        logo_h = cached_h;
                        has_logo = true;
                    }
                } else {
                    uint16_t *raw_pixels = NULL;
                    int raw_w = 0, raw_h = 0;
                    
                    if (load_raw_horiz_rgb565(path1, &raw_pixels, &raw_w, &raw_h) ||
                        load_raw_horiz_rgb565(path2, &raw_pixels, &raw_w, &raw_h)) {
                        
                        logo_pixels = raw_pixels;
                        logo_alpha = NULL;
                        logo_w = raw_w;
                        logo_h = raw_h;
                        has_logo = true;
                        
                        add_horiz_cached_thumb(path1, raw_pixels, raw_w, raw_h);
                        local_thumb_allocated = true; // We malloced inside load_raw_horiz_rgb565
                    } else {
                        // Check for -icon.png in game folder
                        char path2_png[256];
                        strncpy(path2_png, path2, sizeof(path2_png) - 1);
                        path2_png[sizeof(path2_png) - 1] = '\0';
                        char *ext = strstr(path2_png, ".rgb565");
                        if (ext) strcpy(ext, ".png");
                        
                        uint8_t *raw_alpha = NULL;
                        extern int load_png_rgba565(const char* filename, uint16_t** pixels, uint8_t** alpha, int* width, int* height);
                        if (load_png_rgba565(path2_png, &raw_pixels, &raw_alpha, &raw_w, &raw_h)) {
                            logo_pixels = raw_pixels;
                            logo_alpha = raw_alpha;
                            logo_w = raw_w;
                            logo_h = raw_h;
                            has_logo = true;
                            // Note: caching of PNG with alpha in this cache isn't fully supported without alpha array, 
                            // but we can skip caching or handle it. The cache only stores RGB565.
                            // We won't cache PNGs to keep it simple and avoid memory leaks.
                            local_thumb_allocated = true; 
                            local_alpha_allocated = true;
                        } else {
                            add_horiz_cached_thumb(path1, NULL, 0, 0); // Cache negative hit
                        }
                    }
                }
            }
            
            if (has_logo) {
                if (is_selected && sel_bg) {
                    render_rect(framebuffer, logo_x - 3, logo_y - 3, tile_w + 6, tile_h + 6, COLOR_SELECT_BG);
                }
                
                uint8_t alpha_mult = 255;
                if (!is_selected && dim_unsel) {
                    alpha_mult = 128; // 50% opacity
                }
                
                render_draw_image_sized(framebuffer, logo_x, logo_y, tile_w, tile_h, logo_pixels, logo_alpha, logo_w, logo_h, alpha_mult);
            } else {
                // Bare empty square tile ("goły kwadrat") when show_icons is enabled but no logo image
                if (is_selected) {
                    if (sel_bg) {
                        render_rounded_rect(framebuffer, logo_x, logo_y, tile_w, tile_h, 8, COLOR_SELECT_BG);
                    }
                } else {
                    if (empty_bg) {
                        render_rounded_rect(framebuffer, logo_x, logo_y, tile_w, tile_h, 8, 0x18C3);
                    }
                }

                if (text_in_empty && (!is_selected || hide_system_name)) {
                    int text_w = font_measure_text(name);
                    int text_x = logo_x + (tile_w - text_w) / 2;
                    int text_y = logo_y + (tile_h - FONT_CHAR_HEIGHT) / 2;
                    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, text_y, name, is_selected ? COLOR_SELECT_TEXT : COLOR_TEXT);
                }
            }
        } else {
            // show_icons is false, draw text-only carousel
            int text_w = font_measure_text(name);
            int text_x = item_center_x - (text_w / 2);
            if (is_selected) {
                render_text_pillbox(framebuffer, text_x, center_y, name, 0x4A49, 0xFFFF, 6);
            } else {
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, center_y, name, COLOR_TEXT);
            }
        }

        // Draw system/game label below the selected item if not hidden
        if (is_selected && !hide_system_name) {
            int label_w = font_measure_text(name);
            int label_x = center_x - (label_w / 2);
            int label_y = center_y + (tile_h / 2) + 12;
            const char *use_pillbox = settings_get_value("frogui_list_pillbox");
            if (use_pillbox && strcmp(use_pillbox, "true") == 0) {
                render_text_pillbox(framebuffer, label_x, label_y, name, 0x4A49, game_name_color, 7);
            } else {
                // Simple highlighted text
                render_text_pillbox(framebuffer, label_x, label_y, name, 0x0000, game_name_color, 7);
            }
        }
        
        if (local_thumb_allocated && logo_pixels) {
            free((void*)logo_pixels);
        }
        if (local_alpha_allocated && logo_alpha) {
            free((void*)logo_alpha);
        }
        return;
    }

    int cols = 1;
    if (grid_setting) {
        if (strcmp(grid_setting, "2_columns") == 0 || strcmp(grid_setting, "grid_2_columns") == 0 || strcmp(grid_setting, "grid_2") == 0) cols = 2;
        else if (strcmp(grid_setting, "3_columns") == 0 || strcmp(grid_setting, "grid_3_columns") == 0 || strcmp(grid_setting, "grid_3") == 0) cols = 3;
        else if (strcmp(grid_setting, "grid") == 0) cols = gfx_theme_get_grid_cols();
    }

    int visible_index = index - scroll_offset;
    if (visible_index < 0 || visible_index >= visible_items * cols) return;

    int col = visible_index % cols;
    int row = visible_index / cols;

    if (cols > 1 && show_icons) {
        int grid_x = gfx_theme_get_grid_x(cols);
        int grid_y = gfx_theme_get_grid_y();
        int tile_w = gfx_theme_get_grid_tile_w(cols);
        int tile_h = gfx_theme_get_grid_tile_h(cols);
        int spacing_x = gfx_theme_get_grid_spacing_x(cols);
        int spacing_y = gfx_theme_get_grid_spacing_y(cols);

        int item_x = grid_x + col * spacing_x;
        int item_y = grid_y + row * spacing_y;

        uint16_t *logo_pixels = NULL;
        uint8_t *logo_alpha = NULL;
        int logo_w = 0, logo_h = 0;
        bool local_thumb_allocated = false;
        bool local_alpha_allocated = false;
        bool has_logo = gfx_theme_load_entry_logo(name, in_platform_menu, &logo_pixels, &logo_alpha, &logo_w, &logo_h);

        bool sel_bg = true;
        const char *sel_bg_setting = settings_get_value("frogui_show_selected_icon_bg");
        if (gfx_theme_has_custom_show_selected_icon_bg() && (!sel_bg_setting || strcmp(sel_bg_setting, "theme_default") == 0)) {
            sel_bg = gfx_theme_get_show_selected_icon_bg();
        } else if (sel_bg_setting && strcmp(sel_bg_setting, "false") == 0) {
            sel_bg = false;
        }

        bool dim_unsel = false;
        const char *dim_setting = settings_get_value("frogui_dim_unselected_icons");
        if (gfx_theme_has_custom_dim_unselected_icons() && (!dim_setting || strcmp(dim_setting, "theme_default") == 0)) {
            dim_unsel = gfx_theme_get_dim_unselected_icons();
        } else if (dim_setting && strcmp(dim_setting, "true") == 0) {
            dim_unsel = true;
        }

        bool empty_bg = true;
        const char *empty_bg_setting = settings_get_value("frogui_show_empty_icon_bg");
        if (gfx_theme_has_custom_show_empty_icon_bg() && (!empty_bg_setting || strcmp(empty_bg_setting, "theme_default") == 0)) {
            empty_bg = gfx_theme_get_show_empty_icon_bg();
        } else if (empty_bg_setting && strcmp(empty_bg_setting, "false") == 0) {
            empty_bg = false;
        }

        if (!in_platform_menu && game_path && game_path[0] != '\0' && !has_logo) {
            char path1[256];
            get_thumbnail_path(game_path, path1, sizeof(path1));
            char *d1 = strrchr(path1, '.');
            if (d1) strcpy(d1, "-icon.rgb565");

            char path2[256];
            strncpy(path2, game_path, sizeof(path2) - 1);
            path2[sizeof(path2) - 1] = '\0';
            char *d2 = strrchr(path2, '.');
            if (d2) strcpy(d2, "-icon.rgb565");
            else strncat(path2, "-icon.rgb565", sizeof(path2) - strlen(path2) - 1);

            uint16_t *cached_px = NULL;
            int cached_w = 0, cached_h = 0;
            if (get_horiz_cached_thumb(path1, &cached_px, &cached_w, &cached_h)) {
                if (cached_px) {
                    logo_pixels = cached_px;
                    logo_alpha = NULL;
                    logo_w = cached_w;
                    logo_h = cached_h;
                    has_logo = true;
                }
            } else {
                uint16_t *raw_pixels = NULL;
                int raw_w = 0, raw_h = 0;
                if (load_raw_horiz_rgb565(path1, &raw_pixels, &raw_w, &raw_h) ||
                    load_raw_horiz_rgb565(path2, &raw_pixels, &raw_w, &raw_h)) {
                    logo_pixels = raw_pixels;
                    logo_alpha = NULL;
                    logo_w = raw_w;
                    logo_h = raw_h;
                    has_logo = true;
                    add_horiz_cached_thumb(path1, raw_pixels, raw_w, raw_h);
                    local_thumb_allocated = true;
                } else {
                    char path2_png[256];
                    strncpy(path2_png, path2, sizeof(path2_png) - 1);
                    path2_png[sizeof(path2_png) - 1] = '\0';
                    char *ext = strstr(path2_png, ".rgb565");
                    if (ext) strcpy(ext, ".png");

                    uint8_t *raw_alpha = NULL;
                    extern int load_png_rgba565(const char* filename, uint16_t** pixels, uint8_t** alpha, int* width, int* height);
                    if (load_png_rgba565(path2_png, &raw_pixels, &raw_alpha, &raw_w, &raw_h)) {
                        logo_pixels = raw_pixels;
                        logo_alpha = raw_alpha;
                        logo_w = raw_w;
                        logo_h = raw_h;
                        has_logo = true;
                        local_thumb_allocated = true;
                        local_alpha_allocated = true;
                    }
                }
            }
        }

        if (has_logo) {
            if (is_selected && sel_bg) {
                render_rounded_rect(framebuffer, item_x - 3, item_y - 3, tile_w + 6, tile_h + 6, 6, COLOR_SELECT_BG);
            }
            uint8_t alpha_mult = 255;
            if (!is_selected && dim_unsel) {
                alpha_mult = 128;
            }
            render_draw_image_sized(framebuffer, item_x, item_y, tile_w, tile_h, logo_pixels, logo_alpha, logo_w, logo_h, alpha_mult);
        } else {
            if (is_selected) {
                if (sel_bg) {
                    render_rounded_rect(framebuffer, item_x, item_y, tile_w, tile_h, 6, COLOR_SELECT_BG);
                }
            } else {
                if (empty_bg) {
                    render_rounded_rect(framebuffer, item_x, item_y, tile_w, tile_h, 6, 0x18C3);
                }
            }

            int text_w = font_measure_text(name);
            int text_x = item_x + (tile_w - text_w) / 2;
            if (text_x < item_x + 2) text_x = item_x + 2;
            int text_y = item_y + (tile_h - FONT_CHAR_HEIGHT) / 2;
            font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, text_y, name, is_selected ? COLOR_SELECT_TEXT : COLOR_TEXT);
        }

        // Label below tile if not hidden
        if (!hide_system_name && has_logo) {
            int label_w = font_measure_text(name);
            int label_x = item_x + (tile_w - label_w) / 2;
            if (label_x < 2) label_x = 2;
            if (label_x + label_w > SCREEN_WIDTH - 2) label_x = SCREEN_WIDTH - label_w - 2;
            int label_y = item_y + tile_h + 3;

            const char *use_pillbox = settings_get_value("frogui_list_pillbox");
            if (is_selected) {
                if (use_pillbox && strcmp(use_pillbox, "true") == 0) {
                    render_text_pillbox(framebuffer, label_x, label_y, name, 0x4A49, game_name_color, 4);
                } else {
                    render_text_pillbox(framebuffer, label_x, label_y, name, 0x0000, game_name_color, 4);
                }
            } else {
                font_draw_text_outlined(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, label_x, label_y, name, COLOR_TEXT);
            }
        }

        if (local_thumb_allocated && logo_pixels) {
            free((void*)logo_pixels);
        }
        if (local_alpha_allocated && logo_alpha) {
            free((void*)logo_alpha);
        }
        return;
    }

    int col_width = (SCREEN_WIDTH - 2 * list_x) / cols;
    int item_x = list_x + col * col_width;
    int y = list_y + row * item_height;

    // Draw favorite star if favorited
    int text_x = item_x;
    if (is_favorited) {
        const char *star = "*"; // Asterisk as favorite marker
        if (use_outline) {
            font_draw_text_outlined(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, item_x, y, star, COLOR_HEADER);
        } else {
            font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, item_x, y, star, COLOR_HEADER);
        }
        text_x = item_x + 15; // Offset text to the right of the star
    }

    if (hide_system_name) return;

    if (is_selected) {
        const char *use_pillbox = settings_get_value("frogui_list_pillbox");
        if (use_pillbox && strcmp(use_pillbox, "true") == 0) {
            // Use unified pillbox rendering with gray background (RGB565: 0x4A49 - approx RGB 74,73,74)
            render_text_pillbox(framebuffer, text_x, y, name, 0x4A49, 0xFFFF, 7);
        } else {
            // Traditional highlighted text
            uint16_t text_color = COLOR_SELECT_TEXT;
            bool use_text_bg = false;
            if (gfx_theme_is_active()) {
                if (in_platform_menu) {
                    use_text_bg = gfx_theme_platform_text_background();
                } else {
                    use_text_bg = gfx_theme_game_text_background();
                }
            }
            if (use_text_bg) {
                render_text_pillbox(framebuffer, text_x, y, name, 0x0000, text_color, 7);
            } else if (use_outline) {
                font_draw_text_outlined(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, y, name, text_color);
            } else {
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, y, name, text_color);
            }
        }
    } else {
        uint16_t text_color = is_dir ? COLOR_FOLDER : COLOR_TEXT;

        bool use_text_bg = false;
        if (gfx_theme_is_active()) {
            if (in_platform_menu) {
                use_text_bg = gfx_theme_platform_text_background();
            } else {
                use_text_bg = gfx_theme_game_text_background();
            }
        }

        if (use_text_bg) {
            render_text_pillbox(framebuffer, text_x, y, name, 0x0000, text_color, 7);
        } else if (use_outline) {
            font_draw_text_outlined(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, y, name, text_color);
        } else {
            font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, y, name, text_color);
        }
    }
}

// Thumbnail implementation

void get_thumbnail_path(const char *game_path, char *thumb_path, size_t thumb_path_size) {
    if (!game_path || !thumb_path || game_path[0] == '\0') {
        thumb_path[0] = '\0';
        return;
    }
    
    // Find the last slash to get directory
    const char *last_slash = strrchr(game_path, '/');
    if (!last_slash) {
        thumb_path[0] = '\0';
        return;
    }
    
    // Copy directory path
    size_t dir_len = last_slash - game_path;
    if (dir_len + 1 >= thumb_path_size) {
        thumb_path[0] = '\0';
        return;
    }
    
    strncpy(thumb_path, game_path, dir_len);
    thumb_path[dir_len] = '\0';
    
    // Add /.res/ subdirectory
    strncat(thumb_path, "/.res/", thumb_path_size - strlen(thumb_path) - 1);
    
    // Get filename without extension
    const char *filename = last_slash + 1;
    const char *last_dot = strrchr(filename, '.');
    
    if (last_dot) {
        size_t name_len = last_dot - filename;
        strncat(thumb_path, filename, min(name_len, thumb_path_size - strlen(thumb_path) - 1));
    } else {
        strncat(thumb_path, filename, thumb_path_size - strlen(thumb_path) - 1);
    }
    
    // Use raw RGB565 format - no parsing, fixed size, minimal memory
    strncat(thumb_path, ".rgb565", thumb_path_size - strlen(thumb_path) - 1);
}

static uint16_t rgb24_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// Debug logging
extern void xlog(const char *fmt, ...);

int load_thumbnail(const char *rgb565_path, Thumbnail *thumb) {
    if (!rgb565_path || !thumb) return 0;

    // Initialize thumbnail
    thumb->data = NULL;
    thumb->width = 0;
    thumb->height = 0;

    // v42: Try multiple formats
    // 1. rgb565 from .res folder (original format, fastest)
    // 2. webp/png/jpg from ROM folder (same folder as game file)

    xlog("THUMB: input=%s\n", rgb565_path);

    // 1. Try raw RGB565 from .res folder
    if (load_raw_rgb565(rgb565_path, thumb)) {
        xlog("THUMB: rgb565 OK\n");
        return 1;
    }

    // v72: Use universal buffer for converted thumbnails
    uint16_t *loaded_data = NULL;
    int w = 0, h = 0;
    char try_path[520];

    // Build .res folder base path (remove .rgb565 extension)
    char res_base[512];
    strncpy(res_base, rgb565_path, sizeof(res_base) - 1);
    res_base[sizeof(res_base) - 1] = '\0';
    size_t res_len = strlen(res_base);
    if (res_len > 7 && strcmp(res_base + res_len - 7, ".rgb565") == 0) {
        res_base[res_len - 7] = '\0';
    }

    // v72: 2. Try other formats in .res folder (PNG, JPG, WebP, BMP, GIF)
    snprintf(try_path, sizeof(try_path), "%s.png", res_base);
    if (load_png_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: .res png OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.jpg", res_base);
    if (load_jpeg_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: .res jpg OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.webp", res_base);
    if (load_webp_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: .res webp OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.bmp", res_base);
    if (load_bmp_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: .res bmp OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.gif", res_base);
    if (load_gif_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: .res gif OK %dx%d\n", w, h);
        goto convert_success;
    }

    // 3. Build ROM folder path by removing "/.res/" from path
    // rgb565_path: /roms/nes/.res/mario.rgb565
    // we want:     /roms/nes/mario
    char rom_path[512];
    strncpy(rom_path, rgb565_path, sizeof(rom_path) - 1);
    rom_path[sizeof(rom_path) - 1] = '\0';

    // Find and remove /.res/ from path
    char *res_ptr = strstr(rom_path, "/.res/");
    if (!res_ptr) {
        res_ptr = strstr(rom_path, "\\.res\\");  // Windows style
    }
    if (res_ptr) {
        // Move everything after /.res/ to replace it
        memmove(res_ptr + 1, res_ptr + 6, strlen(res_ptr + 6) + 1);
    }

    // Remove .rgb565 extension
    size_t len = strlen(rom_path);
    if (len > 7 && strcmp(rom_path + len - 7, ".rgb565") == 0) {
        rom_path[len - 7] = '\0';
    }

    // 4. Try formats in ROM folder (WebP, PNG, JPG, BMP, GIF)
    snprintf(try_path, sizeof(try_path), "%s.webp", rom_path);
    if (load_webp_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: rom webp OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.png", rom_path);
    if (load_png_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: rom png OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.jpg", rom_path);
    if (load_jpeg_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: rom jpg OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.bmp", rom_path);
    if (load_bmp_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: rom bmp OK %dx%d\n", w, h);
        goto convert_success;
    }

    snprintf(try_path, sizeof(try_path), "%s.gif", rom_path);
    if (load_gif_rgb565(try_path, &loaded_data, &w, &h)) {
        xlog("THUMB: rom gif OK %dx%d\n", w, h);
        goto convert_success;
    }

    // Nothing found
    xlog("THUMB: nothing found\n");
    return 0;

convert_success:
    // v42: Copy to universal buffer if it fits
    if ((size_t)(w * h) <= UNIVERSAL_MAX_PIXELS_RGB565) {
        memcpy(universal_buffer_u16, loaded_data, w * h * sizeof(uint16_t));
        free(loaded_data);
        thumb->data = universal_buffer_u16;
        thumb->width = w;
        thumb->height = h;
        return 1;
    }

    // Too large for universal buffer
    xlog("THUMB: too large %dx%d > %d\n", w, h, (int)UNIVERSAL_MAX_PIXELS_RGB565);
    free(loaded_data);
    return 0;
}

// v42: load_raw_rgb565 uses universal_buffer

int load_raw_rgb565(const char *path, Thumbnail *thumb) {
    // Check if file exists
    if (access(path, F_OK) != 0) {
        return 0;
    }
    
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    
    // Try common dimensions - including larger sizes (v42: added 320x240, 320x256, 400x300)
    int dimensions[][2] = {{64,64}, {128,128}, {144,200}, {144,208}, {160,160}, {200,200}, {250,200}, {200,250}, {320,240}, {320,256}, {400,300}};
    int num_dims = sizeof(dimensions) / sizeof(dimensions[0]);
    
    for (int i = 0; i < num_dims; i++) {
        int w = dimensions[i][0];
        int h = dimensions[i][1];
        if (w * h * 2 == file_size) {

            // v42: Check if it fits in universal buffer
            if ((size_t)(w * h) > UNIVERSAL_MAX_PIXELS_RGB565) {
                fclose(fp);
                return 0;
            }

            thumb->width = w;
            thumb->height = h;
            thumb->data = universal_buffer_u16; // v42: Use universal buffer

            size_t read_bytes = fread(thumb->data, 1, file_size, fp);
            fclose(fp);
            
            if (read_bytes == file_size) {
                return 1;
            } else {
                return 0;
            }
        }
    }
    
    fclose(fp);
    return 0;
}

void free_thumbnail(Thumbnail *thumb) {
    if (thumb) {
        // No need to free static buffer, just reset pointer
        thumb->data = NULL;
        thumb->width = 0;
        thumb->height = 0;
    }
}

static const uint16_t *last_thumb_data = NULL;
static int thumb_fade_step = 255;

void render_thumbnail(uint16_t *framebuffer, const Thumbnail *thumb) {
    if (!framebuffer || !thumb || !thumb->data) {
        last_thumb_data = NULL;
        thumb_fade_step = 255;
        return;
    }
    
    // Calculate scaled dimensions to fit in thumbnail area
    int display_width = thumb->width;
    int display_height = thumb->height;
    
    // Scale down if too large
    if (display_width > THUMBNAIL_MAX_WIDTH) {
        display_height = (display_height * THUMBNAIL_MAX_WIDTH) / display_width;
        display_width = THUMBNAIL_MAX_WIDTH;
    }
    
    if (display_height > THUMBNAIL_MAX_HEIGHT) {
        display_width = (display_width * THUMBNAIL_MAX_HEIGHT) / display_height;
        display_height = THUMBNAIL_MAX_HEIGHT;
    }
    
    // Center in thumbnail area (vertically) and align to right edge
    int start_x = SCREEN_WIDTH - display_width;  // Align to right edge of screen
    
    // Center thumbnail vertically on screen
    int start_y = (SCREEN_HEIGHT - display_height) / 2;

    // Check if thumbnail changed to trigger smooth cross-fade transition
    if (thumb->data != last_thumb_data) {
        if (last_thumb_data != NULL) {
            thumb_fade_step = 0; // Trigger fade-in
        } else {
            thumb_fade_step = 255;
        }
        last_thumb_data = thumb->data;
    }

    if (thumb_fade_step < 255) {
        thumb_fade_step += 25; // Smooth ~10 frame transition (~0.25s)
        if (thumb_fade_step > 255) thumb_fade_step = 255;
    }
    
    // Draw background frame with dark gray border and light gray fill
    #define FRAME_COLOR 0x39E7      // Dark gray border (RGB565: 7,15,7)
    #define BG_COLOR    0x2104      // Very dark gray background (RGB565: 4,8,4)
    
    int frame_x = start_x - 2;
    int frame_y = start_y - 2; 
    int frame_w = display_width + 4;
    int frame_h = display_height + 4;
    
    // Draw border frame
    render_fill_rect(framebuffer, frame_x, frame_y, frame_w, frame_h, FRAME_COLOR);
    // Draw inner background
    render_fill_rect(framebuffer, start_x, start_y, display_width, display_height, BG_COLOR);
    
    // v61: Draw scaled thumbnail with bilinear filtering and smooth alpha cross-fade
    for (int y = 0; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            int screen_x = start_x + x;
            int screen_y = start_y + y;

            if (screen_x >= 0 && screen_x < SCREEN_WIDTH &&
                screen_y >= 0 && screen_y < SCREEN_HEIGHT) {

                // Fixed-point source coordinates (8 fractional bits)
                int src_x_fp = (x * thumb->width * 256) / display_width;
                int src_y_fp = (y * thumb->height * 256) / display_height;

                int src_x0 = src_x_fp >> 8;
                int src_y0 = src_y_fp >> 8;
                int frac_x = src_x_fp & 0xFF;
                int frac_y = src_y_fp & 0xFF;

                int src_x1 = (src_x0 + 1 < thumb->width) ? src_x0 + 1 : src_x0;
                int src_y1 = (src_y0 + 1 < thumb->height) ? src_y0 + 1 : src_y0;

                // Get 4 surrounding pixels
                uint16_t p00 = thumb->data[src_y0 * thumb->width + src_x0];
                uint16_t p10 = thumb->data[src_y0 * thumb->width + src_x1];
                uint16_t p01 = thumb->data[src_y1 * thumb->width + src_x0];
                uint16_t p11 = thumb->data[src_y1 * thumb->width + src_x1];

                // Extract RGB components
                int r00 = (p00 >> 11) & 0x1F, g00 = (p00 >> 5) & 0x3F, b00 = p00 & 0x1F;
                int r10 = (p10 >> 11) & 0x1F, g10 = (p10 >> 5) & 0x3F, b10 = p10 & 0x1F;
                int r01 = (p01 >> 11) & 0x1F, g01 = (p01 >> 5) & 0x3F, b01 = p01 & 0x1F;
                int r11 = (p11 >> 11) & 0x1F, g11 = (p11 >> 5) & 0x3F, b11 = p11 & 0x1F;

                // Bilinear interpolation
                int inv_frac_x = 256 - frac_x;
                int inv_frac_y = 256 - frac_y;

                int r = (r00 * inv_frac_x * inv_frac_y + r10 * frac_x * inv_frac_y +
                         r01 * inv_frac_x * frac_y + r11 * frac_x * frac_y) >> 16;
                int g = (g00 * inv_frac_x * inv_frac_y + g10 * frac_x * inv_frac_y +
                         g01 * inv_frac_x * frac_y + g11 * frac_x * frac_y) >> 16;
                int b = (b00 * inv_frac_x * inv_frac_y + b10 * frac_x * inv_frac_y +
                         b01 * inv_frac_x * frac_y + b11 * frac_x * frac_y) >> 16;

                uint16_t pixel = (r << 11) | (g << 5) | b;

                // Only draw non-black pixels, let dark gray background show through
                if (pixel != 0x0000) {
                    if (thumb_fade_step < 255) {
                        uint16_t old_pix = framebuffer[screen_y * SCREEN_WIDTH + screen_x];
                        if (old_pix != 0x0000 && old_pix != BG_COLOR && old_pix != FRAME_COLOR) {
                            int r_old = (old_pix >> 11) & 0x1F, g_old = (old_pix >> 5) & 0x3F, b_old = old_pix & 0x1F;
                            int r_blend = (r_old * (255 - thumb_fade_step) + r * thumb_fade_step) >> 8;
                            int g_blend = (g_old * (255 - thumb_fade_step) + g * thumb_fade_step) >> 8;
                            int b_blend = (b_old * (255 - thumb_fade_step) + b * thumb_fade_step) >> 8;
                            pixel = (r_blend << 11) | (g_blend << 5) | b_blend;
                        }
                    }
                    framebuffer[screen_y * SCREEN_WIDTH + screen_x] = pixel;
                }
            }
        }
    }
}

// ===== GFX THEME SUPPORT =====

// Load PNG file to RGB565 format (using lodepng like santa_game)
int load_png_rgb565(const char* filename, uint16_t** data, int* width, int* height) {
    unsigned char* rgba_data = NULL;
    unsigned int w, h;

    // Load PNG with lodepng (RGBA 8-bit)
    unsigned error = lodepng_decode32_file(&rgba_data, &w, &h, filename);

    if (error) {
        // PNG load failed
        return 0;
    }

    // Allocate RGB565 buffer
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) {
        free(rgba_data);
        return 0;
    }

    // Convert RGBA -> RGB565 (like santa_game)
    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            unsigned int rgba_idx = (y * w + x) * 4;
            unsigned int idx = y * w + x;

            unsigned char r = rgba_data[rgba_idx + 0];
            unsigned char g = rgba_data[rgba_idx + 1];
            unsigned char b = rgba_data[rgba_idx + 2];
            // Alpha ignored for background

            (*data)[idx] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }

    free(rgba_data);

    *width = (int)w;
    *height = (int)h;
    return 1;
}

// v19: Load PNG file to RGB565 format WITH alpha channel (for transparent overlays)
// COPIED FROM santa_game120 load_city_skyline pattern
int load_png_rgba565(const char* filename, uint16_t** pixels, uint8_t** alpha, int* width, int* height) {
    unsigned char* rgba_data = NULL;
    unsigned int w, h;

    // Load PNG with lodepng (RGBA 8-bit)
    unsigned error = lodepng_decode32_file(&rgba_data, &w, &h, filename);

    if (error) {
        // PNG load failed
        return 0;
    }

    // Allocate RGB565 buffer and alpha buffer
    *pixels = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    *alpha = (uint8_t*)malloc(w * h);

    if (!*pixels || !*alpha) {
        if (*pixels) { free(*pixels); *pixels = NULL; }
        if (*alpha) { free(*alpha); *alpha = NULL; }
        free(rgba_data);
        return 0;
    }

    // Convert RGBA -> RGB565 + separate alpha (like santa_game120)
    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            unsigned int rgba_idx = (y * w + x) * 4;
            unsigned int idx = y * w + x;

            unsigned char r = rgba_data[rgba_idx + 0];
            unsigned char g = rgba_data[rgba_idx + 1];
            unsigned char b = rgba_data[rgba_idx + 2];
            unsigned char a = rgba_data[rgba_idx + 3];

            (*pixels)[idx] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            (*alpha)[idx] = a;
        }
    }

    free(rgba_data);

    *width = (int)w;
    *height = (int)h;
    return 1;
}

// v40: JPEG loading using stb_image (supports progressive JPEG)
#include "stb_image.h"
#include "gifdec.h"
// v42: WebP loading using simplewebp (lossy + lossless, integer-only)
#include "simplewebp.h"

// v40: Load JPEG file to RGB565 format using stb_image
int load_jpeg_rgb565(const char* filename, uint16_t** data, int* width, int* height) {
    // Load file into memory first (stb_image needs memory buffer when STBI_NO_STDIO)
    FILE* fp = fopen(filename, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t* file_data = (uint8_t*)malloc(file_size);
    if (!file_data) {
        fclose(fp);
        return 0;
    }

    if (fread(file_data, 1, file_size, fp) != (size_t)file_size) {
        free(file_data);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    // Decode JPEG using stb_image
    int w, h, channels;
    uint8_t* rgb_data = stbi_load_from_memory(file_data, file_size, &w, &h, &channels, 3);
    free(file_data);

    if (!rgb_data) {
        return 0;
    }

    // Allocate RGB565 output buffer
    *width = w;
    *height = h;
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) {
        stbi_image_free(rgb_data);
        return 0;
    }

    // Convert RGB888 to RGB565
    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgb_data[i * 3 + 0];
        uint8_t g = rgb_data[i * 3 + 1];
        uint8_t b = rgb_data[i * 3 + 2];
        (*data)[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    stbi_image_free(rgb_data);
    return 1;
}

// v42: Load WebP file to RGB565 format using simplewebp (supports lossy + lossless)
// Uses universal_buffer for output, malloc's temp RGBA buffer for decode

int load_webp_rgb565(const char* filename, uint16_t** data, int* width, int* height) {
    // Load file into memory
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        xlog("WEBP: fopen failed: %s\n", filename);
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    xlog("WEBP: file size=%ld\n", file_size);

    // Limit file size to avoid huge allocations
    if (file_size > 500000) {  // 500KB max file size
        xlog("WEBP: file too large\n");
        fclose(fp);
        return 0;
    }

    uint8_t* file_data = (uint8_t*)malloc(file_size);
    if (!file_data) {
        xlog("WEBP: malloc failed\n");
        fclose(fp);
        return 0;
    }

    if (fread(file_data, 1, file_size, fp) != (size_t)file_size) {
        xlog("WEBP: fread failed\n");
        free(file_data);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    // Load WebP using simplewebp
    simplewebp* webp = NULL;
    simplewebp_error err = simplewebp_load_from_memory(file_data, file_size, NULL, &webp);
    /* v59: DO NOT free file_data here - simplewebp holds a pointer to it! */

    if (err != SIMPLEWEBP_NO_ERROR || !webp) {
        xlog("WEBP: load_from_memory failed err=%d\n", (int)err);
        free(file_data);  /* v59: Free only on early exit */
        return 0;
    }

    // Get dimensions BEFORE attempting decode
    size_t w, h;
    simplewebp_get_dimensions(webp, &w, &h);
    xlog("WEBP: dimensions %dx%d\n", (int)w, (int)h);

    // v42: Check if fits in universal buffer (256,000 pixels for RGB565)
    if (w == 0 || h == 0 || w * h > UNIVERSAL_MAX_PIXELS_RGB565) {
        xlog("WEBP: too large for buffer (%d > %d)\n", (int)(w * h), (int)UNIVERSAL_MAX_PIXELS_RGB565);
        simplewebp_unload(webp);
        free(file_data);  /* v59: Free only on early exit */
        return 0;
    }

    // Malloc temp RGBA buffer for decode (freed after conversion)
    size_t pixel_count = w * h;
    uint8_t* rgba_temp = (uint8_t*)malloc(pixel_count * 4);
    if (!rgba_temp) {
        xlog("WEBP: rgba malloc failed\n");
        simplewebp_unload(webp);
        free(file_data);  /* v59: Free only on early exit */
        return 0;
    }

    // Decode to RGBA
    xlog("WEBP: decoding...\n");
    err = simplewebp_decode(webp, rgba_temp, NULL);
    simplewebp_unload(webp);
    free(file_data);  /* v59: NOW safe to free - after decode and unload */

    if (err != SIMPLEWEBP_NO_ERROR) {
        xlog("WEBP: decode failed err=%d\n", (int)err);
        free(rgba_temp);
        return 0;
    }
    xlog("WEBP: decode OK\n");

    // Convert RGBA8888 to RGB565 using universal buffer
    xlog("WEBP: converting to RGB565...\n");
    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t r = rgba_temp[i * 4 + 0];
        uint8_t g = rgba_temp[i * 4 + 1];
        uint8_t b = rgba_temp[i * 4 + 2];
        universal_buffer_u16[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    free(rgba_temp);

    // Malloc output for caller
    *width = (int)w;
    *height = (int)h;
    xlog("WEBP: allocating output %d bytes\n", (int)(pixel_count * sizeof(uint16_t)));
    *data = (uint16_t*)malloc(pixel_count * sizeof(uint16_t));
    if (!*data) {
        xlog("WEBP: output malloc failed\n");
        return 0;
    }

    // Copy from universal buffer to malloc'd buffer
    memcpy(*data, universal_buffer_u16, pixel_count * sizeof(uint16_t));

    xlog("WEBP: done\n");
    return 1;
}

// v40: Helper to count trailing zeros (for mask shift calculation)
static int count_shift(uint32_t mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while ((mask & 1) == 0) { mask >>= 1; shift++; }
    return shift;
}

// v40: Helper to count bits in mask
static int count_bits(uint32_t mask) {
    int bits = 0;
    while (mask) { bits += (mask & 1); mask >>= 1; }
    return bits;
}

// v40: Load BMP file to RGB565 format (supports 1/4/8/16/24/32 bit)
int load_bmp_rgb565(const char* filename, uint16_t** data, int* width, int* height) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return 0;

    // Read BMP file header + DIB header (need up to 70 bytes for BITMAPV2INFOHEADER with masks)
    uint8_t header[70];
    if (fread(header, 1, 70, fp) < 54) {
        fclose(fp);
        return 0;
    }

    // Check BMP signature
    if (header[0] != 'B' || header[1] != 'M') {
        fclose(fp);
        return 0;
    }

    // Parse header fields
    uint32_t data_offset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    uint32_t dib_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
    int32_t img_width = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    int32_t img_height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    uint16_t bit_depth = header[28] | (header[29] << 8);
    uint32_t compression = header[30] | (header[31] << 8) | (header[32] << 16) | (header[33] << 24);

    // Handle negative height (top-down DIB)
    int top_down = 0;
    if (img_height < 0) {
        img_height = -img_height;
        top_down = 1;
    }

    // Validate dimensions
    if (img_width <= 0 || img_height <= 0 || img_width > 2048 || img_height > 2048) {
        fclose(fp);
        return 0;
    }

    // Only support uncompressed or BI_BITFIELDS
    if (compression != 0 && compression != 3) {
        fclose(fp);
        return 0;
    }

    // v40: Read color masks for BI_BITFIELDS (16/32-bit)
    uint32_t r_mask = 0, g_mask = 0, b_mask = 0;
    int r_shift = 0, g_shift = 0, b_shift = 0;
    int r_bits = 0, g_bits = 0, b_bits = 0;

    if (compression == 3 && (bit_depth == 16 || bit_depth == 32)) {
        // Masks are at offset 54 in BITMAPINFOHEADER (after 40-byte DIB header)
        r_mask = header[54] | (header[55] << 8) | (header[56] << 16) | (header[57] << 24);
        g_mask = header[58] | (header[59] << 8) | (header[60] << 16) | (header[61] << 24);
        b_mask = header[62] | (header[63] << 8) | (header[64] << 16) | (header[65] << 24);
        r_shift = count_shift(r_mask);
        g_shift = count_shift(g_mask);
        b_shift = count_shift(b_mask);
        r_bits = count_bits(r_mask);
        g_bits = count_bits(g_mask);
        b_bits = count_bits(b_mask);
    }

    // Read color palette for indexed formats
    uint8_t palette[1024] = {0};  // 256 colors * 4 bytes (BGRA)
    int palette_colors = 0;
    if (bit_depth <= 8) {
        palette_colors = 1 << bit_depth;
        fseek(fp, 14 + dib_size, SEEK_SET);  // Palette starts after DIB header
        fread(palette, 4, palette_colors, fp);
    }

    // Allocate output buffer
    *width = img_width;
    *height = img_height;
    *data = (uint16_t*)malloc(img_width * img_height * sizeof(uint16_t));
    if (!*data) {
        fclose(fp);
        return 0;
    }

    // Calculate row size (rows are 4-byte aligned)
    int bits_per_row = img_width * bit_depth;
    int row_size = ((bits_per_row + 31) / 32) * 4;
    uint8_t* row_buffer = (uint8_t*)malloc(row_size);
    if (!row_buffer) {
        free(*data);
        *data = NULL;
        fclose(fp);
        return 0;
    }

    // Seek to pixel data
    fseek(fp, data_offset, SEEK_SET);

    // Read and convert pixel data
    for (int y = 0; y < img_height; y++) {
        int dst_y = top_down ? y : (img_height - 1 - y);
        if (fread(row_buffer, 1, row_size, fp) != (size_t)row_size) break;

        for (int x = 0; x < img_width; x++) {
            uint8_t r, g, b;

            switch (bit_depth) {
                case 1: {
                    int byte_idx = x / 8;
                    int bit_idx = 7 - (x % 8);
                    int pal_idx = (row_buffer[byte_idx] >> bit_idx) & 1;
                    b = palette[pal_idx * 4 + 0];
                    g = palette[pal_idx * 4 + 1];
                    r = palette[pal_idx * 4 + 2];
                    break;
                }
                case 4: {
                    int byte_idx = x / 2;
                    int pal_idx = (x % 2 == 0) ? (row_buffer[byte_idx] >> 4) : (row_buffer[byte_idx] & 0x0F);
                    b = palette[pal_idx * 4 + 0];
                    g = palette[pal_idx * 4 + 1];
                    r = palette[pal_idx * 4 + 2];
                    break;
                }
                case 8: {
                    int pal_idx = row_buffer[x];
                    b = palette[pal_idx * 4 + 0];
                    g = palette[pal_idx * 4 + 1];
                    r = palette[pal_idx * 4 + 2];
                    break;
                }
                case 16: {
                    uint16_t pixel = row_buffer[x * 2] | (row_buffer[x * 2 + 1] << 8);
                    if (compression == 3 && r_mask) {
                        // BI_BITFIELDS - use masks (RGB565, RGB555, etc.)
                        int rv = (pixel & r_mask) >> r_shift;
                        int gv = (pixel & g_mask) >> g_shift;
                        int bv = (pixel & b_mask) >> b_shift;
                        // Scale to 8-bit
                        r = r_bits == 5 ? (rv << 3) | (rv >> 2) : (rv << (8 - r_bits));
                        g = g_bits == 6 ? (gv << 2) | (gv >> 4) : g_bits == 5 ? (gv << 3) | (gv >> 2) : (gv << (8 - g_bits));
                        b = b_bits == 5 ? (bv << 3) | (bv >> 2) : (bv << (8 - b_bits));
                    } else {
                        // Default: RGB555 (X1R5G5B5)
                        r = ((pixel >> 10) & 0x1F) << 3;
                        g = ((pixel >> 5) & 0x1F) << 3;
                        b = (pixel & 0x1F) << 3;
                    }
                    break;
                }
                case 24: {
                    b = row_buffer[x * 3 + 0];
                    g = row_buffer[x * 3 + 1];
                    r = row_buffer[x * 3 + 2];
                    break;
                }
                case 32: {
                    // 32-bit: BGRA, ignore alpha
                    b = row_buffer[x * 4 + 0];
                    g = row_buffer[x * 4 + 1];
                    r = row_buffer[x * 4 + 2];
                    break;
                }
                default:
                    r = g = b = 0;
                    break;
            }

            // Convert to RGB565
            (*data)[dst_y * img_width + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }

    free(row_buffer);
    fclose(fp);
    return 1;
}

// v40: Load GIF file to RGB565 format (first frame only for screenshots)
int load_gif_rgb565(const char* filename, uint16_t** data, int* width, int* height) {
    gd_GIF* gif = gd_open_gif(filename);
    if (!gif) return 0;

    // Get first frame
    int ret = gd_get_frame(gif);
    if (ret != 1) {
        gd_close_gif(gif);
        return 0;
    }

    // Allocate RGB888 buffer for rendering
    uint8_t* rgb_buffer = (uint8_t*)malloc(gif->width * gif->height * 3);
    if (!rgb_buffer) {
        gd_close_gif(gif);
        return 0;
    }

    // Render frame to RGB888
    gd_render_frame(gif, rgb_buffer);

    // Allocate RGB565 output buffer
    *width = gif->width;
    *height = gif->height;
    *data = (uint16_t*)malloc(gif->width * gif->height * sizeof(uint16_t));
    if (!*data) {
        free(rgb_buffer);
        gd_close_gif(gif);
        return 0;
    }

    // Convert RGB888 to RGB565
    for (int i = 0; i < gif->width * gif->height; i++) {
        uint8_t r = rgb_buffer[i * 3 + 0];
        uint8_t g = rgb_buffer[i * 3 + 1];
        uint8_t b = rgb_buffer[i * 3 + 2];
        (*data)[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    free(rgb_buffer);
    gd_close_gif(gif);
    return 1;
}

// ============================================================================
// v70: Memory-based image loaders for chunked loading
// ============================================================================

int load_png_rgb565_mem(const uint8_t* buffer, uint32_t size, uint16_t** data, int* width, int* height) {
    unsigned char* rgba_data = NULL;
    unsigned int w, h;

    // Decode PNG from memory
    unsigned error = lodepng_decode32(&rgba_data, &w, &h, buffer, size);
    if (error) return 0;

    // Allocate RGB565 buffer
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) {
        free(rgba_data);
        return 0;
    }

    // Convert RGBA -> RGB565
    for (unsigned int i = 0; i < w * h; i++) {
        unsigned char r = rgba_data[i * 4 + 0];
        unsigned char g = rgba_data[i * 4 + 1];
        unsigned char b = rgba_data[i * 4 + 2];
        (*data)[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    free(rgba_data);
    *width = (int)w;
    *height = (int)h;
    return 1;
}

int load_jpeg_rgb565_mem(const uint8_t* buffer, uint32_t size, uint16_t** data, int* width, int* height) {
    int w, h, channels;
    uint8_t* rgb_data = stbi_load_from_memory(buffer, size, &w, &h, &channels, 3);
    if (!rgb_data) return 0;

    *width = w;
    *height = h;
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) {
        stbi_image_free(rgb_data);
        return 0;
    }

    // Convert RGB888 to RGB565
    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgb_data[i * 3 + 0];
        uint8_t g = rgb_data[i * 3 + 1];
        uint8_t b = rgb_data[i * 3 + 2];
        (*data)[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    stbi_image_free(rgb_data);
    return 1;
}

int load_bmp_rgb565_mem(const uint8_t* buffer, uint32_t size, uint16_t** data, int* width, int* height) {
    // BMP from memory - parse header and pixel data directly
    if (size < 54) return 0;  // Minimum BMP header size

    // Check BMP signature
    if (buffer[0] != 'B' || buffer[1] != 'M') return 0;

    // Get header info
    uint32_t data_offset = *(uint32_t*)(buffer + 10);
    int32_t w = *(int32_t*)(buffer + 18);
    int32_t h = *(int32_t*)(buffer + 22);
    uint16_t bpp = *(uint16_t*)(buffer + 28);

    if (w <= 0 || h == 0) return 0;
    int flip = (h > 0);  // BMP is bottom-up if h > 0
    if (h < 0) h = -h;

    // Only support 24-bit BMP for simplicity in memory version
    if (bpp != 24) return 0;

    if (data_offset + w * h * 3 > size) return 0;

    *width = w;
    *height = h;
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) return 0;

    int row_size = ((w * 3 + 3) / 4) * 4;  // Row padding
    for (int y = 0; y < h; y++) {
        int src_y = flip ? (h - 1 - y) : y;
        const uint8_t* row = buffer + data_offset + src_y * row_size;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            (*data)[y * w + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
    return 1;
}

int load_gif_rgb565_mem(const uint8_t* buffer, uint32_t size, uint16_t** data, int* width, int* height) {
    // GIF memory loading not supported - gifdec only has file API
    // Fallback: write temp file and use file loader
    (void)buffer; (void)size; (void)data; (void)width; (void)height;
    return 0;  // Not implemented - will fall back to file-based loading
}

int load_webp_rgb565_mem(const uint8_t* buffer, uint32_t size, uint16_t** data, int* width, int* height) {
    // NOTE: simplewebp holds a pointer to buffer, so caller must NOT free it
    // until after this function returns. The chunked loader handles this.
    simplewebp* webp = NULL;
    simplewebp_error err = simplewebp_load_from_memory((void*)buffer, size, NULL, &webp);
    if (err != SIMPLEWEBP_NO_ERROR || !webp) return 0;

    size_t w, h;
    simplewebp_get_dimensions(webp, &w, &h);
    if (w == 0 || h == 0) {
        simplewebp_unload(webp);
        return 0;
    }

    // Temp RGBA buffer
    uint8_t* rgba_temp = (uint8_t*)malloc(w * h * 4);
    if (!rgba_temp) {
        simplewebp_unload(webp);
        return 0;
    }

    err = simplewebp_decode(webp, rgba_temp, NULL);
    simplewebp_unload(webp);

    if (err != SIMPLEWEBP_NO_ERROR) {
        free(rgba_temp);
        return 0;
    }

    // Allocate RGB565 output
    *width = (int)w;
    *height = (int)h;
    *data = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    if (!*data) {
        free(rgba_temp);
        return 0;
    }

    // Convert RGBA -> RGB565
    for (size_t i = 0; i < w * h; i++) {
        uint8_t r = rgba_temp[i * 4 + 0];
        uint8_t g = rgba_temp[i * 4 + 1];
        uint8_t b = rgba_temp[i * 4 + 2];
        (*data)[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    free(rgba_temp);
    return 1;
}

// External check if settings menu is open
extern int settings_is_active(void);

static uint16_t *current_active_bg = NULL;
static uint16_t *previous_active_bg = NULL;
static int bg_fade_alpha = 255;  // 0 to 255 (255 = fully current_active_bg)

static int sine_lut[256];
static bool sine_lut_initialized = false;

static void init_sine_lut() {
    for (int i = 0; i < 256; i++) {
        sine_lut[i] = (int)(sinf(i * 3.14159265f * 2.0f / 256.0f) * 256.0f);
    }
    sine_lut_initialized = true;
}

// Fixed-point sine (0-255 angle, returns value * 256)
static int fast_sin(int angle) {
    if (!sine_lut_initialized) init_sine_lut();
    return sine_lut[angle & 255];
}

static float xmb_wave_time = 0.0f;

void render_xmb_waves(uint16_t *framebuffer) {
    bool show_waves = false;
    const char *opt = settings_get_value("frogui_xmb_waves");
    if (opt && strcmp(opt, "theme_default") != 0) {
        if (strcmp(opt, "true") == 0) show_waves = true;
    } else {
        if (gfx_theme_has_custom_xmb_waves()) {
            show_waves = gfx_theme_get_xmb_waves();
        }
    }
    if (!show_waves) return;

    xmb_wave_time += 1.0f;

    uint32_t wave_hex = 0xFFFFFF;
    const char *color_opt = settings_get_value("frogui_xmb_wave_color");
    if (color_opt && strcmp(color_opt, "theme_default") != 0) {
        if (strcmp(color_opt, "white") == 0) wave_hex = 0xFFFFFF;
        else if (strcmp(color_opt, "black") == 0) wave_hex = 0x000000;
        else if (strcmp(color_opt, "red") == 0) wave_hex = 0xFF0000;
        else if (strcmp(color_opt, "green") == 0) wave_hex = 0x00FF00;
        else if (strcmp(color_opt, "blue") == 0) wave_hex = 0x0000FF;
        else if (strcmp(color_opt, "yellow") == 0) wave_hex = 0xFFFF00;
        else if (strcmp(color_opt, "cyan") == 0) wave_hex = 0x00FFFF;
        else if (strcmp(color_opt, "magenta") == 0) wave_hex = 0xFF00FF;
        else if (strcmp(color_opt, "gray") == 0) wave_hex = 0x808080;
        else if (strcmp(color_opt, "orange") == 0) wave_hex = 0xFFA500;
        else if (strcmp(color_opt, "purple") == 0) wave_hex = 0x800080;
    } else {
        if (gfx_theme_has_custom_xmb_wave_color()) {
            wave_hex = gfx_theme_get_xmb_wave_color();
        }
    }
    int wave_color_r = (wave_hex >> 16) & 0xFF;
    int wave_color_g = (wave_hex >> 8) & 0xFF;
    int wave_color_b = wave_hex & 0xFF;

    int glow = 2; // medium
    const char *glow_opt = settings_get_value("frogui_xmb_wave_glow");
    if (glow_opt && strcmp(glow_opt, "theme_default") != 0) {
        if (strcmp(glow_opt, "low") == 0) glow = 1;
        else if (strcmp(glow_opt, "medium") == 0) glow = 2;
        else if (strcmp(glow_opt, "high") == 0) glow = 3;
    } else {
        if (gfx_theme_has_custom_xmb_wave_glow()) {
            glow = gfx_theme_get_xmb_wave_glow();
        }
    }

    int variety = 3; // complex
    const char *var_opt = settings_get_value("frogui_xmb_wave_variety");
    if (var_opt && strcmp(var_opt, "theme_default") != 0) {
        if (strcmp(var_opt, "single") == 0) variety = 1;
        else if (strcmp(var_opt, "dual") == 0) variety = 2;
        else if (strcmp(var_opt, "complex") == 0) variety = 3;
    } else {
        if (gfx_theme_has_custom_xmb_wave_variety()) {
            variety = gfx_theme_get_xmb_wave_variety();
        }
    }

    int base_alpha = (glow == 1) ? 25 : (glow == 2) ? 50 : 85; 
    int glow_dist = (glow == 1) ? 8 : (glow == 2) ? 12 : 18;

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        int y1 = (SCREEN_HEIGHT / 2) + (fast_sin((int)(x * 0.8f + xmb_wave_time * 2.0f)) * 40) / 256;
        int y2 = (SCREEN_HEIGHT / 2) + (fast_sin((int)(x * 0.5f - xmb_wave_time * 1.5f)) * 60) / 256;
        int y3 = (SCREEN_HEIGHT / 2) + (fast_sin((int)(x * 1.2f + xmb_wave_time * 3.0f)) * 25) / 256;

        int min_y = y1;
        int max_y = y1;

        if (variety >= 2) {
            if (y2 < min_y) min_y = y2;
            if (y2 > max_y) max_y = y2;
        }
        if (variety >= 3) {
            if (y3 < min_y) min_y = y3;
            if (y3 > max_y) max_y = y3;
        }
        
        min_y -= (glow_dist - 2);
        max_y += (glow_dist - 2);

        for (int y = min_y; y <= max_y; y++) {
            if (y >= 0 && y < SCREEN_HEIGHT) {
                int dist1 = abs(y - y1);
                int min_dist = dist1;

                if (variety >= 2) {
                    int dist2 = abs(y - y2);
                    if (dist2 < min_dist) min_dist = dist2;
                }
                if (variety >= 3) {
                    int dist3 = abs(y - y3);
                    if (dist3 < min_dist) min_dist = dist3;
                }

                if (min_dist < glow_dist) {
                    int alpha = base_alpha - (min_dist * base_alpha / glow_dist); 
                    if (alpha > 0) {
                        uint16_t bg = framebuffer[y * SCREEN_WIDTH + x];
                        int bg_r = (bg >> 11) << 3;
                        int bg_g = ((bg >> 5) & 0x3F) << 2;
                        int bg_b = (bg & 0x1F) << 3;

                        int r = bg_r + (((wave_color_r - bg_r) * alpha) >> 8);
                        int g = bg_g + (((wave_color_g - bg_g) * alpha) >> 8);
                        int b = bg_b + (((wave_color_b - bg_b) * alpha) >> 8);
                        
                        framebuffer[y * SCREEN_WIDTH + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    }
                }
            }
        }
    }
}

// Clear screen with GFX theme background if active (supports smooth cross-fade transition)
void render_clear_screen_gfx(uint16_t *framebuffer) {
    if (!framebuffer) return;

    // Disable background image in settings menu
    if (settings_is_active()) {
        render_clear_screen(framebuffer);
        return;
    }

    // Check if GFX theme is active and has background (platform-aware)
    uint16_t* bg = gfx_theme_get_platform_background();
    if (bg) {
        // Detect background switch and trigger cross-fade transition
        if (bg != current_active_bg) {
            if (current_active_bg != NULL) {
                previous_active_bg = current_active_bg;
                bg_fade_alpha = 0;  // Start transition
            }
            current_active_bg = bg;
        }

        if (bg_fade_alpha < 255 && previous_active_bg != NULL) {
            const char *mode_val = settings_get_value("frogui_bg_anim_mode");
            if (mode_val && strcmp(mode_val, "theme_default") == 0) {
                if (gfx_theme_has_custom_bg_anim_mode()) {
                    mode_val = gfx_theme_get_bg_anim_mode();
                } else {
                    mode_val = "fade"; // fallback
                }
            }

            int anim_mode = 0; // 0 = fade, 1 = scroll_h, 2 = scroll_v, 3 = none
            if (mode_val) {
                if (strcmp(mode_val, "scroll_h") == 0) anim_mode = 1;
                else if (strcmp(mode_val, "scroll_v") == 0) anim_mode = 2;
                else if (strcmp(mode_val, "none") == 0) anim_mode = 3;
            }

            if (anim_mode == 3) {
                // none: instant transition
                bg_fade_alpha = 255;
            } else {
                // Compute fade step from frogui_anim_speed setting
                int fade_step = 10;
                const char *speed_val = settings_get_value("frogui_anim_speed");
                if (speed_val) {
                    if (strcmp(speed_val, "instant") == 0) fade_step = 256;
                    else if (strcmp(speed_val, "very_fast") == 0) fade_step = 64;
                    else if (strcmp(speed_val, "fast") == 0) fade_step = 32;
                    else if (strcmp(speed_val, "normal") == 0) fade_step = 10;
                    else if (strcmp(speed_val, "slow") == 0) fade_step = 5;
                    else if (strcmp(speed_val, "very_slow") == 0) fade_step = 2;
                    else if (strcmp(speed_val, "snail") == 0) fade_step = 1;
                    else if (strcmp(speed_val, "theme_default") == 0 && gfx_theme_has_custom_anim_speed()) {
                        float ts = gfx_theme_get_anim_speed();
                        fade_step = (int)(ts * 256.0f);
                        if (fade_step < 1) fade_step = 1;
                        if (fade_step > 256) fade_step = 256;
                    }
                }
                bg_fade_alpha += fade_step;
                if (bg_fade_alpha >= 255) bg_fade_alpha = 255;
            }

            if (bg_fade_alpha >= 255) {
                memcpy(framebuffer, current_active_bg, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
            } else if (anim_mode == 1) {
                // scroll_h
                extern int gfx_theme_get_bg_anim_direction(void);
                int dir = gfx_theme_get_bg_anim_direction();
                int offset_x = (bg_fade_alpha * SCREEN_WIDTH) / 255;
                if (dir == 1) {
                    // Slide current from right, previous to left
                    for (int y = 0; y < SCREEN_HEIGHT; y++) {
                        int row_start = y * SCREEN_WIDTH;
                        memcpy(&framebuffer[row_start], &previous_active_bg[row_start + offset_x], (SCREEN_WIDTH - offset_x) * sizeof(uint16_t));
                        memcpy(&framebuffer[row_start + (SCREEN_WIDTH - offset_x)], &current_active_bg[row_start], offset_x * sizeof(uint16_t));
                    }
                } else {
                    // Slide current from left, previous to right
                    for (int y = 0; y < SCREEN_HEIGHT; y++) {
                        int row_start = y * SCREEN_WIDTH;
                        memcpy(&framebuffer[row_start], &current_active_bg[row_start + (SCREEN_WIDTH - offset_x)], offset_x * sizeof(uint16_t));
                        memcpy(&framebuffer[row_start + offset_x], &previous_active_bg[row_start], (SCREEN_WIDTH - offset_x) * sizeof(uint16_t));
                    }
                }
            } else if (anim_mode == 2) {
                // scroll_v
                extern int gfx_theme_get_bg_anim_direction(void);
                int dir = gfx_theme_get_bg_anim_direction();
                int offset_y = (bg_fade_alpha * SCREEN_HEIGHT) / 255;
                int remaining_y = SCREEN_HEIGHT - offset_y;
                if (dir == 1) {
                    // Slide current from bottom, previous to top
                    if (remaining_y > 0) {
                        memcpy(framebuffer, &previous_active_bg[offset_y * SCREEN_WIDTH], remaining_y * SCREEN_WIDTH * sizeof(uint16_t));
                    }
                    if (offset_y > 0) {
                        memcpy(&framebuffer[remaining_y * SCREEN_WIDTH], current_active_bg, offset_y * SCREEN_WIDTH * sizeof(uint16_t));
                    }
                } else {
                    // Slide current from top, previous to bottom
                    if (offset_y > 0) {
                        memcpy(framebuffer, &current_active_bg[remaining_y * SCREEN_WIDTH], offset_y * SCREEN_WIDTH * sizeof(uint16_t));
                    }
                    if (remaining_y > 0) {
                        memcpy(&framebuffer[offset_y * SCREEN_WIDTH], previous_active_bg, remaining_y * SCREEN_WIDTH * sizeof(uint16_t));
                    }
                }
            } else {
                // fade: crossfade
                int a = bg_fade_alpha;
                int inv_a = 255 - a;
                for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                    uint16_t c1 = previous_active_bg[i];
                    uint16_t c2 = current_active_bg[i];
                    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
                    int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
                    int r = (r1 * inv_a + r2 * a) >> 8;
                    int g = (g1 * inv_a + g2 * a) >> 8;
                    int b = (b1 * inv_a + b2 * a) >> 8;
                    framebuffer[i] = (r << 11) | (g << 5) | b;
                }
            }
        } else {
            // Transition complete: fast direct copy
            memcpy(framebuffer, current_active_bg, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
        }
        
        render_xmb_waves(framebuffer);
        return;
    } else {
        current_active_bg = NULL;
        previous_active_bg = NULL;
        bg_fade_alpha = 255;
        // Fall back to solid color
        render_clear_screen(framebuffer);
    }
}

// Get current visible items count (from gfx_theme if active, otherwise default)
int render_get_visible_items(void) {
    const char *grid_setting = in_platform_menu ? settings_get_value("frogui_menu_layout") : settings_get_value("frogui_game_list_layout");
    if (!grid_setting || strcmp(grid_setting, "theme_default") == 0) {
        grid_setting = in_platform_menu ? gfx_theme_get_menu_layout() : gfx_theme_get_game_list_layout();
    }
    if (grid_setting && (strcmp(grid_setting, "grid") == 0 || strcmp(grid_setting, "2_columns") == 0 || strcmp(grid_setting, "3_columns") == 0 || strcmp(grid_setting, "grid_2_columns") == 0 || strcmp(grid_setting, "grid_3_columns") == 0 || strcmp(grid_setting, "grid_2") == 0 || strcmp(grid_setting, "grid_3") == 0)) {
        int custom_rows = gfx_theme_get_grid_visible_rows();
        if (custom_rows > 0) return custom_rows;
        return 2;
    }
    if (gfx_theme_is_active()) {
        const GfxThemeLayout* layout = gfx_theme_get_layout();
        if (layout) {
            if (in_platform_menu) {
                return layout->platform_visible_items > 0 ? layout->platform_visible_items : VISIBLE_ENTRIES;
            } else {
                return layout->game_visible_items > 0 ? layout->game_visible_items : VISIBLE_ENTRIES;
            }
        }
    }
    return VISIBLE_ENTRIES;
}

// v56: Access universal buffer for image viewer
uint8_t* render_get_universal_buffer(void) {
    return universal_buffer;
}

size_t render_get_universal_buffer_size(void) {
    return UNIVERSAL_BUFFER_BYTES;
}
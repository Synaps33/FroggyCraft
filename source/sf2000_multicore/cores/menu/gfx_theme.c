#include "gfx_theme.h"
#include "render.h"
#include "avi_bg.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

#ifndef ROMS_PATH
#define ROMS_PATH "/mnt/sda1/ROMS"
#endif

// v37: Debug logging
extern void xlog(const char *fmt, ...);

// External from render.c for PNG loading (using lodepng)
extern int load_png_rgb565(const char* filename, uint16_t** data, int* width, int* height);

// v19: Load PNG with alpha channel (for transparent overlays)
// Returns: 1 on success, 0 on failure
// pixels: RGB565 pixel data (caller must free)
// alpha: 8-bit alpha data (caller must free)
extern int load_png_rgba565(const char* filename, uint16_t** pixels, uint8_t** alpha, int* width, int* height);

void gfx_theme_free_logo_cache(void);

// Animated background state
static bool main_bg_is_animated = false;
static char main_bg_avi_path[MAX_THEME_PATH_LEN] = "";

// v19: PNG overlay on animation
static int bg_anim_direction = 1; // 1 = Right/Down, -1 = Left/Up
static uint16_t* main_bg_overlay_pixels = NULL;
static uint8_t* main_bg_overlay_alpha = NULL;
static bool main_bg_has_overlay = false;

// v28: Pre-computed overlay blend factors (avoid per-pixel alpha math at runtime)
// For each pixel: if alpha > 250, use overlay directly; if alpha < 5, use bg directly
// Otherwise store pre-multiplied values: overlay_premult = overlay * alpha / 255
static uint16_t* overlay_premult_rgb = NULL;  // Pre-multiplied overlay RGB
static uint8_t* overlay_blend_mode = NULL;    // 0=transparent, 1=blend, 2=opaque

// v62: Global sections overlay (resources/sections/background_anim.png)
// Used for all platforms when no platform-specific background exists
static uint16_t* sections_overlay_pixels = NULL;
static uint8_t* sections_overlay_alpha = NULL;
static uint8_t* sections_overlay_blend_mode = NULL;
static bool sections_has_overlay = false;

// v62: Dither matrix for 16-bit blending (4x4 Bayer)
static const int8_t dither_matrix[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

// v19: Composite buffer for applying overlay to animation frames
static uint16_t* composite_buffer = NULL;

// v28: Fast overlay application using pre-computed blend data
static void apply_overlay_to_frame_fast(uint16_t* dst, const uint16_t* src) {
    if (!main_bg_has_overlay || !overlay_blend_mode) {
        // No overlay - just copy
        memcpy(dst, src, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
        return;
    }

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint8_t mode = overlay_blend_mode[i];
        if (mode == 0) {
            // Transparent - use source directly
            dst[i] = src[i];
        } else if (mode == 2) {
            // Opaque - use overlay directly
            dst[i] = main_bg_overlay_pixels[i];
        } else {
            // Blend mode - use pre-computed alpha
            uint8_t alpha = main_bg_overlay_alpha[i];
            uint16_t fg = main_bg_overlay_pixels[i];
            uint16_t bg = src[i];

            int fg_r = (fg >> 11) & 0x1F;
            int fg_g = (fg >> 5) & 0x3F;
            int fg_b = fg & 0x1F;

            int bg_r = (bg >> 11) & 0x1F;
            int bg_g = (bg >> 5) & 0x3F;
            int bg_b = bg & 0x1F;

            int a = alpha + 1;
            int inv_a = 257 - a;

            int r = (fg_r * a + bg_r * inv_a) >> 8;
            int g = (fg_g * a + bg_g * inv_a) >> 8;
            int b = (fg_b * a + bg_b * inv_a) >> 8;

            dst[i] = (r << 11) | (g << 5) | b;
        }
    }
}

// v28: Pre-compute overlay blend modes (called once at load time)
// v37: Removed 95% transparency check that was incorrectly disabling overlays
static void precompute_overlay_blend(void) {
    if (!main_bg_overlay_alpha || !main_bg_overlay_pixels) return;

    // Allocate blend mode buffer
    if (overlay_blend_mode) { free(overlay_blend_mode); }
    overlay_blend_mode = (uint8_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!overlay_blend_mode) return;

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint8_t alpha = main_bg_overlay_alpha[i];
        if (alpha < 5) {
            overlay_blend_mode[i] = 0;  // Transparent
        } else if (alpha > 250) {
            overlay_blend_mode[i] = 2;  // Opaque
        } else {
            overlay_blend_mode[i] = 1;  // Needs blending
        }
    }
    // v37: Always keep overlay enabled if it was loaded successfully
    // (removed the 95% transparency check that was causing issues)
}

// v62: Pre-compute sections overlay blend modes
static void precompute_sections_overlay_blend(void) {
    if (!sections_overlay_alpha || !sections_overlay_pixels) return;

    if (sections_overlay_blend_mode) { free(sections_overlay_blend_mode); }
    sections_overlay_blend_mode = (uint8_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!sections_overlay_blend_mode) return;

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint8_t alpha = sections_overlay_alpha[i];
        if (alpha < 5) {
            sections_overlay_blend_mode[i] = 0;
        } else if (alpha > 250) {
            sections_overlay_blend_mode[i] = 2;
        } else {
            sections_overlay_blend_mode[i] = 1;
        }
    }
}

// Available GFX themes (index 0 is always "None")
static GfxTheme gfx_themes[MAX_GFX_THEMES];
static int num_gfx_themes = 0;
static int current_gfx_theme = 0;  // 0 = None/Disabled
static char current_platform[MAX_PLATFORM_NAME_LEN] = "";  // Current platform (e.g., "nes", "gba")

// Default layout
static const GfxThemeLayout default_layout = {
    // Platform list (main menu)
    .platform_list_x = DEFAULT_PLATFORM_LIST_X,
    .platform_list_y_start = DEFAULT_PLATFORM_LIST_Y_START,
    .platform_list_y_end = DEFAULT_PLATFORM_LIST_Y_END,
    .platform_item_height = DEFAULT_PLATFORM_ITEM_HEIGHT,
    .platform_visible_items = DEFAULT_PLATFORM_VISIBLE_ITEMS,
    // Game list (inside folders)
    .game_list_x = DEFAULT_GAME_LIST_X,
    .game_list_y_start = DEFAULT_GAME_LIST_Y_START,
    .game_list_y_end = DEFAULT_GAME_LIST_Y_END,
    .game_item_height = DEFAULT_GAME_ITEM_HEIGHT,
    .game_visible_items = DEFAULT_GAME_VISIBLE_ITEMS,
    // Thumbnail
    .thumb_x = DEFAULT_THUMB_X,
    .thumb_y = DEFAULT_THUMB_Y,
    .thumb_width = DEFAULT_THUMB_WIDTH,
    .thumb_height = DEFAULT_THUMB_HEIGHT,
    // Header
    .header_x = DEFAULT_HEADER_X,
    .header_y = DEFAULT_HEADER_Y,
    // Legend
    .legend_x = DEFAULT_LEGEND_X,
    .legend_y = DEFAULT_LEGEND_Y,
    // Counter
    .counter_x = DEFAULT_COUNTER_X,
    .counter_y = DEFAULT_COUNTER_Y
};

// Helper: trim whitespace from string
static char* trim(char* str) {
    if (!str) return str;

    // Trim leading whitespace
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

// Helper: parse hex color like "FFFFFF" or "#FFFFFF"
static uint16_t parse_hex_color(const char* str) {
    if (!str || !*str) return 0xFFFF;  // Invalid marker

    // Skip # if present
    if (*str == '#') str++;

    unsigned int r, g, b;
    if (sscanf(str, "%2x%2x%2x", &r, &g, &b) == 3) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    return 0xFFFF;  // Invalid
}

// Helper: parse integer with default
static int parse_int(const char* str, int def) {
    if (!str || !*str) return def;
    return atoi(str);
}

// Helper: parse boolean value from string (true, yes, on, 1)
static bool parse_bool_value(const char* val) {
    if (!val || !*val) return false;
    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "enabled") == 0 ||
        atoi(val) != 0) {
        return true;
    }
    return false;
}

// Parse theme.ini file
static int parse_theme_ini(const char* ini_path, GfxTheme* theme) {
    FILE* f = fopen(ini_path, "r");
    if (!f) return 0;

    char line[512];
    char section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        char* trimmed = trim(line);

        // Skip empty lines and comments
        if (!*trimmed || *trimmed == ';' || *trimmed == '#') continue;

        // Section header
        if (*trimmed == '[') {
            char* end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(section, trimmed + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            continue;
        }

        // Key=value pair
        char* eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trim(trimmed);
        char* value = eq + 1;

        // Strip inline comments starting with ';' or '#'
        char* c1 = strchr(value, ';');
        if (c1) *c1 = '\0';
        char* c2 = strchr(value, '#');
        if (c2) *c2 = '\0';
        value = trim(value);

        // Normalize carousel layout alias
        if (strcasecmp(value, "carousel") == 0) {
            value = "horizontal";
        }

        // Global options parsed regardless of section header
        if (strcasecmp(key, "hide_system_names") == 0 || strcasecmp(key, "hide_platform_names") == 0 || strcasecmp(key, "hide_system_name") == 0 || strcasecmp(key, "hide_system_labels") == 0) {
            theme->hide_system_names = parse_bool_value(value);
            theme->has_custom_hide_system_names = true;
            continue;
        } else if (strcasecmp(key, "hide_header_text") == 0 || strcasecmp(key, "hide_header_names") == 0 || strcasecmp(key, "hide_header_name") == 0) {
            theme->hide_header_text = parse_bool_value(value);
            theme->has_custom_hide_header_text = true;
            continue;
        } else if (strcasecmp(key, "show_system_names") == 0 || strcasecmp(key, "show_platform_names") == 0 || strcasecmp(key, "show_system_labels") == 0) {
            theme->hide_system_names = !parse_bool_value(value);
            theme->has_custom_hide_system_names = true;
            continue;
        } else if (strcasecmp(key, "show_header_text") == 0 || strcasecmp(key, "show_header_names") == 0 || strcasecmp(key, "show_header_name") == 0) {
            theme->hide_header_text = !parse_bool_value(value);
            theme->has_custom_hide_header_text = true;
            continue;
        } else if (strcasecmp(key, "hide_game_names") == 0 || strcasecmp(key, "hide_game_labels") == 0 || strcasecmp(key, "hide_game_name") == 0) {
            theme->hide_game_names = parse_bool_value(value);
            theme->has_custom_hide_game_names = true;
            continue;
        } else if (strcasecmp(key, "game_name_color") == 0 || strcasecmp(key, "game_label_color") == 0) {
            theme->game_name_color = parse_hex_color(value);
            theme->has_custom_game_name_color = true;
            continue;
        } else if (strcasecmp(key, "xmb_waves") == 0 || strcasecmp(key, "show_xmb_waves") == 0) {
            theme->xmb_waves = parse_bool_value(value);
            theme->has_custom_xmb_waves = true;
            continue;
        } else if (strcasecmp(key, "xmb_wave_color") == 0) {
            theme->xmb_wave_color = parse_hex_color(value);
            theme->has_custom_xmb_wave_color = true;
            continue;
        } else if (strcasecmp(key, "xmb_wave_glow") == 0) {
            if (strcasecmp(value, "low") == 0 || strcmp(value, "1") == 0) theme->xmb_wave_glow = 1;
            else if (strcasecmp(value, "medium") == 0 || strcmp(value, "2") == 0) theme->xmb_wave_glow = 2;
            else if (strcasecmp(value, "high") == 0 || strcmp(value, "3") == 0) theme->xmb_wave_glow = 3;
            else theme->xmb_wave_glow = 2; // Default to medium
            theme->has_custom_xmb_wave_glow = true;
            continue;
        } else if (strcasecmp(key, "xmb_wave_variety") == 0) {
            if (strcasecmp(value, "single") == 0 || strcmp(value, "1") == 0) theme->xmb_wave_variety = 1;
            else if (strcasecmp(value, "dual") == 0 || strcmp(value, "2") == 0) theme->xmb_wave_variety = 2;
            else if (strcasecmp(value, "complex") == 0 || strcmp(value, "3") == 0) theme->xmb_wave_variety = 3;
            else theme->xmb_wave_variety = 3; // Default to complex
            theme->has_custom_xmb_wave_variety = true;
            continue;
        } else if (strcasecmp(key, "show_game_names") == 0 || strcasecmp(key, "show_game_labels") == 0 || strcasecmp(key, "show_game_name") == 0) {
            theme->hide_game_names = !parse_bool_value(value);
            theme->has_custom_hide_game_names = true;
            continue;
        } else if (strcasecmp(key, "show_icons") == 0 || strcasecmp(key, "show_logos") == 0 || strcasecmp(key, "icons") == 0 ||
                   strcasecmp(key, "show_system_icons") == 0 || strcasecmp(key, "show_platform_icons") == 0 ||
                   strcasecmp(key, "show_system_logos") == 0 || strcasecmp(key, "show_platform_logos") == 0 ||
                   strcasecmp(key, "system_icons") == 0 || strcasecmp(key, "platform_icons") == 0) {
            theme->show_icons = parse_bool_value(value);
            theme->has_custom_show_icons = true;
            continue;
        } else if (strcasecmp(key, "hide_icons") == 0 || strcasecmp(key, "hide_logos") == 0 || strcasecmp(key, "disable_icons") == 0 ||
                   strcasecmp(key, "hide_system_icons") == 0 || strcasecmp(key, "hide_platform_icons") == 0) {
            theme->show_icons = !parse_bool_value(value);
            theme->has_custom_show_icons = true;
            continue;
        } else if (strcasecmp(key, "show_game_icons") == 0 || strcasecmp(key, "show_game_logos") == 0 || strcasecmp(key, "game_icons") == 0) {
            theme->show_game_icons = parse_bool_value(value);
            theme->has_custom_show_game_icons = true;
            continue;
        } else if (strcasecmp(key, "menu_layout") == 0 || strcasecmp(key, "layout_mode") == 0) {
            strncpy(theme->menu_layout, value, sizeof(theme->menu_layout) - 1);
            theme->menu_layout[sizeof(theme->menu_layout) - 1] = '\0';
            theme->has_custom_menu_layout = true;
            continue;
        } else if (strcasecmp(key, "game_list_layout") == 0 || strcasecmp(key, "gamelist_layout") == 0) {
            strncpy(theme->game_list_layout, value, sizeof(theme->game_list_layout) - 1);
            theme->game_list_layout[sizeof(theme->game_list_layout) - 1] = '\0';
            theme->has_custom_game_list_layout = true;
            continue;
        } else if (strcasecmp(key, "horizontal_y") == 0 || strcasecmp(key, "carousel_y") == 0 || strcasecmp(key, "horizontal_menu_y") == 0) {
            theme->horizontal_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "logo_size") == 0 || strcasecmp(key, "tile_size") == 0 || strcasecmp(key, "horizontal_tile_size") == 0) {
            int s = atoi(value);
            if (s > 0) {
                theme->horizontal_tile_w = s;
                theme->horizontal_tile_h = s;
            }
            continue;
        } else if (strcasecmp(key, "logo_width") == 0 || strcasecmp(key, "tile_width") == 0 || strcasecmp(key, "horizontal_tile_w") == 0) {
            theme->horizontal_tile_w = atoi(value);
            continue;
        } else if (strcasecmp(key, "logo_height") == 0 || strcasecmp(key, "tile_height") == 0 || strcasecmp(key, "horizontal_tile_h") == 0) {
            theme->horizontal_tile_h = atoi(value);
            continue;
        } else if (strcasecmp(key, "item_spacing") == 0 || strcasecmp(key, "carousel_spacing") == 0 || strcasecmp(key, "horizontal_item_spacing") == 0) {
            theme->horizontal_item_spacing = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_cols") == 0 || strcasecmp(key, "grid_columns") == 0 || strcasecmp(key, "columns") == 0) {
            theme->grid_cols = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_tile_size") == 0 || strcasecmp(key, "grid_logo_size") == 0) {
            int s = atoi(value);
            if (s > 0) {
                theme->grid_tile_w = s;
                theme->grid_tile_h = s;
            }
            continue;
        } else if (strcasecmp(key, "grid_tile_w") == 0 || strcasecmp(key, "grid_tile_width") == 0 || strcasecmp(key, "grid_logo_w") == 0) {
            theme->grid_tile_w = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_tile_h") == 0 || strcasecmp(key, "grid_tile_height") == 0 || strcasecmp(key, "grid_logo_h") == 0) {
            theme->grid_tile_h = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_spacing_x") == 0 || strcasecmp(key, "grid_spacing") == 0 || strcasecmp(key, "grid_item_spacing") == 0 || strcasecmp(key, "grid_item_spacing_x") == 0) {
            theme->grid_spacing_x = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_spacing_y") == 0 || strcasecmp(key, "grid_row_spacing") == 0 || strcasecmp(key, "grid_item_spacing_y") == 0) {
            theme->grid_spacing_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_bottom_spacing") == 0 || strcasecmp(key, "bottom_spacing") == 0 || strcasecmp(key, "icon_spacing_bottom") == 0 || strcasecmp(key, "platform_icon_spacing") == 0 || strcasecmp(key, "bottom_icon_spacing") == 0) {
            theme->grid_bottom_spacing = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_x") == 0 || strcasecmp(key, "grid_start_x") == 0) {
            theme->grid_x = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_y") == 0 || strcasecmp(key, "grid_start_y") == 0) {
            theme->grid_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "grid_visible_rows") == 0 || strcasecmp(key, "grid_rows") == 0) {
            theme->grid_visible_rows = atoi(value);
            continue;
        } else if (strcasecmp(key, "platform_label_y") == 0 || strcasecmp(key, "system_label_y") == 0 || strcasecmp(key, "menu_label_y") == 0 || strcasecmp(key, "system_y") == 0 || strcasecmp(key, "platform_y") == 0 || strcasecmp(key, "label_y") == 0) {
            theme->platform_label_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "platform_label_offset_y") == 0 || strcasecmp(key, "system_label_offset_y") == 0 || strcasecmp(key, "menu_label_offset_y") == 0 || strcasecmp(key, "platform_label_y_offset") == 0 || strcasecmp(key, "system_label_y_offset") == 0) {
            theme->platform_label_offset_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "platform_label_offset_x") == 0 || strcasecmp(key, "system_label_offset_x") == 0 || strcasecmp(key, "menu_label_offset_x") == 0 || strcasecmp(key, "platform_label_x_offset") == 0 || strcasecmp(key, "system_label_x_offset") == 0) {
            theme->platform_label_offset_x = atoi(value);
            continue;
        } else if (strcasecmp(key, "game_label_y") == 0 || strcasecmp(key, "gamelist_label_y") == 0 || strcasecmp(key, "game_y") == 0 || strcasecmp(key, "gamelist_y") == 0) {
            theme->game_label_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "game_label_offset_y") == 0 || strcasecmp(key, "game_label_y_offset") == 0 || strcasecmp(key, "gamelist_label_offset_y") == 0) {
            theme->game_label_offset_y = atoi(value);
            continue;
        } else if (strcasecmp(key, "game_label_offset_x") == 0 || strcasecmp(key, "game_label_x_offset") == 0 || strcasecmp(key, "gamelist_label_offset_x") == 0) {
            theme->game_label_offset_x = atoi(value);
            continue;
        } else if (strcasecmp(key, "text_in_empty_icon") == 0) {
            theme->text_in_empty_icon = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            theme->has_custom_text_in_empty_icon = true;
        } else if (strcasecmp(key, "show_empty_icon_bg") == 0) {
            theme->show_empty_icon_bg = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            theme->has_custom_show_empty_icon_bg = true;
        } else if (strcasecmp(key, "show_selected_icon_bg") == 0) {
            theme->show_selected_icon_bg = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            theme->has_custom_show_selected_icon_bg = true;
        } else if (strcasecmp(key, "dim_unselected_icons") == 0) {
            theme->dim_unselected_icons = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            theme->has_custom_dim_unselected_icons = true;
        } else if (strcasecmp(key, "anim_speed") == 0 || strcasecmp(key, "transition_speed") == 0 || strcasecmp(key, "carousel_speed") == 0 || strcasecmp(key, "animation_speed") == 0 || strcasecmp(key, "speed") == 0) {
            if (strcasecmp(value, "instant") == 0 || strcasecmp(value, "none") == 0 || strcasecmp(value, "off") == 0) {
                theme->anim_speed = 1.0f;
            } else if (strcasecmp(value, "fast") == 0) {
                theme->anim_speed = 0.60f;
            } else if (strcasecmp(value, "normal") == 0) {
                theme->anim_speed = 0.35f;
            } else if (strcasecmp(value, "slow") == 0) {
                theme->anim_speed = 0.20f;
            } else if (strcasecmp(value, "very_slow") == 0) {
                theme->anim_speed = 0.10f;
            } else {
                char val_clean[64];
                strncpy(val_clean, value, sizeof(val_clean) - 1);
                val_clean[sizeof(val_clean) - 1] = '\0';
                for (int k = 0; val_clean[k]; k++) {
                    if (val_clean[k] == ',') val_clean[k] = '.';
                    if (val_clean[k] == '%') val_clean[k] = '\0';
                }
                float v = (float)atof(val_clean);
                if (v > 1.0f && v <= 100.0f) {
                    v = v / 100.0f;
                }
                if (v <= 0.0001f) v = 1.0f;
                if (v > 1.0f) v = 1.0f;
                theme->anim_speed = v;
            }
            theme->has_custom_anim_speed = true;
            continue;
        } else if (strcasecmp(key, "bg_anim_mode") == 0 || strcasecmp(key, "background_animation") == 0 || strcasecmp(key, "anim_mode") == 0) {
            strncpy(theme->bg_anim_mode, value, sizeof(theme->bg_anim_mode) - 1);
            theme->bg_anim_mode[sizeof(theme->bg_anim_mode) - 1] = '\0';
            theme->has_custom_bg_anim_mode = true;
            continue;
        }

        // Parse based on section
        if (strcasecmp(section, "theme") == 0 || strcasecmp(section, "general") == 0) {
            // v19: name= is IGNORED - theme name comes from folder name only
            // This prevents issues where theme.ini has wrong name after copying folders
            // background= also ignored - loaded automatically from resources/general/

            // v20: Text background options
            if (strcasecmp(key, "platform_text_background") == 0) {
                theme->platform_text_background = (atoi(value) != 0);
            } else if (strcasecmp(key, "game_text_background") == 0) {
                theme->game_text_background = (atoi(value) != 0);
            }
            // v32: Game screenshot area
            else if (strcasecmp(key, "game_screenshot_x_start") == 0) {
                theme->game_screenshot_x_start = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_x_end") == 0) {
                theme->game_screenshot_x_end = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_y_start") == 0) {
                theme->game_screenshot_y_start = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_y_end") == 0) {
                theme->game_screenshot_y_end = atoi(value);
            }
        }
        // FrogUI layout section - our custom format
        else if (strcasecmp(section, "layout") == 0) {
            theme->has_custom_layout = true;

            // Platform list (main menu)
            if (strcasecmp(key, "platform_list_x") == 0) {
                theme->layout.platform_list_x = parse_int(value, DEFAULT_PLATFORM_LIST_X);
            } else if (strcasecmp(key, "platform_list_y_start") == 0) {
                theme->layout.platform_list_y_start = parse_int(value, DEFAULT_PLATFORM_LIST_Y_START);
            } else if (strcasecmp(key, "platform_list_y_end") == 0) {
                theme->layout.platform_list_y_end = parse_int(value, DEFAULT_PLATFORM_LIST_Y_END);
            } else if (strcasecmp(key, "platform_item_height") == 0) {
                theme->layout.platform_item_height = parse_int(value, DEFAULT_PLATFORM_ITEM_HEIGHT);
            } else if (strcasecmp(key, "platform_visible_items") == 0) {
                theme->layout.platform_visible_items = parse_int(value, DEFAULT_PLATFORM_VISIBLE_ITEMS);
            }
            // Game list (inside folders)
            else if (strcasecmp(key, "game_list_x") == 0) {
                theme->layout.game_list_x = parse_int(value, DEFAULT_GAME_LIST_X);
            } else if (strcasecmp(key, "game_list_y_start") == 0) {
                theme->layout.game_list_y_start = parse_int(value, DEFAULT_GAME_LIST_Y_START);
            } else if (strcasecmp(key, "game_list_y_end") == 0) {
                theme->layout.game_list_y_end = parse_int(value, DEFAULT_GAME_LIST_Y_END);
            } else if (strcasecmp(key, "game_item_height") == 0) {
                theme->layout.game_item_height = parse_int(value, DEFAULT_GAME_ITEM_HEIGHT);
            } else if (strcasecmp(key, "game_visible_items") == 0) {
                theme->layout.game_visible_items = parse_int(value, DEFAULT_GAME_VISIBLE_ITEMS);
            }
            // Thumbnail
            else if (strcasecmp(key, "thumb_x") == 0) {
                theme->layout.thumb_x = parse_int(value, DEFAULT_THUMB_X);
            } else if (strcasecmp(key, "thumb_y") == 0) {
                theme->layout.thumb_y = parse_int(value, DEFAULT_THUMB_Y);
            } else if (strcasecmp(key, "thumb_width") == 0) {
                theme->layout.thumb_width = parse_int(value, DEFAULT_THUMB_WIDTH);
            } else if (strcasecmp(key, "thumb_height") == 0) {
                theme->layout.thumb_height = parse_int(value, DEFAULT_THUMB_HEIGHT);
            }
            // Header
            else if (strcasecmp(key, "header_x") == 0) {
                theme->layout.header_x = parse_int(value, DEFAULT_HEADER_X);
            } else if (strcasecmp(key, "header_y") == 0) {
                theme->layout.header_y = parse_int(value, DEFAULT_HEADER_Y);
            }
            // Legend
            else if (strcasecmp(key, "legend_x") == 0) {
                theme->layout.legend_x = parse_int(value, DEFAULT_LEGEND_X);
            } else if (strcasecmp(key, "legend_y") == 0) {
                theme->layout.legend_y = parse_int(value, DEFAULT_LEGEND_Y);
            }
            // Counter
            else if (strcasecmp(key, "counter_x") == 0) {
                theme->layout.counter_x = parse_int(value, DEFAULT_COUNTER_X);
            } else if (strcasecmp(key, "counter_y") == 0) {
                theme->layout.counter_y = parse_int(value, DEFAULT_COUNTER_Y);
            }
            // v22: Text background options (moved from [theme] to [layout])
            else if (strcasecmp(key, "platform_text_background") == 0) {
                theme->platform_text_background = (atoi(value) != 0);
            } else if (strcasecmp(key, "game_text_background") == 0) {
                theme->game_text_background = (atoi(value) != 0);
            }
            // Menu layout default from theme.ini (vertical, horizontal, 2_columns, 3_columns)
            else if (strcasecmp(key, "menu_layout") == 0 || strcasecmp(key, "layout_mode") == 0) {
                strncpy(theme->menu_layout, value, sizeof(theme->menu_layout) - 1);
                theme->menu_layout[sizeof(theme->menu_layout) - 1] = '\0';
                theme->has_custom_menu_layout = true;
            } else if (strcasecmp(key, "game_list_layout") == 0 || strcasecmp(key, "gamelist_layout") == 0) {
                strncpy(theme->game_list_layout, value, sizeof(theme->game_list_layout) - 1);
                theme->game_list_layout[sizeof(theme->game_list_layout) - 1] = '\0';
                theme->has_custom_game_list_layout = true;
            } else if (strcasecmp(key, "hide_system_names") == 0 || strcasecmp(key, "hide_platform_names") == 0) {
                theme->hide_system_names = (strcasecmp(value, "true") == 0 || atoi(value) != 0);
                theme->has_custom_hide_system_names = true;
            } else if (strcasecmp(key, "hide_header_text") == 0 || strcasecmp(key, "hide_header_names") == 0 || strcasecmp(key, "hide_header_name") == 0) {
                theme->hide_header_text = (strcasecmp(value, "true") == 0 || atoi(value) != 0);
                theme->has_custom_hide_header_text = true;
            } else if (strcasecmp(key, "show_icons") == 0 || strcasecmp(key, "show_logos") == 0 || strcasecmp(key, "icons") == 0) {
                theme->show_icons = (strcasecmp(value, "true") == 0 || atoi(value) != 0);
                theme->has_custom_show_icons = true;
            } else if (strcasecmp(key, "show_game_icons") == 0 || strcasecmp(key, "show_game_logos") == 0 || strcasecmp(key, "game_icons") == 0) {
                theme->show_game_icons = (strcasecmp(value, "true") == 0 || atoi(value) != 0);
                theme->has_custom_show_game_icons = true;
            } else if (strcasecmp(key, "horizontal_y") == 0 || strcasecmp(key, "carousel_y") == 0 || strcasecmp(key, "horizontal_menu_y") == 0) {
                theme->horizontal_y = atoi(value);
            } else if (strcasecmp(key, "logo_size") == 0 || strcasecmp(key, "tile_size") == 0 || strcasecmp(key, "horizontal_tile_size") == 0) {
                int s = atoi(value);
                if (s > 0) {
                    theme->horizontal_tile_w = s;
                    theme->horizontal_tile_h = s;
                }
            } else if (strcasecmp(key, "logo_width") == 0 || strcasecmp(key, "tile_width") == 0 || strcasecmp(key, "horizontal_tile_w") == 0) {
                theme->horizontal_tile_w = atoi(value);
            } else if (strcasecmp(key, "logo_height") == 0 || strcasecmp(key, "tile_height") == 0 || strcasecmp(key, "horizontal_tile_h") == 0) {
                theme->horizontal_tile_h = atoi(value);
            } else if (strcasecmp(key, "item_spacing") == 0 || strcasecmp(key, "carousel_spacing") == 0 || strcasecmp(key, "horizontal_item_spacing") == 0) {
                theme->horizontal_item_spacing = atoi(value);
            } else if (strcasecmp(key, "grid_cols") == 0 || strcasecmp(key, "grid_columns") == 0 || strcasecmp(key, "columns") == 0) {
                theme->grid_cols = atoi(value);
            } else if (strcasecmp(key, "grid_tile_size") == 0 || strcasecmp(key, "grid_logo_size") == 0) {
                int s = atoi(value);
                if (s > 0) {
                    theme->grid_tile_w = s;
                    theme->grid_tile_h = s;
                }
            } else if (strcasecmp(key, "grid_tile_w") == 0 || strcasecmp(key, "grid_tile_width") == 0 || strcasecmp(key, "grid_logo_w") == 0) {
                theme->grid_tile_w = atoi(value);
            } else if (strcasecmp(key, "grid_tile_h") == 0 || strcasecmp(key, "grid_tile_height") == 0 || strcasecmp(key, "grid_logo_h") == 0) {
                theme->grid_tile_h = atoi(value);
            } else if (strcasecmp(key, "grid_spacing_x") == 0 || strcasecmp(key, "grid_spacing") == 0 || strcasecmp(key, "grid_item_spacing") == 0 || strcasecmp(key, "grid_item_spacing_x") == 0) {
                theme->grid_spacing_x = atoi(value);
            } else if (strcasecmp(key, "grid_spacing_y") == 0 || strcasecmp(key, "grid_row_spacing") == 0 || strcasecmp(key, "grid_item_spacing_y") == 0) {
                theme->grid_spacing_y = atoi(value);
            } else if (strcasecmp(key, "grid_bottom_spacing") == 0 || strcasecmp(key, "bottom_spacing") == 0 || strcasecmp(key, "icon_spacing_bottom") == 0 || strcasecmp(key, "platform_icon_spacing") == 0 || strcasecmp(key, "bottom_icon_spacing") == 0) {
                theme->grid_bottom_spacing = atoi(value);
            } else if (strcasecmp(key, "grid_x") == 0 || strcasecmp(key, "grid_start_x") == 0) {
                theme->grid_x = atoi(value);
            } else if (strcasecmp(key, "grid_y") == 0 || strcasecmp(key, "grid_start_y") == 0) {
                theme->grid_y = atoi(value);
            } else if (strcasecmp(key, "grid_visible_rows") == 0 || strcasecmp(key, "grid_rows") == 0) {
                theme->grid_visible_rows = atoi(value);
            }
            // v32: Game screenshot area (also can be in [layout])
            else if (strcasecmp(key, "game_screenshot_x_start") == 0) {
                theme->game_screenshot_x_start = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_x_end") == 0) {
                theme->game_screenshot_x_end = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_y_start") == 0) {
                theme->game_screenshot_y_start = atoi(value);
            } else if (strcasecmp(key, "game_screenshot_y_end") == 0) {
                theme->game_screenshot_y_end = atoi(value);
            }
        }
        else if (strcasecmp(section, "colors") == 0) {
            uint16_t color = parse_hex_color(value);
            if (color != 0xFFFF) {
                theme->has_custom_colors = true;

                if (strcasecmp(key, "bg") == 0) {
                    theme->bg_color = color;
                } else if (strcasecmp(key, "text") == 0) {
                    theme->text_color = color;
                } else if (strcasecmp(key, "select_bg") == 0) {
                    theme->select_bg_color = color;
                } else if (strcasecmp(key, "select_text") == 0) {
                    theme->select_text_color = color;
                }
            }
        }
    }

    fclose(f);
    return 1;
}

// v19: Load background image with new priority:
// 1. background_anim.avi (animation)
// 2. background_anim.png (transparent overlay for animation)
// 3. If no AVI, use background.png (static)
// Also supports legacy background.avi for backward compatibility
static int load_background_image(GfxTheme* theme) {
    if (theme->background_loaded) return 1;
    if (!theme->path[0]) return 0;

    char bg_path[MAX_THEME_PATH_LEN];
    int width, height;

    // Reset overlay state
    main_bg_has_overlay = false;
    if (main_bg_overlay_pixels) { free(main_bg_overlay_pixels); main_bg_overlay_pixels = NULL; }
    if (main_bg_overlay_alpha) { free(main_bg_overlay_alpha); main_bg_overlay_alpha = NULL; }

    // Step 1: Try animated backgrounds (AVI)
    // Priority: background_anim.avi > background.avi (legacy)
    bool anim_loaded = false;

    // 1a. resources/general/background_anim.avi (new format)
    snprintf(bg_path, sizeof(bg_path), "%s/resources/general/background_anim.avi", theme->path);
    if (avi_bg_load(bg_path)) {
        main_bg_is_animated = true;
        strncpy(main_bg_avi_path, bg_path, MAX_THEME_PATH_LEN - 1);
        anim_loaded = true;
    }

    // 1b. background_anim.avi in theme root
    if (!anim_loaded) {
        snprintf(bg_path, sizeof(bg_path), "%s/background_anim.avi", theme->path);
        if (avi_bg_load(bg_path)) {
            main_bg_is_animated = true;
            strncpy(main_bg_avi_path, bg_path, MAX_THEME_PATH_LEN - 1);
            anim_loaded = true;
        }
    }

    // 1c. Legacy: resources/general/background.avi
    if (!anim_loaded) {
        snprintf(bg_path, sizeof(bg_path), "%s/resources/general/background.avi", theme->path);
        if (avi_bg_load(bg_path)) {
            main_bg_is_animated = true;
            strncpy(main_bg_avi_path, bg_path, MAX_THEME_PATH_LEN - 1);
            anim_loaded = true;
        }
    }

    // 1d. Legacy: background.avi in theme root
    if (!anim_loaded) {
        snprintf(bg_path, sizeof(bg_path), "%s/background.avi", theme->path);
        if (avi_bg_load(bg_path)) {
            main_bg_is_animated = true;
            strncpy(main_bg_avi_path, bg_path, MAX_THEME_PATH_LEN - 1);
            anim_loaded = true;
        }
    }

    // Helper function / block to try loading main overlay PNG
    main_bg_has_overlay = false;
    if (main_bg_overlay_pixels) { free(main_bg_overlay_pixels); main_bg_overlay_pixels = NULL; }
    if (main_bg_overlay_alpha) { free(main_bg_overlay_alpha); main_bg_overlay_alpha = NULL; }
    if (overlay_blend_mode) { free(overlay_blend_mode); overlay_blend_mode = NULL; }

    const char *overlay_candidates[] = {
        "overlay.png",
        "background_overlay.png",
        "background_anim.png",
        "resources/general/overlay.png",
        "resources/general/background_overlay.png",
        "resources/general/background_anim.png",
        NULL
    };

    for (int i = 0; overlay_candidates[i]; i++) {
        snprintf(bg_path, sizeof(bg_path), "%s/%s", theme->path, overlay_candidates[i]);
        if (load_png_rgba565(bg_path, &main_bg_overlay_pixels, &main_bg_overlay_alpha, &width, &height)) {
            if (width == SCREEN_WIDTH && height == SCREEN_HEIGHT) {
                main_bg_has_overlay = true;
                precompute_overlay_blend();
                break;
            } else {
                free(main_bg_overlay_pixels); main_bg_overlay_pixels = NULL;
                free(main_bg_overlay_alpha); main_bg_overlay_alpha = NULL;
            }
        }
    }

    // Try sections overlay (resources/sections/background_anim.png or overlay.png)
    sections_has_overlay = false;
    if (sections_overlay_pixels) { free(sections_overlay_pixels); sections_overlay_pixels = NULL; }
    if (sections_overlay_alpha) { free(sections_overlay_alpha); sections_overlay_alpha = NULL; }
    if (sections_overlay_blend_mode) { free(sections_overlay_blend_mode); sections_overlay_blend_mode = NULL; }

    const char *sections_candidates[] = {
        "resources/sections/overlay.png",
        "resources/sections/background_anim.png",
        "resources/sections/background_overlay.png",
        "overlay_platform.png",
        NULL
    };

    for (int i = 0; sections_candidates[i]; i++) {
        snprintf(bg_path, sizeof(bg_path), "%s/%s", theme->path, sections_candidates[i]);
        if (load_png_rgba565(bg_path, &sections_overlay_pixels, &sections_overlay_alpha, &width, &height)) {
            if (width == SCREEN_WIDTH && height == SCREEN_HEIGHT) {
                sections_has_overlay = true;
                precompute_sections_overlay_blend();
                break;
            } else {
                free(sections_overlay_pixels); sections_overlay_pixels = NULL;
                free(sections_overlay_alpha); sections_overlay_alpha = NULL;
            }
        }
    }

    if (anim_loaded) {
        theme->background_loaded = true;
        return 1;
    }

    // Step 3: No animation - fall back to static PNG backgrounds
    main_bg_is_animated = false;
    main_bg_avi_path[0] = '\0';

    // 3a. resources/general/background.png (SimpleMenu style)
    snprintf(bg_path, sizeof(bg_path), "%s/resources/general/background.png", theme->path);
    if (load_png_rgb565(bg_path, &theme->background_data, &width, &height)) {
        if (width == SCREEN_WIDTH && height == SCREEN_HEIGHT) {
            theme->background_loaded = true;
            return 1;
        }
        free(theme->background_data);
        theme->background_data = NULL;
    }

    // 3b. background.png in theme root
    snprintf(bg_path, sizeof(bg_path), "%s/background.png", theme->path);
    if (load_png_rgb565(bg_path, &theme->background_data, &width, &height)) {
        if (width == SCREEN_WIDTH && height == SCREEN_HEIGHT) {
            theme->background_loaded = true;
            return 1;
        }
        free(theme->background_data);
        theme->background_data = NULL;
    }

    return 0;
}

void gfx_theme_init(void) {
    memset(gfx_themes, 0, sizeof(gfx_themes));
    num_gfx_themes = 0;
    current_gfx_theme = 0;
    main_bg_is_animated = false;
    main_bg_avi_path[0] = '\0';

    // v19: Reset overlay state
    main_bg_overlay_pixels = NULL;
    main_bg_overlay_alpha = NULL;
    main_bg_has_overlay = false;

    // v19: Allocate composite buffer for overlay blending
    if (!composite_buffer) {
        composite_buffer = (uint16_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    }

    // Initialize animated background system
    avi_bg_init();

    // First entry is always "None" / default
    memset(&gfx_themes[0], 0, sizeof(GfxTheme));
    strcpy(gfx_themes[0].name, "None");
    gfx_themes[0].layout = default_layout;
    num_gfx_themes = 1;

    // Check for themes/default/theme.ini or themes/theme_default/theme.ini
    const char* default_ini_paths[] = {
        "/mnt/sda1/themes/default/theme.ini",
        "/mnt/sda1/themes/default/Theme.ini",
        "/mnt/sda1/themes/default/THEME.INI",
        "/mnt/sda1/themes/theme_default/theme.ini",
        "/mnt/sda1/THEMES/default/theme.ini",
        "sdcard/themes/default/theme.ini",
        "sdcard/themes/theme_default/theme.ini",
        "themes/default/theme.ini",
        "themes/theme_default/theme.ini",
        NULL
    };
    for (int i = 0; default_ini_paths[i]; i++) {
        FILE* f = fopen(default_ini_paths[i], "r");
        if (f) {
            fclose(f);
            parse_theme_ini(default_ini_paths[i], &gfx_themes[0]);
            break;
        }
    }
}

int gfx_theme_scan(void) {
    // Directory scanning disabled - themes are loaded from multicore.opt only
    gfx_theme_cleanup();
    gfx_theme_init();
    return 0;
}

int gfx_theme_count(void) {
    return num_gfx_themes;
}

const char* gfx_theme_get_name(int index) {
    if (index < 0 || index >= num_gfx_themes) return "Unknown";
    return gfx_themes[index].name;
}

int gfx_theme_apply(int index) {
    if (index < 0 || index >= num_gfx_themes) return 0;

    // Re-parse theme.ini so any edits take effect immediately
    if (index > 0 && gfx_themes[index].path[0]) {
        const char* ini_filenames[] = { "theme.ini", "Theme.ini", "THEME.INI", "theme.cfg", "Theme.cfg", NULL };
        for (int i = 0; ini_filenames[i]; i++) {
            char ini_path[MAX_THEME_PATH_LEN];
            snprintf(ini_path, sizeof(ini_path), "%s/%s", gfx_themes[index].path, ini_filenames[i]);
            FILE* f = fopen(ini_path, "r");
            if (f) {
                fclose(f);
                parse_theme_ini(ini_path, &gfx_themes[index]);
                break;
            }
        }
    }

    // Free previous background and cached logos
    gfx_theme_free_background();
    gfx_theme_free_logo_cache();

    current_gfx_theme = index;

    // Load new background if not "None"
    if (index > 0 && gfx_themes[index].path[0]) {
        load_background_image(&gfx_themes[index]);
    }

    return 1;
}

// Apply theme by name - searches known paths, registers on-demand
int gfx_theme_apply_by_name(const char* name) {
    if (!name || name[0] == '\0') return 0;

    // "theme_default" or "None" means no theme
    if (strcmp(name, "theme_default") == 0 || strcmp(name, "None") == 0) {
        gfx_theme_free_background();
        gfx_theme_free_logo_cache();
        current_gfx_theme = 0;
        return 1;
    }

    // Check if already registered
    for (int t = 1; t < num_gfx_themes; t++) {
        if (strcmp(gfx_themes[t].name, name) == 0) {
            return gfx_theme_apply(t);
        }
    }

    // Not registered yet - try to find it in standard locations
    const char* search_dirs[] = {
        "/mnt/sda1/themes",
        "/mnt/sda1/THEMES",
        NULL
    };

    for (int d = 0; search_dirs[d]; d++) {
        char theme_path[MAX_THEME_PATH_LEN];
        snprintf(theme_path, sizeof(theme_path), "%s/%s", search_dirs[d], name);

        DIR* subdir = opendir(theme_path);
        if (!subdir) continue;
        closedir(subdir);

        // Register this theme
        if (num_gfx_themes >= MAX_GFX_THEMES) break;
        GfxTheme* theme = &gfx_themes[num_gfx_themes];
        memset(theme, 0, sizeof(GfxTheme));
        strncpy(theme->name, name, MAX_THEME_NAME_LEN - 1);
        strncpy(theme->path, theme_path, MAX_THEME_PATH_LEN - 1);
        theme->layout = default_layout;

        const char* ini_filenames[] = { "theme.ini", "Theme.ini", "THEME.INI", "theme.cfg", "Theme.cfg", NULL };
        for (int i = 0; ini_filenames[i]; i++) {
            char ini_path[MAX_THEME_PATH_LEN];
            snprintf(ini_path, sizeof(ini_path), "%s/%s", theme_path, ini_filenames[i]);
            FILE* f = fopen(ini_path, "r");
            if (f) {
                fclose(f);
                parse_theme_ini(ini_path, theme);
                break;
            }
        }

        int new_index = num_gfx_themes;
        num_gfx_themes++;
        return gfx_theme_apply(new_index);
    }

    return 0;
}


int gfx_theme_get_current_index(void) {
    return current_gfx_theme;
}

bool gfx_theme_is_active(void) {
    return current_gfx_theme > 0;
}

const GfxTheme* gfx_theme_get_current(void) {
    if (current_gfx_theme <= 0) return NULL;
    return &gfx_themes[current_gfx_theme];
}

const GfxThemeLayout* gfx_theme_get_layout(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_layout) {
        return &gfx_themes[current_gfx_theme].layout;
    }
    return &default_layout;
}

const char* gfx_theme_get_menu_layout(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_menu_layout) {
        return gfx_themes[current_gfx_theme].menu_layout;
    }
    return NULL;
}

const char* gfx_theme_get_game_list_layout(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_game_list_layout) {
        return gfx_themes[current_gfx_theme].game_list_layout;
    }
    return NULL;
}

bool gfx_theme_get_hide_system_names(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_system_names) {
        return gfx_themes[current_gfx_theme].hide_system_names;
    }
    return false;
}

bool gfx_theme_get_hide_header_text(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_header_text) {
        return gfx_themes[current_gfx_theme].hide_header_text;
    }
    return false;
}

bool gfx_theme_get_hide_game_names(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_game_names) {
        return gfx_themes[current_gfx_theme].hide_game_names;
    }
    return false;
}

bool gfx_theme_has_custom_hide_system_names(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_system_names) {
        return true;
    }
    return false;
}

bool gfx_theme_has_custom_hide_header_text(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_header_text) {
        return true;
    }
    return false;
}

bool gfx_theme_has_custom_hide_game_names(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_hide_game_names) {
        return true;
    }
    return false;
}

bool gfx_theme_get_show_icons(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_icons) {
        return gfx_themes[current_gfx_theme].show_icons;
    }
    return true;
}

bool gfx_theme_get_show_game_icons(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_game_icons) {
        return gfx_themes[current_gfx_theme].show_game_icons;
    }
    return false;
}

uint32_t gfx_theme_get_game_name_color(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_game_name_color) {
        return gfx_themes[current_gfx_theme].game_name_color;
    }
    return 0xFFFF; // Default white
}

bool gfx_theme_has_custom_game_name_color(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_game_name_color) {
        return true;
    }
    return false;
}

bool gfx_theme_get_xmb_waves(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_waves) {
        return gfx_themes[current_gfx_theme].xmb_waves;
    }
    return false;
}

bool gfx_theme_has_custom_xmb_waves(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_waves) {
        return true;
    }
    return false;
}

uint32_t gfx_theme_get_xmb_wave_color(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_color) {
        return gfx_themes[current_gfx_theme].xmb_wave_color;
    }
    return 0xFFFFFF; // White
}

bool gfx_theme_has_custom_xmb_wave_color(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_color) {
        return true;
    }
    return false;
}

int gfx_theme_get_xmb_wave_glow(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_glow) {
        return gfx_themes[current_gfx_theme].xmb_wave_glow;
    }
    return 2; // Medium
}

bool gfx_theme_has_custom_xmb_wave_glow(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_glow) {
        return true;
    }
    return false;
}

int gfx_theme_get_xmb_wave_variety(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_variety) {
        return gfx_themes[current_gfx_theme].xmb_wave_variety;
    }
    return 3; // Complex
}

bool gfx_theme_has_custom_xmb_wave_variety(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_xmb_wave_variety) {
        return true;
    }
    return false;
}

float gfx_theme_get_anim_speed(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_anim_speed) {
        return gfx_themes[current_gfx_theme].anim_speed;
    }
    return 0.80f;
}

bool gfx_theme_has_custom_anim_speed(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_anim_speed) {
        return true;
    }
    return false;
}

const char* gfx_theme_get_bg_anim_mode(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_bg_anim_mode) {
        return gfx_themes[current_gfx_theme].bg_anim_mode;
    }
    return "fade";
}

bool gfx_theme_has_custom_bg_anim_mode(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_bg_anim_mode) {
        return true;
    }
    return false;
}

bool gfx_theme_get_text_in_empty_icon(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_text_in_empty_icon) {
        return gfx_themes[current_gfx_theme].text_in_empty_icon;
    }
    return true; // default
}

bool gfx_theme_has_custom_text_in_empty_icon(void) {
    return (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_text_in_empty_icon);
}

bool gfx_theme_get_show_empty_icon_bg(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_empty_icon_bg) {
        return gfx_themes[current_gfx_theme].show_empty_icon_bg;
    }
    return true; // default
}

bool gfx_theme_has_custom_show_empty_icon_bg(void) {
    return (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_empty_icon_bg);
}

bool gfx_theme_get_show_selected_icon_bg(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_selected_icon_bg) {
        return gfx_themes[current_gfx_theme].show_selected_icon_bg;
    }
    return true; // default
}

bool gfx_theme_has_custom_show_selected_icon_bg(void) {
    return (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_show_selected_icon_bg);
}

bool gfx_theme_get_dim_unselected_icons(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_dim_unselected_icons) {
        return gfx_themes[current_gfx_theme].dim_unselected_icons;
    }
    return false; // default
}

bool gfx_theme_has_custom_dim_unselected_icons(void) {
    return (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].has_custom_dim_unselected_icons);
}

int gfx_theme_get_horizontal_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].horizontal_y > 0) {
        return gfx_themes[current_gfx_theme].horizontal_y;
    }
    return (SCREEN_HEIGHT / 2) - 8;
}

int gfx_theme_get_horizontal_tile_w(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].horizontal_tile_w > 0) {
        return gfx_themes[current_gfx_theme].horizontal_tile_w;
    }
    return 60;
}

int gfx_theme_get_horizontal_tile_h(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].horizontal_tile_h > 0) {
        return gfx_themes[current_gfx_theme].horizontal_tile_h;
    }
    return 60;
}

int gfx_theme_get_horizontal_item_spacing(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].horizontal_item_spacing > 0) {
        return gfx_themes[current_gfx_theme].horizontal_item_spacing;
    }
    return 95;
}

int gfx_theme_get_grid_cols(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_cols > 0) {
        return (gfx_themes[current_gfx_theme].grid_cols == 3) ? 3 : 2;
    }
    return 2;
}

int gfx_theme_get_grid_tile_w(int cols) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_tile_w > 0) {
        return gfx_themes[current_gfx_theme].grid_tile_w;
    }
    return (cols == 3) ? 80 : 120;
}

int gfx_theme_get_grid_tile_h(int cols) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_tile_h > 0) {
        return gfx_themes[current_gfx_theme].grid_tile_h;
    }
    return (cols == 3) ? 60 : 70;
}

int gfx_theme_get_grid_spacing_x(int cols) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_spacing_x > 0) {
        return gfx_themes[current_gfx_theme].grid_spacing_x;
    }
    return (cols == 3) ? 95 : 140;
}

int gfx_theme_get_grid_spacing_y(int cols) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_spacing_y > 0) {
        return gfx_themes[current_gfx_theme].grid_spacing_y;
    }
    return (cols == 3) ? 72 : 82;
}

int gfx_theme_get_grid_bottom_spacing(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_bottom_spacing > 0) {
        return gfx_themes[current_gfx_theme].grid_bottom_spacing;
    }
    return 12;
}

int gfx_theme_get_grid_x(int cols) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_x > 0) {
        return gfx_themes[current_gfx_theme].grid_x;
    }
    return (cols == 3) ? 20 : 25;
}

int gfx_theme_get_grid_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_y > 0) {
        return gfx_themes[current_gfx_theme].grid_y;
    }
    return 40;
}

int gfx_theme_get_grid_visible_rows(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].grid_visible_rows > 0) {
        return gfx_themes[current_gfx_theme].grid_visible_rows;
    }
    return 2;
}

void gfx_theme_set_bg_anim_direction(int dir) {
    bg_anim_direction = (dir >= 0) ? 1 : -1;
}

int gfx_theme_get_bg_anim_direction(void) {
    return bg_anim_direction;
}

int gfx_theme_get_platform_label_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].platform_label_y > 0) {
        return gfx_themes[current_gfx_theme].platform_label_y;
    }
    return 0;
}

int gfx_theme_get_platform_label_offset_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].platform_label_offset_y != 0) {
        return gfx_themes[current_gfx_theme].platform_label_offset_y;
    }
    return 12;
}

int gfx_theme_get_platform_label_offset_x(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].platform_label_offset_x != 0) {
        return gfx_themes[current_gfx_theme].platform_label_offset_x;
    }
    return 0;
}

int gfx_theme_get_game_label_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].game_label_y > 0) {
        return gfx_themes[current_gfx_theme].game_label_y;
    }
    return 0;
}

int gfx_theme_get_game_label_offset_y(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].game_label_offset_y != 0) {
        return gfx_themes[current_gfx_theme].game_label_offset_y;
    }
    return 12;
}

int gfx_theme_get_game_label_offset_x(void) {
    if (current_gfx_theme >= 0 && gfx_themes[current_gfx_theme].game_label_offset_x != 0) {
        return gfx_themes[current_gfx_theme].game_label_offset_x;
    }
    return 0;
}

typedef struct {
    char name[32];
    uint16_t *pixels;
    uint8_t *alpha;
    int width;
    int height;
    bool checked;
    bool found;
} PlatformLogoCache;

#define MAX_LOGO_CACHE 64
static PlatformLogoCache logo_cache[MAX_LOGO_CACHE];
static int logo_cache_count = 0;

void gfx_theme_free_logo_cache(void) {
    for (int i = 0; i < logo_cache_count; i++) {
        if (logo_cache[i].pixels) free(logo_cache[i].pixels);
        if (logo_cache[i].alpha) free(logo_cache[i].alpha);
    }
    logo_cache_count = 0;
}

// Search and load 60x60 platform logo/icon (logo.png) - cached in RAM
int gfx_theme_load_platform_logo(const char *platform_name, uint16_t **pixels, uint8_t **alpha, int *width, int *height) {
    if (!platform_name || platform_name[0] == '\0') return 0;

    // Clean whitespace from platform_name
    char clean_platform[64];
    strncpy(clean_platform, platform_name, sizeof(clean_platform) - 1);
    clean_platform[sizeof(clean_platform) - 1] = '\0';
    int len = strlen(clean_platform);
    while (len > 0 && (clean_platform[len - 1] == ' ' || clean_platform[len - 1] == '\t' || clean_platform[len - 1] == '\r' || clean_platform[len - 1] == '\n')) {
        clean_platform[--len] = '\0';
    }
    if (clean_platform[0] == '\0') return 0;

    // Check cache first
    for (int i = 0; i < logo_cache_count; i++) {
        if (strcasecmp(logo_cache[i].name, clean_platform) == 0) {
            if (logo_cache[i].found) {
                *pixels = logo_cache[i].pixels;
                *alpha = logo_cache[i].alpha;
                *width = logo_cache[i].width;
                *height = logo_cache[i].height;
                return 1;
            }
            return 0; // Cached as not found
        }
    }

    if (logo_cache_count >= MAX_LOGO_CACHE) return 0;

    int idx = logo_cache_count;
    strncpy(logo_cache[idx].name, clean_platform, 31);
    logo_cache[idx].name[31] = '\0';
    logo_cache[idx].checked = true;
    logo_cache[idx].found = false;
    logo_cache[idx].pixels = NULL;
    logo_cache[idx].alpha = NULL;

    char logo_path[512];
    char name_lower[64];
    strncpy(name_lower, clean_platform, sizeof(name_lower) - 1);
    name_lower[sizeof(name_lower) - 1] = '\0';
    for (int i = 0; name_lower[i]; i++) {
        name_lower[i] = tolower((unsigned char)name_lower[i]);
        if (name_lower[i] == ' ') name_lower[i] = '_';
    }

    const GfxTheme *theme = gfx_theme_get_current();
    int loaded = 0;

    // 1. Try active theme paths (platforms/name/logo.png or resources/platforms/name/logo.png)
    if (theme && theme->path[0] != '\0') {
        // Lowercase folder search
        snprintf(logo_path, sizeof(logo_path), "%s/platforms/%s/logo.png", theme->path, name_lower);
        if (load_png_rgba565(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;

        // Exact case folder search (e.g. platforms/FC/logo.png)
        if (!loaded && strcmp(clean_platform, name_lower) != 0) {
            snprintf(logo_path, sizeof(logo_path), "%s/platforms/%s/logo.png", theme->path, clean_platform);
            if (load_png_rgba565(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
        }

        if (!loaded) {
            snprintf(logo_path, sizeof(logo_path), "%s/resources/platforms/%s/logo.png", theme->path, name_lower);
            if (load_png_rgba565(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
        }

        if (!loaded && strcmp(clean_platform, name_lower) != 0) {
            snprintf(logo_path, sizeof(logo_path), "%s/resources/platforms/%s/logo.png", theme->path, clean_platform);
            if (load_png_rgba565(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
        }
    }

    // Only load platform logos from the active theme folder (not stock ROMS/logo.png black squares)


    if (loaded) {
        logo_cache[idx].found = true;
        logo_cache_count++;
        *pixels = logo_cache[idx].pixels;
        *alpha = logo_cache[idx].alpha;
        *width = logo_cache[idx].width;
        *height = logo_cache[idx].height;
        return 1;
    }

    logo_cache_count++;
    return 0;
}

static int load_raw_rgb565_logo(const char *path, uint16_t **pixels, uint8_t **alpha, int *width, int *height) {
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
        *alpha = NULL;
        *width = w;
        *height = h;
        return 1;
    }

    free(data);
    fclose(fp);
    return 0;
}

// Load platform or game logo (supports game_name-logo.png, game_name.png, or cover art .rgb565)
int gfx_theme_load_entry_logo(const char *name, bool is_platform, uint16_t **pixels, uint8_t **alpha, int *width, int *height) {
    if (is_platform) {
        return gfx_theme_load_platform_logo(name, pixels, alpha, width, height);
    }

    if (!name || name[0] == '\0') return 0;

    // Check logo_cache first
    for (int i = 0; i < logo_cache_count; i++) {
        if (strcasecmp(logo_cache[i].name, name) == 0) {
            if (logo_cache[i].found) {
                *pixels = logo_cache[i].pixels;
                *alpha = logo_cache[i].alpha;
                *width = logo_cache[i].width;
                *height = logo_cache[i].height;
                return 1;
            }
            return 0;
        }
    }

    if (logo_cache_count >= MAX_LOGO_CACHE) return 0;

    int idx = logo_cache_count;
    strncpy(logo_cache[idx].name, name, 31);
    logo_cache[idx].name[31] = '\0';
    logo_cache[idx].checked = true;
    logo_cache[idx].found = false;
    logo_cache[idx].pixels = NULL;
    logo_cache[idx].alpha = NULL;

    // Clean game name without extension
    char clean_name[128];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';

    char *dot = strrchr(clean_name, '.');
    if (dot) *dot = '\0';

    char clean_lower[128];
    strncpy(clean_lower, clean_name, sizeof(clean_lower) - 1);
    clean_lower[sizeof(clean_lower) - 1] = '\0';
    for (int i = 0; clean_lower[i]; i++) {
        clean_lower[i] = tolower((unsigned char)clean_lower[i]);
    }

    const GfxTheme *theme = gfx_theme_get_current();
    char logo_path[512];
    int loaded = 0;

    // 1. Try theme path: <theme>/games/<game_clean_lower>.rgb565
    if (theme && theme->path[0] != '\0') {
        snprintf(logo_path, sizeof(logo_path), "%s/games/%s.rgb565", theme->path, clean_lower);
        if (load_raw_rgb565_logo(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
    }

    // 2. Try current ROM folder path: .rgb565 first
    if (!loaded && current_platform[0] != '\0') {
        // Fast paths: .rgb565 variants
        snprintf(logo_path, sizeof(logo_path), "%s/%s/.res/%s.rgb565", ROMS_PATH, current_platform, clean_name);
        if (load_raw_rgb565_logo(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;

        if (!loaded) {
            snprintf(logo_path, sizeof(logo_path), "%s/%s/%s.rgb565", ROMS_PATH, current_platform, clean_name);
            if (load_raw_rgb565_logo(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
        }

        if (!loaded) {
            snprintf(logo_path, sizeof(logo_path), "%s/%s/.res/%s-logo.rgb565", ROMS_PATH, current_platform, clean_name);
            if (load_raw_rgb565_logo(logo_path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) loaded = 1;
        }
    }

    if (loaded) {
        logo_cache[idx].found = true;
        logo_cache_count++;
        *pixels = logo_cache[idx].pixels;
        *alpha = logo_cache[idx].alpha;
        *width = logo_cache[idx].width;
        *height = logo_cache[idx].height;
        return 1;
    }

    logo_cache_count++;
    return 0;
}

int gfx_theme_load_entry_logo_from_path(const char *path, uint16_t **pixels, uint8_t **alpha, int *width, int *height) {
    if (!path || path[0] == '\0') return 0;

    // Check logo_cache first
    for (int i = 0; i < logo_cache_count; i++) {
        if (strcasecmp(logo_cache[i].name, path) == 0) {
            if (logo_cache[i].found) {
                *pixels = logo_cache[i].pixels;
                *alpha = logo_cache[i].alpha;
                *width = logo_cache[i].width;
                *height = logo_cache[i].height;
                return 1;
            }
            return 0;
        }
    }

    if (logo_cache_count >= MAX_LOGO_CACHE) return 0;

    int idx = logo_cache_count;
    strncpy(logo_cache[idx].name, path, 31); // Store path as name in cache
    logo_cache[idx].name[31] = '\0';
    logo_cache[idx].checked = true;
    logo_cache[idx].found = false;
    logo_cache[idx].pixels = NULL;
    logo_cache[idx].alpha = NULL;

    if (load_raw_rgb565_logo(path, &logo_cache[idx].pixels, &logo_cache[idx].alpha, &logo_cache[idx].width, &logo_cache[idx].height)) {
        logo_cache[idx].found = true;
        logo_cache_count++;
        *pixels = logo_cache[idx].pixels;
        *alpha = logo_cache[idx].alpha;
        *width = logo_cache[idx].width;
        *height = logo_cache[idx].height;
        return 1;
    }

    logo_cache_count++;
    return 0;
}

uint16_t* gfx_theme_get_background(void) {
    if (current_gfx_theme <= 0) return NULL;

    GfxTheme* theme = &gfx_themes[current_gfx_theme];

    if (!theme->background_loaded) {
        load_background_image(theme);
    }

    // If animated background is active
    if (main_bg_is_animated && avi_bg_is_active()) {
        uint16_t* frame = avi_bg_get_frame();
        if (!frame) return theme->background_data;

        // v61: DON'T apply overlay here - return just AVI frame
        // Overlay will be applied later by gfx_theme_apply_overlay()
        return frame;
    }

    return theme->background_data;
}

extern int settings_is_active(void);

// v61: Apply PNG overlay to framebuffer (call after drawing thumbnails, before text)
// v62: Use sections overlay when in platform, add dithering
void gfx_theme_apply_overlay(uint16_t* framebuffer) {
    if (!framebuffer) return;
    if (settings_is_active()) return;

    // v62: Select which overlay to use
    // If we're in a platform and sections overlay exists, use it
    // Otherwise use main overlay
    uint16_t* overlay_pixels = main_bg_overlay_pixels;
    uint8_t* overlay_alpha = main_bg_overlay_alpha;
    uint8_t* blend_mode = overlay_blend_mode;
    bool has_overlay = main_bg_has_overlay;

    if (current_platform[0] != '\0' && sections_has_overlay && sections_overlay_blend_mode) {
        // In a platform (section) - use sections overlay
        overlay_pixels = sections_overlay_pixels;
        overlay_alpha = sections_overlay_alpha;
        blend_mode = sections_overlay_blend_mode;
        has_overlay = true;
    }

    if (!has_overlay || !blend_mode) return;

    // Apply overlay with alpha blending and dithering
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint8_t mode = blend_mode[i];
        if (mode == 0) {
            // Transparent - keep framebuffer pixel
        } else if (mode == 2) {
            // Opaque - use overlay directly
            framebuffer[i] = overlay_pixels[i];
        } else {
            // Blend mode with dithering
            uint8_t alpha = overlay_alpha[i];
            uint16_t fg = overlay_pixels[i];
            uint16_t bg = framebuffer[i];

            int fg_r = (fg >> 11) & 0x1F;
            int fg_g = (fg >> 5) & 0x3F;
            int fg_b = fg & 0x1F;

            int bg_r = (bg >> 11) & 0x1F;
            int bg_g = (bg >> 5) & 0x3F;
            int bg_b = bg & 0x1F;

            int a = alpha + 1;
            int inv_a = 257 - a;

            // v62: Add dithering to reduce banding on 16-bit display
            int x = i % SCREEN_WIDTH;
            int y = i / SCREEN_WIDTH;
            int dither = dither_matrix[y & 3][x & 3] - 8;  // Range: -8 to +7

            int r = (fg_r * a + bg_r * inv_a + dither) >> 8;
            int g = (fg_g * a + bg_g * inv_a + (dither * 2)) >> 8;  // Green has 6 bits
            int b = (fg_b * a + bg_b * inv_a + dither) >> 8;

            // Clamp values
            if (r < 0) r = 0; else if (r > 31) r = 31;
            if (g < 0) g = 0; else if (g > 63) g = 63;
            if (b < 0) b = 0; else if (b > 31) b = 31;

            framebuffer[i] = (r << 11) | (g << 5) | b;
        }
    }
}

// Check if main background is animated
bool gfx_theme_is_animated(void) {
    return main_bg_is_animated && avi_bg_is_active();
}

// Advance animation frame (call at 15fps rate)
void gfx_theme_advance_animation(void) {
    if (main_bg_is_animated && avi_bg_is_active() && !avi_bg_is_paused()) {
        avi_bg_advance_frame();
    }
}

// Pause animation when entering platform folder with static background
void gfx_theme_pause_animation(void) {
    if (main_bg_is_animated && avi_bg_is_active()) {
        avi_bg_pause();
    }
}

// Resume animation when returning to main menu
void gfx_theme_resume_animation(void) {
    if (main_bg_is_animated && avi_bg_is_active()) {
        avi_bg_resume();
    }
}

// v20: Text background style getters
bool gfx_theme_platform_text_background(void) {
    if (current_gfx_theme <= 0) return false;
    return gfx_themes[current_gfx_theme].platform_text_background;
}

bool gfx_theme_game_text_background(void) {
    if (current_gfx_theme <= 0) return false;
    return gfx_themes[current_gfx_theme].game_text_background;
}

// v32: Game screenshot area getters
int gfx_theme_get_screenshot_x_start(void) {
    if (current_gfx_theme <= 0) return 0;
    return gfx_themes[current_gfx_theme].game_screenshot_x_start;
}

int gfx_theme_get_screenshot_x_end(void) {
    if (current_gfx_theme <= 0) return 0;
    return gfx_themes[current_gfx_theme].game_screenshot_x_end;
}

int gfx_theme_get_screenshot_y_start(void) {
    if (current_gfx_theme <= 0) return 0;
    return gfx_themes[current_gfx_theme].game_screenshot_y_start;
}

int gfx_theme_get_screenshot_y_end(void) {
    if (current_gfx_theme <= 0) return 0;
    return gfx_themes[current_gfx_theme].game_screenshot_y_end;
}

void gfx_theme_set_platform(const char* platform) {
    if (platform) {
        strncpy(current_platform, platform, MAX_PLATFORM_NAME_LEN - 1);
        current_platform[MAX_PLATFORM_NAME_LEN - 1] = '\0';
    } else {
        current_platform[0] = '\0';
    }
}

// Helper to load raw RGB565 320x240 background image file
static uint16_t* load_raw_rgb565_bg(const char* filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size != SCREEN_WIDTH * SCREEN_HEIGHT * (long)sizeof(uint16_t)) {
        fclose(fp);
        return NULL;
    }
    uint16_t *data = (uint16_t*)malloc(size);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return data;
}

// Helper to scale RGB565 image buffer to 320x240
static uint16_t* scale_rgb565_to_screen(const uint16_t *src, int src_w, int src_h) {
    if (!src || src_w <= 0 || src_h <= 0) return NULL;
    if (src_w == SCREEN_WIDTH && src_h == SCREEN_HEIGHT) {
        return (uint16_t*)src; // No scaling needed
    }
    uint16_t *dst = (uint16_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    if (!dst) return NULL;

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int sy = (y * src_h) / SCREEN_HEIGHT;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int sx = (x * src_w) / SCREEN_WIDTH;
            dst[y * SCREEN_WIDTH + x] = src[sy * src_w + sx];
        }
    }
    return dst;
}

// Helper to try loading any image format and scale to 320x240
static uint16_t* load_and_scale_bg_file(const char *filepath) {
    if (!filepath || access(filepath, F_OK) != 0) return NULL;

    uint16_t *data = NULL;
    int w = 0, h = 0;

    const char *ext = strrchr(filepath, '.');
    if (!ext) return NULL;

    if (strcasecmp(ext, ".rgb565") == 0) {
        data = load_raw_rgb565_bg(filepath);
        if (data) return data;
    } else if (strcasecmp(ext, ".png") == 0) {
        if (load_png_rgb565(filepath, &data, &w, &h)) {
            uint16_t *scaled = scale_rgb565_to_screen(data, w, h);
            if (scaled != data) free(data);
            return scaled;
        }
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        if (load_jpeg_rgb565(filepath, &data, &w, &h)) {
            uint16_t *scaled = scale_rgb565_to_screen(data, w, h);
            if (scaled != data) free(data);
            return scaled;
        }
    } else if (strcasecmp(ext, ".bmp") == 0) {
        if (load_bmp_rgb565(filepath, &data, &w, &h)) {
            uint16_t *scaled = scale_rgb565_to_screen(data, w, h);
            if (scaled != data) free(data);
            return scaled;
        }
    } else if (strcasecmp(ext, ".webp") == 0) {
        if (load_webp_rgb565(filepath, &data, &w, &h)) {
            uint16_t *scaled = scale_rgb565_to_screen(data, w, h);
            if (scaled != data) free(data);
            return scaled;
        }
    } else if (strcasecmp(ext, ".gif") == 0) {
        if (load_gif_rgb565(filepath, &data, &w, &h)) {
            uint16_t *scaled = scale_rgb565_to_screen(data, w, h);
            if (scaled != data) free(data);
            return scaled;
        }
    }

    return NULL;
}

// Try to load platform background dynamically based on folder name - 1 single path check
static uint16_t* try_load_dynamic_platform_bg(GfxTheme* theme, const char* platform) {
    if (!platform || !platform[0]) return NULL;

    char bg_path[MAX_THEME_PATH_LEN];
    uint16_t* data = NULL;

    char platform_lower[MAX_PLATFORM_NAME_LEN];
    strncpy(platform_lower, platform, MAX_PLATFORM_NAME_LEN - 1);
    platform_lower[MAX_PLATFORM_NAME_LEN - 1] = '\0';
    for (int i = 0; platform_lower[i]; i++) {
        if (platform_lower[i] >= 'A' && platform_lower[i] <= 'Z') {
            platform_lower[i] += 32;
        }
    }

    // 1 single theme path check if theme active
    if (theme && theme->path[0]) {
        snprintf(bg_path, sizeof(bg_path), "%s/resources/platforms/%s/background.png", theme->path, platform_lower);
        if ((data = load_and_scale_bg_file(bg_path)) != NULL) return data;
    }

    // 1 single ROMS path check
    snprintf(bg_path, sizeof(bg_path), "%s/%s/background.png", ROMS_PATH, platform_lower);
    return load_and_scale_bg_file(bg_path);
}

uint16_t* gfx_theme_get_platform_background(void) {
    if (current_gfx_theme <= 0) return NULL;

    GfxTheme* theme = &gfx_themes[current_gfx_theme];

    // If we have a current platform (folder), try to load its background dynamically
    if (current_platform[0]) {
        // Check if we already have this platform cached
        for (int i = 0; i < theme->num_platforms; i++) {
            if (strcasecmp(theme->platform_names[i], current_platform) == 0) {
                if (theme->platform_bg_data[i]) {
                    return theme->platform_bg_data[i];
                }
                // Already checked and has no custom background - return main theme background immediately!
                return gfx_theme_get_background();
            }
        }

        // Not cached - try to load dynamically once
        if (theme->num_platforms < MAX_PLATFORMS) {
            uint16_t* bg = try_load_dynamic_platform_bg(theme, current_platform);

            // Cache the result (even if NULL, to avoid retrying)
            int idx = theme->num_platforms;
            strncpy(theme->platform_names[idx], current_platform, MAX_PLATFORM_NAME_LEN - 1);
            theme->platform_names[idx][MAX_PLATFORM_NAME_LEN - 1] = '\0';
            theme->platform_bg_data[idx] = bg;
            theme->platform_bg_loaded[idx] = true;
            theme->num_platforms++;

            if (bg) return bg;
        }
    }

    // Fall back to main background
    return gfx_theme_get_background();
}

void gfx_theme_free_background(void) {
    // Close animated background if active
    if (main_bg_is_animated) {
        avi_bg_close();
        main_bg_is_animated = false;
        main_bg_avi_path[0] = '\0';
    }

    // v19: Free overlay
    if (main_bg_overlay_pixels) { free(main_bg_overlay_pixels); main_bg_overlay_pixels = NULL; }
    if (main_bg_overlay_alpha) { free(main_bg_overlay_alpha); main_bg_overlay_alpha = NULL; }
    // v28: Free pre-computed blend data
    if (overlay_blend_mode) { free(overlay_blend_mode); overlay_blend_mode = NULL; }
    main_bg_has_overlay = false;

    // v62: Free sections overlay
    if (sections_overlay_pixels) { free(sections_overlay_pixels); sections_overlay_pixels = NULL; }
    if (sections_overlay_alpha) { free(sections_overlay_alpha); sections_overlay_alpha = NULL; }
    if (sections_overlay_blend_mode) { free(sections_overlay_blend_mode); sections_overlay_blend_mode = NULL; }
    sections_has_overlay = false;

    for (int i = 0; i < num_gfx_themes; i++) {
        // v22: Always reset background_loaded flag (fixes animation not reloading after theme switch)
        gfx_themes[i].background_loaded = false;

        // Free main background
        if (gfx_themes[i].background_data) {
            free(gfx_themes[i].background_data);
            gfx_themes[i].background_data = NULL;
        }
        // Free platform backgrounds
        for (int p = 0; p < gfx_themes[i].num_platforms; p++) {
            if (gfx_themes[i].platform_bg_data[p]) {
                free(gfx_themes[i].platform_bg_data[p]);
                gfx_themes[i].platform_bg_data[p] = NULL;
            }
            gfx_themes[i].platform_bg_loaded[p] = false;
        }
        // v22: Reset platform counter
        gfx_themes[i].num_platforms = 0;

        // v36: Free theme logo
        if (gfx_themes[i].theme_logo_pixels) {
            free(gfx_themes[i].theme_logo_pixels);
            gfx_themes[i].theme_logo_pixels = NULL;
        }
        if (gfx_themes[i].theme_logo_alpha) {
            free(gfx_themes[i].theme_logo_alpha);
            gfx_themes[i].theme_logo_alpha = NULL;
        }
        gfx_themes[i].theme_logo_loaded = 0;
    }
}

void gfx_theme_cleanup(void) {
    gfx_theme_free_background();
    avi_bg_shutdown();
    current_gfx_theme = 0;

    // v19: Free composite buffer
    if (composite_buffer) {
        free(composite_buffer);
        composite_buffer = NULL;
    }
}

// v36: Get theme logo (resources/general/frogui_logo.png if it exists)
int gfx_theme_get_logo(uint16_t** pixels, uint8_t** alpha, int* width, int* height) {
    if (!pixels || !alpha || !width || !height) return 0;
    if (current_gfx_theme <= 0) return 0;  // No theme active

    GfxTheme* theme = &gfx_themes[current_gfx_theme];

    // Try to load if not loaded yet
    if (theme->theme_logo_loaded == 0 && theme->path[0]) {
        char logo_path[MAX_THEME_PATH_LEN];
        snprintf(logo_path, sizeof(logo_path), "%s/resources/general/frogui_logo.png", theme->path);

        int w, h;
        if (load_png_rgba565(logo_path, &theme->theme_logo_pixels, &theme->theme_logo_alpha, &w, &h)) {
            theme->theme_logo_width = w;
            theme->theme_logo_height = h;
            theme->theme_logo_loaded = 1;
        } else {
            theme->theme_logo_loaded = -1;  // Failed
        }
    }

    // Return logo if loaded
    if (theme->theme_logo_loaded == 1 && theme->theme_logo_pixels) {
        *pixels = theme->theme_logo_pixels;
        *alpha = theme->theme_logo_alpha;
        *width = theme->theme_logo_width;
        *height = theme->theme_logo_height;
        return 1;
    }

    return 0;  // No theme logo
}

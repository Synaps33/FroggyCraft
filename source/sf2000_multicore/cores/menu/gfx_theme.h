#ifndef GFX_THEME_H
#define GFX_THEME_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of GFX themes that can be loaded
#define MAX_GFX_THEMES 32
#define MAX_THEME_NAME_LEN 64
#define MAX_THEME_PATH_LEN 256
#define MAX_PLATFORMS 64
#define MAX_PLATFORM_NAME_LEN 32

// GFX Theme Layout - defines where UI elements are positioned
typedef struct {
    // Platform list (main ROMS menu)
    int platform_list_x;
    int platform_list_y_start;    // Top of list area
    int platform_list_y_end;      // Bottom of list area
    int platform_item_height;
    int platform_visible_items;   // Auto-calculated if 0

    // Game list (inside console folders)
    int game_list_x;
    int game_list_y_start;        // Top of list area
    int game_list_y_end;          // Bottom of list area
    int game_item_height;
    int game_visible_items;       // Auto-calculated if 0

    // Thumbnail area
    int thumb_x;
    int thumb_y;
    int thumb_width;
    int thumb_height;

    // Header
    int header_x;
    int header_y;

    // Legend (bottom bar with A/B/SELECT hints)
    int legend_x;
    int legend_y;

    // Counter (e.g., "3/125" in corner)
    int counter_x;
    int counter_y;
} GfxThemeLayout;

// GFX Theme - graphical theme with background image
typedef struct {
    char name[MAX_THEME_NAME_LEN];
    char path[MAX_THEME_PATH_LEN];          // Path to theme folder

    // Layout overrides (0 = use default)
    GfxThemeLayout layout;
    bool has_custom_layout;

    // Color overrides (0xFFFF = use color theme)
    uint16_t bg_color;
    uint16_t text_color;
    uint16_t select_bg_color;
    uint16_t select_text_color;
    bool has_custom_colors;

    // Background image data (loaded on demand)
    uint16_t* background_data;
    bool background_loaded;

    // Per-platform backgrounds - cached as loaded on-demand
    char platform_names[MAX_PLATFORMS][MAX_PLATFORM_NAME_LEN];
    uint16_t* platform_bg_data[MAX_PLATFORMS];
    bool platform_bg_loaded[MAX_PLATFORMS];  // true = tried loading (even if failed)
    int num_platforms;  // Number of cached platform entries

    // v20: Text background options
    // platform_text_background: 0 = shadow/outline (default), 1 = rounded black background
    // game_text_background: 0 = shadow/outline (default), 1 = rounded black background
    bool platform_text_background;
    bool game_text_background;

    // v32: Game screenshot area
    int game_screenshot_x_start;
    int game_screenshot_x_end;
    int game_screenshot_y_start;
    int game_screenshot_y_end;

    // Custom menu layout default from theme.ini (vertical, horizontal, 2_columns, 3_columns)
    char menu_layout[32];
    bool has_custom_menu_layout;
    char game_list_layout[32];
    bool has_custom_game_list_layout;
    bool hide_system_names;
    bool has_custom_hide_system_names;
    bool hide_header_text;
    bool has_custom_hide_header_text;
    bool hide_game_names;
    bool has_custom_hide_game_names;
    bool show_icons;
    bool has_custom_show_icons;
    bool show_game_icons;
    bool has_custom_show_game_icons;
    uint32_t game_name_color;
    bool has_custom_game_name_color;
    
    bool xmb_waves;
    bool has_custom_xmb_waves;
    uint32_t xmb_wave_color;
    bool has_custom_xmb_wave_color;
    int xmb_wave_glow;
    bool has_custom_xmb_wave_glow;
    int xmb_wave_variety;
    bool has_custom_xmb_wave_variety;
    
    bool text_in_empty_icon;
    bool has_custom_text_in_empty_icon;
    bool show_empty_icon_bg;
    bool has_custom_show_empty_icon_bg;
    bool show_selected_icon_bg;
    bool has_custom_show_selected_icon_bg;
    bool dim_unselected_icons;
    bool has_custom_dim_unselected_icons;

    // Custom horizontal menu Y height & tile dimensions from theme.ini
    int horizontal_y;
    int horizontal_tile_w;
    int horizontal_tile_h;
    int horizontal_item_spacing;

    // Custom grid menu dimensions and spacing from theme.ini
    int grid_cols;
    int grid_tile_w;
    int grid_tile_h;
    int grid_spacing_x;
    int grid_spacing_y;
    int grid_bottom_spacing;
    int grid_x;
    int grid_y;
    int grid_visible_rows;

    // Custom label positioning from theme.ini (separate for platform menu vs game list)
    int platform_label_y;          // Absolute Y coordinate (0 = use offset)
    int platform_label_offset_y;   // Y offset from tile bottom (default 12)
    int platform_label_offset_x;   // X offset (default 0)

    int game_label_y;              // Absolute Y coordinate (0 = use offset)
    int game_label_offset_y;       // Y offset from tile bottom (default 12)
    int game_label_offset_x;       // X offset (default 0)

    // Transition animation speed from theme.ini (0.0 - 1.0)
    float anim_speed;
    bool has_custom_anim_speed;

    char bg_anim_mode[32];
    bool has_custom_bg_anim_mode;

    // v36: Custom theme logo (resources/general/frogui_logo.png)
    uint16_t* theme_logo_pixels;
    uint8_t* theme_logo_alpha;
    int theme_logo_width;
    int theme_logo_height;
    int theme_logo_loaded;  // 0=not tried, 1=loaded, -1=failed
} GfxTheme;

// Get menu layout specified by active GFX theme (or NULL if none)
const char* gfx_theme_get_menu_layout(void);
const char* gfx_theme_get_game_list_layout(void);
bool gfx_theme_get_hide_system_names(void);
bool gfx_theme_get_hide_game_names(void);
bool gfx_theme_get_show_icons(void);
bool gfx_theme_get_show_game_icons(void);
float gfx_theme_get_anim_speed(void);
bool gfx_theme_has_custom_anim_speed(void);
const char* gfx_theme_get_bg_anim_mode(void);
bool gfx_theme_has_custom_bg_anim_mode(void);
int gfx_theme_get_horizontal_y(void);
int gfx_theme_get_horizontal_tile_w(void);
int gfx_theme_get_horizontal_tile_h(void);
int gfx_theme_get_horizontal_item_spacing(void);

// Grid menu getters
int gfx_theme_get_grid_cols(void);
int gfx_theme_get_grid_tile_w(int cols);
int gfx_theme_get_grid_tile_h(int cols);
int gfx_theme_get_grid_spacing_x(int cols);
int gfx_theme_get_grid_spacing_y(int cols);
int gfx_theme_get_grid_bottom_spacing(void);
int gfx_theme_get_grid_x(int cols);
int gfx_theme_get_grid_y(void);
int gfx_theme_get_grid_visible_rows(void);

bool gfx_theme_get_hide_header_text(void);
bool gfx_theme_has_custom_hide_header_text(void);
bool gfx_theme_get_hide_game_names(void);
bool gfx_theme_has_custom_hide_game_names(void);
bool gfx_theme_get_show_icons(void);
bool gfx_theme_has_custom_show_icons(void);
bool gfx_theme_get_show_game_icons(void);
bool gfx_theme_has_custom_show_game_icons(void);

uint32_t gfx_theme_get_game_name_color(void);
bool gfx_theme_has_custom_game_name_color(void);

bool gfx_theme_get_xmb_waves(void);
bool gfx_theme_has_custom_xmb_waves(void);
uint32_t gfx_theme_get_xmb_wave_color(void);
bool gfx_theme_has_custom_xmb_wave_color(void);
int gfx_theme_get_xmb_wave_glow(void);
bool gfx_theme_has_custom_xmb_wave_glow(void);
int gfx_theme_get_xmb_wave_variety(void);
bool gfx_theme_has_custom_xmb_wave_variety(void);

bool gfx_theme_get_show_selected_icon_bg(void);
bool gfx_theme_has_custom_show_selected_icon_bg(void);
bool gfx_theme_get_dim_unselected_icons(void);
bool gfx_theme_has_custom_dim_unselected_icons(void);

bool gfx_theme_get_hide_system_names(void);
bool gfx_theme_has_custom_hide_system_names(void);

bool gfx_theme_get_text_in_empty_icon(void);
bool gfx_theme_has_custom_text_in_empty_icon(void);
bool gfx_theme_get_show_empty_icon_bg(void);
bool gfx_theme_has_custom_show_empty_icon_bg(void);

int gfx_theme_get_platform_label_y(void);
int gfx_theme_get_platform_label_offset_y(void);
int gfx_theme_get_platform_label_offset_x(void);
int gfx_theme_get_game_label_y(void);
int gfx_theme_get_game_label_offset_y(void);
int gfx_theme_get_game_label_offset_x(void);

// Load platform or game logo if available
int gfx_theme_load_platform_logo(const char *platform_name, uint16_t **pixels, uint8_t **alpha, int *width, int *height);
int gfx_theme_load_entry_logo(const char *name, bool is_platform, uint16_t **pixels, uint8_t **alpha, int *width, int *height);
int gfx_theme_load_entry_logo_from_path(const char *path, uint16_t **pixels, uint8_t **alpha, int *width, int *height);

// Theme directory on SD card
#define GFX_THEMES_DIR "/mnt/sda1/THEMES"

// Initialize GFX theme system
void gfx_theme_init(void);

// Scan THEMES directory for available themes
int gfx_theme_scan(void);

// Get number of available GFX themes
int gfx_theme_count(void);

// Get theme name by index (0 = "None/Disabled")
const char* gfx_theme_get_name(int index);

// Apply GFX theme by index (0 = disable GFX themes)
int gfx_theme_apply(int index);

// Apply GFX theme by name - loads on-demand from filesystem
int gfx_theme_apply_by_name(const char* name);

// Get current GFX theme index
int gfx_theme_get_current_index(void);

// Check if GFX theme is active
bool gfx_theme_is_active(void);

// Get current GFX theme (NULL if none active)
const GfxTheme* gfx_theme_get_current(void);

// Get current layout (returns default if no custom layout)
const GfxThemeLayout* gfx_theme_get_layout(void);

// Get background image data (loads if not loaded yet)
// Returns NULL if no background or load failed
uint16_t* gfx_theme_get_background(void);

// v61: Apply PNG overlay to framebuffer (for animated backgrounds)
// Call after drawing images/thumbnails, before text
void gfx_theme_apply_overlay(uint16_t* framebuffer);

// Set current platform for per-platform backgrounds (e.g., "nes", "gba")
void gfx_theme_set_platform(const char* platform);

// Get background for current platform (falls back to main if no platform-specific)
uint16_t* gfx_theme_get_platform_background(void);

// Free background image data
void gfx_theme_free_background(void);

// Cleanup all GFX theme resources
void gfx_theme_cleanup(void);

// Animated background support (for background.avi)
// Check if main background is animated (AVI)
bool gfx_theme_is_animated(void);

// Advance animation frame - call at 15fps rate (~67ms)
void gfx_theme_advance_animation(void);

// Pause animation (e.g., when entering platform folder with static PNG)
void gfx_theme_pause_animation(void);

// Resume animation (e.g., when returning to main menu)
void gfx_theme_resume_animation(void);

// Returns -1 for Left/Up, 1 for Right/Down
void gfx_theme_set_bg_anim_direction(int dir);
int gfx_theme_get_bg_anim_direction(void);

// v20: Text background style options
// Returns true if menu items should have rounded black background instead of shadow
bool gfx_theme_platform_text_background(void);
// Returns true if section items (platforms) should have rounded black background
bool gfx_theme_game_text_background(void);

// v32: Get game screenshot area (returns 0 if not configured or disabled)
int gfx_theme_get_screenshot_x_start(void);
int gfx_theme_get_screenshot_x_end(void);
int gfx_theme_get_screenshot_y_start(void);
int gfx_theme_get_screenshot_y_end(void);

// v36: Get theme logo (resources/general/frogui_logo.png if it exists)
// Returns 1 if logo available, fills out parameters
// Returns 0 if no theme logo (use built-in)
int gfx_theme_get_logo(uint16_t** pixels, uint8_t** alpha, int* width, int* height);

// Default layout constants

// Platform list (main menu) - left side
#define DEFAULT_PLATFORM_LIST_X         16
#define DEFAULT_PLATFORM_LIST_Y_START   40
#define DEFAULT_PLATFORM_LIST_Y_END     208
#define DEFAULT_PLATFORM_ITEM_HEIGHT    24
#define DEFAULT_PLATFORM_VISIBLE_ITEMS  7

// Game list (inside folders) - can be different position
#define DEFAULT_GAME_LIST_X             16
#define DEFAULT_GAME_LIST_Y_START       40
#define DEFAULT_GAME_LIST_Y_END         208
#define DEFAULT_GAME_ITEM_HEIGHT        24
#define DEFAULT_GAME_VISIBLE_ITEMS      7

// Thumbnail
#define DEFAULT_THUMB_X                 160
#define DEFAULT_THUMB_Y                 40
#define DEFAULT_THUMB_WIDTH             150
#define DEFAULT_THUMB_HEIGHT            180

// Header
#define DEFAULT_HEADER_X                16
#define DEFAULT_HEADER_Y                10

// Legend (bottom bar)
#define DEFAULT_LEGEND_X                16
#define DEFAULT_LEGEND_Y                220

// Counter position (top right)
#define DEFAULT_COUNTER_X               308
#define DEFAULT_COUNTER_Y               8

#endif // GFX_THEME_H

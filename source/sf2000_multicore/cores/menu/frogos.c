/*
 * FrogUI - MinUI-style File Browser for Multicore
 * A libretro core that provides a file browser interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#ifdef SF2000

#include "../../stockfw.h"

// Direct call to loader - bypasses run_gba
typedef void (*loader_func_t)(const char*, int);
#define LOADER_ADDR 0x80001500
static loader_func_t direct_loader = (loader_func_t)LOADER_ADDR;

// For SF2000, use the custom dirent implementation
#include "../../dirent.h"
#else
#include <dirent.h>
#endif

#include "libretro.h"
#include "font.h"
#include "render.h"
#include "theme.h"
#include "gfx_theme.h"
#include "recent_games.h"
#include "favorites.h"
#include "settings.h"
#include "display_opts.h"
#include "osk.h"
#include "text_editor.h"
#include "frogui_logo_data.h"
#include "lodepng.h"
#include "video_browser.h"
#include "video_player.h"
#include "music_player.h"
#include "image_viewer.h"
#include "calculator.h"
#include "filemanager.h"

// Console to core name mapping (from buildcoresworking.sh)
typedef struct {
    const char *console_name;
    const char *core_name;
} ConsoleMapping;

static const ConsoleMapping console_mappings[] = {
    // Handhelds
    {"gb", "gambatte"},
    {"gbb", "TGB Dual"}, 
    {"gbgb", "Gearboy"},
    {"dblcherrygb", "DoubleCherryGB"},
    {"gba", "gpsp"},
    {"gbaf", "gpsp"}, 
    {"gbaff", "gpsp"},
    {"gbav", "VBA Next"},
    {"mgba", "mGBA"},
    {"gg", "Gearsystem"},
    {"ngpc", "RACE"},
    {"lnx", "Handy"},
    {"lnxb", "Beetle Lynx"},
    {"wswan", "Beetle WonderSwan"},
    {"wsv", "Potator"},
    {"pokem", "PokeMini"},
    {"gw", "Game & Watch"},

    // Nintendo
    {"nes", "FCEUmm"},
    {"nesq", "QuickNES"},
    {"nesf", "FCEUmm"},
    {"nest", "Nestopia"},
    {"snes", "Snes9x 2005"},
    {"snes02", "Snes9x 2002"},
    {"snesm", "Mednafen-SNES"},
    {"vb", "Beetle VB"},

    // Sega
    {"sega", "PicoDrive"},
    {"gpgx", "Genesis Plus GX"},
    {"segacd", "PicoDrive"},
    {"32x", "PicoDrive"},
    {"scd", "Genesis Plus GX"},

    // NEC
    {"pce", "Beetle PCE Fast"},
    {"pcesgx", "Beetle SuperGrafx"},
    {"pcfx", "Beetle PC-FX"},

    // Atari
    {"a26", "Stella 2014"},
    {"a5200", "a5200"},
    {"a78", "ProSystem"},
    {"a800", "Atari 800 (libatari800)"},
    {"atari", "Hatari"},

    // Arcade & others
    {"m2k", "MAME 2000"},
    {"mam", "MAME 2000"},
    {"mame", "MAME 2000"},
    {"geolith", "Geolith"},
    {"col", "Gearcoleco"},
    {"int", "FreeInv"},
    {"vec", "VecX"},
    {"fcf", "FreeChaF"},
    {"arc", "Geolith"},
    {"neo", "Geolith"},
    {"cps", "Geolith"},
    {"fba", "Geolith"},

    // Computers
    {"c64", "VICE x64"},
    {"c64sc", "VICE x64sc"},
    {"c64f", "Frodo"},
    {"c64fc", "Frodo"},
    {"vic20", "VICE xvic"},
    {"amstradb", "cap32"},
    {"amstrad", "crocods"},
    {"spec", "fuse"},
    {"zx81", "EightyOne"},
    {"thom", "theodore"},
    {"msx", "blueMSX"},
    {"pc8800", "QUASI88"},
    {"xmil", "xmil"},
    {"x1", "x1"},
    {"x48", "x48"},
    {"bk", "bk"},
    {"o2em", "O2EM"},
    {"amiga", "UAE4ALL"},
    {"puf", "PUAE2021"},
    {"dos", "DOSBox-SVN"},
    {"dosb", "DOSBox-SVN"},

    // Engines & Games
    {"2048", "2048"},
    {"gong", "gong"},
    {"prboom", "PrBoom"},
    {"doom", "PrBoom"},
    {"doomp", "PrBoom-Plus"},
    {"wolf3d", "ecwolf"},
    {"wolf", "ecwolf"},
    {"xrick", "xrick"},
    {"outrun", "Cannonball"},
    {"flashback", "REminiscence"},
    {"fheroes2", "Free Heroes II"},
    {"homm2", "Free Heroes II"},
    {"jnb", "Jump 'n Bump"},
    {"cavestory", "NXEngine"},
    {"nx", "NXEngine"},
    {"nxb", "NXEngine"},
    {"quake", "TyrQuake"},
    {"tyrquake", "TyrQuake"},
    {"tyr", "TyrQuake"},
    {"quake2", "quake2"},
    {"duke3d", "Duke3D"},
    {"duke", "Duke3D"},
    {"eduke3d", "Duke3D"},
    {"openlara", "openlara"},
    {"fake08", "fake-08"},
    {"pico8", "fake-08"},
    {"retro8", "retro-8 (alpha)"},
    {"lowres-nx", "LowRes NX"},
    {"lowres", "LowRes NX"},
    {"chip8", "JAXE"},
    {"vapor", "Vaporspec"},
    {"arduboy", "arduous"},
    {"ard", "arduous"},
    {"cdg", "pocketcdg"},
    {"gme", "Game Music Emulator"},
    {"lutro", "lutro"},
    {"videos", "WAV player"},
    {"psx", "pcsx4all"},
    {"qpsx", "pcsx4all"},
    {"psxb", "Beetle-PSX"},
    {"js", "js2000"},
    {"js2000", "js2000"}
};

// Get core name for a console folder
// v24: Made non-static for use by display_opts.c
const char* get_core_name_for_console(const char* console_name) {
    int mapping_count = sizeof(console_mappings) / sizeof(console_mappings[0]);
    for (int i = 0; i < mapping_count; i++) {
        if (strcmp(console_mappings[i].console_name, console_name) == 0) {
            return console_mappings[i].core_name;
        }
    }
    return NULL; // Unknown console
}

// v24: show_core_settings removed - now handled by display_opts

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define MAX_PATH_LEN 512
#define ROMS_PATH "/mnt/sda1/ROMS"
#define HISTORY_FILE "/mnt/sda1/game_history.txt"
#define MAX_RECENT_GAMES 10
#define INITIAL_ENTRIES_CAPACITY 64

// Empty folders cache - avoid rescanning on every navigation
#define EMPTY_DIRS_CACHE_FILE "/mnt/sda1/configs/frogui_empty_dirs.cache"
#define MAX_EMPTY_DIRS 256
static char empty_dirs[MAX_EMPTY_DIRS][64];  // Store folder names (not full paths)
static int empty_dirs_count = 0;
static int empty_dirs_loaded = 0;

// v30: Header logo (decoded from embedded PNG)
static uint16_t *header_logo_pixels = NULL;
static uint8_t *header_logo_alpha = NULL;
static int header_logo_width = 0;
static int header_logo_height = 0;
static int header_logo_loaded = 0;

// Forward declarations
static void rebuild_empty_dirs_cache(void);
static void show_cache_rebuild_screen(void);

// v30: Decode header logo from embedded PNG
static void decode_header_logo(void) {
    if (header_logo_loaded) return;

    unsigned char *rgba = NULL;
    unsigned w, h;

    unsigned error = lodepng_decode32(&rgba, &w, &h, frogui_logo_png, frogui_logo_png_size);
    if (error || !rgba) {
        header_logo_loaded = -1;
        return;
    }

    header_logo_width = (int)w;
    header_logo_height = (int)h;

    header_logo_pixels = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    header_logo_alpha = (uint8_t*)malloc(w * h);

    if (!header_logo_pixels || !header_logo_alpha) {
        free(rgba);
        if (header_logo_pixels) { free(header_logo_pixels); header_logo_pixels = NULL; }
        if (header_logo_alpha) { free(header_logo_alpha); header_logo_alpha = NULL; }
        header_logo_loaded = -1;
        return;
    }

    for (unsigned i = 0; i < w * h; i++) {
        unsigned char r = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];

        header_logo_pixels[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        header_logo_alpha[i] = a;
    }

    free(rgba);
    header_logo_loaded = 1;
}

// v30: Draw header logo with alpha blending, returns width drawn (or 0 if failed)
static int draw_header_logo(uint16_t *framebuffer, int x, int y) {
    if (!header_logo_loaded) {
        decode_header_logo();
    }

    if (!header_logo_pixels || header_logo_loaded != 1) {
        return 0;  // Failed, caller should use text fallback
    }

    for (int sy = 0; sy < header_logo_height; sy++) {
        int screen_y = y + sy;
        if (screen_y < 0 || screen_y >= SCREEN_HEIGHT) continue;

        for (int sx = 0; sx < header_logo_width; sx++) {
            int screen_x = x + sx;
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            int src_idx = sy * header_logo_width + sx;
            uint8_t alpha = header_logo_alpha ? header_logo_alpha[src_idx] : 255;

            if (alpha > 128) {
                framebuffer[screen_y * SCREEN_WIDTH + screen_x] = header_logo_pixels[src_idx];
            }
        }
    }

    return header_logo_width;
}

// Load empty directories cache from file (or rebuild if missing)
static void load_empty_dirs_cache(void) {
    if (empty_dirs_loaded) return;
    empty_dirs_loaded = 1;
    empty_dirs_count = 0;

    FILE *fp = fopen(EMPTY_DIRS_CACHE_FILE, "r");
    if (!fp) {
        // Cache file doesn't exist - rebuild it
#ifdef SF2000
        xlog("Empty dirs cache: file not found, rebuilding...\n");
#else
        printf("Empty dirs cache: file not found, rebuilding...\n");
#endif
        rebuild_empty_dirs_cache();
        return;
    }

    char line[64];
    while (fgets(line, sizeof(line), fp) && empty_dirs_count < MAX_EMPTY_DIRS) {
        // Remove newline
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (line[0] != '\0') {
            strncpy(empty_dirs[empty_dirs_count], line, sizeof(empty_dirs[0]) - 1);
            empty_dirs[empty_dirs_count][sizeof(empty_dirs[0]) - 1] = '\0';
            empty_dirs_count++;
        }
    }
    fclose(fp);
#ifdef SF2000
    xlog("Empty dirs cache: loaded %d entries\n", empty_dirs_count);
#else
    printf("Empty dirs cache: loaded %d entries\n", empty_dirs_count);
#endif
}

// Check if a folder name is in the empty dirs cache
static int is_in_empty_cache(const char *folder_name) {
    for (int i = 0; i < empty_dirs_count; i++) {
        if (strcasecmp(empty_dirs[i], folder_name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Rebuild and save empty directories cache by scanning ROMS folder
static void rebuild_empty_dirs_cache(void) {
    show_cache_rebuild_screen();
    empty_dirs_count = 0;

    DIR *dir = opendir(ROMS_PATH);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && empty_dirs_count < MAX_EMPTY_DIRS) {
        if (ent->d_name[0] == '.') continue;
        if (strcasecmp(ent->d_name, "frogui") == 0 ||
            strcasecmp(ent->d_name, "saves") == 0 ||
            strcasecmp(ent->d_name, "save") == 0) continue;

        // Skip non-directories using d_type (avoids stat() syscall)
        if (ent->d_type != DT_DIR) continue;

        // Save entry name BEFORE inner readdir (readdir uses static buffer!)
        char entry_name[64];
        strncpy(entry_name, ent->d_name, sizeof(entry_name) - 1);
        entry_name[sizeof(entry_name) - 1] = '\0';

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", ROMS_PATH, entry_name);

        // Check if directory is empty via opendir/readdir
        DIR *check = opendir(full_path);
        if (check) {
            int has_content = 0;
            struct dirent *sub;
            while ((sub = readdir(check)) != NULL) {
                if (sub->d_name[0] != '.') {
                    has_content = 1;
                    break;
                }
            }
            closedir(check);

            if (!has_content) {
                strncpy(empty_dirs[empty_dirs_count], entry_name, sizeof(empty_dirs[0]) - 1);
                empty_dirs[empty_dirs_count][sizeof(empty_dirs[0]) - 1] = '\0';
                empty_dirs_count++;
            }
        }
    }
    closedir(dir);

    // Save to file
    FILE *fp = fopen(EMPTY_DIRS_CACHE_FILE, "w");
    if (fp) {
        for (int i = 0; i < empty_dirs_count; i++) {
            fprintf(fp, "%s\n", empty_dirs[i]);
        }
        fclose(fp);
    }
#ifdef SF2000
    xlog("Empty dirs cache: rebuilt with %d entries\n", empty_dirs_count);
#else
    printf("Empty dirs cache: rebuilt with %d entries\n", empty_dirs_count);
#endif
}

// Layout constants are now in render.h

// Thumbnail cache
static Thumbnail current_thumbnail;
static char cached_thumbnail_path[MAX_PATH_LEN];
static int thumbnail_cache_valid = 0;
static int last_selected_index = -1;

// v32: Screenshot cache (game_name.png in same folder as game)
static Thumbnail current_screenshot;
static char cached_screenshot_path[MAX_PATH_LEN];
static int screenshot_cache_valid = 0;

// Text scrolling state
static int text_scroll_frame_counter = 0;
static int text_scroll_offset = 0;
static int text_scroll_direction = 1;

// Menu state
typedef struct {
    char path[MAX_PATH_LEN];
    char name[256];
    int is_dir;
    // v44: Cached thumbnail path (avoids repeated filesystem lookups)
    // thumb_checked: 0=not checked, 1=checked and found, -1=checked but not found
    int thumb_checked;
    char thumb_path[MAX_PATH_LEN];
    // v52: Cached screenshot path (built during directory scan)
    // screenshot_checked: 0=not checked, 1=found, -1=not found
    int screenshot_checked;
    char screenshot_path[MAX_PATH_LEN];
} MenuEntry;

static MenuEntry *entries = NULL;
static int entry_count = 0;
static int entries_capacity = 0;
static int selected_index = 0;
static int scroll_offset = 0;
static char current_path[MAX_PATH_LEN];
static uint16_t *framebuffer = NULL;

// v43: Main sections (switchable with left/right at top)
// v77: TOOLS at end
typedef enum {
    SECTION_SYSTEMS = 0,
    SECTION_MUSIC,
    SECTION_VIDEOS,
    SECTION_IMAGES,
    SECTION_TEXT,
    SECTION_TOOLS,
    SECTION_COUNT
} MainSection;

static const char* section_names[SECTION_COUNT] = {
    "SYSTEMS",
    "MUSIC",
    "VIDEOS",
    "IMAGES",
    "TEXT",
    "TOOLS"
};

static MainSection current_section = SECTION_SYSTEMS;
static int header_selected = 0;  // 1 if header is selected (for section switching)

// ============== BUILT-IN BITMAP FONT (5x7, from pmp) ==============
static const unsigned char builtin_font[96][5] = {
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

// v31: Draw single character with built-in bitmap font (with black outline)
static void fps_draw_char(uint16_t *pixels, int x, int y, char c, uint16_t col) {
    if (c < 32 || c > 127) c = '?';
    const unsigned char *g = builtin_font[c - 32];
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
                    if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                        int ox = cx + dx[d], oy = cy + dy[d];
                        if (ox < 0 || ox >= 5 || oy < 0 || oy >= 7 || !(g[ox] & (1 << oy))) {
                            pixels[py * SCREEN_WIDTH + px] = outline;
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
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    pixels[py * SCREEN_WIDTH + px] = col;
                }
            }
        }
    }
}

// v31: Draw string with built-in bitmap font
static void fps_draw_str(uint16_t *pixels, int x, int y, const char *s, uint16_t col) {
    while (*s) { fps_draw_char(pixels, x, y, *s++, col); x += 6; }
}

// ============== FPS COUNTER (from QPSX) ==============
// v32: fps_show is now read from settings dynamically
static int fps_current = 0;
static int fps_frame_count = 0;
static unsigned long fps_last_time = 0;

// Average FPS over last 40 seconds for better precision
#define FPS_AVG_SAMPLES 40
static int fps_history[FPS_AVG_SAMPLES];
static int fps_history_idx = 0;
static int fps_history_count = 0;
static int fps_avg_x100 = 0;  // Average * 100 for 2 decimal precision

// Get time in milliseconds
static unsigned long get_time_ms(void) {
    return (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
}

// FPS overlay colors (RGB565)
#define FPS_BG    0x0000  // Black background
#define FPS_GOOD  0x07E0  // Green
#define FPS_OK    0xFFE0  // Yellow
#define FPS_BAD   0xF800  // Red
#define FPS_DIM   0x8410  // Gray for average

// Update FPS counter - call every frame
static void update_fps_counter(void) {
    fps_frame_count++;

    unsigned long now = get_time_ms();
    if (fps_last_time == 0) {
        fps_last_time = now;
        return;
    }

    unsigned long elapsed = now - fps_last_time;
    if (elapsed >= 1000) {
        fps_current = (fps_frame_count * 1000) / elapsed;
        fps_frame_count = 0;

        // Update average FPS (ring buffer)
        fps_history[fps_history_idx] = fps_current;
        fps_history_idx = (fps_history_idx + 1) % FPS_AVG_SAMPLES;
        if (fps_history_count < FPS_AVG_SAMPLES) fps_history_count++;

        // Calculate average * 100 for 2 decimal precision
        int sum = 0;
        for (int i = 0; i < fps_history_count; i++) sum += fps_history[i];
        fps_avg_x100 = (sum * 100) / fps_history_count;

        fps_last_time = now;
    }
}

// Draw FPS overlay in top-right corner (v32: restored background box)
static void draw_fps_overlay(uint16_t *pixels) {
    if (!pixels) return;

    // v32: Check setting for FPS display
    const char *fps_setting = settings_get_value("frogui_show_fps");
    if (!fps_setting || strcmp(fps_setting, "true") != 0) return;

    // Choose color based on FPS (target is ~30 for menu)
    uint16_t col;
    if (fps_current >= 27) col = FPS_GOOD;
    else if (fps_current >= 20) col = FPS_OK;
    else col = FPS_BAD;

    // v32: Draw background box (restored)
    render_fill_rect(pixels, 280, 1, 39, 20, FPS_BG);

    // Draw current FPS with built-in font
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", fps_current);
    fps_draw_str(pixels, 300, 2, buf, col);

    // Draw average FPS with 2 decimals (line 2)
    int avg_int = fps_avg_x100 / 100;
    int avg_dec = fps_avg_x100 % 100;
    snprintf(buf, sizeof(buf), "~%d.%02d", avg_int, avg_dec);
    fps_draw_str(pixels, 282, 11, buf, FPS_DIM);
}

// Boundary scroll delay (frames to wait before wrapping)
#define BOUNDARY_DELAY_FRAMES 30
static int boundary_delay_timer = 0;
static int at_boundary = 0; // 1 = at top, 2 = at bottom

// A-Z picker state
static int az_picker_active = 0;
static int az_selected_index = 0; // 0-25 for A-Z, 26 for 0-9, 27 for #

// Core picker state
static int letter_search_active = 0;
static char letter_search_char = 'A';

// v79: File manager hold timer (6 seconds = 6000 ms)
static uint32_t fm_hold_start = 0;
static int fm_hold_active = 0;
#define FM_HOLD_MS 6000

// Ensure entries array has enough capacity
static void ensure_entries_capacity(int required_capacity) {
    if (entries_capacity >= required_capacity) {
        return; // Already have enough capacity
    }

    // Double capacity each time, or use initial capacity
    int new_capacity = entries_capacity == 0 ? INITIAL_ENTRIES_CAPACITY : entries_capacity;
    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }

    MenuEntry *new_entries = (MenuEntry*)realloc(entries, new_capacity * sizeof(MenuEntry));
    if (!new_entries) {
        // Memory allocation failed - keep old array
        return;
    }

    // v44: Zero-initialize new entries (important for thumb_checked cache)
    if (new_capacity > entries_capacity) {
        memset(&new_entries[entries_capacity], 0, (new_capacity - entries_capacity) * sizeof(MenuEntry));
    }

    entries = new_entries;
    entries_capacity = new_capacity;
}

// Helper to get effective layout mode considering theme.ini and settings override for Systems vs Game List
static const char* get_effective_menu_layout(void) {
    if (render_is_in_platform_menu()) {
        const char *setting = settings_get_value("frogui_menu_layout");
        if (!setting) setting = settings_get_value("frogui_grid_navigation");

        if (setting && strcmp(setting, "theme_default") != 0 && strcmp(setting, "disabled") != 0) {
            return setting;
        }

        const char *theme_layout = gfx_theme_get_menu_layout();
        if (theme_layout && theme_layout[0] != '\0') {
            return theme_layout;
        }
    } else {
        const char *setting = settings_get_value("frogui_game_list_layout");

        if (setting && strcmp(setting, "theme_default") != 0) {
            return setting;
        }

        const char *theme_layout = gfx_theme_get_game_list_layout();
        if (theme_layout && theme_layout[0] != '\0') {
            return theme_layout;
        }
    }

    return "vertical";
}

// Helper to check if horizontal carousel menu is active
static int is_horizontal_menu(void) {
    const char *layout = get_effective_menu_layout();
    return (layout && strcmp(layout, "horizontal") == 0);
}

// Smooth animation interpolation state for horizontal carousel
static float anim_selected_index = 0.0f;

// Helper to get grid columns count from settings
static int get_grid_cols(void) {
    const char *layout = get_effective_menu_layout();
    if (!layout) return 1;
    if (strcmp(layout, "3_columns") == 0 || strcmp(layout, "grid_3_columns") == 0 || strcmp(layout, "grid_3") == 0) return 3;
    if (strcmp(layout, "2_columns") == 0 || strcmp(layout, "grid_2_columns") == 0 || strcmp(layout, "grid_2") == 0) return 2;
    if (strcmp(layout, "grid") == 0) {
        int c = gfx_theme_get_grid_cols();
        return (c == 3) ? 3 : 2;
    }
    return 1;
}

// Reset navigation state when entering new folder
static void reset_navigation_state(void) {
    selected_index = 0;
    anim_selected_index = 0.0f;
    scroll_offset = 0;
    boundary_delay_timer = 0;
    at_boundary = 0;
}

// Libretro callbacks
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

// Input state
static int prev_input[16] = {0};
static bool game_queued = false;  // Flag to indicate game is queued
bool show_multicore_opt = false;  // Flag to indicate showing multicore.opt
bool resume_on_boot = false;
bool hide_empty_folders = true;

// Helper to get game core override
static void get_game_core_override(const char *sys_name, const char *filename, char *out_core, size_t out_size) {
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "/mnt/sda1/configs/custom_cores/%s/games.opt", sys_name);
    FILE *f = fopen(config_path, "r");
    if (!f) return;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *sep = strchr(line, '|');
        if (sep) {
            *sep = '\0';
            if (strcmp(line, filename) == 0) {
                strncpy(out_core, sep + 1, out_size - 1);
                out_core[out_size - 1] = '\0';
                break;
            }
        }
    }
    fclose(f);
}

// Helper to set game core override
static void set_game_core_override(const char *sys_name, const char *filename, const char *core_name) {
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "/mnt/sda1/configs/custom_cores/%s/games.opt", sys_name);
    
    // Ensure directory exists
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "/mnt/sda1/configs/custom_cores/%s", sys_name);
    extern void ensure_directory(const char *path);
    ensure_directory(dir_path);
    
    // Read all existing entries to memory
    char entries[256][256];
    int count = 0;
    
    FILE *f = fopen(config_path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && count < 256) {
            line[strcspn(line, "\r\n")] = 0;
            char *sep = strchr(line, '|');
            if (sep) {
                *sep = '\0';
                // Skip if it's the game we are overriding (we will write it later)
                if (strcmp(line, filename) != 0) {
                    snprintf(entries[count++], sizeof(entries[0]), "%s|%s", line, sep + 1);
                }
            }
        }
        fclose(f);
    }
    
    // Write them back along with the new override
    f = fopen(config_path, "w");
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s\n", entries[i]);
        }
        // If core_name is "Default", we don't write it (it deletes the override)
        if (strcmp(core_name, "Default") != 0) {
            fprintf(f, "%s|%s\n", filename, core_name);
        }
        fclose(f);
    }
}

void init_direct_loader(const char* core_name, const char* directory, const char* filename) {
#ifdef SF2000
    // Don't set ptr_gs_run_folder - currently inherit from menu core for savestates to work
    // TODO: Find a way to force ptr_gs_run_folder to be /mnt/sda1/ROMS or /mnt/sda1/ARCADE (unified save states folder)
    // TODO: Replace second core_name with full directory (besides /mnt/sda1) and seperate core_name from directory
    sprintf((char *)ptr_gs_run_game_file, "%s;%s;%s.gba", core_name, directory, filename); // Emulate a stub directory, stub doesn't have to exist (loader fixes later)
    sprintf((char *)ptr_gs_run_game_name, "%s", filename); // Expects the filename without any extension

    // Remove extension from ptr_gs_run_game_name
    char *dot_position = strrchr((char*)ptr_gs_run_game_name, '.');
    if (dot_position != NULL) {
        *dot_position = '\0';
    }
#endif

    // Add to recent history
    recent_games_add(core_name, filename, directory);

    game_queued = true; // Pass to retro_run, can only run the loader from there
}

// Apply settings changes
void apply_settings(void) {
    const char *resume_val = settings_get_value("frogui_resume_on_boot");
    if (resume_val) {
        if (strcmp(resume_val, "false") == 0) resume_on_boot = false;
        else if (strcmp(resume_val, "true") == 0) resume_on_boot = true;
    }

    const char *hide_val = settings_get_value("frogui_hide_empty");
    if (hide_val) {
        if (strcmp(hide_val, "false") == 0) hide_empty_folders = false;
        else if (strcmp(hide_val, "true") == 0) hide_empty_folders = true;
    }
}

static void get_corename(const char *path, char *core_name, size_t size) {
    const char *prefix = "/mnt/sda1/ROMS/";
    const char *start;
    const char *end;

    // Skip prefix if present
    if (strncmp(path, prefix, strlen(prefix)) == 0)
        start = path + strlen(prefix);
    else
        start = path;

    // Find end of the first directory
    end = strchr(start, '/');
    if (!end) {
        // No further '/', take the rest of the string
        strncpy(core_name, start, size - 1);
        core_name[size - 1] = '\0';
    } else {
        size_t len = end - start;
        if (len >= size) len = size - 1;
        strncpy(core_name, start, len);
        core_name[len] = '\0';
    }
}

void clean_path(char *path)
{
    const char *prefix = "/mnt/sda1/ROMS/";
    size_t len = strlen(prefix);

    // remove prefix
    if (strncmp(path, prefix, len) == 0) {
        memmove(path, path + len, strlen(path + len) + 1);
    }

    // remove filename
    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
}

// Show a loading screen during cache rebuild
static void show_cache_rebuild_screen(void) {
    if (!framebuffer || !video_cb) return;

    // Fill background
    render_fill_rect(framebuffer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, theme_bg());

    // Draw centered message
    const char* msg = "Rebuilding folder cache...";
    int text_width = font_measure_text(msg);
    int x = (SCREEN_WIDTH - text_width) / 2;
    int y = (SCREEN_HEIGHT - FONT_CHAR_HEIGHT) / 2;
    render_text_pillbox(framebuffer, x, y, msg, theme_header(), theme_bg(), 6);

    // Push frame to display
    video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
}

// Get the base name from a path
static const char *get_basename(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Auto-launch most recent game if resume on boot is enabled
static void auto_launch_recent_game(void) {
    // Get the most recent game
    const RecentGame *recent_list = recent_games_get_list();
    int recent_count = recent_games_get_count();

    if (recent_count == 0) {
        return; // No recent games to launch
    }

    // Get the first (most recent) game
    const RecentGame *game = &recent_list[0];

    init_direct_loader(game->core_name, game->full_path, game->game_name);
}

// Get scrolling display text for selected item
static void get_scrolling_text(const char *full_name, int is_selected, char *display_name, size_t display_size) {
    if (!full_name || !display_name) return;

    int name_len = strlen(full_name);

    // Check if we're in main menu or special views (no thumbnails)
    int in_main_menu = (strcmp(current_path, ROMS_PATH) == 0 ||
                        strcmp(current_path, "RECENT_GAMES") == 0 ||
                        strcmp(current_path, "FAVORITES") == 0 ||
                        strcmp(current_path, "TOOLS") == 0 ||
                        strcmp(current_path, "UTILS") == 0 ||
                        strcmp(current_path, "HOTKEYS") == 0 ||
                        strcmp(current_path, "CREDITS") == 0);

    // Use different max lengths: shorter for unselected items only in ROM lists (with thumbnails)
    int max_len = is_selected ? MAX_FILENAME_DISPLAY_LEN :
                  (in_main_menu ? MAX_FILENAME_DISPLAY_LEN : MAX_UNSELECTED_DISPLAY_LEN);

    // If short enough or not selected, just copy/truncate normally
    if (name_len <= max_len || !is_selected) {
        if (name_len <= max_len) {
            strcpy(display_name, full_name);
        } else {
            strncpy(display_name, full_name, max_len);
            display_name[max_len] = '\0';
            strcat(display_name, "...");
        }
        return;
    }
    
    // Handle scrolling for selected long names
    text_scroll_frame_counter++;
    
    // Wait before starting scroll
    if (text_scroll_frame_counter < SCROLL_DELAY_FRAMES) {
        strncpy(display_name, full_name, MAX_FILENAME_DISPLAY_LEN);
        display_name[MAX_FILENAME_DISPLAY_LEN] = '\0';
        return;
    }
    
    // Update scroll position
    if (text_scroll_frame_counter % SCROLL_SPEED_FRAMES == 0) {
        text_scroll_offset += text_scroll_direction;
        
        // Reverse direction at ends
        int max_scroll = name_len - MAX_FILENAME_DISPLAY_LEN;
        if (text_scroll_offset >= max_scroll) {
            text_scroll_direction = -1;
            text_scroll_offset = max_scroll;
        } else if (text_scroll_offset <= 0) {
            text_scroll_direction = 1;
            text_scroll_offset = 0;
        }
    }
    
    // Extract scrolled portion
    int copy_len = min(MAX_FILENAME_DISPLAY_LEN, name_len - text_scroll_offset);
    strncpy(display_name, full_name + text_scroll_offset, copy_len);
    display_name[copy_len] = '\0';
}

// Load thumbnail for currently selected item
static void load_current_thumbnail() {
    if (selected_index < 0 || selected_index >= entry_count || entry_count == 0) {
        thumbnail_cache_valid = 0;
        return;
    }

    // Only load thumbnails for files, not directories
    if (entries[selected_index].is_dir) {
        thumbnail_cache_valid = 0;
        return;
    }

    char thumb_path[MAX_PATH_LEN];
    int use_entry_cache = 0;  // v44: Can we use entry-level path cache?

    // Check if we're in Recent games mode
    if (strcmp(current_path, "RECENT_GAMES") == 0) {
        // For recent games, we need to use the full_path from the RecentGame structure
        const RecentGame* recent_list = recent_games_get_list();
        int recent_count = recent_games_get_count();

        if (selected_index < recent_count) {
            const RecentGame *recent_game = &recent_list[selected_index];

            if (recent_game->full_path[0] != '\0') {
                get_thumbnail_path(recent_game->full_path, thumb_path, sizeof(thumb_path));
            } else {
                // No full path available, skip thumbnail
                thumbnail_cache_valid = 0;
                return;
            }
        } else {
            // This is the ".." entry, no thumbnail
            thumbnail_cache_valid = 0;
            return;
        }
    } else if (strcmp(current_path, "FAVORITES") == 0) {
        // For favorites, we need to use the full_path from the FavoriteGame structure
        const FavoriteGame* favorites_list = favorites_get_list();
        int favorites_count = favorites_get_count();

        if (selected_index < favorites_count) {
            const FavoriteGame *favorite_game = &favorites_list[selected_index];

            if (favorite_game->full_path[0] != '\0') {
                get_thumbnail_path(favorite_game->full_path, thumb_path, sizeof(thumb_path));
            } else {
                // No full path available, skip thumbnail
                thumbnail_cache_valid = 0;
                return;
            }
        } else {
            // This is the ".." entry, no thumbnail
            thumbnail_cache_valid = 0;
            return;
        }
    } else {
        // Regular file browser mode - can use entry-level path cache
        use_entry_cache = 1;

        // v44: Check entry-level thumbnail path cache first
        if (entries[selected_index].thumb_checked == -1) {
            // Already checked and no thumbnail found - skip loading
            thumbnail_cache_valid = 0;
            return;
        }

        if (entries[selected_index].thumb_checked == 1) {
            // Already discovered - use cached path
            strncpy(thumb_path, entries[selected_index].thumb_path, sizeof(thumb_path) - 1);
            thumb_path[sizeof(thumb_path) - 1] = '\0';
        } else {
            // Not checked yet - build path for discovery
            get_thumbnail_path(entries[selected_index].path, thumb_path, sizeof(thumb_path));
        }
    }

    // Check if we already have this thumbnail loaded in memory
    if (thumbnail_cache_valid && strcmp(cached_thumbnail_path, thumb_path) == 0) {
        return; // Already loaded
    }

    // Free previous thumbnail
    if (thumbnail_cache_valid) {
        free_thumbnail(&current_thumbnail);
        thumbnail_cache_valid = 0;
    }

    // Try to load new thumbnail
    if (load_thumbnail(thumb_path, &current_thumbnail)) {
        strncpy(cached_thumbnail_path, thumb_path, sizeof(cached_thumbnail_path) - 1);
        cached_thumbnail_path[sizeof(cached_thumbnail_path) - 1] = '\0';
        thumbnail_cache_valid = 1;

        // v44: Cache the discovered path in entry
        if (use_entry_cache && entries[selected_index].thumb_checked == 0) {
            entries[selected_index].thumb_checked = 1;
            strncpy(entries[selected_index].thumb_path, thumb_path, sizeof(entries[selected_index].thumb_path) - 1);
            entries[selected_index].thumb_path[sizeof(entries[selected_index].thumb_path) - 1] = '\0';
        }
    } else {
        // v44: Mark as "no thumbnail found" in entry cache
        if (use_entry_cache && entries[selected_index].thumb_checked == 0) {
            entries[selected_index].thumb_checked = -1;
        }
    }
}

// v32/v38: Get screenshot base path (without extension)
static void get_screenshot_base_path(const char *game_path, char *base_path, size_t size) {
    // Copy the game path
    strncpy(base_path, game_path, size - 1);
    base_path[size - 1] = '\0';

    // Find the last dot to remove extension
    char *dot = strrchr(base_path, '.');
    if (dot != NULL) {
        *dot = '\0';  // Remove extension
    }
}

// v52: Helper to load screenshot from known path
static int load_screenshot_from_path(const char *path) {
    if (!path || path[0] == '\0') return 0;

    uint16_t *img_data = NULL;
    int img_w = 0, img_h = 0;
    int loaded = 0;

    // Determine format from extension
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        loaded = load_jpeg_rgb565(path, &img_data, &img_w, &img_h);
    } else if (strcasecmp(ext, ".bmp") == 0) {
        loaded = load_bmp_rgb565(path, &img_data, &img_w, &img_h);
    } else if (strcasecmp(ext, ".gif") == 0) {
        loaded = load_gif_rgb565(path, &img_data, &img_w, &img_h);
    } else if (strcasecmp(ext, ".webp") == 0) {
        loaded = load_webp_rgb565(path, &img_data, &img_w, &img_h);
    } else if (strcasecmp(ext, ".rgb565") == 0) {
        // v68: Support for old .rgb565 format screenshots
        Thumbnail temp_thumb = {0};
        if (load_raw_rgb565(path, &temp_thumb)) {
            // Allocate copy since load_raw_rgb565 uses shared buffer
            img_data = (uint16_t*)malloc(temp_thumb.width * temp_thumb.height * sizeof(uint16_t));
            if (img_data) {
                memcpy(img_data, temp_thumb.data, temp_thumb.width * temp_thumb.height * sizeof(uint16_t));
                img_w = temp_thumb.width;
                img_h = temp_thumb.height;
                loaded = 1;
            }
        }
    }

    if (loaded) {
        current_screenshot.data = img_data;
        current_screenshot.width = img_w;
        current_screenshot.height = img_h;
        strncpy(cached_screenshot_path, path, sizeof(cached_screenshot_path) - 1);
        cached_screenshot_path[sizeof(cached_screenshot_path) - 1] = '\0';
        screenshot_cache_valid = 1;
        return 1;
    }
    return 0;
}

// v32/v52: Load screenshot for currently selected game
static void load_current_screenshot() {
    // Check if screenshot feature is enabled in theme
    int x_start = gfx_theme_get_screenshot_x_start();
    int x_end = gfx_theme_get_screenshot_x_end();
    int y_start = gfx_theme_get_screenshot_y_start();
    int y_end = gfx_theme_get_screenshot_y_end();

    // If screenshot area not configured, skip loading
    // v61: Allow start=0 for full-screen, check end > 0 instead
    if (x_start < 0 || x_end <= 0 || x_end <= x_start ||
        y_start < 0 || y_end <= 0 || y_end <= y_start) {
        screenshot_cache_valid = 0;
        return;
    }

    if (selected_index < 0 || selected_index >= entry_count || entry_count == 0) {
        screenshot_cache_valid = 0;
        return;
    }

    // Only load screenshots for files, not directories
    if (entries[selected_index].is_dir) {
        screenshot_cache_valid = 0;
        return;
    }

    // v52: For regular file browser mode - use entry-level cache (FAST!)
    if (strcmp(current_path, "RECENT_GAMES") != 0 &&
        strcmp(current_path, "FAVORITES") != 0) {

        // Check entry-level cache first
        if (entries[selected_index].screenshot_checked == -1) {
            // Already checked during scan - no screenshot exists
            screenshot_cache_valid = 0;
            return;
        }

        if (entries[selected_index].screenshot_checked == 1) {
            // Screenshot exists - check if already loaded
            if (screenshot_cache_valid &&
                strcmp(cached_screenshot_path, entries[selected_index].screenshot_path) == 0) {
                return; // Already loaded
            }

            // Free previous screenshot
            if (screenshot_cache_valid && current_screenshot.data) {
                free(current_screenshot.data);
                current_screenshot.data = NULL;
                screenshot_cache_valid = 0;
            }

            // Load from cached path (single file open - FAST!)
            load_screenshot_from_path(entries[selected_index].screenshot_path);
            return;
        }

        // screenshot_checked == 0 means not yet scanned (shouldn't happen after scan_directory)
        screenshot_cache_valid = 0;
        return;
    }

    // For Recent/Favorites - use old method (they don't have entry-level cache)
    char screenshot_path[MAX_PATH_LEN];

    if (strcmp(current_path, "RECENT_GAMES") == 0) {
        const RecentGame* recent_list = recent_games_get_list();
        int recent_count = recent_games_get_count();

        if (selected_index < recent_count) {
            const RecentGame *recent_game = &recent_list[selected_index];
            if (recent_game->full_path[0] != '\0') {
                get_screenshot_base_path(recent_game->full_path, screenshot_path, sizeof(screenshot_path));
            } else {
                screenshot_cache_valid = 0;
                return;
            }
        } else {
            screenshot_cache_valid = 0;
            return;
        }
    } else if (strcmp(current_path, "FAVORITES") == 0) {
        const FavoriteGame* favorites_list = favorites_get_list();
        int favorites_count = favorites_get_count();

        if (selected_index < favorites_count) {
            const FavoriteGame *favorite_game = &favorites_list[selected_index];
            if (favorite_game->full_path[0] != '\0') {
                get_screenshot_base_path(favorite_game->full_path, screenshot_path, sizeof(screenshot_path));
            } else {
                screenshot_cache_valid = 0;
                return;
            }
        } else {
            screenshot_cache_valid = 0;
            return;
        }
    } else {
        screenshot_cache_valid = 0;
        return;
    }

    // Check if we already have this screenshot cached
    if (screenshot_cache_valid && strcmp(cached_screenshot_path, screenshot_path) == 0) {
        return; // Already cached
    }

    // Free previous screenshot
    if (screenshot_cache_valid) {
        if (current_screenshot.data) {
            free(current_screenshot.data);
            current_screenshot.data = NULL;
        }
        screenshot_cache_valid = 0;
    }

    // Try to load screenshot - try multiple extensions (old method for Recent/Favorites)
    uint16_t *img_data = NULL;
    int img_w = 0, img_h = 0;
    int loaded = 0;
    char try_path[MAX_PATH_LEN];

    const char *exts[] = { ".jpg", ".jpeg", ".bmp", ".gif", ".webp", NULL };

    for (int e = 0; exts[e] && !loaded; e++) {
        snprintf(try_path, sizeof(try_path), "%s%s", screenshot_path, exts[e]);
        if (exts[e][1] == 'j') {
            loaded = load_jpeg_rgb565(try_path, &img_data, &img_w, &img_h);
        } else if (exts[e][1] == 'b') {
            loaded = load_bmp_rgb565(try_path, &img_data, &img_w, &img_h);
        } else if (exts[e][1] == 'g') {
            loaded = load_gif_rgb565(try_path, &img_data, &img_w, &img_h);
        } else if (exts[e][1] == 'w') {
            loaded = load_webp_rgb565(try_path, &img_data, &img_w, &img_h);
        }
    }

    if (loaded) {
        current_screenshot.data = img_data;
        current_screenshot.width = img_w;
        current_screenshot.height = img_h;
        strncpy(cached_screenshot_path, screenshot_path, sizeof(cached_screenshot_path) - 1);
        cached_screenshot_path[sizeof(cached_screenshot_path) - 1] = '\0';
        screenshot_cache_valid = 1;
    }
}

// v32: Render screenshot in theme-defined area with proper aspect ratio
static void render_screenshot(uint16_t *framebuffer) {
    if (!screenshot_cache_valid || !current_screenshot.data) return;

    int x_start = gfx_theme_get_screenshot_x_start();
    int x_end = gfx_theme_get_screenshot_x_end();
    int y_start = gfx_theme_get_screenshot_y_start();
    int y_end = gfx_theme_get_screenshot_y_end();

    // v61: Allow start=0 for full-screen
    if (x_start < 0 || x_end <= 0 || x_end <= x_start ||
        y_start < 0 || y_end <= 0 || y_end <= y_start) {
        return;
    }

    int area_width = x_end - x_start;
    int area_height = y_end - y_start;
    int img_width = current_screenshot.width;
    int img_height = current_screenshot.height;

    if (img_width <= 0 || img_height <= 0) return;

    // Calculate scaling to fit in the area while maintaining aspect ratio
    int scale_w = (area_width * 100) / img_width;
    int scale_h = (area_height * 100) / img_height;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;

    // Calculate displayed size
    int disp_width = (img_width * scale) / 100;
    int disp_height = (img_height * scale) / 100;

    // Center in the designated area
    int offset_x = x_start + (area_width - disp_width) / 2;
    int offset_y = y_start + (area_height - disp_height) / 2;

    // v61: Fill screenshot area with black first (for letterboxing)
    for (int y = y_start; y < y_end && y < SCREEN_HEIGHT; y++) {
        for (int x = x_start; x < x_end && x < SCREEN_WIDTH; x++) {
            if (x >= 0 && y >= 0) {
                framebuffer[y * SCREEN_WIDTH + x] = 0x0000;  // Black
            }
        }
    }

    // Simple nearest-neighbor scaling
    for (int dy = 0; dy < disp_height && (offset_y + dy) < SCREEN_HEIGHT; dy++) {
        int src_y = (dy * img_height) / disp_height;
        if (src_y >= img_height) src_y = img_height - 1;

        for (int dx = 0; dx < disp_width && (offset_x + dx) < SCREEN_WIDTH; dx++) {
            int src_x = (dx * img_width) / disp_width;
            if (src_x >= img_width) src_x = img_width - 1;

            int screen_x = offset_x + dx;
            int screen_y = offset_y + dy;

            if (screen_x >= 0 && screen_x < SCREEN_WIDTH &&
                screen_y >= 0 && screen_y < SCREEN_HEIGHT) {
                framebuffer[screen_y * SCREEN_WIDTH + screen_x] =
                    current_screenshot.data[src_y * img_width + src_x];
            }
        }
    }
}

// Check if path is a directory - optimized to use d_type first
static inline int is_directory_fast(const char *path, unsigned char d_type) {
    // Use d_type if available (much faster, no stat call needed)
    if (d_type != DT_UNKNOWN) {
        return (d_type == DT_DIR);
    }

    // Fallback to stat only if needed
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
}

// v36: Comparison function to sort entries alphabetically by name (case-insensitive)
int compare_entries(const void *a, const void *b) {
    const MenuEntry *entry_a = (const MenuEntry *)a;
    const MenuEntry *entry_b = (const MenuEntry *)b;
    return strcasecmp(entry_a->name, entry_b->name);  // v36: Case-insensitive sort
}

// Show recent games list
static void show_recent_games(void) {
    entry_count = 0;
    reset_navigation_state();
    render_set_in_platform_menu(false);  // Recent games is a game list, not platform menu

    // Set current_path so thumbnail loading knows we're in recent games mode
    strncpy(current_path, "RECENT_GAMES", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';
    
    // Clear thumbnail/screenshot cache when switching to recent games mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;

    const RecentGame* recent_list = recent_games_get_list();
    int recent_count = recent_games_get_count();

    if (recent_count == 0) {
        // Only show back entry if no recent games
        ensure_entries_capacity(entry_count + 1);
        strncpy(entries[entry_count].name, "..", sizeof(entries[entry_count].name) - 1);
        strncpy(entries[entry_count].path, ROMS_PATH, sizeof(entries[entry_count].path) - 1);
        entries[entry_count].is_dir = 1;
        entry_count++;
    } else {
        // Add recent games first
        ensure_entries_capacity(entry_count + recent_count + 1);
        for (int i = 0; i < recent_count; i++) {
            strncpy(entries[entry_count].name, recent_list[i].display_name, sizeof(entries[entry_count].name) - 1);
            snprintf(entries[entry_count].path, sizeof(entries[entry_count].path),
                    "%s;%s", recent_list[i].core_name, recent_list[i].game_name);
            entries[entry_count].is_dir = 0;
            entry_count++;
        }

        // Add back entry after recent games
        strncpy(entries[entry_count].name, "..", sizeof(entries[entry_count].name) - 1);
        strncpy(entries[entry_count].path, ROMS_PATH, sizeof(entries[entry_count].path) - 1);
        entries[entry_count].is_dir = 1;
        entry_count++;
    }
    
    // Load thumbnail/screenshot for initially selected item AND reset last_selected_index to prevent duplicate loading
    load_current_thumbnail();
    load_current_screenshot();
    last_selected_index = selected_index;  // Prevent render loop from detecting this as a "change"
}

// Show favorites
static void show_favorites(void) {
    entry_count = 0;
    reset_navigation_state();
    render_set_in_platform_menu(false);  // Favorites is a game list, not platform menu

    // Set current_path so thumbnail loading knows we're in favorites mode
    strncpy(current_path, "FAVORITES", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    // Clear thumbnail/screenshot cache when switching to favorites mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;

    const FavoriteGame* favorites_list = favorites_get_list();
    int favorites_count = favorites_get_count();

    if (favorites_count == 0) {
        // Only show back entry if no favorites
        ensure_entries_capacity(entry_count + 1);
        strncpy(entries[entry_count].name, "..", sizeof(entries[entry_count].name) - 1);
        strncpy(entries[entry_count].path, ROMS_PATH, sizeof(entries[entry_count].path) - 1);
        entries[entry_count].is_dir = 1;
        entry_count++;
    } else {
        // Add favorites first
        ensure_entries_capacity(entry_count + favorites_count + 1);
        for (int i = 0; i < favorites_count; i++) {
            strncpy(entries[entry_count].name, favorites_list[i].display_name, sizeof(entries[entry_count].name) - 1);
            snprintf(entries[entry_count].path, sizeof(entries[entry_count].path),
                    "%s;%s", favorites_list[i].core_name, favorites_list[i].game_name);
            entries[entry_count].is_dir = 0;
            entry_count++;
        }

        // Add back entry after favorites
        strncpy(entries[entry_count].name, "..", sizeof(entries[entry_count].name) - 1);
        strncpy(entries[entry_count].path, ROMS_PATH, sizeof(entries[entry_count].path) - 1);
        entries[entry_count].is_dir = 1;
        entry_count++;
    }

    // Load thumbnail/screenshot for initially selected item AND reset last_selected_index to prevent duplicate loading
    load_current_thumbnail();
    load_current_screenshot();
    last_selected_index = selected_index;  // Prevent render loop from detecting this as a "change"
}

// Show tools menu
static void show_tools_menu(void) {
    entry_count = 0;
    reset_navigation_state();
    render_set_in_platform_menu(false);  // Tools is not the platform menu

    // Set current_path for tools mode
    strncpy(current_path, "TOOLS", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    // Clear thumbnail/screenshot cache when switching to tools mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;

    // Ensure we have space for 6 entries
    ensure_entries_capacity(6);

    // Add Calculator entry
    strncpy(entries[entry_count].name, "Calculator", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "CALCULATOR", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Add File Manager entry (v76)
    strncpy(entries[entry_count].name, "File Manager", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "FILEMANAGER", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Add Hotkeys entry
    strncpy(entries[entry_count].name, "Hotkeys", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "HOTKEYS", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Add Credits entry
    strncpy(entries[entry_count].name, "Credits", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "CREDITS", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Add Utils entry
    strncpy(entries[entry_count].name, "Utils", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "UTILS", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Add back entry (v76: "Back" instead of "..")
    strncpy(entries[entry_count].name, "Back", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, ROMS_PATH, sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Load thumbnail for initially selected item AND reset last_selected_index to prevent duplicate loading
    load_current_thumbnail();
    last_selected_index = selected_index;  // Prevent render loop from detecting this as a "change"
}

// Show utils menu with js2000 files
static void show_utils_menu(void) {
    entry_count = 0;
    reset_navigation_state();
    
    // Set current_path for utils mode
    strncpy(current_path, "UTILS", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';
    
    // Clear thumbnail/screenshot cache when switching to utils mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;

    // Scan js2000 directory for files
    char js2000_path[MAX_PATH_LEN];
    snprintf(js2000_path, sizeof(js2000_path), "%s/js2000", ROMS_PATH);

    DIR *dir = opendir(js2000_path);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;  // Skip hidden files

            char full_path[MAX_PATH_LEN];
            snprintf(full_path, sizeof(full_path), "%s/%s", js2000_path, ent->d_name);

            struct stat st;
            if (stat(full_path, &st) == 0) {
                ensure_entries_capacity(entry_count + 1);
                strncpy(entries[entry_count].name, ent->d_name, sizeof(entries[entry_count].name) - 1);
                strncpy(entries[entry_count].path, full_path, sizeof(entries[entry_count].path) - 1);
                entries[entry_count].is_dir = S_ISDIR(st.st_mode);
                entry_count++;
            }
        }
        closedir(dir);
    }

    // Add "Rebuild folder cache" option
    ensure_entries_capacity(entry_count + 1);
    strncpy(entries[entry_count].name, "Rebuild folder cache", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "REBUILD_CACHE", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 0;
    entry_count++;

    // Add back entry (v76: "Back" instead of "..")
    ensure_entries_capacity(entry_count + 1);
    strncpy(entries[entry_count].name, "Back", sizeof(entries[entry_count].name) - 1);
    strncpy(entries[entry_count].path, "TOOLS", sizeof(entries[entry_count].path) - 1);
    entries[entry_count].is_dir = 1;
    entry_count++;

    // Load thumbnail for initially selected item
    load_current_thumbnail();
    last_selected_index = selected_index;
}

// Show hotkeys screen
static void show_hotkeys_screen(void) {
    // Set current_path for hotkeys mode
    strncpy(current_path, "HOTKEYS", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    // Clear thumbnail/screenshot cache and entries for hotkeys mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;
    entry_count = 0;
    reset_navigation_state();
}

// Show credits screen
static void show_credits_screen(void) {
    // Set current_path for credits mode
    strncpy(current_path, "CREDITS", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    // Clear thumbnail/screenshot cache and entries for credits mode
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;
    entry_count = 0;
    reset_navigation_state();
}

// Scan directory and populate entries
// Extract platform name from path (e.g., "/mnt/sda1/ROMS/nes" -> "nes")
static void update_current_platform(const char *path) {
    // Default to no platform
    gfx_theme_set_platform(NULL);

    if (!path || strlen(path) <= strlen(ROMS_PATH)) return;

    // Check if we're in a subdirectory of ROMS
    if (strncmp(path, ROMS_PATH, strlen(ROMS_PATH)) != 0) return;

    // Get the part after ROMS_PATH
    const char* subpath = path + strlen(ROMS_PATH);
    if (*subpath == '/') subpath++;

    // Extract first directory name (platform)
    char platform[32];
    const char* slash = strchr(subpath, '/');
    if (slash) {
        int len = slash - subpath;
        if (len > 0 && len < 32) {
            strncpy(platform, subpath, len);
            platform[len] = '\0';
            gfx_theme_set_platform(platform);
        }
    } else if (*subpath) {
        // No further subdirectory, subpath is the platform
        strncpy(platform, subpath, sizeof(platform) - 1);
        platform[sizeof(platform) - 1] = '\0';
        gfx_theme_set_platform(platform);
    }
}

// v52: Temp storage for screenshot filenames during directory scan
#define MAX_SCREENSHOTS_CACHE 512
static char screenshot_cache_names[MAX_SCREENSHOTS_CACHE][256];
static int screenshot_cache_count = 0;

// v52: Temp storage for thumbnail filenames during directory scan
#define MAX_THUMBNAILS_CACHE 512
static char thumbnail_cache_names[MAX_THUMBNAILS_CACHE][256];
static int thumbnail_cache_count = 0;
static int thumbnail_res_exists = 0;  // Does .res/ directory exist?

// v52: Check if filename (without extension) matches image name (without extension)
// Used for both screenshots and thumbnails
static int filename_base_matches(const char *game_name, const char *image_name) {
    // Get base name of game (without extension)
    int game_len = strlen(game_name);
    const char *game_ext = strrchr(game_name, '.');
    if (game_ext) game_len = game_ext - game_name;

    // Get base name of image (without extension)
    int img_len = strlen(image_name);
    const char *img_ext = strrchr(image_name, '.');
    if (img_ext) img_len = img_ext - image_name;

    // Compare lengths first
    if (game_len != img_len) return 0;

    // Case-insensitive compare
    for (int i = 0; i < game_len; i++) {
        char c1 = game_name[i];
        char c2 = image_name[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

static void scan_directory(const char *path);

void force_refresh_menu_entries(void) {
    scan_directory(current_path);
}

static void scan_directory(const char *path) {
    DIR *dir;
    struct dirent *ent;

    entry_count = 0;
    screenshot_cache_count = 0;  // v52: Reset screenshot cache
    reset_navigation_state();

    // Update current platform for per-platform theme backgrounds
    update_current_platform(path);

    // Store whether we're at root for recent games insertion later
    int is_root = (strcasecmp(path, ROMS_PATH) == 0 ||
                   strcasecmp(path, "/mnt/sda1/ROMS/") == 0 ||
                   strcasecmp(path, "sdcard/ROMS") == 0 ||
                   strcasecmp(path, "sdcard/ROMS/") == 0 ||
                   strcasecmp(path, "ROMS") == 0 ||
                   strcasecmp(path, "ROMS/") == 0);

    // Set render mode: platform menu (root ROMS) vs game list (inside folders)
    render_set_in_platform_menu(is_root);

    // Add parent directory entry if not at root
    if (!is_root) {
        ensure_entries_capacity(entry_count + 1);
        strncpy(entries[entry_count].name, "..", sizeof(entries[entry_count].name) - 1);
        strncpy(entries[entry_count].path, path, sizeof(entries[entry_count].path) - 1);
        entries[entry_count].is_dir = 1;
        entries[entry_count].thumb_checked = -1;  // v52: No thumbnail for ".."
        entries[entry_count].screenshot_checked = -1;  // v52: No screenshot for ".."
        entry_count++;
    }

    dir = opendir(path);
    if (!dir) {
        return;
    }

    // Collect all entries in a single pass - optimized
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;  // Skip hidden files

        // Skip frogui, saves, and cores folders
        if (strcasecmp(ent->d_name, "frogui") == 0 || strcasecmp(ent->d_name, "saves") == 0 || strcasecmp(ent->d_name, "save") == 0 || strcasecmp(ent->d_name, "cores") == 0) {
            continue;
        }

        // Save entry name and type BEFORE any nested readdir calls (readdir uses static buffer)
        char entry_name[256];
        strncpy(entry_name, ent->d_name, sizeof(entry_name) - 1);
        entry_name[sizeof(entry_name) - 1] = '\0';
        int entry_type = ent->d_type;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry_name);

        // Fast path: use d_type if available, avoid stat() calls
        int is_dir = is_directory_fast(full_path, entry_type);

        // Skip files if in root ROMS directory (only show folders there)
        if (is_root && !is_dir) {
            continue;
        }

        // v24: Apply display options filtering (only in platform folders, not root)
        if (!is_root) {
            // Filter directories based on display mode
            if (is_dir && !display_opts_should_show_dirs()) {
                continue;
            }
            // v33/v38/v52: Skip image files - these are screenshots, not games
            // v52: But remember them for screenshot cache!
            // v68: Added .rgb565 support for old screenshots
            if (!is_dir) {
                const char *ext = strrchr(entry_name, '.');
                if (ext && (strcasecmp(ext, ".png") == 0 ||
                            strcasecmp(ext, ".jpg") == 0 ||
                            strcasecmp(ext, ".jpeg") == 0 ||
                            strcasecmp(ext, ".gif") == 0 ||
                            strcasecmp(ext, ".bmp") == 0 ||
                            strcasecmp(ext, ".webp") == 0 ||
                            strcasecmp(ext, ".rgb565") == 0)) {
                    // v52: Store screenshot filename for later matching
                    if (screenshot_cache_count < MAX_SCREENSHOTS_CACHE) {
                        strncpy(screenshot_cache_names[screenshot_cache_count], entry_name, 255);
                        screenshot_cache_names[screenshot_cache_count][255] = '\0';
                        screenshot_cache_count++;
                    }
                    continue;
                }
            }
            // Filter files based on pattern matching
            if (!is_dir && !display_opts_matches_pattern(entry_name)) {
                continue;
            }
            // Filter multi-disk games (hide disk 2, 3, 4, etc.)
            if (!is_dir && !display_opts_is_disk1(entry_name)) {
                continue;
            }
        }

        // Skip hidden sections or empty directories in root ROMS directory
        if (is_root && is_dir) {
            // Check if section is hidden by user settings
            extern int is_section_hidden(const char *section_name);
            if (is_section_hidden(entry_name)) {
                continue; // Skip hidden section
            }

            if (hide_empty_folders) {
                // Load cache on first use (default to hiding if setting not found)
                load_empty_dirs_cache();
                if (is_in_empty_cache(entry_name)) {
                    continue; // Skip cached empty directory
                }
            }
        }

        // Ensure we have space for one more entry
        ensure_entries_capacity(entry_count + 1);

        // Add directories first, then files
        if (is_dir) {
            // Add directory entry
            strncpy(entries[entry_count].name, entry_name, sizeof(entries[entry_count].name) - 1);
            strncpy(entries[entry_count].path, full_path, sizeof(entries[entry_count].path) - 1);
            entries[entry_count].is_dir = 1;
            entries[entry_count].thumb_checked = -1;  // v52: No thumbnails for dirs
            entries[entry_count].screenshot_checked = -1;  // v52: No screenshots for dirs
            entry_count++;
        } else {
            // Add file entry
            strncpy(entries[entry_count].name, entry_name, sizeof(entries[entry_count].name) - 1);
            strncpy(entries[entry_count].path, full_path, sizeof(entries[entry_count].path) - 1);
            entries[entry_count].is_dir = 0;
            entries[entry_count].thumb_checked = -1;  // v52: Will update after scan if found
            entries[entry_count].screenshot_checked = -1;  // v52: Will update after scan if found
            entry_count++;
        }
    }

    // Close the directory after reading
    closedir(dir);

    // v52: Scan .res/ subdirectory for thumbnails (if it exists)
    thumbnail_cache_count = 0;
    thumbnail_res_exists = 0;
    if (!is_root) {
        char res_path[MAX_PATH_LEN];
        snprintf(res_path, sizeof(res_path), "%s/.res", path);
        DIR *res_dir = opendir(res_path);
        if (res_dir) {
            thumbnail_res_exists = 1;
            struct dirent *res_ent;
            while ((res_ent = readdir(res_dir)) != NULL && thumbnail_cache_count < MAX_THUMBNAILS_CACHE) {
                if (res_ent->d_name[0] == '.') continue;
                // Check for .rgb565 extension
                const char *ext = strrchr(res_ent->d_name, '.');
                if (ext && strcasecmp(ext, ".rgb565") == 0) {
                    strncpy(thumbnail_cache_names[thumbnail_cache_count], res_ent->d_name, 255);
                    thumbnail_cache_names[thumbnail_cache_count][255] = '\0';
                    thumbnail_cache_count++;
                }
            }
            closedir(res_dir);
        }
    }

    // v52: Match thumbnails to game entries (fast in-memory lookup)
    // Only if .res/ directory exists and has thumbnails
    if (!is_root && thumbnail_res_exists && thumbnail_cache_count > 0) {
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir) continue;  // Skip directories

            // Look for matching thumbnail in cache
            for (int t = 0; t < thumbnail_cache_count; t++) {
                if (filename_base_matches(entries[i].name, thumbnail_cache_names[t])) {
                    // Found matching thumbnail - build full path
                    snprintf(entries[i].thumb_path, MAX_PATH_LEN, "%s/.res/%s",
                             path, thumbnail_cache_names[t]);
                    entries[i].thumb_checked = 1;
                    break;
                }
            }
            // If not found, thumb_checked stays -1 (initialized value)
        }
    }

    // v52: Match screenshots to game entries (fast in-memory lookup)
    if (!is_root && screenshot_cache_count > 0) {
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir) continue;  // Skip directories

            // Look for matching screenshot in cache
            for (int s = 0; s < screenshot_cache_count; s++) {
                if (filename_base_matches(entries[i].name, screenshot_cache_names[s])) {
                    // Found matching screenshot - build full path
                    snprintf(entries[i].screenshot_path, MAX_PATH_LEN, "%s/%s",
                             path, screenshot_cache_names[s]);
                    entries[i].screenshot_checked = 1;
                    break;
                }
            }
            // If not found, screenshot_checked stays -1 (initialized value)
        }
    }

    // Sort all entries alphabetically by name
    qsort(entries, entry_count, sizeof(MenuEntry), compare_entries);

    // Add Recent games, Favorites, and Random game if in root directory and not hidden
    if (is_root) {
        extern int is_section_hidden(const char *section_name);

        // Insert Recent games if not hidden
        if (!is_section_hidden("Recent games") && !is_section_hidden("RECENT_GAMES")) {
            ensure_entries_capacity(entry_count + 1);
            for (int i = entry_count; i > 0; i--) {
                entries[i] = entries[i - 1];
            }
            strncpy(entries[0].name, "Recent games", sizeof(entries[0].name) - 1);
            strncpy(entries[0].path, "RECENT_GAMES", sizeof(entries[0].path) - 1);
            entries[0].is_dir = 1;
            entry_count++;
        }

        // Insert <cores> menu entry if not hidden
        if (!is_section_hidden("<cores>")) {
            ensure_entries_capacity(entry_count + 1);
            int pos = 0;
            if (entry_count > 0 && strcmp(entries[0].path, "RECENT_GAMES") == 0) pos++;
            for (int i = entry_count; i > pos; i--) {
                entries[i] = entries[i - 1];
            }
            strncpy(entries[pos].name, "<cores>", sizeof(entries[pos].name) - 1);
            strncpy(entries[pos].path, "CORES_MENU", sizeof(entries[pos].path) - 1);
            entries[pos].is_dir = 1;
            entry_count++;
        }

        // Insert Favorites if not hidden
        if (!is_section_hidden("Favorites") && !is_section_hidden("FAVORITES")) {
            ensure_entries_capacity(entry_count + 1);
            int pos = 0;
            if (entry_count > 0 && strcmp(entries[0].path, "RECENT_GAMES") == 0) pos++;
            if (entry_count > pos && strcmp(entries[pos].path, "CORES_MENU") == 0) pos++;
            for (int i = entry_count; i > pos; i--) {
                entries[i] = entries[i - 1];
            }
            strncpy(entries[pos].name, "Favorites", sizeof(entries[pos].name) - 1);
            strncpy(entries[pos].path, "FAVORITES", sizeof(entries[pos].path) - 1);
            entries[pos].is_dir = 1;
            entry_count++;
        }

        // Insert Random Game if not hidden
        if (!is_section_hidden("Random game") && !is_section_hidden("RANDOM_GAME")) {
            ensure_entries_capacity(entry_count + 1);
            int pos = 0;
            if (entry_count > 0 && strcmp(entries[0].path, "RECENT_GAMES") == 0) pos++;
            if (entry_count > pos && strcmp(entries[pos].path, "CORES_MENU") == 0) pos++;
            if (entry_count > pos && strcmp(entries[pos].path, "FAVORITES") == 0) pos++;

            for (int i = entry_count; i > pos; i--) {
                entries[i] = entries[i - 1];
            }
            strncpy(entries[pos].name, "Random game", sizeof(entries[pos].name) - 1);
            strncpy(entries[pos].path, "RANDOM_GAME", sizeof(entries[pos].path) - 1);
            entries[pos].is_dir = 1;
            entry_count++;
        }
    }

    // Defer thumbnail loading to first render for faster boot
    // The render loop will handle loading thumbnails on the first frame
    thumbnail_cache_valid = 0;
    screenshot_cache_valid = 0;
    last_selected_index = -1;  // Force load on first render
}

// Render sections visibility sub-menu
static void render_sections_visibility_menu(void) {
    extern int sections_visibility_get_count(void);
    extern int sections_visibility_get_selected(void);
    extern int sections_visibility_get_scroll_offset(void);
    extern const char* sections_visibility_get_name(int index);
    extern int is_section_hidden(const char *section_name);

    render_text_pillbox(framebuffer, PADDING, 10, "UKRYJ / POKAZ SEKCJE", 0x0000, COLOR_HEADER, 7);

    int count = sections_visibility_get_count();
    int selected = sections_visibility_get_selected();
    int offset = sections_visibility_get_scroll_offset();
    int start_y = 45;
    int max_visible = 5;

    for (int i = 0; i < max_visible && (offset + i) < count; i++) {
        int idx = offset + i;
        const char *name = sections_visibility_get_name(idx);
        int hidden = is_section_hidden(name);
        int is_sel = (idx == selected);

        int y = start_y + i * 28;

        char item_text[128];
        snprintf(item_text, sizeof(item_text), " %s : %s ", name, hidden ? "[ UKRYTY ]" : "[ WIDOCZNY ]");

        uint16_t text_color = hidden ? 0xF800 : 0x07E0; // Red if hidden, Green if visible
        uint16_t bg_color = is_sel ? COLOR_SELECT_BG : 0x0000;

        render_text_pillbox(framebuffer, PADDING, y, item_text, bg_color, is_sel ? COLOR_SELECT_TEXT : text_color, 7);
    }

    const char *legend = " A - PRZELACZ   B - POWROT ";
    int legend_y = SCREEN_HEIGHT - 24;
    int legend_width = font_measure_text(legend);
    int legend_x = SCREEN_WIDTH - legend_width - 12;

    render_rounded_rect(framebuffer, legend_x - 4, legend_y - 2, legend_width + 8, 20, 10, COLOR_LEGEND_BG);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, legend_x, legend_y, legend, COLOR_LEGEND);
}

static int sett_scroll_frame = 0;
static int sett_scroll_pos = 0;
static int sett_scroll_dir = 1;
static int sett_last_sel = -1;

static void get_scrolling_settings_text_px(const char *text, int max_px, int is_selected,
                                          int frame_cnt, int scroll_pos, char *out, size_t out_size) {
    if (!text || !out || out_size == 0) return;
    int text_w = font_measure_text(text);
    if (text_w <= max_px) {
        strncpy(out, text, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (!is_selected) {
        int len = strlen(text);
        int fit = len;
        char temp[128];
        while (fit > 1) {
            if (fit >= (int)sizeof(temp) - 3) fit = sizeof(temp) - 4;
            strncpy(temp, text, fit);
            temp[fit] = '\0';
            strcat(temp, "..");
            if (font_measure_text(temp) <= max_px) break;
            fit--;
        }
        strncpy(out, temp, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    // Selected: animated marquee scrolling
    int len = strlen(text);
    int start = (frame_cnt > 35) ? scroll_pos : 0;
    if (start < 0) start = 0;
    if (start >= len) start = len - 1;

    int fit = len - start;
    char temp[128];
    while (fit > 0) {
        if (fit >= (int)sizeof(temp) - 1) fit = sizeof(temp) - 2;
        strncpy(temp, text + start, fit);
        temp[fit] = '\0';
        if (font_measure_text(temp) <= max_px) break;
        fit--;
    }
    strncpy(out, temp, out_size - 1);
    out[out_size - 1] = '\0';
}

// Render settings menu
static void render_settings_menu() {
    // If saving, show saving overlay
    if (settings_is_saving()) {
        const char* saving_text = "SAVING...";
        int text_width = font_measure_text(saving_text);
        int x = (SCREEN_WIDTH - text_width) / 2;
        int y = (SCREEN_HEIGHT - FONT_CHAR_HEIGHT) / 2;

        render_text_pillbox(framebuffer, x, y, saving_text, theme_header(), theme_bg(), 6);
        return;
    }

    extern int sections_visibility_is_active(void);
    if (sections_visibility_is_active()) {
        render_sections_visibility_menu();
        return;
    }

    int settings_count = settings_get_count();
    int selected_index = settings_get_selected_index();
    int scroll_offset = settings_get_scroll_offset();

    // Update scrolling marquee state for selected item
    if (selected_index != sett_last_sel) {
        sett_last_sel = selected_index;
        sett_scroll_frame = 0;
        sett_scroll_pos = 0;
        sett_scroll_dir = 1;
    } else {
        sett_scroll_frame++;
        if (sett_scroll_frame > 35 && (sett_scroll_frame % 5 == 0)) {
            const SettingsOption *sel_opt = settings_get_option(selected_index);
            const char *sel_name = sel_opt ? sel_opt->name : "";
            int name_len = strlen(sel_name);
            int fit_chars = name_len;
            char tmp[128];
            while (fit_chars > 0) {
                if (fit_chars >= (int)sizeof(tmp) - 1) fit_chars = sizeof(tmp) - 2;
                strncpy(tmp, sel_name, fit_chars);
                tmp[fit_chars] = '\0';
                if (font_measure_text(tmp) <= 145) break;
                fit_chars--;
            }
            int max_scroll = name_len - fit_chars;
            if (max_scroll > 0) {
                sett_scroll_pos += sett_scroll_dir;
                if (sett_scroll_pos >= max_scroll) {
                    sett_scroll_dir = -1;
                    sett_scroll_pos = max_scroll;
                } else if (sett_scroll_pos <= 0) {
                    sett_scroll_dir = 1;
                    sett_scroll_pos = 0;
                }
            }
        }
    }

    // LVGL Modal Container Dimensions
    int menu_x = 8;
    int menu_y = 8;
    int menu_w = 304;
    int menu_h = 224;

    // 1. Draw outer frosted container
    render_rounded_rect(framebuffer, menu_x, menu_y, menu_w, menu_h, 8, 0x10A2);
    render_rect(framebuffer, menu_x, menu_y, menu_w, menu_h, 0x31A6);

    // 2. Header Bar: Title Badge on left, Item Counter on right
    const char *title_text = show_multicore_opt ? "MULTICORE SETTINGS" : "CORE SETTINGS";
    int title_w = font_measure_text(title_text);
    render_rounded_rect(framebuffer, menu_x + 8, menu_y + 4, title_w + 14, 18, 5, 0x028A);
    render_rect(framebuffer, menu_x + 8, menu_y + 4, title_w + 14, 18, 0x04DF);
    font_draw_text(framebuffer, 320, 240, menu_x + 15, menu_y + 5, title_text, 0x07FF);

    char count_str[32];
    snprintf(count_str, sizeof(count_str), "[ %02d/%02d ]", (settings_count > 0) ? (selected_index + 1) : 0, settings_count);
    int count_w = font_measure_text(count_str);
    int count_x = menu_x + menu_w - count_w - 18;
    render_rounded_rect(framebuffer, count_x, menu_y + 4, count_w + 10, 18, 4, 0x18E3);
    font_draw_text(framebuffer, 320, 240, count_x + 5, menu_y + 5, count_str, 0xCE79);

    // 3. Option Cards List
    int start_y = menu_y + 26;
    int card_x = menu_x + 8;
    int card_w = menu_w - 24;  // Leaves room for right scrollbar
    int item_h = 21;
    int gap = 2;
    int row_stride = item_h + gap;
    int badge_w = 114;
    int badge_x = card_x + card_w - badge_w - 4;
    int max_label_w = badge_x - card_x - 10;
    int max_visible = 7;

    int drawn_count = 0;
    for (int option_index = scroll_offset; option_index < settings_count && drawn_count < max_visible; option_index++) {
        const SettingsOption *option = settings_get_option(option_index);
        if (!option) continue;

        int y = start_y + drawn_count * row_stride;
        int is_selected = (option_index == selected_index);

        // Format option name for display
        char display_opt_name[64];
        strncpy(display_opt_name, option->name, sizeof(display_opt_name) - 1);
        display_opt_name[sizeof(display_opt_name) - 1] = '\0';
        if (strncmp(display_opt_name, "frogui_", 7) == 0) {
            display_opt_name[0] = 'F'; display_opt_name[1] = 'R'; display_opt_name[2] = 'O';
            display_opt_name[3] = 'G'; display_opt_name[4] = 'u'; display_opt_name[5] = 'i';
        }

        // Draw LVGL Row Card
        if (is_selected) {
            render_rounded_rect(framebuffer, card_x, y, card_w, item_h, 4, 0x0A2D);
            render_rect(framebuffer, card_x, y, card_w, item_h, 0x04DF);
        } else {
            render_rounded_rect(framebuffer, card_x, y, card_w, item_h, 4, 0x18C3);
            render_rect(framebuffer, card_x, y, card_w, item_h, 0x2124);
        }

        // Draw option label with number & scrolling marquee
        char numbered_name[128];
        snprintf(numbered_name, sizeof(numbered_name), "%02d. %s", option_index + 1, display_opt_name);
        char display_label[128];
        get_scrolling_settings_text_px(numbered_name, max_label_w, is_selected, sett_scroll_frame, sett_scroll_pos, display_label, sizeof(display_label));
        uint16_t label_col = is_selected ? 0xFFFF : 0xDEFB;
        font_draw_text(framebuffer, 320, 240, card_x + 6, y + 3, display_label, label_col);

        // Draw option value inside selector badge
        if (option->current_value[0]) {
            uint16_t badge_bg = is_selected ? 0x0110 : 0x0861;
            uint16_t badge_border = is_selected ? 0x05DF : 0x18C3;
            render_rounded_rect(framebuffer, badge_x, y + 2, badge_w, item_h - 4, 3, badge_bg);
            render_rect(framebuffer, badge_x, y + 2, badge_w, item_h - 4, badge_border);

            uint16_t val_col = is_selected ? 0x07FF : 0x8CD1;
            const char *val_str = option->current_value;
            if (strcmp(val_str, "YES") == 0 || strcmp(val_str, "enabled") == 0 || strcmp(val_str, "true") == 0) {
                val_col = 0x07E0;
            } else if (strcmp(val_str, "NO") == 0 || strcmp(val_str, "disabled") == 0 || strcmp(val_str, "false") == 0) {
                val_col = is_selected ? 0xF986 : 0xCE79;
            }

            char formatted_val[128];
            if (is_selected) {
                snprintf(formatted_val, sizeof(formatted_val), "< %s >", val_str);
            } else {
                snprintf(formatted_val, sizeof(formatted_val), "%s", val_str);
            }

            char display_val[128];
            get_scrolling_settings_text_px(formatted_val, badge_w - 8, is_selected, sett_scroll_frame, sett_scroll_pos, display_val, sizeof(display_val));
            int v_w = font_measure_text(display_val);
            int v_x = badge_x + (badge_w - v_w) / 2;
            if (v_x < badge_x + 4) v_x = badge_x + 4;
            font_draw_text(framebuffer, 320, 240, v_x, y + 3, display_val, val_col);
        }

        drawn_count++;
    }

    // 4. Modern LVGL Vertical Scrollbar Track and Thumb
    int sb_x = menu_x + menu_w - 11;
    int sb_y = start_y;
    int sb_h = max_visible * row_stride - gap;
    int sb_w = 4;
    render_rounded_rect(framebuffer, sb_x, sb_y, sb_w, sb_h, 2, 0x18C3);
    if (settings_count > 0) {
        int thumb_h = (settings_count <= max_visible) ? sb_h : (sb_h * max_visible / settings_count);
        if (thumb_h < 14) thumb_h = 14;
        int max_scroll = settings_count - max_visible;
        int thumb_y = (max_scroll <= 0) ? sb_y : (sb_y + (sb_h - thumb_h) * scroll_offset / max_scroll);
        render_rounded_rect(framebuffer, sb_x, thumb_y, sb_w, thumb_h, 2, 0x07E0);
    }

    // 5. Footer Toolbar with key badges
    int y_foot = menu_y + menu_h - 22;
    render_text_pillbox(framebuffer, menu_x + 10, y_foot, "A:SAVE", 0x18E3, 0x07E0, 3);
    render_text_pillbox(framebuffer, menu_x + 75, y_foot, "< / >:CHANGE", 0x18E3, 0x07FF, 3);
    render_text_pillbox(framebuffer, menu_x + 180, y_foot, "Y:RESET", 0x18E3, 0xFFE0, 3);
    render_text_pillbox(framebuffer, menu_x + 245, y_foot, "B:BACK", 0x18E3, 0xF800, 3);
}

// Render hotkeys screen
static void render_hotkeys_screen() {
    // Draw title
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, 10, "HOTKEYS", COLOR_HEADER);

    // Draw hotkey information
    int start_y = 50;
    int line_height = 24;

    // Hotkeys text
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y, "SAVE STATE: L + R + X", COLOR_TEXT);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height, "LOAD STATE: L + R + Y", COLOR_TEXT);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height * 2, "NEXT SLOT: L + R + >", COLOR_TEXT);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height * 3, "PREV SLOT: L + R + <", COLOR_TEXT);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height * 4, "SCREENSHOT: L + R + START", COLOR_TEXT);
    
    // Draw legend
    const char *legend = " B - BACK ";
    int legend_y = SCREEN_HEIGHT - 24;
    int legend_width = font_measure_text(legend);
    int legend_x = SCREEN_WIDTH - legend_width - 12;
    
    render_rounded_rect(framebuffer, legend_x - 4, legend_y - 2, legend_width + 8, 20, 10, COLOR_LEGEND_BG);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, legend_x, legend_y, legend, COLOR_LEGEND);
}

// Render credits screen
static void render_credits_screen() {
    // Draw title
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, 10, "CREDITS", COLOR_HEADER);
    
    // Draw credits information
    int start_y = 50;
    int line_height = 24;
    
    // Credits text with pillboxes for sections
    // FrogUI Dev & Idea section
    const char *section1 = " FrogUI Dev & Idea ";
    int section1_width = font_measure_text(section1);
    render_rounded_rect(framebuffer, PADDING - 4, start_y - 2, section1_width + 8, 20, 10, COLOR_HEADER);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y, section1, COLOR_BG);
    
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height, "Prosty, Desoxyn & THE_Q_DEV", COLOR_TEXT);

    // Design section
    const char *section2 = " Design ";
    int section2_width = font_measure_text(section2);
    render_rounded_rect(framebuffer, PADDING - 4, start_y + line_height * 2 - 2, section2_width + 8, 20, 10, COLOR_HEADER);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height * 2, section2, COLOR_BG);

    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, start_y + line_height * 3, "Q_ta & THE_Q_DEV", COLOR_TEXT);
    
    // Draw legend
    const char *legend = " B - BACK ";
    int legend_y = SCREEN_HEIGHT - 24;
    int legend_width = font_measure_text(legend);
    int legend_x = SCREEN_WIDTH - legend_width - 12;
    
    render_rounded_rect(framebuffer, legend_x - 4, legend_y - 2, legend_width + 8, 20, 10, COLOR_LEGEND_BG);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, legend_x, legend_y, legend, COLOR_LEGEND);
}

// Render the menu using modular render system
static void render_menu() {
static int last_platform_idx = -1;
    if (render_is_in_platform_menu() && selected_index >= 0 && selected_index < entry_count) {
        if (selected_index != last_platform_idx) {
            if (selected_index > last_platform_idx) {
                gfx_theme_set_bg_anim_direction(1);
            } else {
                gfx_theme_set_bg_anim_direction(-1);
            }
            // Handle wrap around
            if (last_platform_idx == entry_count - 1 && selected_index == 0) gfx_theme_set_bg_anim_direction(1);
            if (last_platform_idx == 0 && selected_index == entry_count - 1) gfx_theme_set_bg_anim_direction(-1);
            last_platform_idx = selected_index;
        }

        if (entries[selected_index].is_dir && strcmp(entries[selected_index].name, "..") != 0) {
            gfx_theme_set_platform(entries[selected_index].name);
        } else {
            gfx_theme_set_platform(NULL);
        }
    }

    render_clear_screen_gfx(framebuffer);

    // If game is queued, just show loading screen
    if (game_queued) {
        // Show centered loading pillbox
        const char* loading_text = "LOADING...";
        int text_width = font_measure_text(loading_text);
        int x = (SCREEN_WIDTH - text_width) / 2;
        int y = (SCREEN_HEIGHT - FONT_CHAR_HEIGHT) / 2;
        
        // Use unified pillbox rendering
        render_text_pillbox(framebuffer, x, y, loading_text, theme_header(), theme_bg(), 6);
        return;
    }

    // v24: If display options are active, render display options menu
    if (display_opts_is_active()) {
        display_opts_render(framebuffer);
        return;
    }

    // v33: If text editor is active, render it
    if (text_editor_is_active()) {
        text_editor_render(framebuffer);
        return;
    }

    // If settings are active, render settings menu
    if (settings_is_active()) {
        render_settings_menu();
        return;
    }
    
    // If in hotkeys mode, render hotkeys screen
    if (strcmp(current_path, "HOTKEYS") == 0) {
        render_hotkeys_screen();
        return;
    }
    
    // If in credits mode, render credits screen
    if (strcmp(current_path, "CREDITS") == 0) {
        render_credits_screen();
        return;
    }

    // v62: Header drawing moved after overlay application (see below)

    // Get visible items count (respects gfx_theme layout if active)
    int visible_items = render_get_visible_items();

    // Adjust the scroll_offset if necessary to keep the selected item visible (grid-aware)
    int grid_cols = get_grid_cols();
    int max_visible = visible_items * grid_cols;
    if (selected_index < scroll_offset) {
        scroll_offset = (selected_index / grid_cols) * grid_cols;  // Scroll up to make the item visible
    } else if (selected_index >= scroll_offset + max_visible) {
        scroll_offset = ((selected_index - max_visible + 1) / grid_cols + 1) * grid_cols;  // Scroll down to make the item visible
    }

    // Load and display thumbnail for selected item FIRST (background layer)
    // Only reload if selection changed
    if (last_selected_index != selected_index) {
        load_current_thumbnail();
        load_current_screenshot();  // v32: Load screenshot too
        last_selected_index = selected_index;
        // Reset scrolling state for new selection
        text_scroll_frame_counter = 0;
        text_scroll_offset = 0;
        text_scroll_direction = 1;
    }

    // v42: Only show thumbnail if screenshot is NOT configured in theme
    // Screenshot system takes priority (theme-controlled position)
    // v61: Allow start=0 for full-screen, check end > 0 instead
    int screenshot_enabled = (gfx_theme_get_screenshot_x_end() > 0 &&
                              gfx_theme_get_screenshot_x_end() > gfx_theme_get_screenshot_x_start());

    if (!screenshot_enabled && thumbnail_cache_valid) {
        render_thumbnail(framebuffer, &current_thumbnail);
    }

    // v32: Render screenshot in theme-defined area (if configured)
    if (screenshot_cache_valid) {
        render_screenshot(framebuffer);
    }

    // v61: Apply PNG overlay after images but before text
    // This ensures: AVI -> images -> PNG overlay -> text
    gfx_theme_apply_overlay(framebuffer);

    // v62: Draw header AFTER overlay so it's not covered
    // v43: Section switcher at main menu, regular header for subfolders
    if (strcmp(current_path, ROMS_PATH) == 0 ||
        strcmp(current_path, "MAIN_MENU") == 0 ||
        strcmp(current_path, "TOOLS") == 0) {
        // v43: Main menu - show section switcher "< SECTION_NAME >"
        int logo_w = 0;

        // v36: Try theme logo first (resources/general/frogui_logo.png)
        uint16_t* theme_logo_pixels = NULL;
        uint8_t* theme_logo_alpha = NULL;
        int theme_logo_w = 0, theme_logo_h = 0;
        if (gfx_theme_get_logo(&theme_logo_pixels, &theme_logo_alpha, &theme_logo_w, &theme_logo_h)) {
            // Draw theme logo with alpha blending
            int x = PADDING, y = 8;
            for (int sy = 0; sy < theme_logo_h && y + sy < SCREEN_HEIGHT; sy++) {
                for (int sx = 0; sx < theme_logo_w && x + sx < SCREEN_WIDTH; sx++) {
                    int idx = sy * theme_logo_w + sx;
                    uint8_t a = theme_logo_alpha ? theme_logo_alpha[idx] : 255;
                    if (a > 128) {
                        framebuffer[(y + sy) * SCREEN_WIDTH + (x + sx)] = theme_logo_pixels[idx];
                    }
                }
            }
            logo_w = theme_logo_w;
        }

        // Fall back to built-in logo if no theme logo
        if (logo_w == 0) {
            logo_w = draw_header_logo(framebuffer, PADDING, 8);
        }

        // Check if system names / section header text should be hidden
        bool hide_sys = false;
        const char *hide_sys_setting = settings_get_value("frogui_hide_header_text");
        if (gfx_theme_has_custom_hide_header_text() && (!hide_sys_setting || strcmp(hide_sys_setting, "theme_default") == 0)) {
            hide_sys = gfx_theme_get_hide_header_text();
        } else if (hide_sys_setting && strcmp(hide_sys_setting, "true") == 0) {
            hide_sys = true;
        }
        if (!hide_sys) {
            // v43: Build section switcher text "< SECTION_NAME >"
            char section_text[64];
            snprintf(section_text, sizeof(section_text), "< %s >", section_names[current_section]);

            // Choose color based on whether header is selected
            uint16_t text_color = header_selected ? COLOR_SELECT_TEXT : COLOR_HEADER;

            if (logo_w > 0) {
                // Logo drawn, add section text after it
                if (header_selected) {
                    // Draw highlight background for selected header
                    int text_x = PADDING + logo_w + 6;
                    int text_w = font_measure_text(section_text);
                    render_fill_rect(framebuffer, text_x - 4, 6, text_w + 8, 20, COLOR_SELECT_BG);
                }
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT,
                              PADDING + logo_w + 6, 10, section_text, text_color);
            } else {
                // No logo, just text
                if (header_selected) {
                    int text_w = font_measure_text(section_text);
                    render_fill_rect(framebuffer, PADDING - 4, 6, text_w + 8, 20, COLOR_SELECT_BG);
                }
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT,
                              PADDING, 10, section_text, text_color);
            }
        }
    } else {
        // Show just the folder name, not full path
        const char *display_path = get_basename(current_path);
        render_header(framebuffer, display_path);
    }

    // Draw menu entries ON TOP of thumbnail
    if (is_horizontal_menu()) {
        float speed = 0.80f;
        const char *speed_val = settings_get_value("frogui_anim_speed");
        if (gfx_theme_has_custom_anim_speed() && (!speed_val || strcmp(speed_val, "theme_default") == 0)) {
            speed = gfx_theme_get_anim_speed();
        } else if (speed_val && strcmp(speed_val, "instant") == 0) {
            speed = 1.0f;
        } else if (speed_val && strcmp(speed_val, "fast") == 0) {
            speed = 0.90f;
        } else if (speed_val && strcmp(speed_val, "normal") == 0) {
            speed = 0.80f;
        } else if (speed_val && strcmp(speed_val, "slow") == 0) {
            speed = 0.40f;
        } else if (speed_val && strcmp(speed_val, "very_slow") == 0) {
            speed = 0.20f;
        } else if (speed_val && speed_val[0] != '\0') {
            char val_clean[64];
            strncpy(val_clean, speed_val, sizeof(val_clean) - 1);
            val_clean[sizeof(val_clean) - 1] = '\0';
            for (int k = 0; val_clean[k]; k++) {
                if (val_clean[k] == ',') val_clean[k] = '.';
            }
            float v = (float)atof(val_clean);
            if (v > 0.0001f && v <= 1.0f) speed = v;
            else if (v > 1.0f && v <= 100.0f) speed = v / 100.0f;
            else speed = gfx_theme_get_anim_speed();
        } else {
            speed = gfx_theme_get_anim_speed();
        }

        float diff = (float)selected_index - anim_selected_index;
        if (speed >= 0.99f || diff > 10.0f || diff < -10.0f) {
            anim_selected_index = (float)selected_index;
        } else if (diff > 0.001f || diff < -0.001f) {
            anim_selected_index += diff * speed;
        } else {
            anim_selected_index = (float)selected_index;
        }
        int start_i = (int)anim_selected_index - 5;
        if (start_i < 0) start_i = 0;
        int end_i = (int)anim_selected_index + 5;
        if (end_i >= entry_count) end_i = entry_count - 1;
        
        for (int i = start_i; i <= end_i; i++) {
            char display_name[MAX_FILENAME_DISPLAY_LEN + 4];
            get_scrolling_text(entries[i].name, (i == selected_index), display_name, sizeof(display_name));
            int item_selected = (i == selected_index) && !header_selected;
            render_menu_item(framebuffer, i, display_name, entries[i].path, entries[i].is_dir,
                            item_selected, anim_selected_index, 0);
        }
    } else {
        int grid_cols = get_grid_cols();
        int max_visible = (grid_cols > 1) ? visible_items * grid_cols : visible_items;
        for (int i = scroll_offset; i < entry_count && i < scroll_offset + max_visible; i++) {
            // Get display name (with scrolling for selected item)
            char display_name[MAX_FILENAME_DISPLAY_LEN + 4];
            get_scrolling_text(entries[i].name, (i == selected_index), display_name, sizeof(display_name));

            // Check if this item is favorited
            int is_favorited = 0;
            if (!entries[i].is_dir &&
                strcmp(current_path, ROMS_PATH) != 0 &&
                strcmp(current_path, "RECENT_GAMES") != 0 &&
                strcmp(current_path, "FAVORITES") != 0 &&
                strcmp(current_path, "TOOLS") != 0 &&
                strcmp(current_path, "UTILS") != 0 &&
                strcmp(current_path, "HOTKEYS") != 0 &&
                strcmp(current_path, "CREDITS") != 0) {
                char filename[256];
                char directory[256];
                strcpy(directory, entries[i].path);
                clean_path(directory);
                char *filename_path = strrchr(entries[i].path, '/');
                if (filename_path) snprintf(filename, sizeof(filename), "%s", filename_path + 1);
                else snprintf(filename, sizeof(filename), "%s", entries[i].name);

                is_favorited = favorites_is_favorited(directory, filename);
            }

            // v43: Don't highlight item if header is selected
            int item_selected = (i == selected_index) && !header_selected;
            render_menu_item(framebuffer, i, display_name, entries[i].path, entries[i].is_dir,
                            item_selected, scroll_offset, is_favorited);
        }
    }

    // v44: Hide legend and entry count when video browser is active or horizontal menu
    if (!vb_is_active() && !is_horizontal_menu()) {
        const char *hide_footer_val = settings_get_value("frogui_hide_footer");
        int hide_footer = (hide_footer_val && strcmp(hide_footer_val, "true") == 0);

        if (!hide_footer) {
            // Draw legend - determine X button mode based on current view
            int x_button_mode = LEGEND_X_NONE;
            if (strcmp(current_path, "FAVORITES") == 0) {
                // In favorites menu, show "X - REMOVE"
                x_button_mode = LEGEND_X_REMOVE;
            } else if (strcmp(current_path, ROMS_PATH) != 0 &&
                       strcmp(current_path, "RECENT_GAMES") != 0 &&
                       strcmp(current_path, "TOOLS") != 0 &&
                       strcmp(current_path, "UTILS") != 0 &&
                       strcmp(current_path, "HOTKEYS") != 0 &&
                       strcmp(current_path, "CREDITS") != 0) {
                // In ROM directories, show "X - FAVOURITE"
                x_button_mode = LEGEND_X_FAVOURITE;
            }
            render_legend(framebuffer, x_button_mode);
        }

        // Draw the "current entry/total entries" label in top-right, above the legend
        char entry_label[20];
        snprintf(entry_label, sizeof(entry_label), "%d/%d", selected_index + 1, entry_count); // 1-based indexing for display
        int label_width = font_measure_text(entry_label);
        int label_x = SCREEN_WIDTH - label_width - 12;  // Right-aligned, just above the legend
        int label_y = 8;  // Position it slightly below the top edge
        render_text_pillbox(framebuffer, label_x, label_y, entry_label, COLOR_LEGEND_BG, COLOR_LEGEND, 6);
    }

    // Render Letter Search popup
    if (letter_search_active) {
        int w = (SCREEN_WIDTH * 6) / 10; // 60% of screen width
        int h = 60;
        int x = (SCREEN_WIDTH - w) / 2;
        int y = (SCREEN_HEIGHT - h) / 2;
        
        render_fill_rect(framebuffer, x, y, w, h, COLOR_BG);
        render_rect(framebuffer, x, y, w, h, COLOR_TEXT);
        
        char title_text[] = "Search Letter";
        int t_w = font_measure_text(title_text);
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, x + (w - t_w) / 2, y + 10, title_text, COLOR_TEXT);
        
        char letter_text[8];
        snprintf(letter_text, sizeof(letter_text), "< %c >", letter_search_char);
        int l_w = font_measure_text(letter_text);
        
        render_fill_rect(framebuffer, x + 10, y + 30, w - 20, 20, COLOR_SELECT_BG);
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, x + (w - l_w) / 2, y + 32, letter_text, COLOR_SELECT_TEXT);
    }

    // Draw A-Z picker overlay if active
    if (az_picker_active) {
        // Draw centered background box using theme background color
        int box_width = 280;
        int box_height = 180;
        int box_x = (SCREEN_WIDTH - box_width) / 2;
        int box_y = (SCREEN_HEIGHT - box_height) / 2;
        render_fill_rect(framebuffer, box_x, box_y, box_width, box_height, COLOR_BG);

        // Draw title using theme colors
        const char *title = "QUICK JUMP";
        int title_width = font_measure_text(title);
        int title_x = (SCREEN_WIDTH - title_width) / 2;
        render_text_pillbox(framebuffer, title_x, 30, title, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 6);

        // Draw A-Z grid (7 columns x 4 rows = 28 slots)
        const char *labels[] = {
            "A", "B", "C", "D", "E", "F", "G",
            "H", "I", "J", "K", "L", "M", "N",
            "O", "P", "Q", "R", "S", "T", "U",
            "V", "W", "X", "Y", "Z", "0-9", "#"
        };

        int grid_start_x = 40;
        int grid_start_y = 70;
        int col_width = 38;
        int row_height = 30;

        for (int i = 0; i < 28; i++) {
            int col = i % 7;
            int row = i / 7;
            int x = grid_start_x + col * col_width;
            int y = grid_start_y + row * row_height;

            if (i == az_selected_index) {
                render_text_pillbox(framebuffer, x, y, labels[i], COLOR_SELECT_BG, COLOR_SELECT_TEXT, 6);
            } else {
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, x, y, labels[i], COLOR_TEXT);
            }
        }
    }

    // v44: Draw video browser overlay if active
    if (vb_is_active()) {
        vb_draw(framebuffer);
    }
}

// Pick and launch a random game by randomly navigating the menu
static void pick_random_game(void) {
    int max_attempts = 100;
    int attempts = 0;

    while (attempts < max_attempts) {
        attempts++;

        // Pick a random console directory from root
        strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
        scan_directory(current_path);

        // Filter out non-console entries
        int valid_console_count = 0;
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir &&
                strcmp(entries[i].path, "RECENT_GAMES") != 0 &&
                strcmp(entries[i].path, "FAVORITES") != 0 &&
                strcmp(entries[i].path, "RANDOM_GAME") != 0 &&
                strcmp(entries[i].path, "TOOLS") != 0) {
                valid_console_count++;
            }
        }

        if (valid_console_count == 0) {
            strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
            scan_directory(current_path);
            return;
        }

        // Pick a random console directory
        int random_console = rand() % valid_console_count;
        int console_idx = 0;
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir &&
                strcmp(entries[i].path, "RECENT_GAMES") != 0 &&
                strcmp(entries[i].path, "FAVORITES") != 0 &&
                strcmp(entries[i].path, "RANDOM_GAME") != 0 &&
                strcmp(entries[i].path, "TOOLS") != 0) {
                if (console_idx == random_console) {
                    strncpy(current_path, entries[i].path, sizeof(current_path) - 1);
                    break;
                }
                console_idx++;
            }
        }

        // Scan the console directory
        scan_directory(current_path);

        // Count files (not directories, not ..)
        int file_count = 0;
        for (int i = 0; i < entry_count; i++) {
            if (!entries[i].is_dir && strcmp(entries[i].name, "..") != 0) {
                file_count++;
            }
        }

        if (file_count == 0) {
            continue;
        }

        // Pick a random file
        int random_file = rand() % file_count;
        int file_idx = 0;
        for (int i = 0; i < entry_count; i++) {
            if (!entries[i].is_dir && strcmp(entries[i].name, "..") != 0) {
                if (file_idx == random_file) {
                    char core_name[256];
                    char filename[256];
                    char directory[256];
                    get_corename(entries[i].path, core_name, sizeof(core_name));
                    strcpy(directory, entries[i].path);
                    clean_path(directory);
                    char *filename_path = strrchr(entries[i].path, '/');
                    if (filename_path) snprintf(filename, sizeof(filename), "%s", filename_path + 1);
                    else snprintf(filename, sizeof(filename), "%s", entries[i].name);

                    init_direct_loader(core_name, directory, filename);
                    return;
                }
                file_idx++;
            }
        }
    }

    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
    scan_directory(current_path);
}

// v44: Helper to open file browser for a section with appropriate config
static void open_section_browser(MainSection section) {
    strncpy(current_path, "MAIN_MENU", sizeof(current_path) - 1);
    entry_count = 0;
    switch(section) {
        case SECTION_VIDEOS:
            vb_open_with_config("/mnt/sda1/VIDEOS", VB_FILTER_VIDEOS);
            break;
        case SECTION_IMAGES:
            vb_open_with_config("/mnt/sda1/IMAGES", VB_FILTER_IMAGES);
            break;
        case SECTION_MUSIC:
            vb_open_with_config("/mnt/sda1/MUSIC", VB_FILTER_MUSIC);
            break;
        case SECTION_TEXT:
            vb_open_with_config("/mnt/sda1/TEXT", VB_FILTER_TEXT);
            break;
        default:
            break;
    }
    vb_set_focused(0);
}

// v44: Check if section uses file browser
static int section_has_browser(MainSection section) {
    return section == SECTION_VIDEOS || section == SECTION_IMAGES ||
           section == SECTION_MUSIC || section == SECTION_TEXT;
}

/* =========================
   WAV STRUCT + HELPERS
   ========================= */

typedef struct {
    int sample_rate;
    int channels;
    int bits_per_sample;
    int num_samples;
    uint8_t *data;
} Wav;

static int read_le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static int read_le16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

bool wav_load(const uint8_t *buf, size_t size, Wav *out)
{
    if (size < 44) return false;
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
        return false;

    int pos = 12;

    while (pos + 8 <= (int)size)
    {
        int chunk_size = read_le32(buf + pos + 4);

        if (!memcmp(buf + pos, "fmt ", 4))
        {
            int format = read_le16(buf + pos + 8);
            out->channels = read_le16(buf + pos + 10);
            out->sample_rate = read_le32(buf + pos + 12);
            out->bits_per_sample = read_le16(buf + pos + 22);
            if (format != 1) return false; /* PCM only */
        }
        else if (!memcmp(buf + pos, "data", 4))
        {
            out->data = (uint8_t *)(buf + pos + 8);
            out->num_samples = chunk_size /
                (out->channels * (out->bits_per_sample / 8));
            return true;
        }

        pos += 8 + chunk_size;
    }

    return false;
}

/* =========================
   FILE LOADER
   ========================= */

bool load_file(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    *out_data = data;
    *out_size = size;
    return true;
}

/* =========================
   AUDIO ENGINE
   ========================= */

#define AUDIO_FRAMES 1024
#define MAX_SFX 8

typedef struct {
    const Wav *wav;
    int pos;
    int volume;   /* 0–256 */
    bool active;
} SfxVoice;

/* --- BGM --- */
static const Wav *bgm_wav = NULL;
static int bgm_pos = 0;
static int bgm_volume = 128;
static bool bgm_playing = false;

/* --- SFX --- */
static SfxVoice sfx[MAX_SFX];

/* =========================
   CONTROL FUNCTIONS
   ========================= */

void bgm_play(const Wav *wav, int volume)
{
    bgm_wav = wav;
    bgm_pos = 0;
    bgm_volume = volume;
    bgm_playing = true;
}

void bgm_stop(void)
{
    bgm_playing = false;
}

void sfx_play(const Wav *wav, int volume)
{
    for (int i = 0; i < MAX_SFX; i++)
    {
        if (!sfx[i].active)
        {
            sfx[i].wav = wav;
            sfx[i].pos = 0;
            sfx[i].volume = volume;
            sfx[i].active = true;
            return;
        }
    }
}

/* =========================
   MIXER
   ========================= */

static inline int16_t clamp16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

void output_wav_audio(void)
{
    if (!audio_batch_cb)
        return;

    int16_t buffer[AUDIO_FRAMES * 2] = {0};

    for (int i = 0; i < AUDIO_FRAMES; i++)
    {
        int mix_l = 0;
        int mix_r = 0;

        /* --- BGM (looping) --- */
        if (bgm_playing && bgm_wav)
        {
            if (bgm_pos >= bgm_wav->num_samples)
                bgm_pos = 0;

            int16_t l = 0, r = 0;

            if (bgm_wav->bits_per_sample == 16)
            {
                int16_t *pcm = (int16_t *)bgm_wav->data;
                if (bgm_wav->channels == 1)
                    l = r = pcm[bgm_pos];
                else {
                    l = pcm[bgm_pos * 2 + 0];
                    r = pcm[bgm_pos * 2 + 1];
                }
            }
            else
            {
                uint8_t *pcm = bgm_wav->data;
                if (bgm_wav->channels == 1)
                    l = r = ((int)pcm[bgm_pos] - 128) << 8;
                else {
                    l = ((int)pcm[bgm_pos * 2 + 0] - 128) << 8;
                    r = ((int)pcm[bgm_pos * 2 + 1] - 128) << 8;
                }
            }

            mix_l += (l * bgm_volume) >> 8;
            mix_r += (r * bgm_volume) >> 8;
            bgm_pos++;
        }

        /* --- SFX (one-shot) --- */
        for (int v = 0; v < MAX_SFX; v++)
        {
            SfxVoice *vc = &sfx[v];
            if (!vc->active) continue;

            if (vc->pos >= vc->wav->num_samples)
            {
                vc->active = false;
                continue;
            }

            int16_t l = 0, r = 0;

            if (vc->wav->bits_per_sample == 16)
            {
                int16_t *pcm = (int16_t *)vc->wav->data;
                if (vc->wav->channels == 1)
                    l = r = pcm[vc->pos];
                else {
                    l = pcm[vc->pos * 2 + 0];
                    r = pcm[vc->pos * 2 + 1];
                }
            }
            else
            {
                uint8_t *pcm = vc->wav->data;
                if (vc->wav->channels == 1)
                    l = r = ((int)pcm[vc->pos] - 128) << 8;
                else {
                    l = ((int)pcm[vc->pos * 2 + 0] - 128) << 8;
                    r = ((int)pcm[vc->pos * 2 + 1] - 128) << 8;
                }
            }

            mix_l += (l * vc->volume) >> 8;
            mix_r += (r * vc->volume) >> 8;
            vc->pos++;
        }

        buffer[i * 2 + 0] = clamp16(mix_l);
        buffer[i * 2 + 1] = clamp16(mix_r);
    }

    audio_batch_cb(buffer, AUDIO_FRAMES);
}

static Wav bgm;
static uint8_t *bgm_file;
static size_t bgm_file_size;

void audio_init(void) {
    if (!load_file("/mnt/sda1/frogui/menu_music.wav", &bgm_file, &bgm_file_size))
        return;

    if (!wav_load(bgm_file, bgm_file_size, &bgm))
        return;

    /* Start playing immediately */
    bgm_play(&bgm, 128);  // volume: 0–256
}

static Wav nav;
static uint8_t *nav_file;
static size_t nav_file_size;
bool nav_init_once = false;

void navigation_sfx(void) {
    if (!nav_init_once) {
        if (!load_file("/mnt/sda1/frogui/navigation.wav", &nav_file, &nav_file_size))
            return;

        if (!wav_load(nav_file, nav_file_size, &nav))
            return;
    
        nav_init_once = true;
    }
    sfx_play(&nav, 128);  // volume: 0–256
}

// Handle input
static void handle_input() {
    if (!input_poll_cb || !input_state_cb) return;

    input_poll_cb();

    // If game is queued, just show loading screen
    if (game_queued) {
        // Don't process any input
        return;
    }

    // Get current input state
    int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    int x = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    int y = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
    int l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
    int select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);

    int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);

    if ((prev_input[0] && !up) || (prev_input[1] && !down) || (prev_input[7] && !left) || (prev_input[8] && !right)) { // Play audio for up down left and right
        navigation_sfx();
    }

    // Flag to determine if the menu needs to be redrawn
    int input_changed = (prev_input[0] != up) || (prev_input[1] != down) || 
                        (prev_input[2] != a) || (prev_input[3] != b) ||
                        (prev_input[4] != l) || (prev_input[5] != r) || 
                        (prev_input[6] != select) || (prev_input[7] != left) || 
                        (prev_input[8] != right) || (prev_input[9] != x) || 
                        (prev_input[10] != y);

    // Handle SELECT button to open settings (on button release)
    if (prev_input[6] && !select) {
        if (settings_is_active() || display_opts_is_active()) {
            show_multicore_opt = !show_multicore_opt;
        } else {
            if (strcmp(current_path, ROMS_PATH) == 0) {
                show_multicore_opt = true;
            } else {
                show_multicore_opt = false;
            }
        }

        if (show_multicore_opt) {
            extern void display_opts_close(void);
            display_opts_close();
            // Main menu settings - reload and show multicore.opt
            settings_load();
            settings_show_menu();
        } else {
            extern void settings_close(void);
            settings_close();
            // Check if we're in a console folder that has core-specific settings
            char console_folder[256];
            const char *slash = strrchr(current_path, '/');
            if (slash && slash != current_path) {
                // Extract folder name from path like "/mnt/sda1/ROMS/gb"
                strcpy(console_folder, slash + 1);
                // The display options menu acts as core settings now
                display_opts_show_menu(console_folder);
            } else if (strcmp(current_path, ROMS_PATH) == 0) {
                // If in root, perhaps show main FrogUI display options
                display_opts_show_menu("FrogUI");
            }
        }
        prev_input[6] = select;
        render_menu();
        return;
    }

    // Get visible items count (respects gfx_theme layout if active)
    int visible_items = render_get_visible_items();

    // v44: File browser input handling (for VIDEOS, IMAGES, MUSIC, TEXT sections)
    if (vb_is_active()) {
        // If header is selected, handle header navigation (LEFT/RIGHT/DOWN)
        if (header_selected) {
            vb_set_focused(0);  // No highlight in browser when header selected
            extern int is_section_hidden(const char *section_name);
            // LEFT - previous section (skip hidden)
            if (prev_input[7] && !left) {
                vb_close();
                int start_sec = current_section;
                do {
                    if (current_section > 0) {
                        current_section--;
                    } else {
                        current_section = SECTION_COUNT - 1;
                    }
                } while (is_section_hidden(section_names[current_section]) && current_section != start_sec);

                // Handle new section
                if (current_section == SECTION_SYSTEMS) {
                    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                    scan_directory(current_path);
                } else if (current_section == SECTION_TOOLS) {
                    show_tools_menu();
                } else if (section_has_browser(current_section)) {
                    open_section_browser(current_section);
                }
                selected_index = 0;
                scroll_offset = 0;
            }
            // RIGHT - next section (skip hidden)
            if (prev_input[8] && !right) {
                vb_close();
                int start_sec = current_section;
                do {
                    if (current_section < SECTION_COUNT - 1) {
                        current_section++;
                    } else {
                        current_section = 0;
                    }
                } while (is_section_hidden(section_names[current_section]) && current_section != start_sec);

                // Handle new section
                if (current_section == SECTION_SYSTEMS) {
                    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                    scan_directory(current_path);
                } else if (current_section == SECTION_TOOLS) {
                    show_tools_menu();
                } else if (section_has_browser(current_section)) {
                    open_section_browser(current_section);
                }
                selected_index = 0;
                scroll_offset = 0;
            }
            // DOWN - go into browser
            if (prev_input[1] && !down) {
                header_selected = 0;
                vb_set_focused(1);  // Show highlight when entering browser
            }
            // v61: B at header - do nothing (browser stays open, use LEFT/RIGHT to change sections)
            // User can press DOWN to go back into browser
            if (prev_input[3] && !b) {
                // Don't close browser - just stay at header
                // (no action needed, user can use LEFT/RIGHT or DOWN)
            }
        } else {
            vb_set_focused(1);  // Browser has focus
            // Header not selected - browser handles input
            if (vb_handle_input(prev_input[0] && !up, prev_input[1] && !down,
                                prev_input[7] && !left, prev_input[8] && !right,
                                prev_input[2] && !a, prev_input[3] && !b)) {
                // Check if a file was selected
                if (vb_file_was_selected()) {
                    // v47: If video file selected, open video player
                    if (vb_get_filter_mode() == VB_FILTER_VIDEOS) {
                        const char *video_path = vb_get_selected_path();
                        if (video_path && video_path[0] != '\0') {
                            vp_open(video_path);
                        }
                    }
                    // v52: If music file selected, open music player
                    else if (vb_get_filter_mode() == VB_FILTER_MUSIC) {
                        const char *music_path = vb_get_selected_path();
                        if (music_path && music_path[0] != '\0') {
                            mp_open(music_path);
                        }
                    }
                    // v56: If image file selected, open image viewer
                    else if (vb_get_filter_mode() == VB_FILTER_IMAGES) {
                        const char *image_path = vb_get_selected_path();
                        if (image_path && image_path[0] != '\0') {
                            iv_open(image_path);
                        }
                    }
                    // v59: If text file selected, open text viewer
                    else if (vb_get_filter_mode() == VB_FILTER_TEXT) {
                        const char *text_path = vb_get_selected_path();
                        if (text_path && text_path[0] != '\0') {
                            text_editor_open_viewer(text_path);
                        }
                    }
                }
                // Check if browser was closed (B pressed at root)
                // v60: Stay on current section, just go back to header/section selector
                if (!vb_is_active()) {
                    // Don't change current_section - stay on the same section
                    header_selected = 1;  // Go to section selector
                }
                // Check if user wants to go back to header (UP at top)
                if (vb_wants_go_to_header()) {
                    header_selected = 1;
                    vb_set_focused(0);  // Remove highlight when going to header
                }
            }
        }
        // Update prev_input and return
        prev_input[0] = up;
        prev_input[1] = down;
        prev_input[2] = a;
        prev_input[3] = b;
        prev_input[4] = l;
        prev_input[5] = r;
        prev_input[6] = select;
        prev_input[7] = left;
        prev_input[8] = right;
        prev_input[9] = x;
        prev_input[10] = y;
        return;
    }

    // v24: Check if display options menu should handle input
    if (display_opts_is_active()) {
        display_opts_handle_input(prev_input[0] && !up, prev_input[1] && !down,
                                 prev_input[7] && !left, prev_input[8] && !right,
                                 prev_input[2] && !a, prev_input[3] && !b);
        // Update prev_input and return
        prev_input[0] = up;
        prev_input[1] = down;
        prev_input[2] = a;
        prev_input[3] = b;
        prev_input[4] = l;
        prev_input[5] = r;
        prev_input[6] = select;
        prev_input[7] = left;
        prev_input[8] = right;
        prev_input[9] = x;
        prev_input[10] = y;
        return;
    }

    // v31: Check if display_opts just closed with save - rescan directory
    if (display_opts_needs_rescan()) {
        scan_directory(current_path);
        // Reset selection to top
        selected_index = 0;
        scroll_offset = 0;
    }

    // v34: Check if text editor should handle input
    if (text_editor_is_active()) {
        if (text_editor_handle_input(prev_input[0] && !up, prev_input[1] && !down,
                                     prev_input[7] && !left, prev_input[8] && !right,
                                     prev_input[2] && !a, prev_input[3] && !b,
                                     prev_input[9] && !x, prev_input[10] && !y,
                                     prev_input[4] && !l, prev_input[5] && !r)) {
            // Text editor closed
            // v78: Check if returning to file manager
            if (fm_check_return()) {
                // Return to file manager
            } else {
                // Reload GFX theme if saved (for theme.ini editing)
                if (text_editor_was_saved()) {
                    // Reload current theme to apply changes
                    int current_idx = gfx_theme_get_current_index();
                    if (current_idx > 0) {
                        gfx_theme_apply(0);  // Clear first
                        gfx_theme_apply(current_idx);  // Re-apply
                    }
                }
            }
        }
        // Update prev_input and return
        prev_input[0] = up; prev_input[1] = down; prev_input[2] = a; prev_input[3] = b;
        prev_input[4] = l; prev_input[5] = r; prev_input[6] = select;
        prev_input[7] = left; prev_input[8] = right; prev_input[9] = x; prev_input[10] = y;
        return;
    }

    int prev_was_settings = settings_is_active();
    // Check if settings menu should handle input
    if (settings_handle_input(prev_input[0] && !up, prev_input[1] && !down,
                            prev_input[7] && !left, prev_input[8] && !right,
                            prev_input[2] && !a, prev_input[3] && !b, 
                            prev_input[9] && !x, prev_input[10] && !y)) {
        if (prev_was_settings && !settings_is_active()) {
            // Settings menu just closed - rescan directory to apply section hiding changes
            char last_name[256] = "";
            if (selected_index >= 0 && selected_index < entry_count) {
                strncpy(last_name, entries[selected_index].name, sizeof(last_name) - 1);
            }
            scan_directory(current_path);
            apply_settings();
            if (selected_index >= entry_count && entry_count > 0) {
                selected_index = entry_count - 1;
            } else if (last_name[0] != '\0') {
                for (int i = 0; i < entry_count; i++) {
                    if (strcmp(entries[i].name, last_name) == 0) {
                        selected_index = i;
                        break;
                    }
                }
            }
        }
        // Settings consumed the input, update prev_input and return
        prev_input[0] = up;
        prev_input[1] = down;
        prev_input[2] = a;
        prev_input[3] = b;
        prev_input[4] = l;
        prev_input[5] = r;
        prev_input[6] = select;
        prev_input[7] = left;
        prev_input[8] = right;
        prev_input[9] = x;
        prev_input[10] = y;
        return;
    }

    // Handle Letter Search input
    if (letter_search_active) {
        if (prev_input[0] && !up) {
            letter_search_char++;
            if (letter_search_char == 'Z' + 1) letter_search_char = '0';
            else if (letter_search_char == '9' + 1) letter_search_char = 'A';
        }
        if (prev_input[1] && !down) {
            letter_search_char--;
            if (letter_search_char == '0' - 1) letter_search_char = 'Z';
            else if (letter_search_char == 'A' - 1) letter_search_char = '9';
        }
        if (prev_input[7] && !left) {
            letter_search_char--;
            if (letter_search_char == '0' - 1) letter_search_char = 'Z';
            else if (letter_search_char == 'A' - 1) letter_search_char = '9';
        }
        if (prev_input[8] && !right) {
            letter_search_char++;
            if (letter_search_char == 'Z' + 1) letter_search_char = '0';
            else if (letter_search_char == '9' + 1) letter_search_char = 'A';
        }
        if ((prev_input[2] && !a)) {
            // Confirm search
            letter_search_active = 0;
            char lower_char = (letter_search_char >= 'A' && letter_search_char <= 'Z') ? (letter_search_char + 32) : letter_search_char;
            char upper_char = letter_search_char;
            
            int last_before = -1;
            int found = 0;
            
            for (int i = 0; i < entry_count; i++) {
                char first = entries[i].name[0];
                if (first == lower_char || first == upper_char) {
                    selected_index = i;
                    found = 1;
                    break;
                }
                
                // Track entries that come before the selected letter alphabetically
                char check_upper = (first >= 'a' && first <= 'z') ? (first - 32) : first;
                if (check_upper >= 'A' && check_upper <= 'Z' && check_upper < upper_char) {
                    last_before = i;
                } else if (upper_char >= '0' && upper_char <= '9') {
                     // if searching for numbers, we just pick the first number if any
                }
            }
            
            if (!found && last_before >= 0) {
                selected_index = last_before;
            }
            
            anim_selected_index = (float)selected_index;
            int grid_cols = get_grid_cols();
            int visible_items = render_get_visible_items();
            int max_visible = visible_items * grid_cols;
            
            if (is_horizontal_menu()) {
                scroll_offset = selected_index;
            } else {
                if (selected_index >= scroll_offset + max_visible) {
                    scroll_offset = ((selected_index - max_visible + 1) / grid_cols + 1) * grid_cols;
                } else if (selected_index < scroll_offset) {
                    scroll_offset = (selected_index / grid_cols) * grid_cols;
                }
            }
        }
        if ((prev_input[5] && !b) || (prev_input[10] && !y)) {
            letter_search_active = 0;
        }
        // Consume inputs
        prev_input[0] = up; prev_input[1] = down; prev_input[2] = a; prev_input[3] = b;
        prev_input[6] = select; prev_input[7] = left; prev_input[8] = right;
        prev_input[9] = x; prev_input[10] = y;
        return;
    }

    // Handle A-Z picker input
    if (az_picker_active) {
        // Navigate the A-Z grid
        if (prev_input[0] && !up) { // UP
            if (az_selected_index >= 7) az_selected_index -= 7;
        }
        if (prev_input[1] && !down) { // DOWN
            if (az_selected_index < 21) az_selected_index += 7;
        }
        if (prev_input[7] && !left) { // LEFT - stay in same row
            int col = az_selected_index % 7;
            if (col > 0) az_selected_index--;
        }
        if (prev_input[8] && !right) { // RIGHT - stay in same row
            int col = az_selected_index % 7;
            if (col < 6) az_selected_index++;
        }

        // A button - select letter and jump
        if (prev_input[2] && !a) {
            const char *search_chars[] = {
                "A", "B", "C", "D", "E", "F", "G",
                "H", "I", "J", "K", "L", "M", "N",
                "O", "P", "Q", "R", "S", "T", "U",
                "V", "W", "X", "Y", "Z", "0", ""
            };

            char first_char = search_chars[az_selected_index][0];
            int found = 0;
            int last_before = -1;  // v79: Track last entry before selected letter

            // Find first entry starting with this letter (case insensitive)
            // v79: Also track last entry that comes before this letter
            for (int i = 0; i < entry_count; i++) {
                char entry_first = entries[i].name[0];
                if (entry_first >= 'a' && entry_first <= 'z') {
                    entry_first = entry_first - 'a' + 'A'; // Convert to uppercase
                }

                // Handle 0-9 category
                if (az_selected_index == 26 && entry_first >= '0' && entry_first <= '9') {
                    selected_index = i;
                    found = 1;
                    break;
                }
                // Handle # category (special characters)
                else if (az_selected_index == 27 &&
                         !((entry_first >= 'A' && entry_first <= 'Z') ||
                           (entry_first >= '0' && entry_first <= '9'))) {
                    selected_index = i;
                    found = 1;
                    break;
                }
                // Handle A-Z
                else if (az_selected_index < 26) {
                    if (entry_first == first_char) {
                        selected_index = i;
                        found = 1;
                        break;
                    }
                    // v79: Track entries that come before the selected letter
                    if (entry_first >= 'A' && entry_first <= 'Z' && entry_first < first_char) {
                        last_before = i;
                    }
                }
            }

            // v79: If no exact match found, use last entry before this letter
            if (!found && last_before >= 0) {
                selected_index = last_before;
            }

            az_picker_active = 0;
        }

        // B button - cancel
        if (prev_input[3] && !b) {
            az_picker_active = 0;
        }

        // Update prev_input and return (picker consumed input)
        prev_input[0] = up;
        prev_input[1] = down;
        prev_input[2] = a;
        prev_input[3] = b;
        prev_input[7] = left;
        prev_input[8] = right;
        return;
    }

    // v43: Check if we're at main menu for section switching
    int at_main_menu = (strcmp(current_path, ROMS_PATH) == 0 || strcmp(current_path, "MAIN_MENU") == 0 || strcmp(current_path, "TOOLS") == 0);

    // v43: Handle LEFT/RIGHT for section switching when header is selected
    if (at_main_menu && header_selected) {
        extern int is_section_hidden(const char *section_name);
        if (prev_input[7] && !left) {
            // Switch to previous section (skip hidden)
            int start_sec = current_section;
            do {
                if (current_section > 0) {
                    current_section--;
                } else {
                    current_section = SECTION_COUNT - 1;
                }
            } while (is_section_hidden(section_names[current_section]) && current_section != start_sec);

            // Update view for new section
            if (current_section == SECTION_SYSTEMS) {
                strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                scan_directory(current_path);
            } else if (current_section == SECTION_TOOLS) {
                show_tools_menu();
            } else if (section_has_browser(current_section)) {
                open_section_browser(current_section);
            }
            selected_index = 0;
            scroll_offset = 0;
            header_selected = 1;  // Stay on header
        }
        if (prev_input[8] && !right) {
            // Switch to next section (skip hidden)
            int start_sec = current_section;
            do {
                if (current_section < SECTION_COUNT - 1) {
                    current_section++;
                } else {
                    current_section = 0;
                }
            } while (is_section_hidden(section_names[current_section]) && current_section != start_sec);

            // Update view for new section
            if (current_section == SECTION_SYSTEMS) {
                strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                scan_directory(current_path);
            } else if (current_section == SECTION_TOOLS) {
                show_tools_menu();
            } else if (section_has_browser(current_section)) {
                open_section_browser(current_section);
            }
            selected_index = 0;
            scroll_offset = 0;
            header_selected = 1;  // Stay on header
        }
        // Update prev_input for left/right
        prev_input[7] = left;
        prev_input[8] = right;
    }

    // Handle X button to hide highlighted section on main platform menu
    if (render_is_in_platform_menu() && !header_selected && prev_input[9] && !x) {
        if (selected_index >= 0 && selected_index < entry_count) {
            if (entries[selected_index].is_dir && strcmp(entries[selected_index].name, "..") != 0) {
                int old_idx = selected_index;
                char sec_to_hide[256];
                strncpy(sec_to_hide, entries[selected_index].name, sizeof(sec_to_hide) - 1);
                sec_to_hide[sizeof(sec_to_hide) - 1] = '\0';

                extern void toggle_section_hidden(const char *section_name);
                toggle_section_hidden(sec_to_hide);
                scan_directory(current_path);

                if (old_idx < entry_count) {
                    selected_index = old_idx;
                } else if (entry_count > 0) {
                    selected_index = entry_count - 1;
                } else {
                    selected_index = 0;
                }
                anim_selected_index = (float)selected_index;
                int visible_items = render_get_visible_items();
                if (selected_index >= scroll_offset + visible_items) {
                    scroll_offset = selected_index - visible_items + 1;
                } else if (selected_index < scroll_offset) {
                    scroll_offset = selected_index;
                }
            }
        }
        prev_input[9] = x;
        return;
    }

    // Handle X button to open Letter Search (on button release)
    // Only when header is NOT selected
    if (!header_selected && prev_input[9] && !x) {
        // Don't activate in special menus
        if (strcmp(current_path, "RECENT_GAMES") != 0 &&
            strcmp(current_path, "FAVORITES") != 0 &&
            strcmp(current_path, "TOOLS") != 0 &&
            strcmp(current_path, "UTILS") != 0 &&
            strcmp(current_path, "HOTKEYS") != 0 &&
            strcmp(current_path, "CREDITS") != 0 &&
            strcmp(current_path, "MAIN_MENU") != 0 &&
            entry_count > 0) {
            
            letter_search_active = 1;
            
            // Initialize search letter based on current item if possible
            char c = entries[selected_index].name[0];
            if (c >= 'a' && c <= 'z') c -= 32;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                letter_search_char = c;
            } else {
                letter_search_char = 'A';
            }
        }
    }

    // (Removed old SELECT handler)

    // Scoped Navigation based on Layout Mode (Horizontal vs Vertical vs Grid)
    if (is_horizontal_menu()) {
        // --- HORIZONTAL MENU MODE (Navigation: LEFT / RIGHT only) ---
        if (!header_selected) {
            if (prev_input[7] && !left) { // LEFT
                if (selected_index > 0) {
                    selected_index--;
                } else if (entry_count > 0) {
                    selected_index = entry_count - 1; // Wrap around
                }
                prev_input[7] = left;
            }
            if (prev_input[8] && !right) { // RIGHT
                if (selected_index < entry_count - 1) {
                    selected_index++;
                } else if (entry_count > 0) {
                    selected_index = 0; // Wrap around
                }
                prev_input[8] = right;
            }
        }
        if (prev_input[0] && !up) { // UP
            if (at_main_menu && !header_selected) {
                header_selected = 1;
            }
        }
        if (prev_input[1] && !down) { // DOWN
            if (header_selected) {
                header_selected = 0;
            }
        }
    } else {
        // --- VERTICAL LIST / GRID MENU MODE ---
        int grid_cols = get_grid_cols();
        int visible_items = render_get_visible_items();
        int visible_total = (grid_cols > 1) ? visible_items * grid_cols : visible_items;

        if (!header_selected && grid_cols == 1) {
            if ((prev_input[7] && !left) || (prev_input[8] && !right)) { // LEFT or RIGHT
                if (at_main_menu) {
                    header_selected = 1;
                }
                if (prev_input[7] && !left) prev_input[7] = left;
                if (prev_input[8] && !right) prev_input[8] = right;
            }
        }

        // Handle LEFT/RIGHT for Grid (2D mode cols > 1)
        if (!header_selected && grid_cols > 1) {
            if (prev_input[7] && !left) { // LEFT
                if (selected_index % grid_cols > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset) {
                        scroll_offset = (selected_index / grid_cols) * grid_cols;
                    }
                }
                prev_input[7] = left;
            }
            if (prev_input[8] && !right) { // RIGHT
                if ((selected_index % grid_cols < grid_cols - 1) && (selected_index + 1 < entry_count)) {
                    selected_index++;
                    if (selected_index >= scroll_offset + visible_total) {
                        scroll_offset = ((selected_index - visible_total + 1) / grid_cols + 1) * grid_cols;
                    }
                }
                prev_input[8] = right;
            }
        }

        // Handle UP for Vertical (1D list) or Grid (2D)
        if (prev_input[0] && !up) { // UP
            if (header_selected) {
                // Stay on header
            } else if (selected_index >= grid_cols) {
                selected_index -= grid_cols;
                if (selected_index < scroll_offset) {
                    scroll_offset = (selected_index / grid_cols) * grid_cols;
                }
            } else {
                if (at_main_menu) {
                    header_selected = 1;
                } else if (entry_count > 0) {
                    int last_row_start = ((entry_count - 1) / grid_cols) * grid_cols;
                    int target_idx = last_row_start + (selected_index % grid_cols);
                    if (target_idx >= entry_count) target_idx = entry_count - 1;
                    selected_index = target_idx;
                    if (selected_index >= scroll_offset + visible_total) {
                        scroll_offset = (selected_index / grid_cols) * grid_cols;
                        if (scroll_offset >= visible_total) {
                            scroll_offset = ((selected_index - visible_total + 1) / grid_cols + 1) * grid_cols;
                        }
                    }
                }
            }
            prev_input[0] = up;
        }

        // Handle DOWN for Vertical (1D list) or Grid (2D)
        if (prev_input[1] && !down) { // DOWN
            if (header_selected) {
                header_selected = 0;
                selected_index = 0;
                scroll_offset = 0;
            } else if (selected_index + grid_cols < entry_count) {
                selected_index += grid_cols;
                if (selected_index >= scroll_offset + visible_total) {
                    scroll_offset = ((selected_index - visible_total + 1) / grid_cols + 1) * grid_cols;
                }
            } else if (selected_index < entry_count - 1 && (selected_index / grid_cols) < ((entry_count - 1) / grid_cols)) {
                selected_index = entry_count - 1;
                if (selected_index >= scroll_offset + visible_total) {
                    scroll_offset = ((selected_index - visible_total + 1) / grid_cols + 1) * grid_cols;
                }
            } else {
                if (at_main_menu) {
                    header_selected = 1;
                } else {
                    selected_index = selected_index % grid_cols;
                    if (selected_index >= entry_count) selected_index = 0;
                    scroll_offset = 0;
                }
            }
            prev_input[1] = down;
        }
    }

    // Handle L button (page up)
    if (prev_input[4] && !l) {
        int grid_cols = get_grid_cols();
        int step = (grid_cols > 1) ? visible_items * grid_cols : 7;
        if (selected_index >= step) {
            selected_index -= step;
            if (selected_index < scroll_offset) {
                scroll_offset = (grid_cols > 1) ? (selected_index / grid_cols) * grid_cols : selected_index;
            }
        } else {
            int is_main_section = (strcmp(current_path, ROMS_PATH) == 0 ||
                                   strcmp(current_path, "MAIN_MENU") == 0 ||
                                   strcmp(current_path, "TOOLS") == 0);
            if (is_main_section && !header_selected) {
                header_selected = 1;
                vb_set_focused(0);
            } else {
                selected_index = entry_count > 0 ? entry_count - 1 : 0;
                if (grid_cols > 1) {
                    int visible_total = visible_items * grid_cols;
                    scroll_offset = (entry_count > visible_total) ? ((entry_count - visible_total + grid_cols - 1) / grid_cols) * grid_cols : 0;
                } else {
                    scroll_offset = (entry_count > visible_items) ? entry_count - visible_items : 0;
                }
                if (is_main_section) {
                    header_selected = 0;
                }
            }
        }
    }

    // Handle R button (page down)
    if (prev_input[5] && !r) {
        int grid_cols = get_grid_cols();
        int step = (grid_cols > 1) ? visible_items * grid_cols : 7;
        int visible_total = (grid_cols > 1) ? visible_items * grid_cols : visible_items;
        if (selected_index < entry_count - step) {
            selected_index += step;
        } else {
            selected_index = (entry_count > 0) ? (selected_index + step) % entry_count : 0;
        }
        if (selected_index >= scroll_offset + visible_total) {
            if (grid_cols > 1) {
                scroll_offset = ((selected_index - visible_total + 1) / grid_cols + 1) * grid_cols;
            } else {
                scroll_offset = selected_index - visible_items + 1;
            }
        } else if (selected_index < scroll_offset) {
            scroll_offset = (grid_cols > 1) ? (selected_index / grid_cols) * grid_cols : selected_index;
        }
    }

    // Handle Y button (toggle favorite / remove from favorites) - on button release
    if (prev_input[10] && !y && entry_count > 0) {
        MenuEntry *entry = &entries[selected_index];

        // Handle removing from favorites when in FAVORITES view
        if (strcmp(current_path, "FAVORITES") == 0) {
            // Don't allow removing the ".." back entry
            if (!entry->is_dir && strcmp(entry->name, "..") != 0) {
                // Remove this favorite by index
                favorites_remove_by_index(selected_index);

                // Refresh the favorites list
                show_favorites();

                // Adjust selection if needed
                int new_count = favorites_get_count();
                if (new_count == 0) {
                    selected_index = 0; // Select the ".." entry
                } else if (selected_index >= new_count) {
                    selected_index = new_count - 1;
                }

                // Reset scroll offset if needed
                if (selected_index < scroll_offset) {
                    scroll_offset = selected_index;
                }
            }
        }
        // If on header bar or main platform menu, Y button hides the selected section!
        else if (header_selected || render_is_in_platform_menu()) {
            extern void toggle_section_hidden(const char *section_name);
            if (header_selected) {
                toggle_section_hidden(section_names[current_section]);
                int start_sec = current_section;
                do {
                    current_section = (current_section + 1) % SECTION_COUNT;
                } while (is_section_hidden(section_names[current_section]) && current_section != start_sec);

                if (current_section == SECTION_SYSTEMS) {
                    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                    scan_directory(current_path);
                } else if (current_section == SECTION_TOOLS) {
                    show_tools_menu();
                } else if (section_has_browser(current_section)) {
                    open_section_browser(current_section);
                }
            } else if (entry->is_dir && strcmp(entry->name, "Back") != 0 && strcmp(entry->name, "..") != 0) {
                toggle_section_hidden(entry->name);
                if (strcmp(current_path, "TOOLS") == 0) {
                    show_tools_menu();
                } else {
                    scan_directory(current_path);
                }
            }
        }
        // Only allow favoriting in ROM directories (not in special menus)
        else if (!entry->is_dir &&
            strcmp(current_path, "RECENT_GAMES") != 0 &&
            strcmp(current_path, "FAVORITES") != 0 &&
            strcmp(current_path, "TOOLS") != 0 &&
            strcmp(current_path, "UTILS") != 0 &&
            strcmp(current_path, "HOTKEYS") != 0 &&
            strcmp(current_path, "CREDITS") != 0 &&
            strcmp(current_path, ROMS_PATH) != 0) {

            // Get core name and filename
            char core_name[256];
            char filename[256];
            char directory[256];
            get_corename(entry->path, core_name, sizeof(core_name));
            strcpy(directory, entry->path);
            clean_path(directory);
            char *filename_path = strrchr(entry->path, '/');
            if (filename_path) snprintf(filename, sizeof(filename), "%s", filename_path + 1);
            else snprintf(filename, sizeof(filename), "%s", entry->name);

            // Toggle favorite
            favorites_toggle(core_name, filename, directory);
        }
    }

    // v79: File manager requires holding A for 6 seconds
    // Check if we're on FILEMANAGER entry and A is being held
    int on_filemanager = 0;
    if (entry_count > 0 && selected_index < entry_count && !header_selected) {
        MenuEntry *entry = &entries[selected_index];
        if (entry->is_dir && strcmp(entry->path, "FILEMANAGER") == 0) {
            on_filemanager = 1;
        }
    }

    if (on_filemanager && a) {
        // A is held on FILEMANAGER - use real timer
        if (!fm_hold_active) {
            // Start timing
#ifdef SF2000
            fm_hold_start = os_get_tick_count();
#else
            fm_hold_start = time(NULL) * 1000;
#endif
            fm_hold_active = 1;
        } else {
            // Check if 6 seconds passed
#ifdef SF2000
            uint32_t now = os_get_tick_count();
#else
            uint32_t now = time(NULL) * 1000;
#endif
            if (now - fm_hold_start >= FM_HOLD_MS) {
                // 6 seconds reached - open file manager
                fm_open();
                fm_hold_active = 0;
            }
        }
    } else {
        // Reset timer if not holding A on FILEMANAGER
        fm_hold_active = 0;
    }

    // Handle A button (select) - on button release
    // v43: Also handle A when header is selected (enter first item or do nothing for empty sections)
    // v79: Skip FILEMANAGER - it requires holding A for 6 seconds
    if (prev_input[2] && !a && !on_filemanager) {
        // v43: If header selected, deselect header and go to first item (if any)
        if (header_selected) {
            header_selected = 0;
            selected_index = 0;
            scroll_offset = 0;
            // If no entries (empty section), just stay at header
            if (entry_count == 0) {
                header_selected = 1;
            }
            prev_input[2] = a;
            return;
        }

        if (entry_count == 0) {
            prev_input[2] = a;
            return;
        }

        MenuEntry *entry = &entries[selected_index];

        // v43: Reset header_selected when navigating
        header_selected = 0;

        if (strcmp(entry->name, "..") == 0) {
            // Go to parent directory
            char *last_slash = strrchr(current_path, '/');
            if (last_slash && last_slash != current_path) {
                // Remember which directory we're leaving so we can restore position
                char prev_dir[256];
                strncpy(prev_dir, last_slash + 1, sizeof(prev_dir) - 1);
                prev_dir[sizeof(prev_dir) - 1] = '\0';

                *last_slash = '\0';
                scan_directory(current_path);

                // Find the directory we just left and restore selection to it
                for (int i = 0; i < entry_count; i++) {
                    if (strcmp(entries[i].name, prev_dir) == 0) {
                        selected_index = i;
                        // Update scroll offset to keep selection visible
                        if (selected_index < scroll_offset) {
                            scroll_offset = selected_index;
                        } else if (selected_index >= scroll_offset + visible_items) {
                            scroll_offset = selected_index - visible_items + 1;
                        }
                        break;
                    }
                }
            }
        } else if (entry->is_dir) {
            // Enter directory
            if (strcmp(entry->path, "RECENT_GAMES") == 0) {
                // Show recent games list
                show_recent_games();
                strncpy(current_path, "RECENT_GAMES", sizeof(current_path) - 1);
            } else if (strcmp(entry->path, "CORES_MENU") == 0) {
                // Open Settings Menu for Cores
                extern void display_opts_close(void);
                display_opts_close();
                settings_load();
                settings_show_menu();
            } else if (strcmp(entry->path, "FAVORITES") == 0) {
                // Show favorites list
                show_favorites();
                strncpy(current_path, "FAVORITES", sizeof(current_path) - 1);
            } else if (strcmp(entry->path, "RANDOM_GAME") == 0) {
                // Pick and launch a random game
                pick_random_game();
                return;
            } else if (strcmp(entry->path, "TOOLS") == 0) {
                // Show tools menu
                show_tools_menu();
                strncpy(current_path, "TOOLS", sizeof(current_path) - 1);
            } else if (strcmp(entry->path, "CALCULATOR") == 0) {
                // Open calculator
                calc_open();
            } else if (strcmp(entry->path, "FILEMANAGER") == 0) {
                // Open file manager (v76)
                fm_open();
            } else if (strcmp(entry->path, "HOTKEYS") == 0) {
                // Show hotkeys screen
                show_hotkeys_screen();
                strncpy(current_path, "HOTKEYS", sizeof(current_path) - 1);
            } else if (strcmp(entry->path, "CREDITS") == 0) {
                // Show credits screen
                show_credits_screen();
                strncpy(current_path, "CREDITS", sizeof(current_path) - 1);
            } else if (strcmp(entry->path, "UTILS") == 0) {
                // Show utils menu
                show_utils_menu();
                strncpy(current_path, "UTILS", sizeof(current_path) - 1);
            } else {
                strncpy(current_path, entry->path, sizeof(current_path) - 1);

                // v24: Load display options when entering a platform folder
                // Check if we're entering a direct child of ROMS_PATH (a platform folder)
                const char *slash = strrchr(current_path, '/');
                if (slash) {
                    // Check if parent is ROMS_PATH
                    size_t parent_len = slash - current_path;
                    if (parent_len == strlen(ROMS_PATH) &&
                        strncmp(current_path, ROMS_PATH, parent_len) == 0) {
                        // This is a platform folder - load display options using folder name
                        const char *folder_name = slash + 1;
                        display_opts_load(folder_name);
                    }
                }

                scan_directory(current_path);
            }
        } else {
            // File selected - try to launch it
            char core_name[256];
            char filename[256];
            char directory[256];

            // Check if we're in Utils
            if (strcmp(current_path, "UTILS") == 0) {
                // Handle "Rebuild folder cache" action
                if (strcmp(entry->path, "REBUILD_CACHE") == 0) {
                    rebuild_empty_dirs_cache();
                    // Go back to ROMS root after rebuild
                    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
                    scan_directory(current_path);
                    return;
                }

                init_direct_loader("js2000", "js2000", entry->name);
                if (input_changed) render_menu();
                return;
            }
            
            // Check if we're in Recent games
            if (strcmp(current_path, "RECENT_GAMES") == 0) {
                // Parse core_name;game_name from entry->path
                char *separator = strchr(entry->path, ';');
                if (!separator) {
                    return; // Invalid format
                }

                // Copy core_name
                size_t core_len = (size_t)(separator - entry->path);
                if (core_len >= sizeof(core_name))
                    core_len = sizeof(core_name) - 1;

                memcpy(core_name, entry->path, core_len);
                core_name[core_len] = '\0';

                // Copy filename
                snprintf(filename, sizeof(filename), "%s", separator + 1);

                // For recent games, get the full_path from the RecentGame structure
                const RecentGame* recent_list = recent_games_get_list();
                int recent_count = recent_games_get_count();

                for (int i = 0; i < recent_count; i++) {
                    if (strcmp(recent_list[i].core_name, core_name) == 0 &&
                        strcmp(recent_list[i].game_name, filename) == 0) {
                        strcpy(directory, recent_list[i].full_path);
                        break;
                    }
                }
            } else if (strcmp(current_path, "FAVORITES") == 0) {
                // Parse core_name;game_name from entry->path
                char *separator = strchr(entry->path, ';');
                if (!separator) {
                    return; // Invalid format
                }

                // Copy core_name
                size_t core_len = (size_t)(separator - entry->path);
                if (core_len >= sizeof(core_name))
                    core_len = sizeof(core_name) - 1;

                memcpy(core_name, entry->path, core_len);
                core_name[core_len] = '\0';

                // Copy filename
                snprintf(filename, sizeof(filename), "%s", separator + 1);

                // For favorites, get the full_path from the FavoriteGame structure
                const FavoriteGame* favorites_list = favorites_get_list();
                int favorites_count = favorites_get_count();

                for (int i = 0; i < favorites_count; i++) {
                    if (strcmp(favorites_list[i].core_name, core_name) == 0 &&
                        strcmp(favorites_list[i].game_name, filename) == 0) {
                        strcpy(directory, favorites_list[i].full_path);
                        break;
                    }
                }
            } else {
                get_corename(entry->path, core_name, sizeof(core_name));
                strcpy(directory, entry->path);
                clean_path(directory);
                char *filename_path = strrchr(entry->path, '/');
                if (filename_path) snprintf(filename, sizeof(filename), "%s", filename_path + 1);
                else snprintf(filename, sizeof(filename), "%s", entry->name);
            }

            char override_core[64] = {0};
            char sys_name[64];
            get_corename(entry->path, sys_name, sizeof(sys_name));
            get_game_core_override(sys_name, filename, override_core, sizeof(override_core));
            
            if (override_core[0] != '\0') {
                init_direct_loader(override_core, directory, filename);
            } else {
                init_direct_loader(core_name, directory, filename);
            }
            if (input_changed) render_menu();
            return;
        }
    }

    // Handle B button (back) - on button release
    if (prev_input[3] && !b) {
        // v43: If header is selected, deselect it
        if (header_selected) {
            header_selected = 0;
            if (entry_count > 0) {
                selected_index = 0;
                scroll_offset = 0;
            }
            prev_input[3] = b;
            return;
        }

        // v60: At MAIN_MENU, stay on current section (don't go to SYSTEMS)
        if (strcmp(current_path, "MAIN_MENU") == 0) {
            // Just set header_selected - don't change section
            header_selected = 1;
            prev_input[3] = b;
            return;
        }

        if (strcmp(current_path, "RECENT_GAMES") == 0) {
            // Go back from Recent games to main ROMS directory
            strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
            scan_directory(current_path);
            // Restore selection to "Recent games" entry
            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].path, "RECENT_GAMES") == 0) {
                    selected_index = i;
                    if (selected_index >= scroll_offset + visible_items) {
                        scroll_offset = selected_index - visible_items + 1;
                    }
                    break;
                }
            }
        } else if (strcmp(current_path, "FAVORITES") == 0) {
            // Go back from Favorites to main ROMS directory
            strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
            scan_directory(current_path);
            // Restore selection to "Favorites" entry
            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].path, "FAVORITES") == 0) {
                    selected_index = i;
                    if (selected_index >= scroll_offset + visible_items) {
                        scroll_offset = selected_index - visible_items + 1;
                    }
                    break;
                }
            }
        } else if (strcmp(current_path, "TOOLS") == 0) {
            // v76: TOOLS is a top-level section, B just goes to header
            header_selected = 1;
        } else if (strcmp(current_path, "HOTKEYS") == 0) {
            // Go back from Hotkeys to Tools
            show_tools_menu();
            strncpy(current_path, "TOOLS", sizeof(current_path) - 1);
        } else if (strcmp(current_path, "CREDITS") == 0) {
            // Go back from Credits to Tools
            show_tools_menu();
            strncpy(current_path, "TOOLS", sizeof(current_path) - 1);
        } else if (strcmp(current_path, "UTILS") == 0) {
            // Go back from Utils to Tools
            show_tools_menu();
            strncpy(current_path, "TOOLS", sizeof(current_path) - 1);
        } else if (strcmp(current_path, ROMS_PATH) != 0) {
            // Remember which directory we're leaving so we can restore position
            char prev_dir[256];
            char *last_slash = strrchr(current_path, '/');
            if (last_slash && last_slash != current_path) {
                strncpy(prev_dir, last_slash + 1, sizeof(prev_dir) - 1);
                prev_dir[sizeof(prev_dir) - 1] = '\0';

                *last_slash = '\0';
                scan_directory(current_path);

                // Find the directory we just left and restore selection to it
                for (int i = 0; i < entry_count; i++) {
                    if (strcmp(entries[i].name, prev_dir) == 0) {
                        selected_index = i;
                        // Update scroll offset to keep selection visible
                        if (selected_index < scroll_offset) {
                            scroll_offset = selected_index;
                        } else if (selected_index >= scroll_offset + visible_items) {
                            scroll_offset = selected_index - visible_items + 1;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Store current state for next frame
    prev_input[0] = up;
    prev_input[1] = down;
    prev_input[2] = a;
    prev_input[3] = b;
    prev_input[4] = l;
    prev_input[5] = r;
    prev_input[6] = select;
    prev_input[7] = left;
    prev_input[8] = right;
    prev_input[9] = x;
    prev_input[10] = y;
}

// Libretro API implementation
void retro_init(void) {
    framebuffer = (uint16_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

    // Seed random number generator for random game picker
    srand(time(NULL));

    // Initialize modular systems
    render_init(framebuffer);
    font_init();
    theme_init();
    gfx_theme_init();
    gfx_theme_scan();  // Scan themes ONCE at startup (not on every settings load/save)
    recent_games_init();
    favorites_init();
    settings_init();
    display_opts_init();
    osk_init();
    text_editor_init();
    vb_init();  // v44: Video browser
    vp_init();  // v47: Video player
    vp_set_audio_callback(audio_batch_cb);  // v48: Audio for video player
    mp_init();  // v52: Music player
    mp_set_audio_callback(audio_batch_cb);  // v52: Audio for music player
    iv_init();  // v56: Image viewer

    recent_games_load();
    favorites_load();
    settings_load();

    apply_settings();

    // Auto-launch most recent game if resume on boot is enabled
    if (resume_on_boot) auto_launch_recent_game();

    // Skip directory scan if we're auto-launching a game (faster boot)
    if (!game_queued) {
        strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
        scan_directory(current_path);
    }
    
    audio_init();
}

void retro_deinit(void) {
    // v62: Clean up video player buffers
    if (vp_is_active()) {
        vp_close();
    }

    // v62: Clean up music player buffers
    if (mp_is_active()) {
        mp_close();
    }

    // Clean up GFX theme system
    gfx_theme_cleanup();

    // Free thumbnail cache
    if (thumbnail_cache_valid) {
        free_thumbnail(&current_thumbnail);
        thumbnail_cache_valid = 0;
    }

    // v32: Free screenshot cache
    if (screenshot_cache_valid) {
        if (current_screenshot.data) {
            free(current_screenshot.data);
            current_screenshot.data = NULL;
        }
        screenshot_cache_valid = 0;
    }

    // Free entries array
    if (entries) {
        free(entries);
        entries = NULL;
        entries_capacity = 0;
        entry_count = 0;
    }

    if (framebuffer) {
        free(framebuffer);
        framebuffer = NULL;
    }
}

unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "FrogUI";
    info->library_version  = "0.1";
    info->need_fullpath    = false;
    info->valid_extensions = "frogui";
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    info->timing.fps = 30.0;  // v48: 30fps like pmp123
    info->timing.sample_rate = 22050.0;  // v48: 22kHz like pmp123 (was 44100 causing 2x pitch)

    info->geometry.base_width   = SCREEN_WIDTH;
    info->geometry.base_height  = SCREEN_HEIGHT;
    info->geometry.max_width    = SCREEN_WIDTH;
    info->geometry.max_height   = SCREEN_HEIGHT;
    // v28: Pre-computed constant instead of float division (320/240 = 4/3)
    info->geometry.aspect_ratio = 1.333333f;
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;

    bool no_content = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

void retro_reset(void) {
    strncpy(current_path, ROMS_PATH, sizeof(current_path) - 1);
    scan_directory(current_path);
}

void retro_run(void) {
    // v27: Update FPS counter
    update_fps_counter();

    // v48: Video player mode - skip all menu rendering for max performance
    if (vp_is_active()) {
        // Poll input for player
        input_poll_cb();
        int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
        int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
        int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
        int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        int start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
        int l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
        int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

        // Store previous state for edge detection
        static int vp_prev_up, vp_prev_down, vp_prev_left, vp_prev_right;
        static int vp_prev_a, vp_prev_b, vp_prev_start, vp_prev_l, vp_prev_r;

        // Handle player input (on button release for edge detection)
        if (vp_handle_input(vp_prev_up && !up, vp_prev_down && !down,
                           vp_prev_left && !left, vp_prev_right && !right,
                           vp_prev_a && !a, vp_prev_b && !b, vp_prev_start && !start,
                           vp_prev_l && !l, vp_prev_r && !r)) {
            // Player closed
            // v77: Check if returning to file manager
            if (fm_check_return()) {
                // Return to file manager
            } else {
                // Return to VIDEOS section
                header_selected = 0;
                vb_set_focused(1);
            }
        }

        // Update previous state
        vp_prev_up = up; vp_prev_down = down; vp_prev_left = left; vp_prev_right = right;
        vp_prev_a = a; vp_prev_b = b; vp_prev_start = start; vp_prev_l = l; vp_prev_r = r;

        // Render player (no background, no menu - just video)
        vp_render(framebuffer);

        if (video_cb) {
            video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
        }
        return;
    }

    // v52/v63: Music player mode
    if (mp_is_active()) {
        // v63: If in background mode, just update audio and continue with menu
        if (mp_is_background_mode()) {
            mp_update_audio();
            // Don't return - continue with normal menu rendering below
        } else {
            // Foreground mode - full player UI
            input_poll_cb();
            int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
            int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
            int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
            int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
            int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
            int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
            int start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
            int select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
            int l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
            int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

            // Store previous state for edge detection
            static int mp_prev_up, mp_prev_down, mp_prev_left, mp_prev_right;
            static int mp_prev_a, mp_prev_b, mp_prev_start, mp_prev_select, mp_prev_l, mp_prev_r;

            // v63: SELECT - minimize to background mode
            // v78: Also check if returning to file manager
            if (mp_prev_select && !select) {
                mp_set_background_mode(1);
                // v78: Check if returning to file manager
                if (fm_check_return()) {
                    // Return to file manager (music continues in background)
                } else {
                    header_selected = 0;
                    vb_set_focused(1);
                }
                mp_prev_select = select;
                // Continue with normal rendering
            } else {
                // Handle player input (on button release for edge detection)
                if (mp_handle_input(mp_prev_up && !up, mp_prev_down && !down,
                                   mp_prev_left && !left, mp_prev_right && !right,
                                   mp_prev_a && !a, mp_prev_b && !b, mp_prev_start && !start,
                                   mp_prev_l && !l, mp_prev_r && !r)) {
                    // Player closed
                    mp_close();
                    // v77: Check if returning to file manager
                    if (fm_check_return()) {
                        // Return to file manager
                    } else {
                        // Return to MUSIC section
                        header_selected = 0;
                        vb_set_focused(1);
                    }
                }

                // Update previous state
                mp_prev_up = up; mp_prev_down = down; mp_prev_left = left; mp_prev_right = right;
                mp_prev_a = a; mp_prev_b = b; mp_prev_start = start; mp_prev_select = select;
                mp_prev_l = l; mp_prev_r = r;

                // Render player
                mp_render(framebuffer);

                if (video_cb) {
                    video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
                }
                return;
            }
        }
    }

    // v56: Image viewer mode - full screen image display
    if (iv_is_active()) {
        // Poll input for viewer
        input_poll_cb();
        int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
        int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
        int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
        int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        int x = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
        int y = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
        int l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
        int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

        // Handle viewer input (continuous for D-PAD, edge-detect for buttons)
        if (!iv_handle_input(up, down, left, right, a, b, x, y, l, r)) {
            // Viewer closed
            // v77: Check if returning to file manager
            if (fm_check_return()) {
                // Return to file manager
            } else {
                // Return to IMAGES section
                header_selected = 0;
                vb_set_focused(1);
            }
        }

        // v70: Update chunked loading state (loads one chunk per frame)
        iv_update();

        // Render viewer
        iv_render(framebuffer);

        if (video_cb) {
            video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
        }
        return;
    }

    // v73: Calculator mode
    if (calc_is_active()) {
        // Poll input for calculator
        input_poll_cb();
        int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
        int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
        int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
        int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        int x = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
        int y = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);

        // Store previous state for edge detection
        static int calc_prev_up, calc_prev_down, calc_prev_left, calc_prev_right;
        static int calc_prev_a, calc_prev_b, calc_prev_x, calc_prev_y;

        // Handle calculator input (on button release for edge detection)
        if (calc_handle_input(calc_prev_up && !up, calc_prev_down && !down,
                             calc_prev_left && !left, calc_prev_right && !right,
                             calc_prev_a && !a, calc_prev_b && !b,
                             calc_prev_x && !x, calc_prev_y && !y)) {
            // Calculator closed - return to TOOLS menu
            calc_close();
        }

        // Update previous state
        calc_prev_up = up; calc_prev_down = down; calc_prev_left = left; calc_prev_right = right;
        calc_prev_a = a; calc_prev_b = b; calc_prev_x = x; calc_prev_y = y;

        // Render calculator
        calc_render(framebuffer);

        // v74: Draw FPS overlay on calculator too
        draw_fps_overlay(framebuffer);

        if (video_cb) {
            video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
        }
        return;
    }

    // v76: File Manager mode
    if (fm_is_active()) {
        input_poll_cb();
        int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
        int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
        int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
        int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        int a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        int b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        int x = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
        int y = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
        int l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
        int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
        int start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
        int select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);

        // Edge detection for file manager
        static int fm_prev_up, fm_prev_down, fm_prev_left, fm_prev_right;
        static int fm_prev_a, fm_prev_b, fm_prev_x, fm_prev_y;
        static int fm_prev_l, fm_prev_r, fm_prev_start, fm_prev_select;

        if (fm_handle_input(fm_prev_up && !up, fm_prev_down && !down,
                           fm_prev_left && !left, fm_prev_right && !right,
                           fm_prev_a && !a, fm_prev_b && !b,
                           fm_prev_x && !x, fm_prev_y && !y,
                           fm_prev_l && !l, fm_prev_r && !r,
                           fm_prev_start && !start, fm_prev_select && !select)) {
            fm_close();
        }

        fm_prev_up = up; fm_prev_down = down; fm_prev_left = left; fm_prev_right = right;
        fm_prev_a = a; fm_prev_b = b; fm_prev_x = x; fm_prev_y = y;
        fm_prev_l = l; fm_prev_r = r; fm_prev_start = start; fm_prev_select = select;

        fm_render(framebuffer);
        draw_fps_overlay(framebuffer);

        if (video_cb) {
            video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
        }
        return;
    }

    // Advance animated background every frame
    // (menu runs at ~30fps, video will play at video's native rate)
    gfx_theme_advance_animation();

    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
        apply_settings();
    }

    handle_input();
    render_menu();
    output_wav_audio();

    // v27: Draw FPS overlay on top of everything
    draw_fps_overlay(framebuffer);

    if (video_cb) {
        video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
    }
    if (game_queued) {
#ifdef SF2000
        direct_loader((const char *)ptr_gs_run_game_file, 0);
#endif
        return;
    }
}

bool retro_load_game(const struct retro_game_info *info) {
    (void)info;
    return true;
}

void retro_unload_game(void) {
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
    (void)type;
    (void)info;
    (void)num;
    return false;
}

size_t retro_serialize_size(void) {
    return 0;
}

bool retro_serialize(void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}

void retro_cheat_reset(void) {
}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

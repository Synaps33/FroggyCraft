// video_browser.c - Generic file browser for multiple sections
// v46: Theme-styled with semi-transparent rounded corners, divider line
// Based on pmp123 file browser, adapted for FrogUI
// Supports: VIDEOS, IMAGES, MUSIC, TEXT sections with different filters

#include "video_browser.h"
#include "render.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef SF2000
#include "../../stockfw.h"
#include "../../dirent.h"
// S_ISDIR macro for SF2000 fs_readdir type field
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0x4000) != 0)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// PMP-style 5x7 bitmap font (smaller than FrogUI font)
static const unsigned char vb_font[96][5] = {
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

// Draw string with 5x7 bitmap font
static void vb_draw_str(uint16_t *fb, int x, int y, const char *str, uint16_t color) {
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c >= 32 && c < 128) {
            const unsigned char *glyph = vb_font[c - 32];
            for (int col = 0; col < 5; col++) {
                unsigned char bits = glyph[col];
                for (int row = 0; row < 7; row++) {
                    if (bits & (1 << row)) {
                        int px = x + col;
                        int py = y + row;
                        if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                            fb[py * SCREEN_WIDTH + px] = color;
                        }
                    }
                }
            }
        }
        x += 6;  // 5 pixels + 1 spacing
        str++;
    }
}

// Measure string width with 5x7 font
static int vb_measure_str(const char *str) {
    int len = strlen(str);
    return len * 6 - (len > 0 ? 1 : 0);  // 6 pixels per char, minus trailing space
}

// Browser state
static int vb_active = 0;
static char vb_current_path[VB_MAX_PATH];
static char vb_start_path[VB_MAX_PATH] = "/mnt/sda1/VIDEOS";
static VBFilterMode vb_filter_mode = VB_FILTER_VIDEOS;
static char vb_files[VB_MAX_FILES][VB_MAX_NAME];
static int vb_is_dir[VB_MAX_FILES];
static int vb_file_count = 0;
static int vb_selection = 0;
static int vb_scroll = 0;

// Selected file output
static char vb_selected_path[VB_MAX_PATH] = "";
static int vb_file_selected = 0;
static int vb_wants_header = 0;  // Signal to return to header
static int vb_focused = 0;       // Whether browser has focus (show selection highlight)

// Filename scroll for long names
static int vb_name_scroll = 0;
static int vb_name_scroll_timer = 0;
static int vb_last_selection = -1;
#define VB_NAME_VISIBLE_CHARS 44  // More chars with smaller font
#define VB_NAME_SCROLL_DELAY 8

// Browser titles for each mode
static const char* vb_titles[] = {
    "Video Browser",
    "Image Browser",
    "Music Browser",
    "Text Browser"
};

// Empty messages for each mode
static const char* vb_empty_messages[] = {
    "(No video files found)",
    "(No image files found)",
    "(No music files found)",
    "(No text files found)"
};

// Helper: case-insensitive string ends with
static int str_ends_with_ci(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;

    const char *str_end = str + str_len - suffix_len;
    for (int i = 0; i < suffix_len; i++) {
        char c1 = str_end[i];
        char c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

// Check if file matches current filter mode
static int vb_matches_filter(const char *filename) {
    switch (vb_filter_mode) {
        case VB_FILTER_VIDEOS:
            return str_ends_with_ci(filename, ".avi");

        case VB_FILTER_IMAGES:
            return str_ends_with_ci(filename, ".png") ||
                   str_ends_with_ci(filename, ".jpg") ||
                   str_ends_with_ci(filename, ".jpeg") ||
                   str_ends_with_ci(filename, ".gif") ||
                   str_ends_with_ci(filename, ".bmp") ||
                   str_ends_with_ci(filename, ".webp");

        case VB_FILTER_MUSIC:
            return str_ends_with_ci(filename, ".mp3") ||
                   str_ends_with_ci(filename, ".wav") ||
                   str_ends_with_ci(filename, ".adp") ||
                   str_ends_with_ci(filename, ".adpcm");

        case VB_FILTER_TEXT:
            return str_ends_with_ci(filename, ".txt");

        default:
            return 0;
    }
}

// Scan directory for matching files and subdirectories
static void vb_scan_directory(void) {
    vb_file_count = 0;
    vb_selection = 0;
    vb_scroll = 0;
    vb_name_scroll = 0;
    vb_name_scroll_timer = 0;
    vb_last_selection = -1;

#ifdef SF2000
    // SF2000 uses custom fs_* functions
    union {
        struct {
            uint8_t _1[0x10];
            uint32_t type;
        };
        struct {
            uint8_t _2[0x22];
            char d_name[0x225];
        };
        uint8_t __[0x428];
    } buffer;

    int dir_fd = fs_opendir(vb_current_path);
    if (dir_fd < 0) {
        // Try fallback to /mnt/sda1
        strcpy(vb_current_path, "/mnt/sda1");
        dir_fd = fs_opendir(vb_current_path);
        if (dir_fd < 0) return;
    }

    // Add ".." if not at root
    if (strcmp(vb_current_path, "/mnt/sda1") != 0) {
        strcpy(vb_files[vb_file_count], "..");
        vb_is_dir[vb_file_count] = 1;
        vb_file_count++;
    }

    while (vb_file_count < VB_MAX_FILES) {
        memset(&buffer, 0, sizeof(buffer));
        if (fs_readdir(dir_fd, &buffer) < 0) break;

        // Skip . and ..
        if (buffer.d_name[0] == '.' &&
            (buffer.d_name[1] == '\0' ||
             (buffer.d_name[1] == '.' && buffer.d_name[2] == '\0'))) {
            continue;
        }

        int is_dir = S_ISDIR(buffer.type);
        int matches = vb_matches_filter(buffer.d_name);

        // Only show directories and matching files
        if (!is_dir && !matches) continue;

        strncpy(vb_files[vb_file_count], buffer.d_name, VB_MAX_NAME - 1);
        vb_files[vb_file_count][VB_MAX_NAME - 1] = '\0';
        vb_is_dir[vb_file_count] = is_dir;
        vb_file_count++;
    }

    fs_closedir(dir_fd);

#else
    // Standard POSIX for testing
    DIR *dir = opendir(vb_current_path);
    if (!dir) {
        strcpy(vb_current_path, "/mnt/sda1");
        dir = opendir(vb_current_path);
        if (!dir) return;
    }

    // Add ".." if not at root
    if (strcmp(vb_current_path, "/mnt/sda1") != 0) {
        strcpy(vb_files[vb_file_count], "..");
        vb_is_dir[vb_file_count] = 1;
        vb_file_count++;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && vb_file_count < VB_MAX_FILES) {
        // Skip . and ..
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        int is_dir = (entry->d_type == DT_DIR);
        int matches = vb_matches_filter(entry->d_name);

        if (!is_dir && !matches) continue;

        strncpy(vb_files[vb_file_count], entry->d_name, VB_MAX_NAME - 1);
        vb_files[vb_file_count][VB_MAX_NAME - 1] = '\0';
        vb_is_dir[vb_file_count] = is_dir;
        vb_file_count++;
    }

    closedir(dir);
#endif
}

void vb_init(void) {
    vb_active = 0;
    strcpy(vb_start_path, "/mnt/sda1/VIDEOS");
    strcpy(vb_current_path, vb_start_path);
    vb_filter_mode = VB_FILTER_VIDEOS;
    vb_file_count = 0;
    vb_selection = 0;
    vb_scroll = 0;
    vb_selected_path[0] = '\0';
    vb_file_selected = 0;
    vb_wants_header = 0;
    vb_focused = 0;
}

int vb_is_active(void) {
    return vb_active;
}

void vb_open_with_config(const char *start_path, VBFilterMode filter_mode) {
    vb_active = 1;
    vb_file_selected = 0;
    vb_selected_path[0] = '\0';
    vb_wants_header = 0;
    vb_focused = 0;  // Start unfocused (header selected)

    // Store configuration
    strncpy(vb_start_path, start_path, VB_MAX_PATH - 1);
    vb_start_path[VB_MAX_PATH - 1] = '\0';
    vb_filter_mode = filter_mode;

    // Ensure directory exists
#ifdef SF2000
    int fd = fs_open(start_path, 0, 0);
    if (fd >= 0) {
        fs_close(fd);
    } else {
        fs_mkdir(start_path, 0777);
    }
#endif

    strcpy(vb_current_path, vb_start_path);
    vb_scan_directory();
}

// Legacy open - defaults to VIDEOS
void vb_open(void) {
    vb_open_with_config("/mnt/sda1/VIDEOS", VB_FILTER_VIDEOS);
}

void vb_close(void) {
    vb_active = 0;
}

int vb_wants_go_to_header(void) {
    if (vb_wants_header) {
        vb_wants_header = 0;
        return 1;
    }
    return 0;
}

void vb_set_focused(int focused) {
    vb_focused = focused;
}

VBFilterMode vb_get_filter_mode(void) {
    return vb_filter_mode;
}

int vb_handle_input(int up, int down, int left, int right, int a, int b) {
    if (!vb_active) return 0;

    // UP - move selection up, or go to header if at top
    if (up) {
        if (vb_selection > 0) {
            vb_selection--;
            if (vb_selection < vb_scroll) {
                vb_scroll = vb_selection;
            }
        } else {
            // At top - signal to go to header
            vb_wants_header = 1;
        }
        return 1;
    }

    // DOWN - move selection down
    if (down) {
        if (vb_selection < vb_file_count - 1) {
            vb_selection++;
            if (vb_selection >= vb_scroll + VB_VISIBLE_ITEMS) {
                vb_scroll = vb_selection - VB_VISIBLE_ITEMS + 1;
            }
        }
        return 1;
    }

    // LEFT - page up (or go to header if would go past top)
    if (left) {
        if (vb_selection > 0) {
            vb_selection -= VB_VISIBLE_ITEMS;
            if (vb_selection < 0) vb_selection = 0;
            if (vb_selection < vb_scroll) {
                vb_scroll = vb_selection;
            }
        } else {
            // Already at top - go to header
            vb_wants_header = 1;
        }
        return 1;
    }

    // RIGHT - page down
    if (right) {
        vb_selection += VB_VISIBLE_ITEMS;
        if (vb_selection >= vb_file_count) {
            vb_selection = vb_file_count - 1;
            if (vb_selection < 0) vb_selection = 0;
        }
        if (vb_selection >= vb_scroll + VB_VISIBLE_ITEMS) {
            vb_scroll = vb_selection - VB_VISIBLE_ITEMS + 1;
        }
        return 1;
    }

    // A - select/enter
    if (a && vb_file_count > 0) {
        if (vb_is_dir[vb_selection]) {
            // Enter directory
            if (strcmp(vb_files[vb_selection], "..") == 0) {
                // Go up
                char *last_slash = strrchr(vb_current_path, '/');
                if (last_slash && last_slash != vb_current_path) {
                    *last_slash = '\0';
                }
            } else {
                // Enter subdirectory
                int len = strlen(vb_current_path);
                if (len + 1 + strlen(vb_files[vb_selection]) < VB_MAX_PATH) {
                    strcat(vb_current_path, "/");
                    strcat(vb_current_path, vb_files[vb_selection]);
                }
            }
            vb_scan_directory();
        } else {
            // Select file
            snprintf(vb_selected_path, VB_MAX_PATH, "%s/%s",
                     vb_current_path, vb_files[vb_selection]);
            vb_file_selected = 1;
            // Don't close browser - let caller decide
        }
        return 1;
    }

    // B - go back / go to header at root
    if (b) {
        if (strcmp(vb_current_path, "/mnt/sda1") != 0 &&
            strcmp(vb_current_path, vb_start_path) != 0) {
            // Go up one level
            char *last_slash = strrchr(vb_current_path, '/');
            if (last_slash && last_slash != vb_current_path) {
                *last_slash = '\0';
            }
            vb_scan_directory();
        } else {
            // v61: At root or start path - go to header (don't close browser)
            vb_wants_header = 1;
        }
        return 1;
    }

    return 0;
}

// Helper: blend two RGB565 colors with alpha (0-255, 255=fully foreground)
static uint16_t vb_blend_color(uint16_t fg, uint16_t bg, int alpha) {
    // Extract RGB components from RGB565
    int fg_r = (fg >> 11) & 0x1F;
    int fg_g = (fg >> 5) & 0x3F;
    int fg_b = fg & 0x1F;

    int bg_r = (bg >> 11) & 0x1F;
    int bg_g = (bg >> 5) & 0x3F;
    int bg_b = bg & 0x1F;

    // Blend (alpha is 0-255)
    int r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    int g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    int b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Helper: draw semi-transparent rounded rectangle with alpha blending
static void vb_draw_rounded_rect_alpha(uint16_t *fb, int x, int y, int w, int h, int radius, uint16_t color, int alpha) {
    // Draw main body (excluding corners) with blending
    for (int py = y + radius; py < y + h - radius; py++) {
        for (int px = x; px < x + w; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = vb_blend_color(color, fb[idx], alpha);
            }
        }
    }
    // Top and bottom strips
    for (int py = y; py < y + radius; py++) {
        for (int px = x + radius; px < x + w - radius; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = vb_blend_color(color, fb[idx], alpha);
            }
        }
    }
    for (int py = y + h - radius; py < y + h; py++) {
        for (int px = x + radius; px < x + w - radius; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = vb_blend_color(color, fb[idx], alpha);
            }
        }
    }

    // Draw rounded corners using circle approximation
    for (int corner_y = 0; corner_y < radius; corner_y++) {
        for (int corner_x = 0; corner_x < radius; corner_x++) {
            int dx = radius - corner_x;
            int dy = radius - corner_y;
            int dist_sq = dx * dx + dy * dy;
            int radius_sq = radius * radius;

            if (dist_sq <= radius_sq) {
                int px, py, idx;

                // Top-left corner
                px = x + corner_x;
                py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = vb_blend_color(color, fb[idx], alpha);
                }

                // Top-right corner
                px = x + w - 1 - corner_x;
                py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = vb_blend_color(color, fb[idx], alpha);
                }

                // Bottom-left corner
                px = x + corner_x;
                py = y + h - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = vb_blend_color(color, fb[idx], alpha);
                }

                // Bottom-right corner
                px = x + w - 1 - corner_x;
                py = y + h - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = vb_blend_color(color, fb[idx], alpha);
                }
            }
        }
    }
}

// Helper: create directory color (50% red, same G/B as text color)
static uint16_t vb_make_dir_color(uint16_t text_color) {
    int r = (text_color >> 11) & 0x1F;
    int g = (text_color >> 5) & 0x3F;
    int b = text_color & 0x1F;
    r = r / 2;  // 50% red
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void vb_draw(uint16_t *framebuffer) {
    if (!vb_active) return;

    // Window dimensions
    int fb_x = 20;
    int fb_y = 28;
    int fb_w = 280;
    int fb_h = 200;
    int radius = 10;  // Rounded corner radius (like pillbox)

    // Colors from theme
    uint16_t col_bg = theme_legend_bg();    // Theme legend background
    uint16_t col_text = theme_text();       // Theme text color
    uint16_t col_dir = vb_make_dir_color(col_text);  // 50% red directory color
    uint16_t col_sel_bg = theme_select_bg();         // Theme selection background

    // Draw semi-transparent rounded background (90% opacity = 230/255)
    vb_draw_rounded_rect_alpha(framebuffer, fb_x, fb_y, fb_w, fb_h, radius, col_bg, 230);

    // Title - based on filter mode (same color as text)
    const char *title = (vb_filter_mode < VB_FILTER_COUNT) ? vb_titles[vb_filter_mode] : "File Browser";
    vb_draw_str(framebuffer, fb_x + 10, fb_y + 6, title, col_text);

    // Current path (truncated if too long)
    char path_display[50];
    int path_len = strlen(vb_current_path);
    if (path_len > 46) {
        strcpy(path_display, "...");
        strcat(path_display, vb_current_path + path_len - 43);
    } else {
        strcpy(path_display, vb_current_path);
    }
    vb_draw_str(framebuffer, fb_x + 10, fb_y + 17, path_display, col_text);

    // Separator line between header and file list
    render_fill_rect(framebuffer, fb_x + 6, fb_y + 27, fb_w - 12, 1, col_text);

    // File list
    int list_y = fb_y + 31;
    int item_height = 10;  // Smaller with 5x7 font

    // Reset scroll when selection changes
    if (vb_selection != vb_last_selection) {
        vb_name_scroll = 0;
        vb_name_scroll_timer = 0;
        vb_last_selection = vb_selection;
    }

    for (int i = 0; i < VB_VISIBLE_ITEMS && (vb_scroll + i) < vb_file_count; i++) {
        int idx = vb_scroll + i;
        int y = list_y + i * item_height;

        // Selection highlight (only when browser has focus)
        if (vb_focused && idx == vb_selection) {
            render_fill_rect(framebuffer, fb_x + 6, y - 1, fb_w - 12, item_height, col_sel_bg);
        }

        // Build display name
        char full_name[VB_MAX_NAME + 3];
        char display_name[VB_NAME_VISIBLE_CHARS + 1];

        if (vb_is_dir[idx]) {
            snprintf(full_name, sizeof(full_name), "[%s]", vb_files[idx]);
        } else {
            strncpy(full_name, vb_files[idx], VB_MAX_NAME);
            full_name[VB_MAX_NAME - 1] = '\0';
        }

        int name_len = strlen(full_name);

        // Scroll long names for selected item
        if (idx == vb_selection && name_len > VB_NAME_VISIBLE_CHARS) {
            int max_scroll = name_len - VB_NAME_VISIBLE_CHARS;

            vb_name_scroll_timer++;
            if (vb_name_scroll_timer >= VB_NAME_SCROLL_DELAY) {
                vb_name_scroll_timer = 0;
                vb_name_scroll++;
                if (vb_name_scroll > max_scroll + 10) {
                    vb_name_scroll = 0;
                }
            }

            int scroll_pos = (vb_name_scroll > max_scroll) ? max_scroll : vb_name_scroll;
            strncpy(display_name, full_name + scroll_pos, VB_NAME_VISIBLE_CHARS);
            display_name[VB_NAME_VISIBLE_CHARS] = '\0';
        } else {
            strncpy(display_name, full_name, VB_NAME_VISIBLE_CHARS);
            display_name[VB_NAME_VISIBLE_CHARS] = '\0';
        }

        uint16_t col = vb_is_dir[idx] ? col_dir : col_text;
        vb_draw_str(framebuffer, fb_x + 10, y + 1, display_name, col);
    }

    // Scroll indicators
    if (vb_scroll > 0) {
        vb_draw_str(framebuffer, fb_x + fb_w - 16, list_y, "^", col_text);
    }
    if (vb_scroll + VB_VISIBLE_ITEMS < vb_file_count) {
        vb_draw_str(framebuffer, fb_x + fb_w - 16, list_y + (VB_VISIBLE_ITEMS - 1) * item_height, "v", col_text);
    }

    // Instructions and file count
    vb_draw_str(framebuffer, fb_x + 10, fb_y + fb_h - 14, "A:Select B:Back L/R:Page", col_text);

    char count_str[24];
    snprintf(count_str, sizeof(count_str), "%d items", vb_file_count);
    int count_w = vb_measure_str(count_str);
    vb_draw_str(framebuffer, fb_x + fb_w - count_w - 10, fb_y + fb_h - 14, count_str, col_text);

    // Show "No files" message if empty - based on filter mode
    if (vb_file_count == 0) {
        const char *msg = (vb_filter_mode < VB_FILTER_COUNT) ? vb_empty_messages[vb_filter_mode] : "(No files found)";
        int msg_w = vb_measure_str(msg);
        vb_draw_str(framebuffer, fb_x + (fb_w - msg_w) / 2, fb_y + fb_h / 2, msg, col_text);
    }
}

const char* vb_get_selected_path(void) {
    return vb_selected_path;
}

int vb_file_was_selected(void) {
    if (vb_file_selected) {
        vb_file_selected = 0;
        return 1;
    }
    return 0;
}

// v35: Full on-screen keyboard - full width, arrow keys for cursor, fixed navigation
#include "osk.h"
#include "font.h"
#include "render.h"
#include "frogui_logo_data.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// External PNG loader (from render.c)
extern int load_png_rgba565(const char* filename, uint16_t** pixels, uint8_t** alpha, int* width, int* height);

// Key definition - based on Macintosh layout
typedef struct {
    const char *label;      // Display label (normal/lowercase)
    const char *shift_label; // Label when shift active (uppercase/special)
    char character;         // Character to insert (0 = special key)
    char shift_character;   // Character when shifted
    int x, y;              // Position on keyboard grid
    int w;                 // Width in units
    int special;           // Special: 0=char, 1=backspace, 2=shift, 3=caps, 4=ok, 5=cancel, 6=clear, 7=frog(noop)
                           // v34: 8=arrow up, 9=arrow down, 10=arrow left, 11=arrow right
} OSKKey;

// v35: Full width keyboard layout - 15 units wide to use full screen
// Arrow left/right now move cursor in text (like L/R shoulders)
static const OSKKey osk_keys[] = {
    // Row 0: Number row with special chars when shifted
    {"1",   "!",   '1', '!', 0, 0, 1, 0},
    {"2",   "@",   '2', '@', 1, 0, 1, 0},
    {"3",   "#",   '3', '#', 2, 0, 1, 0},
    {"4",   "$",   '4', '$', 3, 0, 1, 0},
    {"5",   "%",   '5', '%', 4, 0, 1, 0},
    {"6",   "^",   '6', '^', 5, 0, 1, 0},
    {"7",   "&",   '7', '&', 6, 0, 1, 0},
    {"8",   "*",   '8', '*', 7, 0, 1, 0},
    {"9",   "(",   '9', '(', 8, 0, 1, 0},
    {"0",   ")",   '0', ')', 9, 0, 1, 0},
    {"-",   "_",   '-', '_', 10, 0, 1, 0},
    {"=",   "+",   '=', '+', 11, 0, 1, 0},
    {"<-",  "<-",  0,   0,   12, 0, 3, 1},  // Backspace (wider)

    // Row 1: QWERTY row - lowercase default
    {"q",   "Q",   'q', 'Q', 0, 1, 1, 0},
    {"w",   "W",   'w', 'W', 1, 1, 1, 0},
    {"e",   "E",   'e', 'E', 2, 1, 1, 0},
    {"r",   "R",   'r', 'R', 3, 1, 1, 0},
    {"t",   "T",   't', 'T', 4, 1, 1, 0},
    {"y",   "Y",   'y', 'Y', 5, 1, 1, 0},
    {"u",   "U",   'u', 'U', 6, 1, 1, 0},
    {"i",   "I",   'i', 'I', 7, 1, 1, 0},
    {"o",   "O",   'o', 'O', 8, 1, 1, 0},
    {"p",   "P",   'p', 'P', 9, 1, 1, 0},
    {"[",   "{",   '[', '{', 10, 1, 1, 0},
    {"]",   "}",   ']', '}', 11, 1, 1, 0},
    {"\\",  "|",   '\\','|', 12, 1, 1, 0},
    {"`",   "~",   '`', '~', 13, 1, 2, 0},

    // Row 2: ASDF row - lowercase default
    {"a",   "A",   'a', 'A', 0, 2, 1, 0},
    {"s",   "S",   's', 'S', 1, 2, 1, 0},
    {"d",   "D",   'd', 'D', 2, 2, 1, 0},
    {"f",   "F",   'f', 'F', 3, 2, 1, 0},
    {"g",   "G",   'g', 'G', 4, 2, 1, 0},
    {"h",   "H",   'h', 'H', 5, 2, 1, 0},
    {"j",   "J",   'j', 'J', 6, 2, 1, 0},
    {"k",   "K",   'k', 'K', 7, 2, 1, 0},
    {"l",   "L",   'l', 'L', 8, 2, 1, 0},
    {";",   ":",   ';', ':', 9, 2, 1, 0},
    {"'",   "\"",  '\'','"', 10, 2, 1, 0},
    {"ENT", "ENT", 0,   0,   11, 2, 4, 4},  // Enter/Confirm (wider)

    // Row 3: ZXCV row - lowercase default
    {"SHF", "SHF", 0,   0,   0, 3, 2, 2},   // Shift toggle
    {"z",   "Z",   'z', 'Z', 2, 3, 1, 0},
    {"x",   "X",   'x', 'X', 3, 3, 1, 0},
    {"c",   "C",   'c', 'C', 4, 3, 1, 0},
    {"v",   "V",   'v', 'V', 5, 3, 1, 0},
    {"b",   "B",   'b', 'B', 6, 3, 1, 0},
    {"n",   "N",   'n', 'N', 7, 3, 1, 0},
    {"m",   "M",   'm', 'M', 8, 3, 1, 0},
    {",",   "<",   ',', '<', 9, 3, 1, 0},
    {".",   ">",   '.', '>', 10, 3, 1, 0},
    {"/",   "?",   '/', '?', 11, 3, 1, 0},
    // v35: Arrow up
    {"^",   "^",   0,   0,   13, 3, 1, 8},   // Arrow Up
    {"*",   "*",   '*', '*', 14, 3, 1, 0},   // v35: Wildcard * restored

    // Row 4: Space row + controls + arrows
    {"CAP", "CAP", 0,   0,   0, 4, 2, 3},   // Caps Lock
    {"FRG", "FRG", 0,   0,   2, 4, 2, 7},   // FrogUI logo
    {"Spc", "Spc", ' ', ' ', 4, 4, 4, 0},   // Space
    {"CLR", "CLR", 0,   0,   8, 4, 2, 6},   // Clear
    {"X",   "X",   0,   0,   10, 4, 2, 5},  // Cancel
    // v35: Arrow keys - left/right now move cursor in text
    {"<",   "<",   0,   0,   12, 4, 1, 10},  // Arrow Left (cursor left)
    {"v",   "v",   0,   0,   13, 4, 1, 9},   // Arrow Down
    {">",   ">",   0,   0,   14, 4, 1, 11},  // Arrow Right (cursor right)
};
#define OSK_KEY_COUNT (sizeof(osk_keys) / sizeof(osk_keys[0]))

// OSK state
static int osk_active = 0;
static int osk_selected = 14;  // Start at 'q'
static char osk_input[OSK_MAX_INPUT + 1];
static char osk_title[32];
static osk_callback_t osk_callback = NULL;
static int osk_cursor = 0;
static int osk_shift_state = 0;   // 0=off, 1=temp shift, 2=permashift (like caps lock)

// v34: Editor mode state
static int osk_editor_mode = 0;

// Embedded logo decoded data
static uint16_t *logo_pixels = NULL;
static uint8_t *logo_alpha = NULL;
static int logo_width = 0;
static int logo_height = 0;
static int logo_loaded = 0;

// Load embedded logo from PNG data in memory using lodepng directly
#include "lodepng.h"

static void decode_logo_from_memory(void) {
    if (logo_loaded) return;

    unsigned char *rgba = NULL;
    unsigned w, h;

    // Decode PNG from embedded data
    unsigned error = lodepng_decode32(&rgba, &w, &h, frogui_logo_png, frogui_logo_png_size);
    if (error || !rgba) {
        logo_loaded = -1;  // Mark as failed
        return;
    }

    logo_width = (int)w;
    logo_height = (int)h;

    // Allocate RGB565 and alpha buffers
    logo_pixels = (uint16_t*)malloc(w * h * sizeof(uint16_t));
    logo_alpha = (uint8_t*)malloc(w * h);

    if (!logo_pixels || !logo_alpha) {
        free(rgba);
        if (logo_pixels) { free(logo_pixels); logo_pixels = NULL; }
        if (logo_alpha) { free(logo_alpha); logo_alpha = NULL; }
        logo_loaded = -1;
        return;
    }

    // Convert RGBA to RGB565 + alpha
    for (unsigned i = 0; i < w * h; i++) {
        unsigned char r = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];

        // RGB565 conversion
        uint16_t r5 = (r >> 3) & 0x1F;
        uint16_t g6 = (g >> 2) & 0x3F;
        uint16_t b5 = (b >> 3) & 0x1F;
        logo_pixels[i] = (r5 << 11) | (g6 << 5) | b5;
        logo_alpha[i] = a;
    }

    free(rgba);
    logo_loaded = 1;
}

void osk_init(void) {
    osk_active = 0;
    osk_selected = 14;
    osk_input[0] = '\0';
    osk_cursor = 0;
    osk_shift_state = 0;
    osk_editor_mode = 0;
    // Don't decode logo at init - will use text fallback or decode on first render
}

void osk_open(const char *title, const char *initial, osk_callback_t callback) {
    osk_active = 1;
    osk_selected = 14;  // Start at 'q'
    osk_shift_state = 0;
    osk_editor_mode = 0;  // Normal mode

    strncpy(osk_title, title, sizeof(osk_title) - 1);
    osk_title[sizeof(osk_title) - 1] = '\0';

    if (initial) {
        strncpy(osk_input, initial, OSK_MAX_INPUT);
        osk_input[OSK_MAX_INPUT] = '\0';
        osk_cursor = strlen(osk_input);
    } else {
        osk_input[0] = '\0';
        osk_cursor = 0;
    }

    osk_callback = callback;
    // Logo will be decoded lazily on first render if needed
}

// v34: Open OSK in editor mode (keyboard at bottom, no title bar, arrow keys enabled)
void osk_open_editor(const char *initial, int cursor_pos, osk_callback_t callback) {
    osk_active = 1;
    osk_selected = 14;  // Start at 'q'
    osk_shift_state = 0;
    osk_editor_mode = 1;  // Editor mode
    osk_title[0] = '\0';  // No title in editor mode

    if (initial) {
        strncpy(osk_input, initial, OSK_MAX_INPUT);
        osk_input[OSK_MAX_INPUT] = '\0';
        // Clamp cursor position
        int len = strlen(osk_input);
        if (cursor_pos < 0) cursor_pos = 0;
        if (cursor_pos > len) cursor_pos = len;
        osk_cursor = cursor_pos;
    } else {
        osk_input[0] = '\0';
        osk_cursor = 0;
    }

    osk_callback = callback;
}

void osk_close(void) {
    if (osk_active && osk_callback) {
        osk_callback(0, NULL);  // Cancelled
    }
    osk_active = 0;
    osk_callback = NULL;
    osk_editor_mode = 0;
}

int osk_is_active(void) {
    return osk_active;
}

// v34: Check if OSK is in editor mode
int osk_is_editor_mode(void) {
    return osk_editor_mode;
}

// v34: Get current cursor position
int osk_get_cursor_pos(void) {
    return osk_cursor;
}

// v34: Get current input buffer
const char* osk_get_input(void) {
    return osk_input;
}

// v34: Set cursor position
void osk_set_cursor_pos(int pos) {
    int len = strlen(osk_input);
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    osk_cursor = pos;
}

// v35: Find key index by grid position - improved to find closest key in row
static int find_key_at_row_col(int row, int target_x) {
    int best_idx = -1;
    int best_dist = 9999;

    for (int i = 0; i < (int)OSK_KEY_COUNT; i++) {
        const OSKKey *k = &osk_keys[i];
        if (k->y == row) {
            // Calculate key center
            int key_start = k->x;
            int key_end = k->x + k->w;
            int key_center = key_start + k->w / 2;

            // Check if target is within key bounds
            if (target_x >= key_start && target_x < key_end) {
                return i;  // Exact match
            }

            // Find closest key center
            int dist = target_x > key_center ? (target_x - key_center) : (key_center - target_x);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = i;
            }
        }
    }
    return best_idx;
}

// v35: Navigate to key in direction - fixed wrapping behavior
static void osk_navigate(int dx, int dy) {
    const OSKKey *cur = &osk_keys[osk_selected];
    int cur_center = cur->x + cur->w / 2;
    int cur_row = cur->y;

    if (dy != 0) {
        // Vertical movement - no wrapping, just clamp
        int new_row = cur_row + dy;
        if (new_row < 0) new_row = 0;
        if (new_row > 4) new_row = 4;

        // Only move if row actually changed
        if (new_row != cur_row) {
            int idx = find_key_at_row_col(new_row, cur_center);
            if (idx >= 0) osk_selected = idx;
        }
    }

    if (dx != 0) {
        // Horizontal movement - find next/prev key in same row
        int best_idx = -1;
        int best_dist = 9999;

        for (int i = 0; i < (int)OSK_KEY_COUNT; i++) {
            const OSKKey *k = &osk_keys[i];
            if (k->y == cur_row && i != osk_selected) {
                int key_center = k->x + k->w / 2;

                if (dx > 0 && key_center > cur_center) {
                    // Moving right - find closest key to the right
                    int dist = key_center - cur_center;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_idx = i;
                    }
                } else if (dx < 0 && key_center < cur_center) {
                    // Moving left - find closest key to the left
                    int dist = cur_center - key_center;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_idx = i;
                    }
                }
            }
        }

        if (best_idx >= 0) {
            osk_selected = best_idx;
        } else {
            // Wrap around - find furthest key in opposite direction
            int wrap_center = (dx > 0) ? -9999 : 9999;
            for (int i = 0; i < (int)OSK_KEY_COUNT; i++) {
                const OSKKey *k = &osk_keys[i];
                if (k->y == cur_row) {
                    int key_center = k->x + k->w / 2;
                    if ((dx > 0 && key_center < wrap_center) ||
                        (dx < 0 && key_center > wrap_center)) {
                        wrap_center = key_center;
                        best_idx = i;
                    }
                }
            }
            if (best_idx >= 0) {
                osk_selected = best_idx;
            }
        }
    }
}

// v35: Handle input with L/R shoulder buttons and arrow keys for cursor movement
int osk_handle_input(int up, int down, int left, int right, int a, int b, int l, int r) {
    static int prev_up = 0, prev_down = 0, prev_left = 0, prev_right = 0;
    static int prev_a = 0, prev_b = 0, prev_l = 0, prev_r = 0;

    if (!osk_active) {
        prev_up = up; prev_down = down;
        prev_left = left; prev_right = right;
        prev_a = a; prev_b = b;
        prev_l = l; prev_r = r;
        return 0;
    }

    // v35: L/R shoulder buttons for cursor movement (always, not just editor mode)
    if (prev_l && !l) {
        // L = move cursor left in text
        if (osk_cursor > 0) {
            osk_cursor--;
        }
    }
    if (prev_r && !r) {
        // R = move cursor right in text
        int len = strlen(osk_input);
        if (osk_cursor < len) {
            osk_cursor++;
        }
    }

    // Navigation (on release)
    if (prev_up && !up) osk_navigate(0, -1);
    if (prev_down && !down) osk_navigate(0, 1);
    if (prev_left && !left) osk_navigate(-1, 0);
    if (prev_right && !right) osk_navigate(1, 0);

    // B button - cancel
    if (prev_b && !b) {
        if (osk_callback) {
            osk_callback(0, NULL);
        }
        osk_active = 0;
        osk_callback = NULL;
        osk_editor_mode = 0;
        prev_b = b;
        return 1;
    }

    // A button - select key
    if (prev_a && !a) {
        const OSKKey *k = &osk_keys[osk_selected];

        switch (k->special) {
            case 0:  // Regular character
                if (strlen(osk_input) < OSK_MAX_INPUT) {
                    // Use shifted character when shift is active (state 1 or 2)
                    char ch = (osk_shift_state > 0) ? k->shift_character : k->character;
                    // v34: Insert at cursor position, not at end
                    int len = strlen(osk_input);
                    // Shift characters from cursor to end
                    for (int i = len; i >= osk_cursor; i--) {
                        osk_input[i + 1] = osk_input[i];
                    }
                    osk_input[osk_cursor] = ch;
                    osk_cursor++;
                    // Auto-disable temporary shift after typing (state 1 only)
                    if (osk_shift_state == 1) {
                        osk_shift_state = 0;
                    }
                }
                break;

            case 1:  // Backspace
                if (osk_cursor > 0) {
                    // v34: Delete character before cursor
                    int len = strlen(osk_input);
                    for (int i = osk_cursor - 1; i < len; i++) {
                        osk_input[i] = osk_input[i + 1];
                    }
                    osk_cursor--;
                }
                break;

            case 2:  // Shift - triple press cycle: off -> temp -> perma -> off
                if (osk_shift_state == 0) {
                    osk_shift_state = 1;  // Temp shift
                } else if (osk_shift_state == 1) {
                    osk_shift_state = 2;  // Permashift (second consecutive press)
                } else {
                    osk_shift_state = 0;  // Turn off permashift
                }
                break;

            case 3:  // Caps Lock - same as permashift now
                if (osk_shift_state == 2) {
                    osk_shift_state = 0;
                } else {
                    osk_shift_state = 2;
                }
                break;

            case 4:  // OK/Enter - confirm
                if (osk_callback) {
                    osk_callback(1, osk_input);
                }
                osk_active = 0;
                osk_callback = NULL;
                osk_editor_mode = 0;
                prev_a = a;
                return 1;

            case 5:  // Cancel
                if (osk_callback) {
                    osk_callback(0, NULL);
                }
                osk_active = 0;
                osk_callback = NULL;
                osk_editor_mode = 0;
                prev_a = a;
                return 1;

            case 6:  // Clear
                osk_input[0] = '\0';
                osk_cursor = 0;
                break;

            case 7:  // Frog (no action - decorative)
                break;

            // v34: Arrow keys for line navigation
            case 8:  // Arrow Up
                if (osk_callback) {
                    char dir[2] = {'U', '\0'};
                    osk_callback(2, dir);
                }
                break;

            case 9:  // Arrow Down
                if (osk_callback) {
                    char dir[2] = {'D', '\0'};
                    osk_callback(2, dir);
                }
                break;

            // v35: Arrow Left/Right now move cursor in text (like L/R shoulders)
            case 10: // Arrow Left - move cursor left
                if (osk_cursor > 0) {
                    osk_cursor--;
                }
                break;

            case 11: // Arrow Right - move cursor right
                {
                    int len = strlen(osk_input);
                    if (osk_cursor < len) {
                        osk_cursor++;
                    }
                }
                break;
        }
    }

    prev_up = up; prev_down = down;
    prev_left = left; prev_right = right;
    prev_a = a; prev_b = b;
    prev_l = l; prev_r = r;

    return 0;
}

// Draw logo with alpha blending
static void draw_logo(uint16_t *framebuffer, int x, int y, int max_w, int max_h) {
    // Try to decode logo on first use (lazy loading)
    if (!logo_loaded) {
        decode_logo_from_memory();
    }

    if (!logo_pixels || logo_width <= 0 || logo_height <= 0 || logo_loaded != 1) {
        // Fallback: draw "Frog" text
        font_draw_text(framebuffer, 320, 240, x + 4, y + 4, "Frog", 0x07E0);
        return;
    }

    // Center logo in the key area
    int draw_x = x + (max_w - logo_width) / 2;
    int draw_y = y + (max_h - logo_height) / 2;

    // Clip to screen bounds
    int start_sx = 0, start_sy = 0;
    if (draw_x < 0) { start_sx = -draw_x; draw_x = 0; }
    if (draw_y < 0) { start_sy = -draw_y; draw_y = 0; }

    for (int sy = start_sy; sy < logo_height; sy++) {
        int screen_y = draw_y + sy - start_sy;
        if (screen_y >= 240) break;

        for (int sx = start_sx; sx < logo_width; sx++) {
            int screen_x = draw_x + sx - start_sx;
            if (screen_x >= 320) break;

            int src_idx = sy * logo_width + sx;
            uint8_t alpha = logo_alpha ? logo_alpha[src_idx] : 255;

            if (alpha > 128) {  // Simple threshold blending
                framebuffer[screen_y * 320 + screen_x] = logo_pixels[src_idx];
            }
        }
    }
}

void osk_render(uint16_t *framebuffer) {
    if (!osk_active || !framebuffer) return;

    // v35: Full width keyboard - from edge to edge
    int osk_x, osk_y, osk_w, osk_h;
    int key_w, key_h;

    if (osk_editor_mode) {
        // v35: Editor mode - keyboard at bottom, full width
        osk_x = 2;
        osk_y = 115;  // Bottom of screen (leaves room for editor above)
        key_w = 21;   // 15 keys * 21 = 315, leaving 5px margin
        key_h = 24;
        osk_w = 316;  // Full width
        osk_h = 123;  // 5 rows of keys
    } else {
        // v35: Normal mode - also full width now
        osk_x = 2;
        osk_y = 90;
        key_w = 21;
        key_h = 24;
        osk_w = 316;
        osk_h = 139;
    }

    // Background with border
    render_filled_rect(framebuffer, osk_x, osk_y, osk_w, osk_h, 0x2104);  // Dark gray
    render_rect(framebuffer, osk_x, osk_y, osk_w, osk_h, 0xFFFF);         // White border

    // Title and input display (only in normal mode)
    if (!osk_editor_mode) {
        // Title
        font_draw_text(framebuffer, 320, 240, osk_x + 4, osk_y + 2, osk_title, 0x07E0);  // Green

        // Input display with cursor
        char input_display[OSK_MAX_INPUT + 4];
        snprintf(input_display, sizeof(input_display), "[%s_]", osk_input);
        font_draw_text(framebuffer, 320, 240, osk_x + 100, osk_y + 2, input_display, 0xFFFF);
    }

    // Shift indicator - show only when active
    if (osk_shift_state > 0) {
        const char *mode = (osk_shift_state == 2) ? "LOCK" : "SHFT";
        int shift_y = osk_y + 2;
        font_draw_text(framebuffer, 320, 240, osk_x + osk_w - 45, shift_y, mode, 0xF800);
    }

    // Draw keyboard keys
    int keys_y = osk_y + (osk_editor_mode ? 4 : 18);
    for (int i = 0; i < (int)OSK_KEY_COUNT; i++) {
        const OSKKey *k = &osk_keys[i];
        int kx = osk_x + 2 + k->x * key_w;
        int ky = keys_y + k->y * key_h;
        int kw = k->w * key_w - 2;
        int kh = key_h - 2;

        // Key colors
        uint16_t bg_color = 0x4208;  // Gray
        uint16_t fg_color = 0xFFFF;  // White
        uint16_t border_color = 0x8410;  // Light gray

        // Special key colors
        if (k->special == 4) {  // OK/Enter - green
            bg_color = 0x0400;
        } else if (k->special == 5) {  // Cancel - red
            bg_color = 0x8000;
        } else if (k->special == 1 || k->special == 6) {  // Backspace/Clear - orange
            bg_color = 0x8200;
        } else if (k->special == 2) {  // Shift
            if (osk_shift_state == 2) {
                // Permashift - white background, dark text
                bg_color = 0xFFFF;
                fg_color = 0x0000;
            } else if (osk_shift_state == 1) {
                bg_color = 0x001F;  // Blue when temp shift
            }
        } else if (k->special == 3) {  // Caps - same visual as shift
            if (osk_shift_state == 2) {
                bg_color = 0xFFFF;
                fg_color = 0x0000;
            } else if (osk_shift_state == 1) {
                bg_color = 0x001F;
            }
        } else if (k->special == 7) {  // Frog key - dark green
            bg_color = 0x0320;
        } else if (k->special >= 8 && k->special <= 11) {  // Arrow keys - cyan
            bg_color = 0x0410;
        }

        // Selected key highlight
        if (i == osk_selected) {
            bg_color = 0x001F;  // Blue
            fg_color = 0xFFE0;  // Yellow
            border_color = 0xFFFF;
        }

        // Draw key background
        render_filled_rect(framebuffer, kx, ky, kw, kh, bg_color);
        render_rect(framebuffer, kx, ky, kw, kh, border_color);

        // Draw key label or logo
        if (k->special == 7) {
            // Draw FrogUI logo
            draw_logo(framebuffer, kx, ky, kw, kh);
        } else {
            // Draw text label - show uppercase when shift active (state > 0)
            const char *label = (osk_shift_state > 0) ? k->shift_label : k->label;
            int label_len = strlen(label);
            int label_x = kx + (kw - label_len * 10) / 2;
            int label_y = ky + (kh - 12) / 2;

            // Pixel adjustments for specific narrow characters
            char c = k->character;
            if (c == 'w' || c == 'W') label_x -= 2;  // W moves left
            else if (c == '1' || c == 'i' || c == 'I' || c == '[' || c == ']' ||
                     c == ';' || c == '\'' || c == ',' || c == '.' || c == '/') {
                label_x += 2;  // Narrow chars move right
            }

            font_draw_text(framebuffer, 320, 240, label_x, label_y, label, fg_color);
        }
    }
}

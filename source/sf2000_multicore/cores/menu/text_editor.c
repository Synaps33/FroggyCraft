// v59: Text editor/viewer for theme.ini files and text documents
// Supports editing mode (theme.ini) and viewer mode (TEXT section) with word wrap
// v35 fixes: font_measure_text() for cursor position, bright green blinking cursor
#include "text_editor.h"
#include "render.h"
#include "font.h"
#include "osk.h"
#include "gfx_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Editor state
static int editor_active = 0;
static int editor_saved = 0;
static char editor_filepath[256];

// v59: Mode flag - 0 = editor, 1 = viewer
static int viewer_mode = 0;

// v59: Font toggle - 0 = theme font, 1 = built-in font
static int use_builtin_font = 0;

// v59: Flag to reset input state when opening viewer
static int viewer_input_reset_needed = 0;

// Text content - array of lines (original file lines for editor)
static char lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LEN];
static int line_count = 0;

// v59: Wrapped lines for viewer mode (display lines after word-wrap)
static char wrapped_lines[VIEWER_MAX_LINES][VIEWER_MAX_LINE_LEN];
static int wrapped_line_count = 0;

// Cursor position
static int cursor_line = 0;
static int scroll_offset = 0;

// v34: Visible lines - fewer now that keyboard is always visible
#define VISIBLE_LINES_EDITOR 6

// v59: Visible lines in viewer mode (full screen)
// TTF font: 14 lines, Built-in font: 20 lines
#define VISIBLE_LINES_VIEWER_TTF 14
#define VISIBLE_LINES_VIEWER_BUILTIN 20

// v35: Maximum display width in pixels for text (window width minus margins)
#define TEXT_AREA_WIDTH_EDITOR 218  // win_w(250) - left_margin(22) - right_margin(10)
#define TEXT_AREA_WIDTH_VIEWER 268  // win_w(276) - left_margin(4) - right_margin(4)

// v35: Blink counter for cursor
static int blink_counter = 0;
#define BLINK_RATE 15  // Toggle every 15 frames (~4Hz at 60fps)

// Undo system
typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LEN];
    int line_count;
    int cursor_line;
} UndoState;

static UndoState undo_stack[EDITOR_UNDO_LEVELS];
static int undo_index = 0;
static int undo_count = 0;

// Button states
// Editor mode: 0=UNDO, 1=SAVE, 2=FONT, 3=EXIT
// Viewer mode: 0=EDIT, 1=FONT, 2=EXIT
static int selected_button = -1;  // -1 = editing/viewing text, 0-3 = button selected

// v34: OSK editing state
static int osk_editing = 0;
static int edit_cursor_in_line = 0;  // Cursor position within the edited line

// Forward declarations
static void push_undo(void);
static void pop_undo(void);
static void osk_done_callback(int result, const char *input);
static void start_line_edit(void);
static void save_osk_to_line(void);
static void wrap_text_for_viewer(void);

void text_editor_init(void) {
    editor_active = 0;
    editor_saved = 0;
    line_count = 0;
    cursor_line = 0;
    scroll_offset = 0;
    undo_index = 0;
    undo_count = 0;
    selected_button = -1;
    osk_editing = 0;
    edit_cursor_in_line = 0;
    blink_counter = 0;
    viewer_mode = 0;
    use_builtin_font = 0;
    wrapped_line_count = 0;
}

// v59: Measure text width using current font
static int measure_text_current_font(const char *text) {
    if (use_builtin_font) {
        return builtin_measure_text(text);
    } else {
        return font_measure_text(text);
    }
}

// v59: Word-wrap text for viewer mode
static void wrap_text_for_viewer(void) {
    wrapped_line_count = 0;

    // Calculate max width based on font
    int max_width = TEXT_AREA_WIDTH_VIEWER;

    for (int i = 0; i < line_count && wrapped_line_count < VIEWER_MAX_LINES; i++) {
        const char *src = lines[i];
        int src_len = strlen(src);

        // Empty line
        if (src_len == 0) {
            wrapped_lines[wrapped_line_count][0] = '\0';
            wrapped_line_count++;
            continue;
        }

        int pos = 0;
        while (pos < src_len && wrapped_line_count < VIEWER_MAX_LINES) {
            // Find how much text fits on this line
            char temp[VIEWER_MAX_LINE_LEN];
            int temp_len = 0;
            int last_space = -1;
            int width = 0;

            while (pos + temp_len < src_len && temp_len < VIEWER_MAX_LINE_LEN - 1) {
                char c = src[pos + temp_len];

                // Check if adding this character exceeds width
                char single[2] = {c, '\0'};
                int char_width = measure_text_current_font(single);

                if (width + char_width > max_width && temp_len > 0) {
                    break;
                }

                temp[temp_len] = c;
                width += char_width;

                if (c == ' ') {
                    last_space = temp_len;
                }
                temp_len++;
            }
            temp[temp_len] = '\0';

            // If we're not at end of source line, try to break at word boundary
            if (pos + temp_len < src_len && last_space > 0) {
                // Break at last space
                temp[last_space] = '\0';
                strncpy(wrapped_lines[wrapped_line_count], temp, VIEWER_MAX_LINE_LEN - 1);
                wrapped_lines[wrapped_line_count][VIEWER_MAX_LINE_LEN - 1] = '\0';
                pos += last_space + 1;  // Skip past the space
            } else {
                // No space found or at end of line
                strncpy(wrapped_lines[wrapped_line_count], temp, VIEWER_MAX_LINE_LEN - 1);
                wrapped_lines[wrapped_line_count][VIEWER_MAX_LINE_LEN - 1] = '\0';
                pos += temp_len;
            }

            wrapped_line_count++;
        }
    }
}

int text_editor_open(const char *filepath) {
    if (!filepath) return 0;

    // Store filepath
    strncpy(editor_filepath, filepath, sizeof(editor_filepath) - 1);
    editor_filepath[sizeof(editor_filepath) - 1] = '\0';

    // Reset state
    line_count = 0;
    cursor_line = 0;
    scroll_offset = 0;
    undo_index = 0;
    undo_count = 0;
    selected_button = -1;
    osk_editing = 0;
    edit_cursor_in_line = 0;
    editor_saved = 0;
    blink_counter = 0;
    viewer_mode = 0;  // Editor mode
    // Keep current font setting

    // Clear lines
    memset(lines, 0, sizeof(lines));

    // Load file
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        // File doesn't exist - start with empty content
        line_count = 1;
        lines[0][0] = '\0';
        editor_active = 1;
        return 1;
    }

    char buffer[EDITOR_MAX_LINE_LEN];
    while (fgets(buffer, sizeof(buffer), fp) && line_count < EDITOR_MAX_LINES) {
        // Remove newline
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        if (len > 0 && buffer[len - 1] == '\r') {
            buffer[len - 1] = '\0';
        }

        strncpy(lines[line_count], buffer, EDITOR_MAX_LINE_LEN - 1);
        lines[line_count][EDITOR_MAX_LINE_LEN - 1] = '\0';
        line_count++;
    }
    fclose(fp);

    if (line_count == 0) {
        line_count = 1;
        lines[0][0] = '\0';
    }

    editor_active = 1;
    return 1;
}

// v59: Open in viewer mode (read-only, full screen, word-wrapped)
int text_editor_open_viewer(const char *filepath) {
    if (!filepath) return 0;

    // Store filepath
    strncpy(editor_filepath, filepath, sizeof(editor_filepath) - 1);
    editor_filepath[sizeof(editor_filepath) - 1] = '\0';

    // Reset state
    line_count = 0;
    cursor_line = 0;
    scroll_offset = 0;
    selected_button = -1;
    editor_saved = 0;
    blink_counter = 0;
    viewer_mode = 1;  // Viewer mode
    use_builtin_font = 1;  // v59: Start with built-in font in viewer
    viewer_input_reset_needed = 1;  // v59: Reset input state

    // Clear lines
    memset(lines, 0, sizeof(lines));
    memset(wrapped_lines, 0, sizeof(wrapped_lines));
    wrapped_line_count = 0;

    // Load file
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        // File doesn't exist
        line_count = 1;
        lines[0][0] = '\0';
        wrap_text_for_viewer();
        editor_active = 1;
        return 1;
    }

    char buffer[EDITOR_MAX_LINE_LEN * 2];  // Larger buffer for long lines
    while (fgets(buffer, sizeof(buffer), fp) && line_count < EDITOR_MAX_LINES) {
        // Remove newline
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        if (len > 0 && buffer[len - 1] == '\r') {
            buffer[len - 1] = '\0';
        }

        strncpy(lines[line_count], buffer, EDITOR_MAX_LINE_LEN - 1);
        lines[line_count][EDITOR_MAX_LINE_LEN - 1] = '\0';
        line_count++;
    }
    fclose(fp);

    if (line_count == 0) {
        line_count = 1;
        lines[0][0] = '\0';
    }

    // Generate word-wrapped lines
    wrap_text_for_viewer();

    editor_active = 1;
    return 1;
}

void text_editor_close(void) {
    editor_active = 0;
    osk_editing = 0;
    if (osk_is_active()) {
        osk_close();
    }
}

int text_editor_is_active(void) {
    return editor_active;
}

int text_editor_is_viewer_mode(void) {
    return viewer_mode;
}

int text_editor_was_saved(void) {
    return editor_saved;
}

static void push_undo(void) {
    // Save current state to undo stack
    UndoState *state = &undo_stack[undo_index];
    memcpy(state->lines, lines, sizeof(lines));
    state->line_count = line_count;
    state->cursor_line = cursor_line;

    undo_index = (undo_index + 1) % EDITOR_UNDO_LEVELS;
    if (undo_count < EDITOR_UNDO_LEVELS) {
        undo_count++;
    }
}

static void pop_undo(void) {
    if (undo_count == 0) return;

    undo_index = (undo_index - 1 + EDITOR_UNDO_LEVELS) % EDITOR_UNDO_LEVELS;
    undo_count--;

    UndoState *state = &undo_stack[undo_index];
    memcpy(lines, state->lines, sizeof(lines));
    line_count = state->line_count;
    cursor_line = state->cursor_line;

    // Adjust scroll
    if (cursor_line < scroll_offset) {
        scroll_offset = cursor_line;
    } else if (cursor_line >= scroll_offset + VISIBLE_LINES_EDITOR) {
        scroll_offset = cursor_line - VISIBLE_LINES_EDITOR + 1;
    }
}

static int save_file(void) {
    FILE *fp = fopen(editor_filepath, "w");
    if (!fp) return 0;

    for (int i = 0; i < line_count; i++) {
        fprintf(fp, "%s\n", lines[i]);
    }
    fclose(fp);
    editor_saved = 1;
    return 1;
}

// v34: Save current OSK content back to the line
static void save_osk_to_line(void) {
    if (osk_is_active()) {
        const char *input = osk_get_input();
        if (input) {
            strncpy(lines[cursor_line], input, EDITOR_MAX_LINE_LEN - 1);
            lines[cursor_line][EDITOR_MAX_LINE_LEN - 1] = '\0';
        }
        edit_cursor_in_line = osk_get_cursor_pos();
    }
}

// v34: Start editing current line with OSK in editor mode
static void start_line_edit(void) {
    osk_editing = 1;
    // Open OSK in editor mode - keyboard at bottom, cursor position preserved
    osk_open_editor(lines[cursor_line], edit_cursor_in_line, osk_done_callback);
}

// v34: OSK callback - handles confirm, cancel, and arrow key navigation
static void osk_done_callback(int result, const char *input) {
    if (result == 1) {
        // Confirmed - save the edit
        if (input) {
            push_undo();  // Save undo state before change
            strncpy(lines[cursor_line], input, EDITOR_MAX_LINE_LEN - 1);
            lines[cursor_line][EDITOR_MAX_LINE_LEN - 1] = '\0';
        }
        osk_editing = 0;
        edit_cursor_in_line = 0;
    } else if (result == 2 && input) {
        // v34: Arrow key pressed - navigate between lines while editing
        char direction = input[0];

        // First save current edits to line
        save_osk_to_line();

        if (direction == 'U') {
            // Move up
            if (cursor_line > 0) {
                cursor_line--;
                if (cursor_line < scroll_offset) {
                    scroll_offset = cursor_line;
                }
                // Clamp cursor position to new line length
                int new_len = strlen(lines[cursor_line]);
                if (edit_cursor_in_line > new_len) {
                    edit_cursor_in_line = new_len;
                }
                // Reopen OSK for new line
                start_line_edit();
            }
        } else if (direction == 'D') {
            // Move down
            if (cursor_line < line_count - 1) {
                cursor_line++;
                if (cursor_line >= scroll_offset + VISIBLE_LINES_EDITOR) {
                    scroll_offset = cursor_line - VISIBLE_LINES_EDITOR + 1;
                }
                // Clamp cursor position to new line length
                int new_len = strlen(lines[cursor_line]);
                if (edit_cursor_in_line > new_len) {
                    edit_cursor_in_line = new_len;
                }
                // Reopen OSK for new line
                start_line_edit();
            }
        }
        // Left/Right are handled by OSK itself via L/R shoulders
    } else {
        // Cancelled
        osk_editing = 0;
        edit_cursor_in_line = 0;
    }
}

// v59: Handle input for viewer mode
static int handle_viewer_input(int up, int down, int left, int right, int a, int b, int x, int y, int l, int r) {
    static int prev_up = 0, prev_down = 0, prev_left = 0, prev_right = 0;
    static int prev_a = 0, prev_b = 0, prev_l = 0, prev_r = 0;

    // v59 fix: Reset prev state when viewer is opened (not skip frame!)
    if (viewer_input_reset_needed) {
        prev_up = 0; prev_down = 0;
        prev_left = 0; prev_right = 0;
        prev_a = 0; prev_b = 0;
        prev_l = 0; prev_r = 0;
        viewer_input_reset_needed = 0;
    }

    int visible_lines = use_builtin_font ? VISIBLE_LINES_VIEWER_BUILTIN : VISIBLE_LINES_VIEWER_TTF;
    int total_lines = wrapped_line_count;
    int max_scroll = total_lines - visible_lines;
    if (max_scroll < 0) max_scroll = 0;

    // Button selection mode
    if (selected_button >= 0) {
        int max_btn = 2;  // EDIT, FONT, EXIT = 3 buttons (0-2)

        // Navigation on release
        if (prev_up && !up && selected_button > 0) {
            selected_button--;
        }
        if (prev_down && !down && selected_button < max_btn) {
            selected_button++;
        }
        if (prev_left && !left) {
            selected_button = -1;  // Back to viewing
        }
        if (prev_a && !a) {
            switch (selected_button) {
                case 0: // EDIT - switch to editor mode
                    viewer_mode = 0;
                    cursor_line = 0;
                    scroll_offset = 0;
                    selected_button = -1;
                    break;
                case 1: // FONT - toggle font
                    use_builtin_font = !use_builtin_font;
                    wrap_text_for_viewer();  // Re-wrap with new font metrics
                    // Recalculate visible lines for new font
                    visible_lines = use_builtin_font ? VISIBLE_LINES_VIEWER_BUILTIN : VISIBLE_LINES_VIEWER_TTF;
                    max_scroll = wrapped_line_count - visible_lines;
                    if (max_scroll < 0) max_scroll = 0;
                    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                    break;
                case 2: // EXIT
                    text_editor_close();
                    goto update_prev;
            }
        }
        if (prev_b && !b) {
            text_editor_close();
            goto update_prev;
        }

        goto update_prev;
    }

    // Viewing mode - scroll through document (continuous while held)
    if (down && scroll_offset < max_scroll) {
        scroll_offset++;
    }
    if (up && scroll_offset > 0) {
        scroll_offset--;
    }

    // Page up/down with L/R (on release)
    if (prev_l && !l) {
        scroll_offset -= visible_lines;
        if (scroll_offset < 0) scroll_offset = 0;
    }
    if (prev_r && !r) {
        scroll_offset += visible_lines;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    }

    // Right to enter button menu (on release)
    if (prev_right && !right) {
        selected_button = 2;  // Default to EXIT
    }

    // B to exit (on release)
    if (prev_b && !b) {
        text_editor_close();
        goto update_prev;
    }

update_prev:
    prev_up = up; prev_down = down;
    prev_left = left; prev_right = right;
    prev_a = a; prev_b = b;
    prev_l = l; prev_r = r;

    return !editor_active ? 1 : 0;
}

int text_editor_handle_input(int up, int down, int left, int right, int a, int b, int x, int y, int l, int r) {
    if (!editor_active) return 1;

    // v59: Viewer mode has separate input handling
    if (viewer_mode) {
        return handle_viewer_input(up, down, left, right, a, b, x, y, l, r);
    }

    // v34: Track previous state for edge detection
    static int prev_up = 0, prev_down = 0, prev_left = 0, prev_right = 0;
    static int prev_a = 0, prev_b = 0, prev_x = 0, prev_y = 0;

    // If OSK is active in editor mode, let it handle input
    if (osk_is_active() && osk_is_editor_mode()) {
        // v34: Pass L/R shoulder buttons for cursor movement
        int closed = osk_handle_input(up, down, left, right, a, b, l, r);
        if (closed) {
            osk_editing = 0;
        }
        return 0;
    }

    // Handle button navigation (right side buttons)
    // v59: Editor mode has 4 buttons: UNDO, SAVE, FONT, EXIT
    if (selected_button >= 0) {
        int max_btn = 3;  // 4 buttons (0-3)

        // Navigating buttons (on button release)
        if (prev_up && !up && selected_button > 0) {
            selected_button--;
        } else if (prev_down && !down && selected_button < max_btn) {
            selected_button++;
        } else if (prev_left && !left) {
            // Go back to text editing
            selected_button = -1;
        } else if (prev_a && !a) {
            // Activate selected button
            switch (selected_button) {
                case 0: // UNDO
                    pop_undo();
                    break;
                case 1: // SAVE
                    save_file();
                    text_editor_close();
                    prev_a = a;
                    return 1;
                case 2: // FONT
                    use_builtin_font = !use_builtin_font;
                    break;
                case 3: // EXIT
                    text_editor_close();
                    prev_a = a;
                    return 1;
            }
        } else if (prev_b && !b) {
            // Cancel - close editor
            text_editor_close();
            prev_b = b;
            return 1;
        }

        prev_up = up; prev_down = down;
        prev_left = left; prev_right = right;
        prev_a = a; prev_b = b;
        prev_x = x; prev_y = y;
        return 0;
    }

    // Text editing mode (on button release for better control)
    if (prev_up && !up) {
        if (cursor_line > 0) {
            cursor_line--;
            if (cursor_line < scroll_offset) {
                scroll_offset = cursor_line;
            }
        }
    } else if (prev_down && !down) {
        if (cursor_line < line_count - 1) {
            cursor_line++;
            if (cursor_line >= scroll_offset + VISIBLE_LINES_EDITOR) {
                scroll_offset = cursor_line - VISIBLE_LINES_EDITOR + 1;
            }
        }
    } else if (prev_right && !right) {
        // Move to button panel
        selected_button = 1;  // Default to SAVE
    } else if (prev_a && !a) {
        // v34: Edit current line with OSK in editor mode
        push_undo();  // Save undo state before editing
        edit_cursor_in_line = strlen(lines[cursor_line]);  // Start at end of line
        start_line_edit();
    } else if (prev_b && !b) {
        // Cancel - close editor
        text_editor_close();
        prev_b = b;
        return 1;
    } else if (prev_x && !x) {
        // X = Insert new line after current
        if (line_count < EDITOR_MAX_LINES) {
            push_undo();
            // Shift lines down
            for (int i = line_count; i > cursor_line + 1; i--) {
                strcpy(lines[i], lines[i - 1]);
            }
            lines[cursor_line + 1][0] = '\0';
            line_count++;
            cursor_line++;
            if (cursor_line >= scroll_offset + VISIBLE_LINES_EDITOR) {
                scroll_offset = cursor_line - VISIBLE_LINES_EDITOR + 1;
            }
        }
    } else if (prev_y && !y) {
        // Y = Delete current line
        if (line_count > 1) {
            push_undo();
            // Shift lines up
            for (int i = cursor_line; i < line_count - 1; i++) {
                strcpy(lines[i], lines[i + 1]);
            }
            line_count--;
            if (cursor_line >= line_count) {
                cursor_line = line_count - 1;
            }
            if (cursor_line < scroll_offset) {
                scroll_offset = cursor_line;
            }
        }
    }

    prev_up = up; prev_down = down;
    prev_left = left; prev_right = right;
    prev_a = a; prev_b = b;
    prev_x = x; prev_y = y;
    return 0;
}

// v35: Calculate how many characters fit in given pixel width
static int calc_chars_that_fit(const char *text, int max_width) {
    char temp[EDITOR_MAX_LINE_LEN];
    int len = strlen(text);

    for (int i = len; i >= 0; i--) {
        strncpy(temp, text, i);
        temp[i] = '\0';
        int width = measure_text_current_font(temp);
        if (width <= max_width) {
            return i;
        }
    }
    return 0;
}

// v35: Calculate pixel width of text from position 0 to 'pos'
static int calc_text_width_to_pos(const char *text, int pos) {
    char temp[EDITOR_MAX_LINE_LEN];
    if (pos <= 0) return 0;
    int len = strlen(text);
    if (pos > len) pos = len;

    strncpy(temp, text, pos);
    temp[pos] = '\0';
    return measure_text_current_font(temp);
}

// v59: Draw text using current font
static void draw_text_current_font(uint16_t *fb, int x, int y, const char *text, uint16_t color) {
    if (use_builtin_font) {
        builtin_draw_text(fb, 320, 240, x, y, text, color);
    } else {
        font_draw_text(fb, 320, 240, x, y, text, color);
    }
}

// v59: Render viewer mode (full screen, word-wrapped)
static void render_viewer(uint16_t *framebuffer) {
    // v59 fix: Narrower window, buttons at screen edge
    int btn_w = 42;
    int btn_x = 320 - btn_w;  // Buttons at right edge (278-320)
    int win_x = 0;
    int win_y = 0;
    int win_w = btn_x - 2;    // Window ends 2px before buttons (0-276)
    int win_h = 240;
    int text_start_x = win_x + 4;
    int text_start_y = win_y + 18;
    int line_height = use_builtin_font ? 10 : 14;
    int visible_lines = use_builtin_font ? VISIBLE_LINES_VIEWER_BUILTIN : VISIBLE_LINES_VIEWER_TTF;

    // Background
    render_filled_rect(framebuffer, win_x, win_y, win_w, win_h, 0x2104);
    render_rect(framebuffer, win_x, win_y, win_w, win_h, 0xFFFF);

    // Title - show filename
    const char *filename = strrchr(editor_filepath, '/');
    if (!filename) filename = strrchr(editor_filepath, '\\');
    if (filename) filename++;
    else filename = editor_filepath;

    char title[64];
    snprintf(title, sizeof(title), "%.32s", filename);
    draw_text_current_font(framebuffer, win_x + 4, win_y + 2, title, 0x07E0);

    // Show line position
    char pos_info[32];
    snprintf(pos_info, sizeof(pos_info), "%d/%d", scroll_offset + 1, wrapped_line_count);
    int pos_width = measure_text_current_font(pos_info);
    draw_text_current_font(framebuffer, win_w - pos_width - 4, win_y + 2, pos_info, 0x8410);

    // Separator
    render_filled_rect(framebuffer, win_x + 2, win_y + 14, win_w - 4, 1, 0x8410);

    // Render visible lines
    int text_y = text_start_y;
    for (int i = 0; i < visible_lines && (scroll_offset + i) < wrapped_line_count; i++) {
        int line_idx = scroll_offset + i;
        draw_text_current_font(framebuffer, text_start_x, text_y, wrapped_lines[line_idx], 0xFFFF);
        text_y += line_height;
    }

    // Scroll indicators
    if (scroll_offset > 0) {
        draw_text_current_font(framebuffer, win_w - 12, win_y + 16, "^", 0xFFE0);
    }
    int max_scroll = wrapped_line_count - visible_lines;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset < max_scroll) {
        draw_text_current_font(framebuffer, win_w - 12, win_y + win_h - 12, "v", 0xFFE0);
    }

    // Buttons at right edge
    int btn_y = 30;
    int btn_h = 20;
    int btn_spacing = 26;

    const char *btn_labels[] = {"EDIT", "FONT", "EXIT"};
    uint16_t btn_colors[] = {0x07E0, 0xFFE0, 0xF800};  // Green, Yellow, Red

    for (int i = 0; i < 3; i++) {
        int is_btn_selected = (selected_button == i);
        uint16_t bg = is_btn_selected ? btn_colors[i] : 0x4208;
        uint16_t fg = is_btn_selected ? 0x0000 : btn_colors[i];

        render_filled_rect(framebuffer, btn_x, btn_y + i * btn_spacing, btn_w, btn_h, bg);
        render_rect(framebuffer, btn_x, btn_y + i * btn_spacing, btn_w, btn_h, btn_colors[i]);
        draw_text_current_font(framebuffer, btn_x + 4, btn_y + i * btn_spacing + 4, btn_labels[i], fg);
    }

    // Font indicator below buttons
    const char *font_name = use_builtin_font ? "[BLT]" : "[TTF]";
    draw_text_current_font(framebuffer, btn_x + 2, btn_y + 3 * btn_spacing, font_name, 0x8410);

    // Instructions at bottom of window
    draw_text_current_font(framebuffer, win_x + 4, win_y + win_h - 12, "UP/DN:Scroll B:Exit", 0x8410);
}

void text_editor_render(uint16_t *framebuffer) {
    if (!editor_active || !framebuffer) return;

    // v59: Viewer mode has separate rendering
    if (viewer_mode) {
        render_viewer(framebuffer);
        return;
    }

    // v35: Update blink counter
    blink_counter++;
    if (blink_counter >= BLINK_RATE * 2) {
        blink_counter = 0;
    }
    int cursor_visible = (blink_counter < BLINK_RATE);

    // v34: Editor window is ALWAYS at top, keyboard at bottom when editing
    int win_x = 5;
    int win_y = 2;
    int win_w = 250;
    int win_h = 108;  // Smaller to fit above keyboard
    int text_start_x = win_x + 22;  // After line numbers
    int text_end_x = win_x + win_w - 10;  // Before right margin
    int text_area_width = text_end_x - text_start_x;

    // Background
    render_filled_rect(framebuffer, win_x, win_y, win_w, win_h, 0x2104);
    render_rect(framebuffer, win_x, win_y, win_w, win_h, 0xFFFF);

    // Title
    draw_text_current_font(framebuffer, win_x + 4, win_y + 2, "THEME.INI", 0x07E0);

    // v34: Show current line / total lines count
    char line_info[16];
    snprintf(line_info, sizeof(line_info), "%d/%d", cursor_line + 1, line_count);
    draw_text_current_font(framebuffer, win_x + 90, win_y + 2, line_info, 0x8410);

    // v59: Font indicator
    const char *font_ind = use_builtin_font ? "[B]" : "[T]";
    draw_text_current_font(framebuffer, win_x + 140, win_y + 2, font_ind, 0x8410);

    // Render visible lines
    int text_y = win_y + 16;
    int line_height = use_builtin_font ? 10 : 14;

    for (int i = 0; i < VISIBLE_LINES_EDITOR && (scroll_offset + i) < line_count; i++) {
        int line_idx = scroll_offset + i;
        int is_selected = (line_idx == cursor_line && selected_button < 0);
        int is_editing = (is_selected && osk_editing && osk_is_active());

        // Line number
        char num[8];
        snprintf(num, sizeof(num), "%02d", line_idx + 1);
        draw_text_current_font(framebuffer, win_x + 2, text_y, num, 0x8410);

        // Line content - get from OSK if editing this line
        const char *line_content;
        if (is_editing) {
            line_content = osk_get_input();
        } else {
            line_content = lines[line_idx];
        }

        // v35: Calculate text dimensions using current font
        int full_text_width = measure_text_current_font(line_content);
        int has_overflow = (full_text_width > text_area_width);

        // v35: Calculate display start position for scrolling when editing
        int display_start_char = 0;
        int cursor_pos = is_editing ? osk_get_cursor_pos() : 0;
        int line_len = strlen(line_content);

        if (is_editing && has_overflow) {
            // Calculate width of text up to cursor
            int cursor_pixel_x = calc_text_width_to_pos(line_content, cursor_pos);

            // If cursor is beyond visible area, scroll to show it
            if (cursor_pixel_x > text_area_width - 20) {
                // Find starting character that puts cursor in view
                int target_x = cursor_pixel_x - text_area_width + 40;
                int accum_width = 0;
                for (int c = 0; c < line_len; c++) {
                    char ch[2] = {line_content[c], '\0'};
                    int ch_width = measure_text_current_font(ch);
                    if (accum_width >= target_x) {
                        display_start_char = c;
                        break;
                    }
                    accum_width += ch_width;
                }
            }
        }

        // v35: Build display string that fits in text area with proper clipping
        char display_line[EDITOR_MAX_LINE_LEN];
        int display_idx = 0;
        int accum_width = 0;
        int show_left_arrow = (display_start_char > 0);
        int show_right_arrow = 0;

        // Reserve space for arrows if needed
        int available_width = text_area_width;
        if (show_left_arrow) available_width -= 10;
        available_width -= 10;  // Reserve for ">"

        for (int c = display_start_char; c < line_len; c++) {
            char ch[2] = {line_content[c], '\0'};
            int ch_width = measure_text_current_font(ch);

            if (accum_width + ch_width > available_width) {
                show_right_arrow = 1;
                break;
            }

            display_line[display_idx++] = line_content[c];
            accum_width += ch_width;
        }
        display_line[display_idx] = '\0';

        // Background for selected/editing line
        uint16_t text_color = 0xFFFF;  // White
        if (is_editing) {
            render_filled_rect(framebuffer, text_start_x - 2, text_y - 1, text_area_width + 4, line_height, 0x0410);
            text_color = 0xFFE0;  // Yellow
        } else if (is_selected) {
            render_filled_rect(framebuffer, text_start_x - 2, text_y - 1, text_area_width + 4, line_height, 0x4208);
            text_color = 0xFFE0;  // Yellow
        }

        // Draw line content with clipping
        int draw_x = text_start_x;
        if (show_left_arrow) {
            draw_text_current_font(framebuffer, draw_x, text_y, "<", 0x07E0);
            draw_x += 10;
        }

        draw_text_current_font(framebuffer, draw_x, text_y, display_line, text_color);

        if (show_right_arrow) {
            draw_text_current_font(framebuffer, text_end_x - 8, text_y, ">", 0x07E0);
        }

        // v35: Draw cursor when editing
        if (is_editing && cursor_visible) {
            int cursor_x_in_display = calc_text_width_to_pos(line_content + display_start_char,
                                                              cursor_pos - display_start_char);
            int cursor_x = draw_x + cursor_x_in_display;

            if (cursor_x >= text_start_x && cursor_x < text_end_x) {
                render_filled_rect(framebuffer, cursor_x, text_y - 1, 2, line_height, 0x07E0);
            }
        }

        text_y += line_height;
    }

    // Scroll indicators
    if (scroll_offset > 0) {
        draw_text_current_font(framebuffer, win_x + win_w - 14, win_y + 14, "^", 0xFFFF);
    }
    if (scroll_offset + VISIBLE_LINES_EDITOR < line_count) {
        draw_text_current_font(framebuffer, win_x + win_w - 14, win_y + win_h - 10, "v", 0xFFFF);
    }

    // v59: Buttons on the right side - 4 buttons now
    int btn_x = win_x + win_w + 5;
    int btn_y = win_y + 2;
    int btn_w = 50;
    int btn_h = 18;
    int btn_spacing = 20;

    const char *btn_labels[] = {"UNDO", "SAVE", "FONT", "EXIT"};
    uint16_t btn_colors[] = {0xFD20, 0x07E0, 0xFFE0, 0xF800};  // Orange, Green, Yellow, Red

    for (int i = 0; i < 4; i++) {
        int is_btn_selected = (selected_button == i);
        uint16_t bg = is_btn_selected ? btn_colors[i] : 0x4208;
        uint16_t fg = is_btn_selected ? 0x0000 : btn_colors[i];

        render_filled_rect(framebuffer, btn_x, btn_y + i * btn_spacing, btn_w, btn_h, bg);
        render_rect(framebuffer, btn_x, btn_y + i * btn_spacing, btn_w, btn_h, btn_colors[i]);
        draw_text_current_font(framebuffer, btn_x + 4, btn_y + i * btn_spacing + 2, btn_labels[i], fg);
    }

    // Undo count indicator
    char undo_info[16];
    snprintf(undo_info, sizeof(undo_info), "(%d)", undo_count);
    draw_text_current_font(framebuffer, btn_x + btn_w + 2, btn_y + 2, undo_info, 0x8410);

    // v34: Render OSK if active (keyboard at bottom)
    if (osk_is_active()) {
        osk_render(framebuffer);
    } else {
        // Instructions at bottom when not editing
        draw_text_current_font(framebuffer, win_x, win_y + win_h + 2, "A:Edit X:New Y:Del", 0x8410);
    }
}

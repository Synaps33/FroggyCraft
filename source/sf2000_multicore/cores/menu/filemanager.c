/*
 * FrogUI File Manager
 * v78: Total Commander style dual-panel file manager
 *      - 128 char filenames
 *      - Bottom bar selection with L/R
 *      - View images/videos/music/text with return to FM
 *      - Page up/down with Left/Right
 *      - Fixed scroll on wraparound
 *      - Delete: truncate to 5 bytes + move to recycled
 *      - Copy/Move with confirmation
 *      - Mkdir with OSK
 */

#include "filemanager.h"
#include "font.h"
#include "render.h"
#include "gfx_theme.h"
#include "image_viewer.h"
#include "video_player.h"
#include "music_player.h"
#include "text_editor.h"
#include "osk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef SF2000
#include "../../dirent.h"
#else
#include <dirent.h>
#endif

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Panel dimensions
#define PANEL_WIDTH 156
#define PANEL_HEIGHT 190
#define PANEL_LEFT_X 2
#define PANEL_RIGHT_X 162
#define PANEL_Y 8

// Bottom bar
#define BAR_Y 202
#define BAR_HEIGHT 36

// Colors
#define COL_PANEL_BG    0x0000  // Black
#define COL_PANEL_BORDER 0x4208 // Gray border
#define COL_HEADER_BG   0x001F  // Blue header
#define COL_HEADER_TEXT 0xFFFF  // White
#define COL_FILE_TEXT   0xFFFF  // White
#define COL_DIR_TEXT    0xFFE0  // Yellow for directories
#define COL_SELECTED_BG 0x001F  // Blue selection
#define COL_INACTIVE_SEL 0x4208 // Gray selection for inactive panel
#define COL_BAR_BG      0x001F  // Blue bar
#define COL_BAR_TEXT    0xFFFF  // White
#define COL_LEGEND      0xEF7D  // #eee in RGB565
#define COL_DIALOG_BG   0x0000  // Black dialog bg
#define COL_DIALOG_BORDER 0xFFFF // White border

// Max entries per panel
#define MAX_ENTRIES 256
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 128

// File entry
typedef struct {
    char name[MAX_NAME_LEN];
    int is_dir;
    uint32_t size;
} FMEntry;

// Panel state
typedef struct {
    char path[MAX_PATH_LEN];
    FMEntry entries[MAX_ENTRIES];
    int entry_count;
    int selected;
    int scroll;
} Panel;

// File manager state
static int fm_active = 0;
static Panel left_panel;
static Panel right_panel;
static int active_panel = 0;  // 0 = left, 1 = right
static int bar_mode = 0;      // 0 = file panels, 1 = bottom bar
static int bar_selected = 0;  // selected button in bottom bar (0-5)

// v78: Confirmation dialog state
typedef enum {
    DIALOG_NONE = 0,
    DIALOG_DELETE,
    DIALOG_COPY,
    DIALOG_MOVE,
    DIALOG_MKDIR
} DialogType;

static DialogType dialog_type = DIALOG_NONE;
static int dialog_selected = 0;  // 0 = Yes, 1 = No
static char dialog_filepath[512];
static char dialog_filename[MAX_NAME_LEN];

// v78: Copy/Move source info
static char copy_source_path[512];
static int copy_is_move = 0;  // 0 = copy, 1 = move

// Return from viewer flags
static int fm_return_pending = 0;

// Visible lines per panel
#define VISIBLE_LINES 10
#define LINE_HEIGHT 16
#define FM_HEADER_HEIGHT 14

// Current operation message
static char status_msg[64] = "";
static int status_timer = 0;

// Forward declarations
static void scan_panel(Panel *panel);
static void draw_panel(uint16_t *fb, Panel *panel, int x, int is_active);
static void draw_bottom_bar(uint16_t *fb);
static void draw_dialog(uint16_t *fb);
static void enter_directory(Panel *panel);
static void go_up_directory(Panel *panel);
static void show_status(const char *msg);

// v78: Check file extension
static int has_extension(const char *name, const char *ext) {
    int nlen = strlen(name);
    int elen = strlen(ext);
    if (nlen < elen + 1) return 0;
    return strcasecmp(name + nlen - elen, ext) == 0;
}

static int is_image_file(const char *name) {
    return has_extension(name, ".png") || has_extension(name, ".jpg") ||
           has_extension(name, ".jpeg") || has_extension(name, ".gif") ||
           has_extension(name, ".webp") || has_extension(name, ".bmp");
}

static int is_video_file(const char *name) {
    return has_extension(name, ".avi") || has_extension(name, ".mp4") ||
           has_extension(name, ".mkv") || has_extension(name, ".mov");
}

static int is_music_file(const char *name) {
    return has_extension(name, ".mp3") || has_extension(name, ".wav") ||
           has_extension(name, ".ogg") || has_extension(name, ".flac");
}

static int is_text_file(const char *name) {
    return has_extension(name, ".txt") || has_extension(name, ".ini") ||
           has_extension(name, ".cfg") || has_extension(name, ".log") ||
           has_extension(name, ".md") || has_extension(name, ".json") ||
           has_extension(name, ".xml") || has_extension(name, ".html") ||
           has_extension(name, ".css") || has_extension(name, ".js") ||
           has_extension(name, ".c") || has_extension(name, ".h") ||
           has_extension(name, ".py") || has_extension(name, ".sh");
}

void fm_init(void) {
    fm_active = 0;
    strcpy(left_panel.path, "/mnt/sda1");
    strcpy(right_panel.path, "/mnt/sda1");
    left_panel.entry_count = 0;
    right_panel.entry_count = 0;
    left_panel.selected = 0;
    right_panel.selected = 0;
    left_panel.scroll = 0;
    right_panel.scroll = 0;
    bar_mode = 0;
    bar_selected = 0;
    fm_return_pending = 0;
    dialog_type = DIALOG_NONE;
}

void fm_open(void) {
    fm_active = 1;
    active_panel = 0;
    bar_mode = 0;
    bar_selected = 0;
    dialog_type = DIALOG_NONE;
    strcpy(left_panel.path, "/mnt/sda1");
    strcpy(right_panel.path, "/mnt/sda1");
    scan_panel(&left_panel);
    scan_panel(&right_panel);
    status_msg[0] = '\0';
}

void fm_close(void) {
    fm_active = 0;
}

int fm_is_active(void) {
    return fm_active;
}

// Mark return to file manager pending
void fm_set_return_pending(void) {
    fm_return_pending = 1;
}

// Check and clear return pending
int fm_check_return(void) {
    if (fm_return_pending) {
        fm_return_pending = 0;
        fm_active = 1;
        return 1;
    }
    return 0;
}

static int compare_entries(const void *a, const void *b) {
    const FMEntry *ea = (const FMEntry *)a;
    const FMEntry *eb = (const FMEntry *)b;

    // ".." always first
    if (strcmp(ea->name, "..") == 0) return -1;
    if (strcmp(eb->name, "..") == 0) return 1;

    // Directories before files
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;

    // Alphabetical (case-insensitive)
    return strcasecmp(ea->name, eb->name);
}

static void scan_panel(Panel *panel) {
    panel->entry_count = 0;
    panel->selected = 0;
    panel->scroll = 0;

    DIR *dir = opendir(panel->path);
    if (!dir) {
        // Can't open - add just ".."
        strcpy(panel->entries[0].name, "..");
        panel->entries[0].is_dir = 1;
        panel->entries[0].size = 0;
        panel->entry_count = 1;
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && panel->entry_count < MAX_ENTRIES) {
        // Skip "." but keep ".."
        if (strcmp(ent->d_name, ".") == 0) continue;

        // Skip hidden files (except ..)
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;

        FMEntry *e = &panel->entries[panel->entry_count];
        strncpy(e->name, ent->d_name, MAX_NAME_LEN - 1);
        e->name[MAX_NAME_LEN - 1] = '\0';

        // Get file info
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", panel->path, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = st.st_size;
        } else {
            e->is_dir = 0;
            e->size = 0;
        }

        panel->entry_count++;
    }
    closedir(dir);

    // Add ".." if not at root
    if (strcmp(panel->path, "/") != 0 && strcmp(panel->path, "/mnt/sda1") != 0) {
        int has_parent = 0;
        for (int i = 0; i < panel->entry_count; i++) {
            if (strcmp(panel->entries[i].name, "..") == 0) {
                has_parent = 1;
                break;
            }
        }
        if (!has_parent && panel->entry_count < MAX_ENTRIES) {
            FMEntry *e = &panel->entries[panel->entry_count];
            strcpy(e->name, "..");
            e->is_dir = 1;
            e->size = 0;
            panel->entry_count++;
        }
    }

    // Sort entries
    if (panel->entry_count > 0) {
        qsort(panel->entries, panel->entry_count, sizeof(FMEntry), compare_entries);
    }
}

static void enter_directory(Panel *panel) {
    if (panel->entry_count == 0) return;

    FMEntry *e = &panel->entries[panel->selected];
    if (!e->is_dir) return;

    if (strcmp(e->name, "..") == 0) {
        go_up_directory(panel);
    } else {
        char newpath[512];
        snprintf(newpath, sizeof(newpath), "%s/%s", panel->path, e->name);
        strncpy(panel->path, newpath, MAX_PATH_LEN - 1);
        scan_panel(panel);
    }
}

static void go_up_directory(Panel *panel) {
    char *last_slash = strrchr(panel->path, '/');
    if (last_slash && last_slash != panel->path) {
        *last_slash = '\0';
    } else if (last_slash == panel->path) {
        panel->path[1] = '\0';  // Root "/"
    }
    scan_panel(panel);
}

static void show_status(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_timer = 90;  // 3 seconds at 30fps
}

// Get full path of selected file
static void get_selected_path(char *buf, int bufsize) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) {
        buf[0] = '\0';
        return;
    }
    FMEntry *e = &p->entries[p->selected];
    snprintf(buf, bufsize, "%s/%s", p->path, e->name);
}

// Get destination panel path
static const char* get_dest_path(void) {
    Panel *p = active_panel ? &left_panel : &right_panel;
    return p->path;
}

static void do_view(void) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) return;
    FMEntry *e = &p->entries[p->selected];

    if (e->is_dir) {
        show_status("Cannot view directory");
        return;
    }

    char filepath[512];
    get_selected_path(filepath, sizeof(filepath));

    if (is_image_file(e->name)) {
        fm_set_return_pending();
        fm_active = 0;
        iv_open(filepath);
    } else if (is_video_file(e->name)) {
        fm_set_return_pending();
        fm_active = 0;
        vp_open(filepath);
    } else if (is_music_file(e->name)) {
        fm_set_return_pending();
        fm_active = 0;
        mp_open(filepath);
    } else if (is_text_file(e->name)) {
        fm_set_return_pending();
        fm_active = 0;
        text_editor_open_viewer(filepath);
    } else {
        show_status("Unknown file type");
    }
}

static void do_edit(void) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) return;
    FMEntry *e = &p->entries[p->selected];

    if (e->is_dir) {
        show_status("Cannot edit directory");
        return;
    }

    char filepath[512];
    get_selected_path(filepath, sizeof(filepath));

    if (is_text_file(e->name)) {
        fm_set_return_pending();
        fm_active = 0;
        text_editor_open(filepath);
    } else {
        show_status("Not a text file");
    }
}

// v78: Show copy confirmation dialog
static void do_copy(void) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) return;
    FMEntry *e = &p->entries[p->selected];

    if (strcmp(e->name, "..") == 0) {
        show_status("Cannot copy ..");
        return;
    }

    get_selected_path(copy_source_path, sizeof(copy_source_path));
    strncpy(dialog_filename, e->name, sizeof(dialog_filename) - 1);
    copy_is_move = 0;
    dialog_selected = 0;
    dialog_type = DIALOG_COPY;
}

// v78: Show move confirmation dialog
static void do_move(void) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) return;
    FMEntry *e = &p->entries[p->selected];

    if (strcmp(e->name, "..") == 0) {
        show_status("Cannot move ..");
        return;
    }

    get_selected_path(copy_source_path, sizeof(copy_source_path));
    strncpy(dialog_filename, e->name, sizeof(dialog_filename) - 1);
    copy_is_move = 1;
    dialog_selected = 0;
    dialog_type = DIALOG_MOVE;
}

// v78: OSK callback for mkdir
static void mkdir_callback(int result, const char *input) {
    if (result == 1 && input && input[0] != '\0') {
        Panel *p = active_panel ? &right_panel : &left_panel;
        char newdir[512];
        snprintf(newdir, sizeof(newdir), "%s/%s", p->path, input);

        if (mkdir(newdir, 0755) == 0) {
            show_status("Directory created");
            scan_panel(p);
        } else {
            show_status("Failed to create dir");
        }
    }
    dialog_type = DIALOG_NONE;
}

// v78: Show mkdir dialog with OSK
static void do_mkdir(void) {
    dialog_type = DIALOG_MKDIR;
    osk_open("New directory name:", "", mkdir_callback);
}

// v78: Show delete confirmation dialog
static void do_delete(void) {
    Panel *p = active_panel ? &right_panel : &left_panel;
    if (p->entry_count == 0) return;
    FMEntry *e = &p->entries[p->selected];

    if (strcmp(e->name, "..") == 0) {
        show_status("Cannot delete ..");
        return;
    }

    if (e->is_dir) {
        show_status("Cannot delete directory");
        return;
    }

    get_selected_path(dialog_filepath, sizeof(dialog_filepath));
    strncpy(dialog_filename, e->name, sizeof(dialog_filename) - 1);
    dialog_selected = 1;  // Default to No
    dialog_type = DIALOG_DELETE;
}

// v78: Execute confirmed delete
static void execute_delete(void) {
    // Create recycled directory if needed
    mkdir("/mnt/sda1/recycled", 0755);

    // Truncate file to 5 bytes
    FILE *f = fopen(dialog_filepath, "wb");
    if (f) {
        fwrite("DEAD\n", 1, 5, f);
        fclose(f);
    }

    // Move to recycled
    char dest[512];
    snprintf(dest, sizeof(dest), "/mnt/sda1/recycled/%s", dialog_filename);

    if (rename(dialog_filepath, dest) == 0) {
        show_status("Moved to recycled");
        Panel *p = active_panel ? &right_panel : &left_panel;
        scan_panel(p);
    } else {
        show_status("Delete failed");
    }
}

// v78: Execute confirmed copy
static void execute_copy(void) {
    const char *dest_dir = get_dest_path();
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, dialog_filename);

    // Simple file copy
    FILE *src = fopen(copy_source_path, "rb");
    if (!src) {
        show_status("Cannot open source");
        return;
    }

    FILE *dst = fopen(dest_path, "wb");
    if (!dst) {
        fclose(src);
        show_status("Cannot create dest");
        return;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }

    fclose(src);
    fclose(dst);

    show_status("File copied");

    // Refresh destination panel
    Panel *dest_panel = active_panel ? &left_panel : &right_panel;
    scan_panel(dest_panel);
}

// v78: Execute confirmed move
static void execute_move(void) {
    const char *dest_dir = get_dest_path();
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, dialog_filename);

    if (rename(copy_source_path, dest_path) == 0) {
        show_status("File moved");
        // Refresh both panels
        scan_panel(&left_panel);
        scan_panel(&right_panel);
    } else {
        // Try copy + delete if rename fails (cross-filesystem)
        execute_copy();
        if (remove(copy_source_path) == 0) {
            show_status("File moved");
            Panel *src_panel = active_panel ? &right_panel : &left_panel;
            scan_panel(src_panel);
        }
    }
}

// Execute bar action
static void execute_bar_action(void) {
    switch (bar_selected) {
        case 0: do_view(); break;
        case 1: do_edit(); break;
        case 2: do_copy(); break;
        case 3: do_move(); break;
        case 4: do_mkdir(); break;
        case 5: do_delete(); break;
    }
}

int fm_handle_input(int up, int down, int left, int right, int a, int b, int x, int y, int l, int r, int start, int select) {
    (void)x; (void)y; (void)start; (void)select;

    Panel *panel = active_panel ? &right_panel : &left_panel;

    // v78: Handle OSK for mkdir
    if (osk_is_active()) {
        if (osk_handle_input(up, down, left, right, a, b, l, r)) {
            // OSK closed - callback already called
        }
        return 0;
    }

    // v78: Handle confirmation dialogs
    if (dialog_type != DIALOG_NONE && dialog_type != DIALOG_MKDIR) {
        if (left || right) {
            dialog_selected = dialog_selected ? 0 : 1;
        }
        if (a) {
            if (dialog_selected == 0) {  // Yes
                switch (dialog_type) {
                    case DIALOG_DELETE:
                        execute_delete();
                        break;
                    case DIALOG_COPY:
                        execute_copy();
                        break;
                    case DIALOG_MOVE:
                        execute_move();
                        break;
                    default:
                        break;
                }
            }
            dialog_type = DIALOG_NONE;
        }
        if (b) {
            dialog_type = DIALOG_NONE;
        }
        return 0;
    }

    // B - close file manager or exit bar mode
    if (b) {
        if (bar_mode) {
            bar_mode = 0;
            return 0;
        }
        return 1;
    }

    // Bar mode navigation
    if (bar_mode) {
        if (left && bar_selected > 0) {
            bar_selected--;
        }
        if (right && bar_selected < 5) {
            bar_selected++;
        }
        if (a) {
            execute_bar_action();
            bar_mode = 0;
        }
        // L/R in bar mode - exit bar and switch panel
        if (l) {
            bar_mode = 0;
            active_panel = 0;
        }
        if (r) {
            bar_mode = 0;
            active_panel = 1;
        }
        if (up) {
            bar_mode = 0;  // Go back to file panel
        }
        return 0;
    }

    // L/R - switch panels OR enter bar mode
    if (l) {
        if (active_panel == 0) {
            // Already on left panel, go to bar
            bar_mode = 1;
        } else {
            active_panel = 0;
        }
    }
    if (r) {
        if (active_panel == 1) {
            // Already on right panel, go to bar
            bar_mode = 1;
        } else {
            active_panel = 1;
        }
    }

    // Navigation - Up
    if (up && panel->entry_count > 0) {
        panel->selected--;
        if (panel->selected < 0) {
            panel->selected = panel->entry_count - 1;
            // Fix scroll when wrapping to bottom
            if (panel->entry_count > VISIBLE_LINES) {
                panel->scroll = panel->entry_count - VISIBLE_LINES;
            } else {
                panel->scroll = 0;
            }
        } else if (panel->selected < panel->scroll) {
            panel->scroll = panel->selected;
        }
    }

    // Navigation - Down
    if (down && panel->entry_count > 0) {
        panel->selected++;
        if (panel->selected >= panel->entry_count) {
            panel->selected = 0;
            panel->scroll = 0;  // Fix scroll when wrapping to top
        } else if (panel->selected >= panel->scroll + VISIBLE_LINES) {
            panel->scroll = panel->selected - VISIBLE_LINES + 1;
        }
    }

    // Left/Right - page up/down
    if (left && panel->entry_count > 0) {
        panel->selected -= VISIBLE_LINES;
        if (panel->selected < 0) panel->selected = 0;
        if (panel->selected < panel->scroll) {
            panel->scroll = panel->selected;
        }
    }

    if (right && panel->entry_count > 0) {
        panel->selected += VISIBLE_LINES;
        if (panel->selected >= panel->entry_count) {
            panel->selected = panel->entry_count - 1;
        }
        if (panel->selected >= panel->scroll + VISIBLE_LINES) {
            panel->scroll = panel->selected - VISIBLE_LINES + 1;
        }
    }

    // A - enter directory or view file
    if (a && panel->entry_count > 0) {
        FMEntry *e = &panel->entries[panel->selected];
        if (e->is_dir) {
            enter_directory(panel);
        } else {
            // Auto-view based on file type
            do_view();
        }
    }

    return 0;
}

static void draw_panel(uint16_t *fb, Panel *panel, int x, int is_active) {
    // Panel background
    for (int py = PANEL_Y; py < PANEL_Y + PANEL_HEIGHT; py++) {
        for (int px = x; px < x + PANEL_WIDTH; px++) {
            fb[py * SCREEN_WIDTH + px] = COL_PANEL_BG;
        }
    }

    // Border - only highlight if active and not in bar mode
    uint16_t border_col = (is_active && !bar_mode) ? COL_HEADER_BG : COL_PANEL_BORDER;
    for (int px = x; px < x + PANEL_WIDTH; px++) {
        fb[PANEL_Y * SCREEN_WIDTH + px] = border_col;
        fb[(PANEL_Y + PANEL_HEIGHT - 1) * SCREEN_WIDTH + px] = border_col;
    }
    for (int py = PANEL_Y; py < PANEL_Y + PANEL_HEIGHT; py++) {
        fb[py * SCREEN_WIDTH + x] = border_col;
        fb[py * SCREEN_WIDTH + x + PANEL_WIDTH - 1] = border_col;
    }

    // Header with path
    for (int py = PANEL_Y + 1; py < PANEL_Y + FM_HEADER_HEIGHT; py++) {
        for (int px = x + 1; px < x + PANEL_WIDTH - 1; px++) {
            fb[py * SCREEN_WIDTH + px] = (is_active && !bar_mode) ? COL_HEADER_BG : COL_PANEL_BORDER;
        }
    }

    // Truncate path to fit
    char disp_path[32];
    int path_len = strlen(panel->path);
    if (path_len > 24) {
        strcpy(disp_path, "...");
        strcat(disp_path, panel->path + path_len - 21);
    } else {
        strcpy(disp_path, panel->path);
    }
    builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, x + 4, PANEL_Y + 3, disp_path, COL_HEADER_TEXT);

    // File list
    int list_y = PANEL_Y + FM_HEADER_HEIGHT + 2;
    for (int i = 0; i < VISIBLE_LINES && (i + panel->scroll) < panel->entry_count; i++) {
        int idx = i + panel->scroll;
        FMEntry *e = &panel->entries[idx];

        int line_y = list_y + i * LINE_HEIGHT;

        // Selection highlight
        if (idx == panel->selected) {
            uint16_t sel_col = (is_active && !bar_mode) ? COL_SELECTED_BG : COL_INACTIVE_SEL;
            for (int py = line_y; py < line_y + LINE_HEIGHT && py < PANEL_Y + PANEL_HEIGHT - 2; py++) {
                for (int px = x + 2; px < x + PANEL_WIDTH - 2; px++) {
                    fb[py * SCREEN_WIDTH + px] = sel_col;
                }
            }
        }

        // File/dir name
        uint16_t text_col = e->is_dir ? COL_DIR_TEXT : COL_FILE_TEXT;

        // Truncate name to fit panel
        char disp_name[24];
        if (strlen(e->name) > 22) {
            strncpy(disp_name, e->name, 19);
            disp_name[19] = '\0';
            strcat(disp_name, "...");
        } else {
            strcpy(disp_name, e->name);
        }

        // Add directory indicator
        if (e->is_dir && strcmp(e->name, "..") != 0) {
            char dir_name[26];
            snprintf(dir_name, sizeof(dir_name), "[%s]", disp_name);
            builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, x + 4, line_y + 4, dir_name, text_col);
        } else {
            builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, x + 4, line_y + 4, disp_name, text_col);
        }
    }

    // Scroll indicators
    if (panel->scroll > 0) {
        builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, x + PANEL_WIDTH - 12, PANEL_Y + FM_HEADER_HEIGHT + 2, "^", COL_FILE_TEXT);
    }
    if (panel->scroll + VISIBLE_LINES < panel->entry_count) {
        builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, x + PANEL_WIDTH - 12, PANEL_Y + PANEL_HEIGHT - 14, "v", COL_FILE_TEXT);
    }
}

static void draw_bottom_bar(uint16_t *fb) {
    // Bar background
    for (int py = BAR_Y; py < BAR_Y + BAR_HEIGHT; py++) {
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            fb[py * SCREEN_WIDTH + px] = COL_BAR_BG;
        }
    }

    // Function buttons
    const char *buttons[] = {"View", "Edit", "Copy", "Move", "Mkdir", "Del"};
    int btn_width = 52;
    int btn_x = 2;

    for (int i = 0; i < 6; i++) {
        // Button background - highlight if selected in bar mode
        uint16_t btn_bg = (bar_mode && bar_selected == i) ? COL_SELECTED_BG : 0x0000;
        for (int py = BAR_Y + 2; py < BAR_Y + 16; py++) {
            for (int px = btn_x; px < btn_x + btn_width - 2; px++) {
                fb[py * SCREEN_WIDTH + px] = btn_bg;
            }
        }

        // Button text
        int tw = builtin_measure_text(buttons[i]);
        builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, btn_x + (btn_width - tw) / 2, BAR_Y + 5, buttons[i], COL_BAR_TEXT);

        btn_x += btn_width;
    }

    // Status message or help text
    if (status_timer > 0) {
        builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, 4, BAR_Y + 22, status_msg, 0xFFE0);
        status_timer--;
    } else {
        // Legend color #eee
        const char *help = bar_mode ? "Left/Right:Select A:Execute B:Cancel" : "L/R:Panel/Bar A:Open B:Exit";
        builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, 4, BAR_Y + 22, help, COL_LEGEND);
    }
}

// v78: Draw confirmation dialog
static void draw_dialog(uint16_t *fb) {
    if (dialog_type == DIALOG_NONE || dialog_type == DIALOG_MKDIR) return;

    // Dialog box dimensions
    int dw = 280, dh = 80;
    int dx = (SCREEN_WIDTH - dw) / 2;
    int dy = (SCREEN_HEIGHT - dh) / 2;

    // Background
    for (int py = dy; py < dy + dh; py++) {
        for (int px = dx; px < dx + dw; px++) {
            fb[py * SCREEN_WIDTH + px] = COL_DIALOG_BG;
        }
    }

    // Border
    for (int px = dx; px < dx + dw; px++) {
        fb[dy * SCREEN_WIDTH + px] = COL_DIALOG_BORDER;
        fb[(dy + dh - 1) * SCREEN_WIDTH + px] = COL_DIALOG_BORDER;
    }
    for (int py = dy; py < dy + dh; py++) {
        fb[py * SCREEN_WIDTH + dx] = COL_DIALOG_BORDER;
        fb[py * SCREEN_WIDTH + dx + dw - 1] = COL_DIALOG_BORDER;
    }

    // Title
    const char *title = "";
    switch (dialog_type) {
        case DIALOG_DELETE: title = "Delete file?"; break;
        case DIALOG_COPY: title = "Copy file?"; break;
        case DIALOG_MOVE: title = "Move file?"; break;
        default: break;
    }
    int tw = builtin_measure_text(title);
    builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, dx + (dw - tw) / 2, dy + 8, title, 0xFFFF);

    // Filename (truncated)
    char disp_name[36];
    if (strlen(dialog_filename) > 34) {
        strncpy(disp_name, dialog_filename, 31);
        disp_name[31] = '\0';
        strcat(disp_name, "...");
    } else {
        strcpy(disp_name, dialog_filename);
    }
    tw = builtin_measure_text(disp_name);
    builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, dx + (dw - tw) / 2, dy + 26, disp_name, COL_LEGEND);

    // Yes/No buttons
    int btn_y = dy + 50;
    int yes_x = dx + dw / 4 - 20;
    int no_x = dx + 3 * dw / 4 - 20;

    // Yes button
    uint16_t yes_bg = (dialog_selected == 0) ? COL_SELECTED_BG : COL_PANEL_BORDER;
    for (int py = btn_y; py < btn_y + 18; py++) {
        for (int px = yes_x; px < yes_x + 40; px++) {
            fb[py * SCREEN_WIDTH + px] = yes_bg;
        }
    }
    builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, yes_x + 12, btn_y + 5, "Yes", 0xFFFF);

    // No button
    uint16_t no_bg = (dialog_selected == 1) ? COL_SELECTED_BG : COL_PANEL_BORDER;
    for (int py = btn_y; py < btn_y + 18; py++) {
        for (int px = no_x; px < no_x + 40; px++) {
            fb[py * SCREEN_WIDTH + px] = no_bg;
        }
    }
    builtin_draw_text(fb, SCREEN_WIDTH, SCREEN_HEIGHT, no_x + 14, btn_y + 5, "No", 0xFFFF);
}

void fm_render(uint16_t *fb) {
    if (!fm_active) return;

    // Clear with dark background
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        fb[i] = 0x0000;
    }

    // Draw panels
    draw_panel(fb, &left_panel, PANEL_LEFT_X, active_panel == 0);
    draw_panel(fb, &right_panel, PANEL_RIGHT_X, active_panel == 1);

    // Draw bottom bar
    draw_bottom_bar(fb);

    // v78: Draw dialog on top if active
    draw_dialog(fb);

    // v78: Draw OSK on top if active
    if (osk_is_active()) {
        osk_render(fb);
    }
}

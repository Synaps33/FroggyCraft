#ifndef DISPLAY_OPTS_H
#define DISPLAY_OPTS_H

#include <stdint.h>

// v24: Display filtering options for platforms

#define MAX_DISPLAY_PATTERNS 4
#define MAX_PATTERN_LEN 16

// Display mode: what to show in file browser
typedef enum {
    DISPLAY_FILES_AND_DIRS = 0,  // Show files + directories
    DISPLAY_FILES_ONLY = 1       // Show only files
} DisplayMode;

// Display options structure
typedef struct {
    DisplayMode mode;                           // Files+dirs or files only
    int pattern_count;                          // 0-4 active patterns
    char patterns[MAX_DISPLAY_PATTERNS][MAX_PATTERN_LEN];  // e.g., "*.zip", "*.nes"
    int disk1_only;                             // Only show (Disk 1), (Disc 1), etc.
    int modified;                               // Has user changed from defaults?
} DisplayOptions;

// Initialize display options system
void display_opts_init(void);

// Load display options for a core (creates defaults if not found)
void display_opts_load(const char *core_name);

// Save display options for current core (only if modified)
void display_opts_save(void);

// Get current display options
const DisplayOptions* display_opts_get(void);

// Show display options menu (called when SELECT pressed in platform)
void display_opts_show_menu(const char *core_name);

// Handle display options menu input
// Returns: 0 = still in menu, 1 = closed menu
int display_opts_handle_input(int up, int down, int left, int right, int a, int b);

// Check if display options menu is active
int display_opts_is_active(void);

// v31: Check if caller should rescan directory after settings save (clears flag)
int display_opts_needs_rescan(void);

// Render display options menu
void display_opts_render(uint16_t *framebuffer);

// Filtering functions for file browser
int display_opts_should_show_dirs(void);
int display_opts_matches_pattern(const char *filename);
int display_opts_is_disk1(const char *filename);

// Get currently loaded core name
const char* display_opts_get_core_name(void);

#endif // DISPLAY_OPTS_H

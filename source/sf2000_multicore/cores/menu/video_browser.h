// video_browser.h - Generic file browser for multiple sections
// v46: Theme-styled with semi-transparent rounded corners, divider line
// Supports: VIDEOS, IMAGES, MUSIC, TEXT sections with different filters

#ifndef VIDEO_BROWSER_H
#define VIDEO_BROWSER_H

#include <stdint.h>

// Configuration
#define VB_MAX_FILES 1024
#define VB_MAX_PATH 256
#define VB_MAX_NAME 128
#define VB_VISIBLE_ITEMS 15

// Filter modes for different sections
typedef enum {
    VB_FILTER_VIDEOS = 0,  // .avi
    VB_FILTER_IMAGES,      // .png, .jpg, .jpeg, .gif, .bmp, .webp
    VB_FILTER_MUSIC,       // .mp3, .wav, .adp, .adpcm
    VB_FILTER_TEXT,        // .txt
    VB_FILTER_COUNT
} VBFilterMode;

// Initialize file browser
void vb_init(void);

// Check if file browser is active
int vb_is_active(void);

// Configure and open browser for a specific section
// start_path: initial directory (e.g., "/mnt/sda1/VIDEOS")
// filter_mode: which file types to show
void vb_open_with_config(const char *start_path, VBFilterMode filter_mode);

// Legacy open (defaults to VIDEOS)
void vb_open(void);

// Close browser
void vb_close(void);

// Handle input (returns 1 if input was consumed)
// Parameters are button release events (was pressed, now released)
int vb_handle_input(int up, int down, int left, int right, int a, int b);

// Draw the browser window
void vb_draw(uint16_t *framebuffer);

// Get selected file path (empty string if none selected)
const char* vb_get_selected_path(void);

// Check if a file was selected (and clear the flag)
int vb_file_was_selected(void);

// Check if browser wants to return to header (up at top of list)
int vb_wants_go_to_header(void);

// Set browser focus state (controls whether selection is highlighted)
void vb_set_focused(int focused);

// Get current filter mode
VBFilterMode vb_get_filter_mode(void);

#endif // VIDEO_BROWSER_H

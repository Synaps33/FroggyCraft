// v59: Text editor/viewer for theme.ini and text files
// Supports both editing mode (for theme.ini) and viewer mode (for TEXT section)
#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

#include <stdint.h>

// Maximum sizes
#define EDITOR_MAX_LINES 64
#define EDITOR_MAX_LINE_LEN 128
#define EDITOR_MAX_FILE_SIZE (EDITOR_MAX_LINES * EDITOR_MAX_LINE_LEN)
#define EDITOR_UNDO_LEVELS 10

// v59: Text viewer mode - max lines increased for wrapped text
#define VIEWER_MAX_LINES 512
#define VIEWER_MAX_LINE_LEN 128

// Initialize text editor
void text_editor_init(void);

// Open text editor with a file (edit mode for theme.ini)
// Returns 1 on success, 0 on failure
int text_editor_open(const char *filepath);

// v59: Open text viewer (read-only mode for TEXT section files)
// Returns 1 on success, 0 on failure
int text_editor_open_viewer(const char *filepath);

// Close text editor/viewer (discards changes)
void text_editor_close(void);

// Check if text editor/viewer is active
int text_editor_is_active(void);

// v59: Check if in viewer mode (vs edit mode)
int text_editor_is_viewer_mode(void);

// Handle input
// v34: Added l, r parameters for shoulder buttons (used for cursor movement in OSK)
// Returns: 0 = still editing/viewing, 1 = closed (saved or cancelled)
int text_editor_handle_input(int up, int down, int left, int right, int a, int b, int x, int y, int l, int r);

// Render text editor/viewer
void text_editor_render(uint16_t *framebuffer);

// Check if changes were saved
int text_editor_was_saved(void);

#endif // TEXT_EDITOR_H

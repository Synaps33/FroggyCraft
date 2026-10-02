// v25: Full on-screen keyboard like Macintosh, with FrogUI logo for Cmd key
// v34: Added editor mode with arrow keys, wider layout, bottom position
#ifndef OSK_H
#define OSK_H

#include <stdint.h>

// Maximum length of input string
#define OSK_MAX_INPUT 128  // v34: Increased from 32 for editor

// Callback when input is confirmed or cancelled
// result: 0 = cancelled, 1 = confirmed, 2 = arrow key (up/down/left/right)
// input: the edited string (only valid if result = 1)
// For result=2: input[0] = 'U'/'D'/'L'/'R' for direction
typedef void (*osk_callback_t)(int result, const char *input);

// Initialize OSK system
void osk_init(void);

// Open OSK with initial value
// title: displayed at top of keyboard
// initial: initial value to edit
// callback: called when done
void osk_open(const char *title, const char *initial, osk_callback_t callback);

// v34: Open OSK in editor mode (keyboard at bottom, no title bar, arrow keys enabled)
// cursor_pos: initial cursor position in the string
void osk_open_editor(const char *initial, int cursor_pos, osk_callback_t callback);

// Close OSK (cancels)
void osk_close(void);

// Check if OSK is active
int osk_is_active(void);

// v34: Check if OSK is in editor mode
int osk_is_editor_mode(void);

// Handle OSK input - v34: added l and r shoulder buttons
// Returns: 0 = still editing, 1 = closed
int osk_handle_input(int up, int down, int left, int right, int a, int b, int l, int r);

// Render OSK to framebuffer
void osk_render(uint16_t *framebuffer);

// v34: Get current cursor position
int osk_get_cursor_pos(void);

// v34: Get current input buffer
const char* osk_get_input(void);

// v34: Set cursor position
void osk_set_cursor_pos(int pos);

#endif // OSK_H

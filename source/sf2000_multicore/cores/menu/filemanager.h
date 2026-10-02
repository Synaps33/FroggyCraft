/*
 * FrogUI File Manager
 * v77: Total Commander style dual-panel file manager
 */

#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <stdint.h>

// Initialize file manager
void fm_init(void);

// Open file manager
void fm_open(void);

// Close file manager
void fm_close(void);

// Check if file manager is active
int fm_is_active(void);

// v77: Mark return pending (called before opening viewer)
void fm_set_return_pending(void);

// v77: Check and clear return pending
int fm_check_return(void);

// Handle input - returns 1 if should close
int fm_handle_input(int up, int down, int left, int right, int a, int b, int x, int y, int l, int r, int start, int select);

// Render file manager
void fm_render(uint16_t *framebuffer);

#endif // FILEMANAGER_H

/*
 * FrogUI Image Viewer
 * v56: Full-featured image viewer for comics and photos
 * Supports: PNG, JPG, BMP, GIF, WebP
 * Features: Zoom, Pan, Previous/Next navigation
 */

#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include <stdint.h>

// Initialize image viewer (call once at startup)
void iv_init(void);

// Open image file and enter viewer mode
// Returns 1 on success, 0 on failure
int iv_open(const char *path);

// Close viewer and free resources
void iv_close(void);

// Check if viewer is active
int iv_is_active(void);

// Handle input and update state
// Returns 1 if viewer should remain active, 0 if closed
int iv_handle_input(int up, int down, int left, int right,
                    int a, int b, int x, int y, int l, int r);

// v70: Update loading state - call every frame to continue chunked loading
// Returns 1 if still loading, 0 if done or idle
int iv_update(void);

// Render current view to framebuffer
void iv_render(uint16_t *framebuffer);

// Get current image info
int iv_get_image_width(void);
int iv_get_image_height(void);
int iv_get_zoom_percent(void);

#endif // IMAGE_VIEWER_H

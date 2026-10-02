/*
 * avi_bg.h - Animated AVI background support for FrogUI
 *
 * Plays XVID-encoded AVI files as animated backgrounds at 15 fps.
 * Video-only (no audio), loops automatically.
 *
 * Designed to coexist with static PNG backgrounds:
 * - Main menu may have animated background (background.avi)
 * - Platform screens may have static PNG backgrounds
 * - Switching between them is seamless
 */

#ifndef AVI_BG_H
#define AVI_BG_H

#include <stdint.h>
#include <stdbool.h>

/* Screen dimensions */
#define AVI_SCREEN_WIDTH  320
#define AVI_SCREEN_HEIGHT 240

/* Target frame rate for animation */
#define AVI_TARGET_FPS 15

/* Frame timing (ms per frame at 15fps = 66.67ms) */
#define AVI_FRAME_TIME_MS 67

/* Initialize animated background system (call once at startup) */
void avi_bg_init(void);

/* Shutdown animated background system (call at cleanup) */
void avi_bg_shutdown(void);

/* Load an AVI file for animated background
 * path: full path to background.avi
 * Returns 1 on success, 0 on failure
 * Note: Only one AVI can be loaded at a time. Loading a new one closes the previous. */
int avi_bg_load(const char *path);

/* Close current animated background without shutting down system
 * Use this when switching to a static PNG background */
void avi_bg_close(void);

/* Check if animated background is currently loaded and active */
bool avi_bg_is_active(void);

/* Get the current frame as RGB565 data (320x240)
 * Returns pointer to internal framebuffer, or NULL if not active
 * The buffer content is only valid until next avi_bg_advance_frame() call */
uint16_t* avi_bg_get_frame(void);

/* Advance to next frame
 * Should be called at 15fps rate (every ~67ms)
 * Handles looping automatically when reaching end
 * Returns 1 if frame was advanced, 0 if not active or error */
int avi_bg_advance_frame(void);

/* Reset animation to beginning (frame 0) */
void avi_bg_reset(void);

/* Pause/resume animation without unloading */
void avi_bg_pause(void);
void avi_bg_resume(void);
bool avi_bg_is_paused(void);

/* Get video info */
int avi_bg_get_width(void);
int avi_bg_get_height(void);
int avi_bg_get_total_frames(void);
int avi_bg_get_current_frame(void);

#endif /* AVI_BG_H */

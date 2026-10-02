/*
 * FrogPMP - Video player for FrogUI
 * by Grzegorz Korycki
 */

#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <stdint.h>
#include <stddef.h>

// Audio callback type (from libretro)
typedef size_t (*vp_audio_batch_cb_t)(const int16_t *data, size_t frames);

// Initialize player (call once at startup)
void vp_init(void);

// Set audio callback (must be called before vp_open)
void vp_set_audio_callback(vp_audio_batch_cb_t cb);

// Open a video file and start playing
// Returns 1 on success, 0 on failure
int vp_open(const char *path);

// Close the player and free resources
void vp_close(void);

// Check if player is active
int vp_is_active(void);

// Handle input - returns 1 if player should close (user pressed B to exit)
// All parameters are current button states (not edges)
int vp_handle_input(int up, int down, int left, int right, int a, int b, int start, int l, int r);

// Advance to next frame and render to framebuffer
// Also handles audio playback
void vp_render(uint16_t *framebuffer);

// Get playback state
int vp_is_paused(void);
void vp_toggle_pause(void);

// Get video info
int vp_get_total_frames(void);
int vp_get_current_frame(void);
int vp_get_fps(void);

// Play mode: check if next video requested (0=none, 1=next A-Z, 2=shuffle)
int vp_get_next_video_request(void);
void vp_clear_next_video_request(void);
const char *vp_get_current_dir(void);
const char *vp_get_current_path(void);

#endif // VIDEO_PLAYER_H

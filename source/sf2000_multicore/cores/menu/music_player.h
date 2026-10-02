/*
 * FrogMP - Music player for FrogUI
 * Supports: MP3, WAV (PCM/ADPCM), ADP/ADPCM raw files
 * Uses libmad for MP3 decoding (GPL v2)
 * by Grzegorz Korycki
 */

#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <stdint.h>
#include <stddef.h>

// Audio callback type (from libretro)
typedef size_t (*mp_audio_batch_cb_t)(const int16_t *data, size_t frames);

// Initialize player (call once at startup)
void mp_init(void);

// Set audio callback (must be called before mp_open)
void mp_set_audio_callback(mp_audio_batch_cb_t cb);

// Open a music file and start playing
// Returns 1 on success, 0 on failure
int mp_open(const char *path);

// Close the player and free resources
void mp_close(void);

// Check if player is active
int mp_is_active(void);

// Handle input - returns 1 if player should close
// All parameters are current button states (not edges)
int mp_handle_input(int up, int down, int left, int right, int a, int b, int start, int l, int r);

// Render player interface and handle audio output
void mp_render(uint16_t *framebuffer);

// v63: Update audio only (for background music mode)
// Call this every frame when music is playing in background
void mp_update_audio(void);

// v71: Reset audio timing after long pause (e.g. during image decode)
// Prevents audio system from trying to catch up with huge chunks
void mp_reset_audio_timing(void);

// v63: Background mode - music plays but UI not shown
void mp_set_background_mode(int enabled);
int mp_is_background_mode(void);

// Playback controls
int mp_is_paused(void);
void mp_toggle_pause(void);

// Get audio info
int mp_get_duration_seconds(void);
int mp_get_position_seconds(void);
int mp_get_sample_rate(void);
int mp_get_channels(void);
const char *mp_get_format_name(void);

// Get file info
const char *mp_get_current_path(void);
const char *mp_get_current_filename(void);

// Play mode: check if next track requested (0=none, 1=next A-Z, 2=shuffle)
int mp_get_next_track_request(void);
void mp_clear_next_track_request(void);
const char *mp_get_current_dir(void);

#endif // MUSIC_PLAYER_H

/*
 * FrogMP - Music player for FrogUI
 * Supports: MP3, WAV (PCM/ADPCM), ADP/ADPCM raw files
 * Uses libmad for MP3 decoding (GPL v2)
 * by Grzegorz Korycki
 */

#include "music_player.h"
#include "libmad/libmad.h"
#include "font.h"
#include "theme.h"
#include "render.h"
#include "gfx_theme.h"  // v64: For background animation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Directory scanning for playlist
#ifdef SF2000
#include "../../stockfw.h"
#include "../../dirent.h"
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0x4000) != 0)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Screen dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Audio formats
#define MP_FMT_UNKNOWN  0
#define MP_FMT_MP3      1
#define MP_FMT_WAV_PCM  2
#define MP_FMT_WAV_ADPCM 3
#define MP_FMT_RAW_ADPCM 4

// Audio buffer sizes - large buffers needed for smooth playback
#define MP_AUDIO_RING_SIZE (176 * 1024)
#define MP_MAX_AUDIO_BUFFER 4096
#define MP_MP3_INPUT_BUF_SIZE 16384
#define MP_MP3_DECODE_BUF_SIZE 4608
#define MP_ADPCM_DECODE_BUF_SIZE 8192

// Play modes
#define MP_PLAY_MODE_REPEAT   0
#define MP_PLAY_MODE_ONCE     1
#define MP_PLAY_MODE_AZ       2
#define MP_PLAY_MODE_SHUFFLE  3
#define MP_PLAY_MODE_COUNT    4

// Menu/UI modes
#define MP_UI_PLAYING     0
#define MP_UI_CREDITS     1

// Max path length
#define MP_MAX_PATH 512
#define MP_MAX_FILENAME 128

// Playlist
#define MP_MAX_PLAYLIST 256

// Window dimensions (same as video_browser)
#define MP_WIN_X 20
#define MP_WIN_Y 28
#define MP_WIN_W 280
#define MP_WIN_H 200
#define MP_WIN_RADIUS 10

// State
static int mp_active = 0;
static int mp_paused = 0;
static FILE *mp_file = NULL;
static char mp_current_path[MP_MAX_PATH];
static char mp_current_dir[MP_MAX_PATH];
static char mp_current_filename[MP_MAX_FILENAME];

// Audio format info
static int mp_format = MP_FMT_UNKNOWN;
static int mp_sample_rate = 44100;
static int mp_channels = 2;
static int mp_bits_per_sample = 16;
static uint32_t mp_data_offset = 0;
static uint32_t mp_data_size = 0;
static uint32_t mp_file_size = 0;

// ADPCM state
static int mp_adpcm_block_align = 0;
static int mp_adpcm_samples_per_block = 0;
static int mp_adpcm_sample1[2] = {0, 0};
static int mp_adpcm_sample2[2] = {0, 0};
static int mp_adpcm_delta[2] = {0, 0};
static int mp_adpcm_coef_idx[2] = {0, 0};

// ADPCM tables
static const int mp_adpcm_adapt_table[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};
static const int mp_adpcm_coef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
static const int mp_adpcm_coef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

// Decode buffers
static int16_t mp_adpcm_decode_buf[MP_ADPCM_DECODE_BUF_SIZE];
static uint8_t mp_adpcm_read_buf[8192];

// MP3 decoder state
static void *mp_mp3_handle = NULL;
static int mp_mp3_initialized = 0;
static uint8_t mp_mp3_input_buf[MP_MP3_INPUT_BUF_SIZE];
static int mp_mp3_input_len = 0;
static int mp_mp3_input_remaining = 0;
static int16_t mp_mp3_decode_buf[MP_MP3_DECODE_BUF_SIZE];
static int mp_mp3_detected_samplerate = 0;
static int mp_mp3_detected_channels = 0;

// Audio ring buffer
static uint8_t *mp_audio_ring = NULL;
static int mp_aring_read = 0;
static int mp_aring_write = 0;
static int mp_aring_count = 0;

// v61: Audio mute after seek to avoid crackle
static int mp_audio_mute_samples = 0;
#define MP_AUDIO_MUTE_AFTER_SEEK 4096  // ~93ms at 44.1kHz - covers 2+ batches

// Audio output buffer
static int16_t mp_audio_out_buffer[MP_MAX_AUDIO_BUFFER * 2];

// Audio callback
static mp_audio_batch_cb_t mp_audio_batch_cb = NULL;

// Time-based audio output (for variable FPS)
static uint32_t mp_last_output_time = 0;
static uint32_t mp_audio_acc_us = 0;  // Accumulated microseconds for sub-ms precision

// Playback position
static uint32_t mp_file_pos = 0;
static uint64_t mp_samples_played = 0;
static uint64_t mp_total_samples = 0;  // Total samples in file (for duration)

// MP3 bitrate tracking (for seek/duration)
static int mp_mp3_bitrate = 128;  // kbps, default 128
static int mp_mp3_vbr = 0;        // 1 if VBR detected
static int mp_mp3_bitrate_from_header = 0;  // 1 if bitrate was detected from header parsing

// EOF handling
static int mp_eof_pending = 0;    // Prevents repeated EOF handling

// v63: Background mode - music plays but UI not shown
static int mp_background_mode = 0;

// Playlist
static char mp_playlist[MP_MAX_PLAYLIST][MP_MAX_FILENAME];
static int mp_playlist_count = 0;
static int mp_playlist_current = -1;
static int mp_play_mode = MP_PLAY_MODE_REPEAT;
static int mp_next_track_request = 0;

// UI mode
static int mp_ui_mode = MP_UI_PLAYING;

// v57: Title scrolling
// v59: Added end pause and proper font measurement
static int mp_title_scroll_offset = 0;
static int mp_title_scroll_delay = 0;
static int mp_title_at_end = 0;       // v59: Flag for pause at end of scroll
static int mp_title_scroll_timer = 0; // v59: Module-level timer
static int mp_title_end_timer = 0;    // v59: Module-level end timer
#define MP_TITLE_SCROLL_DELAY 90     // 1.5 sec at 60fps before scrolling starts
#define MP_TITLE_SCROLL_END_DELAY 60 // 1 sec pause at end before resetting
#define MP_TITLE_SCROLL_SPEED 6      // Frames between scroll steps
#define MP_TITLE_MAX_DISPLAY_WIDTH 256  // Max pixels for title display area

// Simple random
static uint32_t mp_rand_state = 12345;

// Resampling accumulator for fractional sample rates
static uint32_t mp_resample_acc = 0;

// Forward declarations
static void mp_reset_state(void);
static int mp_detect_format(void);
static int mp_parse_wav_header(void);
static void mp_read_and_decode_audio(void);
static void mp_output_audio(void);
static void mp_scan_playlist(void);

// ============================================================================
// Utility functions
// ============================================================================

static uint16_t mp_read_u16_le(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

static uint32_t mp_read_u32_le(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static int mp_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int mp_str_ends_with_ci(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return mp_strcasecmp(str + str_len - suf_len, suffix) == 0;
}

static uint32_t mp_rand(void) {
    mp_rand_state = mp_rand_state * 1103515245 + 12345;
    return (mp_rand_state >> 16) & 0x7FFF;
}

// MPEG-1 Layer III bitrate table (kbps), index 0 = free, 15 = bad
static const int mp3_bitrate_table[16] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
};

// Parse MP3 frame header to extract bitrate, sample rate, and channels
// Returns bitrate in kbps, or 0 on failure
static int mp_parse_mp3_frame_header(const uint8_t *data, int *out_samplerate, int *out_channels) {
    // Check sync (11 bits = 0x7FF, stored as 0xFF 0xE0+)
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) return 0;

    // MPEG version: bits 4-3 of byte 1 (00=2.5, 01=reserved, 10=2, 11=1)
    int version_bits = (data[1] >> 3) & 0x03;
    if (version_bits == 1) return 0;  // Reserved
    int is_mpeg1 = (version_bits == 3);

    // Layer: bits 2-1 of byte 1 (00=reserved, 01=III, 10=II, 11=I)
    int layer_bits = (data[1] >> 1) & 0x03;
    if (layer_bits == 0) return 0;  // Reserved

    // Bitrate index: bits 7-4 of byte 2
    int br_index = (data[2] >> 4) & 0x0F;
    if (br_index == 0 || br_index == 15) return 0;  // Free or bad

    // Sample rate index: bits 3-2 of byte 2
    int sr_index = (data[2] >> 2) & 0x03;
    if (sr_index == 3) return 0;  // Reserved

    // Get bitrate (only MPEG-1 Layer III supported for now)
    int bitrate = 0;
    if (is_mpeg1 && layer_bits == 1) {  // MPEG-1 Layer III
        bitrate = mp3_bitrate_table[br_index];
    } else if (!is_mpeg1 && layer_bits == 1) {  // MPEG-2/2.5 Layer III
        // Different table for MPEG-2
        static const int mp3_bitrate_table_v2[16] = {
            0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
        };
        bitrate = mp3_bitrate_table_v2[br_index];
    } else {
        // Other layers - use rough estimate
        bitrate = mp3_bitrate_table[br_index];
    }

    // Get sample rate
    if (out_samplerate) {
        static const int sr_table[3][3] = {
            {44100, 48000, 32000},  // MPEG-1
            {22050, 24000, 16000},  // MPEG-2
            {11025, 12000,  8000}   // MPEG-2.5
        };
        int sr_ver = is_mpeg1 ? 0 : (version_bits == 0 ? 2 : 1);
        *out_samplerate = sr_table[sr_ver][sr_index];
    }

    // Get channel mode: byte 3, bits 7-6
    // 00 = Stereo, 01 = Joint stereo, 10 = Dual channel, 11 = Single channel (mono)
    if (out_channels) {
        int mode_bits = (data[3] >> 6) & 0x03;
        *out_channels = (mode_bits == 3) ? 1 : 2;  // Only mode 3 is mono
    }

    return bitrate;
}

// Scan file for first valid MP3 frame and extract info
static void mp_scan_mp3_header(void) {
    if (!mp_file) return;

    uint8_t buf[4096];
    uint32_t scan_pos = mp_data_offset;
    uint32_t max_scan = 32768;  // Scan up to 32KB

    while (scan_pos < mp_data_offset + max_scan && scan_pos < mp_file_size) {
        if (fseek(mp_file, scan_pos, SEEK_SET) != 0) break;
        size_t got = fread(buf, 1, sizeof(buf), mp_file);
        if (got < 4) break;

        for (size_t i = 0; i < got - 4; i++) {
            if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
                int sr = 0;
                int ch = 0;
                int br = mp_parse_mp3_frame_header(buf + i, &sr, &ch);
                if (br > 0) {
                    mp_mp3_bitrate = br;
                    mp_mp3_bitrate_from_header = 1;  // Mark that we got bitrate from header
                    if (sr > 0) {
                        mp_mp3_detected_samplerate = sr;
                        mp_sample_rate = sr;
                    }
                    if (ch > 0) {
                        mp_mp3_detected_channels = ch;
                        mp_channels = ch;
                    }
                    return;  // Found valid frame
                }
            }
        }
        scan_pos += got - 3;  // Overlap to catch headers at boundary
    }
}

// Calculate bytes per second for current format (for seek/duration)
static int mp_get_bytes_per_sec(void) {
    switch (mp_format) {
        case MP_FMT_MP3:
            // MP3: use detected bitrate (kbps * 1000 / 8 = bytes/sec)
            return (mp_mp3_bitrate * 1000) / 8;

        case MP_FMT_WAV_PCM:
            // PCM: sample_rate * channels * bytes_per_sample
            return mp_sample_rate * mp_channels * (mp_bits_per_sample / 8);

        case MP_FMT_WAV_ADPCM:
        case MP_FMT_RAW_ADPCM:
            // ADPCM: (sample_rate * block_align) / samples_per_block
            if (mp_adpcm_samples_per_block > 0) {
                return (mp_sample_rate * mp_adpcm_block_align) / mp_adpcm_samples_per_block;
            }
            // Fallback for raw ADPCM without header
            return mp_sample_rate / 2;  // ~4-bit mono estimate

        default:
            return mp_sample_rate * 4;  // Fallback: assume 16-bit stereo
    }
}

// Calculate total samples for duration display
static uint64_t mp_calc_total_samples(void) {
    switch (mp_format) {
        case MP_FMT_MP3: {
            // Estimate from bitrate: total_seconds = data_size / bytes_per_sec
            int bps = mp_get_bytes_per_sec();
            if (bps > 0) {
                int duration_sec = mp_data_size / bps;
                // Use detected sample rate (from first frame)
                int sr = (mp_mp3_detected_samplerate > 0) ? mp_mp3_detected_samplerate : 44100;
                return (uint64_t)duration_sec * sr;
            }
            return 0;
        }

        case MP_FMT_WAV_PCM: {
            int bytes_per_sample = mp_channels * (mp_bits_per_sample / 8);
            if (bytes_per_sample > 0) {
                return mp_data_size / bytes_per_sample;
            }
            return 0;
        }

        case MP_FMT_WAV_ADPCM:
        case MP_FMT_RAW_ADPCM: {
            if (mp_adpcm_block_align > 0 && mp_adpcm_samples_per_block > 0) {
                uint32_t total_blocks = mp_data_size / mp_adpcm_block_align;
                return (uint64_t)total_blocks * mp_adpcm_samples_per_block;
            }
            return 0;
        }

        default:
            return 0;
    }
}

// ============================================================================
// Alpha blending and rounded rect (same as video_browser)
// ============================================================================

// Blend two RGB565 colors with alpha (0-255, 255=fully foreground)
static uint16_t mp_blend_color(uint16_t fg, uint16_t bg, int alpha) {
    int fg_r = (fg >> 11) & 0x1F;
    int fg_g = (fg >> 5) & 0x3F;
    int fg_b = fg & 0x1F;
    int bg_r = (bg >> 11) & 0x1F;
    int bg_g = (bg >> 5) & 0x3F;
    int bg_b = bg & 0x1F;
    int r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    int g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    int b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Draw semi-transparent rounded rectangle with alpha blending
static void mp_draw_rounded_rect_alpha(uint16_t *fb, int x, int y, int w, int h, int radius, uint16_t color, int alpha) {
    // Draw main body (excluding corners)
    for (int py = y + radius; py < y + h - radius; py++) {
        for (int px = x; px < x + w; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = mp_blend_color(color, fb[idx], alpha);
            }
        }
    }
    // Top and bottom strips
    for (int py = y; py < y + radius; py++) {
        for (int px = x + radius; px < x + w - radius; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = mp_blend_color(color, fb[idx], alpha);
            }
        }
    }
    for (int py = y + h - radius; py < y + h; py++) {
        for (int px = x + radius; px < x + w - radius; px++) {
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                int idx = py * SCREEN_WIDTH + px;
                fb[idx] = mp_blend_color(color, fb[idx], alpha);
            }
        }
    }
    // Rounded corners
    for (int corner_y = 0; corner_y < radius; corner_y++) {
        for (int corner_x = 0; corner_x < radius; corner_x++) {
            int dx = radius - corner_x;
            int dy = radius - corner_y;
            int dist_sq = dx * dx + dy * dy;
            int radius_sq = radius * radius;
            if (dist_sq <= radius_sq) {
                int px, py, idx;
                // Top-left
                px = x + corner_x; py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = mp_blend_color(color, fb[idx], alpha);
                }
                // Top-right
                px = x + w - 1 - corner_x; py = y + corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = mp_blend_color(color, fb[idx], alpha);
                }
                // Bottom-left
                px = x + corner_x; py = y + h - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = mp_blend_color(color, fb[idx], alpha);
                }
                // Bottom-right
                px = x + w - 1 - corner_x; py = y + h - 1 - corner_y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    idx = py * SCREEN_WIDTH + px;
                    fb[idx] = mp_blend_color(color, fb[idx], alpha);
                }
            }
        }
    }
}

// Fill rectangle (for progress bar)
static void mp_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h && py < SCREEN_HEIGHT; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < SCREEN_WIDTH; px++) {
            if (px < 0) continue;
            fb[py * SCREEN_WIDTH + px] = color;
        }
    }
}

// ============================================================================
// v68: Icon drawing functions (Winamp-style geometric icons)
// ============================================================================

// Draw play icon (right-pointing triangle)
static void mp_draw_icon_play(uint16_t *fb, int x, int y, int size, uint16_t color) {
    // Triangle pointing right
    for (int row = 0; row < size; row++) {
        int half = size / 2;
        int width = (row <= half) ? (row * 2 / 3 + 1) : ((size - 1 - row) * 2 / 3 + 1);
        int start_x = x;
        for (int col = 0; col < width; col++) {
            int px = start_x + col;
            int py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                fb[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
}

// Draw pause icon (two vertical bars)
static void mp_draw_icon_pause(uint16_t *fb, int x, int y, int size, uint16_t color) {
    int bar_w = size / 4;
    if (bar_w < 2) bar_w = 2;
    int gap = size / 4;
    // Left bar
    mp_fill_rect(fb, x, y, bar_w, size, color);
    // Right bar
    mp_fill_rect(fb, x + bar_w + gap, y, bar_w, size, color);
}

// Draw previous icon (bar + left triangle)
static void mp_draw_icon_prev(uint16_t *fb, int x, int y, int size, uint16_t color) {
    int bar_w = size / 6;
    if (bar_w < 2) bar_w = 2;
    // Left bar
    mp_fill_rect(fb, x, y, bar_w, size, color);
    // Left-pointing triangle
    int tri_x = x + bar_w + 1;
    for (int row = 0; row < size; row++) {
        int half = size / 2;
        int width = (row <= half) ? (half - row + 1) : (row - half + 1);
        int start_x = tri_x + (row <= half ? row : (size - 1 - row));
        for (int col = 0; col < width; col++) {
            int px = start_x + col;
            int py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                fb[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
}

// Draw next icon (right triangle + bar)
static void mp_draw_icon_next(uint16_t *fb, int x, int y, int size, uint16_t color) {
    // Right-pointing triangle
    int tri_w = size * 2 / 3;
    for (int row = 0; row < size; row++) {
        int half = size / 2;
        int width = (row <= half) ? (row + 1) : (size - row);
        for (int col = 0; col < width; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                fb[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
    // Right bar
    int bar_w = size / 6;
    if (bar_w < 2) bar_w = 2;
    mp_fill_rect(fb, x + tri_w + 1, y, bar_w, size, color);
}

// ============================================================================
// ADPCM decoding (MS ADPCM)
// ============================================================================

static inline int16_t mp_decode_adpcm_sample(int nibble, int ch) {
    int signed_nibble = (nibble < 8) ? nibble : (nibble - 16);
    int pred = ((mp_adpcm_sample1[ch] * mp_adpcm_coef1[mp_adpcm_coef_idx[ch]]) +
                (mp_adpcm_sample2[ch] * mp_adpcm_coef2[mp_adpcm_coef_idx[ch]])) >> 8;
    int unsigned_nibble = nibble & 0xF;
    int sample = pred + signed_nibble * mp_adpcm_delta[ch];
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    mp_adpcm_sample2[ch] = mp_adpcm_sample1[ch];
    mp_adpcm_sample1[ch] = sample;
    mp_adpcm_delta[ch] = (mp_adpcm_adapt_table[unsigned_nibble] * mp_adpcm_delta[ch]) >> 8;
    if (mp_adpcm_delta[ch] < 16) mp_adpcm_delta[ch] = 16;
    return (int16_t)sample;
}

static int mp_decode_adpcm_block_mono(uint8_t *src, int src_size, int16_t *dst, int max_samples) {
    if (src_size < 7) return 0;
    mp_adpcm_coef_idx[0] = src[0];
    if (mp_adpcm_coef_idx[0] > 6) mp_adpcm_coef_idx[0] = 0;
    mp_adpcm_delta[0] = (int16_t)(src[1] | (src[2] << 8));
    mp_adpcm_sample1[0] = (int16_t)(src[3] | (src[4] << 8));
    mp_adpcm_sample2[0] = (int16_t)(src[5] | (src[6] << 8));

    int out_idx = 0;
    if (out_idx < max_samples) dst[out_idx++] = mp_adpcm_sample2[0];
    if (out_idx < max_samples) dst[out_idx++] = mp_adpcm_sample1[0];

    for (int i = 7; i < src_size && out_idx < max_samples; i++) {
        dst[out_idx++] = mp_decode_adpcm_sample((src[i] >> 4) & 0xF, 0);
        if (out_idx < max_samples) {
            dst[out_idx++] = mp_decode_adpcm_sample(src[i] & 0xF, 0);
        }
    }
    return out_idx;
}

static int mp_decode_adpcm_block_stereo(uint8_t *src, int src_size, int16_t *dst, int max_samples) {
    if (src_size < 14) return 0;
    mp_adpcm_coef_idx[0] = src[0];
    mp_adpcm_coef_idx[1] = src[1];
    if (mp_adpcm_coef_idx[0] > 6) mp_adpcm_coef_idx[0] = 0;
    if (mp_adpcm_coef_idx[1] > 6) mp_adpcm_coef_idx[1] = 0;

    mp_adpcm_delta[0] = (int16_t)(src[2] | (src[3] << 8));
    mp_adpcm_delta[1] = (int16_t)(src[4] | (src[5] << 8));
    mp_adpcm_sample1[0] = (int16_t)(src[6] | (src[7] << 8));
    mp_adpcm_sample1[1] = (int16_t)(src[8] | (src[9] << 8));
    mp_adpcm_sample2[0] = (int16_t)(src[10] | (src[11] << 8));
    mp_adpcm_sample2[1] = (int16_t)(src[12] | (src[13] << 8));

    int out_idx = 0;
    if (out_idx + 1 < max_samples) {
        dst[out_idx++] = mp_adpcm_sample2[0];
        dst[out_idx++] = mp_adpcm_sample2[1];
    }
    if (out_idx + 1 < max_samples) {
        dst[out_idx++] = mp_adpcm_sample1[0];
        dst[out_idx++] = mp_adpcm_sample1[1];
    }

    for (int i = 14; i < src_size && out_idx + 1 < max_samples; i++) {
        dst[out_idx++] = mp_decode_adpcm_sample((src[i] >> 4) & 0xF, 0);
        dst[out_idx++] = mp_decode_adpcm_sample(src[i] & 0xF, 1);
    }
    return out_idx;
}

// ============================================================================
// MP3 decoding
// ============================================================================

static void mp_mp3_init(void) {
    if (!mp_mp3_initialized) {
        mp_mp3_handle = mad_init();
        if (mp_mp3_handle) {
            mp_mp3_initialized = 1;
        }
        mp_mp3_input_len = 0;
        mp_mp3_input_remaining = 0;
    }
}

// Find next valid MP3 frame sync after seek (align to frame boundary)
static void mp_mp3_resync(void) {
    if (!mp_file) return;

    uint8_t buf[4096];
    uint32_t max_scan = 8192;  // Scan max 8KB for sync
    uint32_t scanned = 0;

    while (scanned < max_scan && mp_file_pos < mp_data_offset + mp_data_size) {
        if (fseek(mp_file, mp_file_pos, SEEK_SET) != 0) break;

        size_t got = fread(buf, 1, sizeof(buf), mp_file);
        if (got < 4) break;

        // Look for MP3 frame sync: 0xFF followed by 0xE0-0xFF (11 sync bits)
        for (size_t i = 0; i < got - 1; i++) {
            if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
                // Found potential sync - verify it's not false positive
                // Check that layer bits are not reserved (00)
                uint8_t layer = (buf[i + 1] >> 1) & 0x03;
                if (layer != 0) {
                    // Valid frame header found
                    mp_file_pos += i;
                    return;
                }
            }
        }

        // Move forward, but keep last byte in case sync spans chunks
        mp_file_pos += got - 1;
        scanned += got - 1;
    }
}

static void mp_mp3_reset(void) {
    // Reset decoder state but KEEP sample rate/channels/bitrate
    // (they are constant for the entire file)
    if (mp_mp3_initialized && mp_mp3_handle) {
        mad_uninit(mp_mp3_handle);
        mp_mp3_handle = mad_init();
    }
    mp_mp3_input_len = 0;
    mp_mp3_input_remaining = 0;

    // Resync to next valid MP3 frame after seek
    mp_mp3_resync();
}

static void mp_mp3_close(void) {
    if (mp_mp3_initialized && mp_mp3_handle) {
        mad_uninit(mp_mp3_handle);
        mp_mp3_handle = NULL;
        mp_mp3_initialized = 0;
    }
    // Reset detection for next file
    mp_mp3_input_len = 0;
    mp_mp3_input_remaining = 0;
    mp_mp3_detected_samplerate = 0;
    mp_mp3_detected_channels = 0;
}

static int mp_mp3_fill_input_buffer(void) {
    // Shift remaining data to beginning
    if (mp_mp3_input_remaining > 0 && mp_mp3_input_remaining < mp_mp3_input_len) {
        memmove(mp_mp3_input_buf, mp_mp3_input_buf + (mp_mp3_input_len - mp_mp3_input_remaining), mp_mp3_input_remaining);
        mp_mp3_input_len = mp_mp3_input_remaining;
    } else if (mp_mp3_input_remaining <= 0) {
        mp_mp3_input_len = 0;
    }

    int space = MP_MP3_INPUT_BUF_SIZE - mp_mp3_input_len - 8;
    if (space <= 0) return mp_mp3_input_len;

    // Read more data
    uint32_t remaining_in_file = mp_data_size - (mp_file_pos - mp_data_offset);
    if (remaining_in_file == 0) return mp_mp3_input_len;

    int to_read = (space < (int)remaining_in_file) ? space : (int)remaining_in_file;

    if (fseek(mp_file, mp_file_pos, SEEK_SET) != 0) return mp_mp3_input_len;

    size_t got = fread(mp_mp3_input_buf + mp_mp3_input_len, 1, to_read, mp_file);
    if (got > 0) {
        mp_mp3_input_len += got;
        mp_file_pos += got;
    }

    mp_mp3_input_remaining = mp_mp3_input_len;
    return mp_mp3_input_len;
}

static int mp_read_audio_mp3(void) {
    if (mp_file_pos >= mp_data_offset + mp_data_size && mp_mp3_input_remaining <= 0) return 0;

    mp_mp3_init();
    if (!mp_mp3_handle) return 0;

    int total_decoded_bytes = 0;
    int free_space = MP_AUDIO_RING_SIZE - mp_aring_count;
    int consecutive_errors = 0;
    int loop_iterations = 0;  // v58: Safety counter to prevent infinite loops

    while (free_space > 512 && consecutive_errors < 100 && loop_iterations < 500) {
        loop_iterations++;

        // v58: Check EOF status each iteration (file_pos updated during loop)
        int at_eof = (mp_file_pos >= mp_data_offset + mp_data_size);

        if (mp_mp3_input_remaining < 2048) {
            int prev_len = mp_mp3_input_len;
            int new_len = mp_mp3_fill_input_buffer();
            // v58: If at EOF and no new data was added, we're done
            if (at_eof && new_len <= prev_len) break;
            if (new_len <= 0) break;
        }

        if (mp_mp3_input_len <= 0) break;

        int bytes_read = 0;
        int bytes_done = 0;
        int out_buf_size = MP_MP3_DECODE_BUF_SIZE * sizeof(int16_t);

        int result = mad_decode(mp_mp3_handle,
                                (char *)mp_mp3_input_buf, mp_mp3_input_len,
                                (char *)mp_mp3_decode_buf, out_buf_size,
                                &bytes_read, &bytes_done, 16, 0);

        if (result == MAD_OK) {
            consecutive_errors = 0;
            if (mp_mp3_detected_samplerate == 0) {
                int sr = 0, ch = 0;
                if (mad_get_info(mp_mp3_handle, &sr, &ch)) {
                    mp_mp3_detected_samplerate = sr;
                    mp_mp3_detected_channels = ch;
                    mp_sample_rate = sr;
                    mp_channels = ch;
                    // Get bitrate for duration/seek calculations - only if header parsing didn't find it
                    if (!mp_mp3_bitrate_from_header) {
                        int br = mad_get_bitrate(mp_mp3_handle);
                        if (br > 0) {
                            mp_mp3_bitrate = br;
                        }
                    }
                }
            }
            mp_mp3_input_remaining = mp_mp3_input_len - bytes_read;
            if (mp_mp3_input_remaining > 0 && bytes_read > 0) {
                memmove(mp_mp3_input_buf, mp_mp3_input_buf + bytes_read, mp_mp3_input_remaining);
            }
            mp_mp3_input_len = mp_mp3_input_remaining;
        } else if (result == MAD_NEED_MORE_INPUT) {
            mp_mp3_input_remaining = mp_mp3_input_len - bytes_read;
            if (mp_mp3_input_remaining > 0 && bytes_read > 0) {
                memmove(mp_mp3_input_buf, mp_mp3_input_buf + bytes_read, mp_mp3_input_remaining);
            }
            mp_mp3_input_len = mp_mp3_input_remaining;
            // v58: Check if we're at EOF before trying to fill more
            int prev_len = mp_mp3_input_len;
            int new_len = mp_mp3_fill_input_buffer();
            if (new_len <= 0) break;
            // v58: If at EOF and no new data was added, we're done
            if (mp_file_pos >= mp_data_offset + mp_data_size && new_len <= prev_len) break;
            continue;
        } else if (result == MAD_ERR) {
            consecutive_errors++;
            if (bytes_read == 0) bytes_read = 1;
            mp_mp3_input_remaining = mp_mp3_input_len - bytes_read;
            if (mp_mp3_input_remaining > 0) {
                memmove(mp_mp3_input_buf, mp_mp3_input_buf + bytes_read, mp_mp3_input_remaining);
            }
            mp_mp3_input_len = mp_mp3_input_remaining;
            continue;
        } else {
            break;
        }

        if (bytes_done <= 0) continue;

        // Handle mono->stereo conversion
        int actual_channels = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
        if (actual_channels == 1) {
            int mono_samples = bytes_done / 2;
            int stereo_bytes = mono_samples * 4;

            if (stereo_bytes > free_space) {
                mono_samples = free_space / 4;
                stereo_bytes = mono_samples * 4;
            }

            int16_t *mono_src = (int16_t *)mp_mp3_decode_buf;
            for (int i = 0; i < mono_samples; i++) {
                int16_t sample = mono_src[i];
                mp_audio_ring[mp_aring_write] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 1] = (sample >> 8) & 0xFF;
                mp_audio_ring[mp_aring_write + 2] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 3] = (sample >> 8) & 0xFF;
                mp_aring_write = (mp_aring_write + 4) % MP_AUDIO_RING_SIZE;
            }

            mp_aring_count += stereo_bytes;
            free_space -= stereo_bytes;
            total_decoded_bytes += stereo_bytes;
        } else {
            int decoded_bytes = bytes_done;
            if (decoded_bytes > free_space) decoded_bytes = free_space;

            uint8_t *src = (uint8_t *)mp_mp3_decode_buf;
            int written = 0;
            while (written < decoded_bytes) {
                int before_wrap = MP_AUDIO_RING_SIZE - mp_aring_write;
                int to_write = decoded_bytes - written;
                if (to_write > before_wrap) to_write = before_wrap;

                memcpy(mp_audio_ring + mp_aring_write, src + written, to_write);
                mp_aring_write = (mp_aring_write + to_write) % MP_AUDIO_RING_SIZE;
                written += to_write;
            }

            mp_aring_count += decoded_bytes;
            free_space -= decoded_bytes;
            total_decoded_bytes += decoded_bytes;
        }

        if (total_decoded_bytes > 4096) break;
    }

    return total_decoded_bytes;
}

// ============================================================================
// WAV PCM reading
// ============================================================================

static int mp_read_audio_pcm(void) {
    int free_space = MP_AUDIO_RING_SIZE - mp_aring_count;
    if (free_space < 1024) return 0;

    uint32_t remaining_in_file = mp_data_size - (mp_file_pos - mp_data_offset);
    if (remaining_in_file == 0) return 0;

    int to_read = (free_space < (int)remaining_in_file) ? free_space : (int)remaining_in_file;
    if (to_read > 4096) to_read = 4096;

    // For 8-bit or mono, we need to convert
    if (mp_bits_per_sample == 8 || mp_channels == 1) {
        uint8_t temp_buf[4096];
        int src_bytes = to_read;

        // Calculate how many source bytes we need for the output
        if (mp_bits_per_sample == 8 && mp_channels == 1) {
            src_bytes = to_read / 4;  // 1 byte -> 4 bytes (8bit mono -> 16bit stereo)
        } else if (mp_bits_per_sample == 8) {
            src_bytes = to_read / 2;  // 2 bytes -> 4 bytes (8bit stereo -> 16bit stereo)
        } else if (mp_channels == 1) {
            src_bytes = to_read / 2;  // 2 bytes -> 4 bytes (16bit mono -> 16bit stereo)
        }

        if (src_bytes > (int)remaining_in_file) src_bytes = remaining_in_file;
        if (src_bytes > (int)sizeof(temp_buf)) src_bytes = sizeof(temp_buf);

        if (fseek(mp_file, mp_file_pos, SEEK_SET) != 0) return 0;
        size_t got = fread(temp_buf, 1, src_bytes, mp_file);
        if (got == 0) return 0;
        mp_file_pos += got;

        int out_bytes = 0;

        if (mp_bits_per_sample == 8 && mp_channels == 1) {
            // 8-bit mono -> 16-bit stereo
            for (size_t i = 0; i < got && mp_aring_count < MP_AUDIO_RING_SIZE - 4; i++) {
                int16_t sample = (int16_t)((temp_buf[i] - 128) << 8);
                mp_audio_ring[mp_aring_write] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 1] = (sample >> 8) & 0xFF;
                mp_audio_ring[mp_aring_write + 2] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 3] = (sample >> 8) & 0xFF;
                mp_aring_write = (mp_aring_write + 4) % MP_AUDIO_RING_SIZE;
                mp_aring_count += 4;
                out_bytes += 4;
            }
        } else if (mp_bits_per_sample == 8) {
            // 8-bit stereo -> 16-bit stereo
            for (size_t i = 0; i + 1 < got && mp_aring_count < MP_AUDIO_RING_SIZE - 4; i += 2) {
                int16_t left = (int16_t)((temp_buf[i] - 128) << 8);
                int16_t right = (int16_t)((temp_buf[i+1] - 128) << 8);
                mp_audio_ring[mp_aring_write] = left & 0xFF;
                mp_audio_ring[mp_aring_write + 1] = (left >> 8) & 0xFF;
                mp_audio_ring[mp_aring_write + 2] = right & 0xFF;
                mp_audio_ring[mp_aring_write + 3] = (right >> 8) & 0xFF;
                mp_aring_write = (mp_aring_write + 4) % MP_AUDIO_RING_SIZE;
                mp_aring_count += 4;
                out_bytes += 4;
            }
        } else {
            // 16-bit mono -> 16-bit stereo
            for (size_t i = 0; i + 1 < got && mp_aring_count < MP_AUDIO_RING_SIZE - 4; i += 2) {
                int16_t sample = temp_buf[i] | (temp_buf[i+1] << 8);
                mp_audio_ring[mp_aring_write] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 1] = (sample >> 8) & 0xFF;
                mp_audio_ring[mp_aring_write + 2] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 3] = (sample >> 8) & 0xFF;
                mp_aring_write = (mp_aring_write + 4) % MP_AUDIO_RING_SIZE;
                mp_aring_count += 4;
                out_bytes += 4;
            }
        }
        return out_bytes;
    }

    // 16-bit stereo - direct copy
    if (fseek(mp_file, mp_file_pos, SEEK_SET) != 0) return 0;

    int written = 0;
    while (written < to_read) {
        int before_wrap = MP_AUDIO_RING_SIZE - mp_aring_write;
        int chunk = to_read - written;
        if (chunk > before_wrap) chunk = before_wrap;

        size_t got = fread(mp_audio_ring + mp_aring_write, 1, chunk, mp_file);
        if (got == 0) break;

        mp_file_pos += got;
        mp_aring_write = (mp_aring_write + got) % MP_AUDIO_RING_SIZE;
        mp_aring_count += got;
        written += got;
    }

    return written;
}

// ============================================================================
// WAV ADPCM reading
// ============================================================================

static int mp_read_audio_adpcm(void) {
    if (mp_adpcm_block_align <= 0) return 0;

    int free_space = MP_AUDIO_RING_SIZE - mp_aring_count;
    if (free_space < 1024) return 0;

    uint32_t remaining_in_file = mp_data_size - (mp_file_pos - mp_data_offset);
    if (remaining_in_file == 0) return 0;

    int total_decoded_bytes = 0;

    while (free_space > 1024 && remaining_in_file > 0) {
        int block_size = mp_adpcm_block_align;
        if (block_size > (int)remaining_in_file) block_size = remaining_in_file;
        if (block_size > (int)sizeof(mp_adpcm_read_buf)) block_size = sizeof(mp_adpcm_read_buf);

        if (fseek(mp_file, mp_file_pos, SEEK_SET) != 0) break;

        size_t got = fread(mp_adpcm_read_buf, 1, block_size, mp_file);
        if (got == 0) break;

        mp_file_pos += got;
        remaining_in_file -= got;

        int samples;
        if (mp_channels == 1) {
            samples = mp_decode_adpcm_block_mono(mp_adpcm_read_buf, got, mp_adpcm_decode_buf, MP_ADPCM_DECODE_BUF_SIZE);
        } else {
            samples = mp_decode_adpcm_block_stereo(mp_adpcm_read_buf, got, mp_adpcm_decode_buf, MP_ADPCM_DECODE_BUF_SIZE);
        }

        if (samples <= 0) continue;

        // Convert to stereo if mono
        if (mp_channels == 1) {
            for (int i = 0; i < samples && mp_aring_count < MP_AUDIO_RING_SIZE - 4; i++) {
                int16_t sample = mp_adpcm_decode_buf[i];
                mp_audio_ring[mp_aring_write] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 1] = (sample >> 8) & 0xFF;
                mp_audio_ring[mp_aring_write + 2] = sample & 0xFF;
                mp_audio_ring[mp_aring_write + 3] = (sample >> 8) & 0xFF;
                mp_aring_write = (mp_aring_write + 4) % MP_AUDIO_RING_SIZE;
                mp_aring_count += 4;
                free_space -= 4;
                total_decoded_bytes += 4;
            }
        } else {
            int decoded_bytes = samples * 2;
            if (decoded_bytes > free_space) decoded_bytes = free_space;

            uint8_t *src = (uint8_t *)mp_adpcm_decode_buf;
            int written = 0;
            while (written < decoded_bytes) {
                int before_wrap = MP_AUDIO_RING_SIZE - mp_aring_write;
                int to_write = decoded_bytes - written;
                if (to_write > before_wrap) to_write = before_wrap;

                memcpy(mp_audio_ring + mp_aring_write, src + written, to_write);
                mp_aring_write = (mp_aring_write + to_write) % MP_AUDIO_RING_SIZE;
                written += to_write;
            }

            mp_aring_count += decoded_bytes;
            free_space -= decoded_bytes;
            total_decoded_bytes += decoded_bytes;
        }

        if (total_decoded_bytes > 4096) break;
    }

    return total_decoded_bytes;
}

// ============================================================================
// Format detection and parsing
// ============================================================================

static int mp_skip_id3v2(void) {
    uint8_t header[10];

    if (fseek(mp_file, 0, SEEK_SET) != 0) return 0;
    if (fread(header, 1, 10, mp_file) != 10) return 0;

    // Check for "ID3" tag
    if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        // Calculate size (syncsafe integer)
        uint32_t size = ((header[6] & 0x7F) << 21) |
                        ((header[7] & 0x7F) << 14) |
                        ((header[8] & 0x7F) << 7) |
                        (header[9] & 0x7F);
        return 10 + size;
    }

    return 0;
}

static int mp_parse_wav_header(void) {
    uint8_t header[256];

    if (fseek(mp_file, 0, SEEK_SET) != 0) return 0;
    if (fread(header, 1, 44, mp_file) != 44) return 0;

    // Check RIFF header
    if (memcmp(header, "RIFF", 4) != 0) return 0;
    if (memcmp(header + 8, "WAVE", 4) != 0) return 0;

    // Find fmt chunk
    uint32_t pos = 12;
    while (pos < mp_file_size - 8) {
        if (fseek(mp_file, pos, SEEK_SET) != 0) return 0;
        if (fread(header, 1, 8, mp_file) != 8) return 0;

        uint32_t chunk_size = mp_read_u32_le(header + 4);

        if (memcmp(header, "fmt ", 4) == 0) {
            if (chunk_size < 16) return 0;
            int to_read = (chunk_size > sizeof(header) - 8) ? sizeof(header) - 8 : chunk_size;
            if (fread(header + 8, 1, to_read, mp_file) != (size_t)to_read) return 0;

            int format_tag = mp_read_u16_le(header + 8);
            mp_channels = mp_read_u16_le(header + 10);
            mp_sample_rate = mp_read_u32_le(header + 12);
            mp_bits_per_sample = mp_read_u16_le(header + 22);

            if (format_tag == 1) {
                // PCM
                mp_format = MP_FMT_WAV_PCM;
            } else if (format_tag == 2) {
                // MS ADPCM
                mp_format = MP_FMT_WAV_ADPCM;
                mp_adpcm_block_align = mp_read_u16_le(header + 20);
                if (chunk_size >= 20) {
                    mp_adpcm_samples_per_block = mp_read_u16_le(header + 26);
                } else {
                    // Calculate samples per block
                    int header_size = (mp_channels == 1) ? 7 : 14;
                    mp_adpcm_samples_per_block = 2 + (mp_adpcm_block_align - header_size) * 2 / mp_channels;
                }
            } else {
                return 0;  // Unsupported format
            }
        } else if (memcmp(header, "data", 4) == 0) {
            mp_data_offset = pos + 8;
            mp_data_size = chunk_size;
            mp_file_pos = mp_data_offset;
            return 1;
        }

        pos += 8 + chunk_size;
        if (chunk_size & 1) pos++;  // Pad to even boundary
    }

    return 0;
}

static int mp_detect_format(void) {
    uint8_t header[12];

    // Get file size
    if (fseek(mp_file, 0, SEEK_END) != 0) return 0;
    mp_file_size = ftell(mp_file);
    if (fseek(mp_file, 0, SEEK_SET) != 0) return 0;

    if (fread(header, 1, 12, mp_file) != 12) return 0;

    // Check for WAV (RIFF...WAVE)
    if (memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0) {
        return mp_parse_wav_header();
    }

    // Check for MP3 (ID3 tag or sync bytes)
    if ((header[0] == 'I' && header[1] == 'D' && header[2] == '3') ||
        (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)) {
        mp_format = MP_FMT_MP3;
        int id3_size = mp_skip_id3v2();
        mp_data_offset = id3_size;
        mp_data_size = mp_file_size - id3_size;
        mp_file_pos = mp_data_offset;
        // Sample rate/channels will be detected during decode
        mp_sample_rate = 44100;  // Default
        mp_channels = 2;
        return 1;
    }

    // Check for raw ADPCM by extension
    const char *ext = strrchr(mp_current_path, '.');
    if (ext && (mp_strcasecmp(ext, ".adp") == 0 || mp_strcasecmp(ext, ".adpcm") == 0)) {
        mp_format = MP_FMT_RAW_ADPCM;
        mp_data_offset = 0;
        mp_data_size = mp_file_size;
        mp_file_pos = 0;
        // Assume typical ADPCM settings
        mp_sample_rate = 22050;
        mp_channels = 1;
        mp_adpcm_block_align = 256;
        mp_adpcm_samples_per_block = 2 + (256 - 7) * 2;
        return 1;
    }

    return 0;
}

// ============================================================================
// Playlist management
// ============================================================================

static int mp_is_music_file(const char *name) {
    return mp_str_ends_with_ci(name, ".mp3") ||
           mp_str_ends_with_ci(name, ".wav") ||
           mp_str_ends_with_ci(name, ".adp") ||
           mp_str_ends_with_ci(name, ".adpcm");
}

static void mp_sort_playlist(void) {
    // Simple bubble sort for A-Z
    for (int i = 0; i < mp_playlist_count - 1; i++) {
        for (int j = 0; j < mp_playlist_count - i - 1; j++) {
            if (mp_strcasecmp(mp_playlist[j], mp_playlist[j+1]) > 0) {
                char temp[MP_MAX_FILENAME];
                strncpy(temp, mp_playlist[j], MP_MAX_FILENAME - 1);
                strncpy(mp_playlist[j], mp_playlist[j+1], MP_MAX_FILENAME - 1);
                strncpy(mp_playlist[j+1], temp, MP_MAX_FILENAME - 1);
            }
        }
    }
}

static void mp_scan_playlist(void) {
    mp_playlist_count = 0;
    mp_playlist_current = -1;

#ifdef SF2000
    union {
        struct {
            uint8_t _1[0x10];
            uint32_t type;
        };
        struct {
            uint8_t _2[0x22];
            char d_name[0x225];
        };
        uint8_t __[0x428];
    } buffer;

    int dir_fd = fs_opendir(mp_current_dir);
    if (dir_fd < 0) return;

    while (fs_readdir(dir_fd, &buffer) >= 0 && mp_playlist_count < MP_MAX_PLAYLIST) {
        if (buffer.type == 0x4000) continue;  // Skip directories
        if (!mp_is_music_file(buffer.d_name)) continue;

        strncpy(mp_playlist[mp_playlist_count], buffer.d_name, MP_MAX_FILENAME - 1);
        mp_playlist[mp_playlist_count][MP_MAX_FILENAME - 1] = '\0';

        if (mp_strcasecmp(buffer.d_name, mp_current_filename) == 0) {
            mp_playlist_current = mp_playlist_count;
        }
        mp_playlist_count++;
    }

    fs_closedir(dir_fd);
#else
    DIR *dir = opendir(mp_current_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && mp_playlist_count < MP_MAX_PLAYLIST) {
        if (entry->d_type == DT_DIR) continue;
        if (!mp_is_music_file(entry->d_name)) continue;

        strncpy(mp_playlist[mp_playlist_count], entry->d_name, MP_MAX_FILENAME - 1);
        mp_playlist[mp_playlist_count][MP_MAX_FILENAME - 1] = '\0';

        if (mp_strcasecmp(entry->d_name, mp_current_filename) == 0) {
            mp_playlist_current = mp_playlist_count;
        }
        mp_playlist_count++;
    }

    closedir(dir);
#endif

    mp_sort_playlist();

    // Find current file in sorted list
    for (int i = 0; i < mp_playlist_count; i++) {
        if (mp_strcasecmp(mp_playlist[i], mp_current_filename) == 0) {
            mp_playlist_current = i;
            break;
        }
    }
}

static int mp_load_next_az(void) {
    if (mp_playlist_count <= 1) return 0;
    int next_idx = (mp_playlist_current + 1) % mp_playlist_count;

    char new_path[MP_MAX_PATH];
    snprintf(new_path, MP_MAX_PATH, "%s/%s", mp_current_dir, mp_playlist[next_idx]);

    mp_close();
    return mp_open(new_path);
}

static int mp_load_shuffle(void) {
    if (mp_playlist_count <= 1) return 0;

    int next_idx;
    do {
        next_idx = mp_rand() % mp_playlist_count;
    } while (next_idx == mp_playlist_current && mp_playlist_count > 1);

    char new_path[MP_MAX_PATH];
    snprintf(new_path, MP_MAX_PATH, "%s/%s", mp_current_dir, mp_playlist[next_idx]);

    mp_close();
    return mp_open(new_path);
}


// ============================================================================
// Public API
// ============================================================================

void mp_init(void) {
    mp_audio_ring = (uint8_t *)malloc(MP_AUDIO_RING_SIZE);
    if (mp_audio_ring) {
        memset(mp_audio_ring, 0, MP_AUDIO_RING_SIZE);
    }
}

void mp_set_audio_callback(mp_audio_batch_cb_t cb) {
    mp_audio_batch_cb = cb;
}

static void mp_reset_state(void) {
    mp_format = MP_FMT_UNKNOWN;
    mp_sample_rate = 44100;
    mp_channels = 2;
    mp_bits_per_sample = 16;
    mp_data_offset = 0;
    mp_data_size = 0;
    mp_file_size = 0;
    mp_file_pos = 0;
    mp_samples_played = 0;

    mp_adpcm_block_align = 0;
    mp_adpcm_samples_per_block = 0;
    memset(mp_adpcm_sample1, 0, sizeof(mp_adpcm_sample1));
    memset(mp_adpcm_sample2, 0, sizeof(mp_adpcm_sample2));
    memset(mp_adpcm_delta, 0, sizeof(mp_adpcm_delta));
    memset(mp_adpcm_coef_idx, 0, sizeof(mp_adpcm_coef_idx));

    mp_mp3_reset();

    mp_aring_read = 0;
    mp_aring_write = 0;
    mp_aring_count = 0;

    mp_next_track_request = 0;
}

int mp_open(const char *path) {
    if (mp_active) {
        mp_close();
    }

    // Store path info
    strncpy(mp_current_path, path, MP_MAX_PATH - 1);
    mp_current_path[MP_MAX_PATH - 1] = '\0';

    // Extract directory
    strncpy(mp_current_dir, path, MP_MAX_PATH - 1);
    char *last_slash = strrchr(mp_current_dir, '/');
    if (!last_slash) last_slash = strrchr(mp_current_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
        strncpy(mp_current_filename, last_slash + 1, MP_MAX_FILENAME - 1);
    } else {
        mp_current_dir[0] = '.';
        mp_current_dir[1] = '\0';
        strncpy(mp_current_filename, path, MP_MAX_FILENAME - 1);
    }
    mp_current_filename[MP_MAX_FILENAME - 1] = '\0';

    // Open file
    mp_file = fopen(path, "rb");
    if (!mp_file) {
        return 0;
    }

    mp_reset_state();

    // v67: Ensure ring buffer is allocated (safety check for re-entry)
    if (!mp_audio_ring) {
        mp_audio_ring = (uint8_t *)malloc(MP_AUDIO_RING_SIZE);
        if (!mp_audio_ring) {
            fclose(mp_file);
            mp_file = NULL;
            return 0;  // Out of memory
        }
        memset(mp_audio_ring, 0, MP_AUDIO_RING_SIZE);
    }

    // Detect and parse format
    if (!mp_detect_format()) {
        fclose(mp_file);
        mp_file = NULL;
        return 0;
    }

    // For MP3: scan header to detect actual bitrate/samplerate
    // (needed for correct duration display)
    if (mp_format == MP_FMT_MP3) {
        mp_mp3_bitrate = 128;  // Default fallback
        mp_mp3_bitrate_from_header = 0;  // Reset header detection flag
        mp_scan_mp3_header();  // This updates mp_mp3_bitrate and mp_sample_rate
    }

    // Scan playlist for A-Z and shuffle modes
    mp_scan_playlist();

    mp_active = 1;
    mp_paused = 0;
    mp_ui_mode = MP_UI_PLAYING;
    mp_resample_acc = 0;  // Reset resampler for new file
    mp_last_output_time = 0;  // Reset time tracking
    mp_audio_acc_us = 0;
    mp_samples_played = 0;
    mp_eof_pending = 0;
    mp_mp3_vbr = 0;
    mp_title_scroll_offset = 0;  // v57: Reset title scrolling
    mp_title_scroll_delay = 0;
    mp_title_at_end = 0;         // v59: Reset end pause flag
    mp_title_scroll_timer = 0;   // v59: Reset scroll timer
    mp_title_end_timer = 0;      // v59: Reset end timer

    // v66: Pre-fill audio buffer before starting playback
    // This ensures we have enough data buffered to handle low FPS
    for (int i = 0; i < 8 && mp_aring_count < MP_AUDIO_RING_SIZE * 3 / 4; i++) {
        mp_read_and_decode_audio();
    }

    return 1;
}

void mp_close(void) {
    if (mp_file) {
        fclose(mp_file);
        mp_file = NULL;
    }

    // v67: DON'T free audio ring buffer - just clear it!
    // Freeing caused re-entry hang because mp_open() doesn't re-allocate
    // The buffer is allocated once in mp_init() and reused
    if (mp_audio_ring) {
        memset(mp_audio_ring, 0, MP_AUDIO_RING_SIZE);
    }
    mp_aring_read = 0;
    mp_aring_write = 0;
    mp_aring_count = 0;

    mp_mp3_close();
    mp_active = 0;
    mp_paused = 0;
    mp_background_mode = 0;  // v67: Reset background mode on close
}

int mp_is_active(void) {
    return mp_active;
}

int mp_is_paused(void) {
    return mp_paused;
}

void mp_toggle_pause(void) {
    mp_paused = !mp_paused;
    if (!mp_paused) {
        // Resuming - reset time tracking to avoid big jump
        mp_last_output_time = 0;
        mp_audio_acc_us = 0;
    }
}

// v63: Background mode functions
void mp_set_background_mode(int enabled) {
    mp_background_mode = enabled;
}

int mp_is_background_mode(void) {
    return mp_background_mode;
}

// v63: Update audio only (for background music mode)
void mp_update_audio(void) {
    if (!mp_active) return;

    // v71: Safety checks
    if (!mp_file) return;
    if (!mp_audio_ring) return;

    // v71: Validate ring buffer state
    if (mp_aring_read < 0 || mp_aring_read >= MP_AUDIO_RING_SIZE) mp_aring_read = 0;
    if (mp_aring_write < 0 || mp_aring_write >= MP_AUDIO_RING_SIZE) mp_aring_write = 0;
    if (mp_aring_count < 0) mp_aring_count = 0;
    if (mp_aring_count > MP_AUDIO_RING_SIZE) mp_aring_count = MP_AUDIO_RING_SIZE;

    // v66: Decode MULTIPLE chunks per frame to keep buffer full
    // One decode may not produce enough samples, so loop until buffer is healthy
    for (int i = 0; i < 8 && !mp_paused && mp_aring_count < MP_AUDIO_RING_SIZE * 3 / 4; i++) {
        mp_read_and_decode_audio();
    }

    // Output audio
    if (!mp_paused) {
        mp_output_audio();
    }

    // Check for end of file
    if (!mp_paused && !mp_eof_pending &&
        mp_file_pos >= mp_data_offset + mp_data_size && mp_aring_count < 256) {

        mp_eof_pending = 1;

        switch (mp_play_mode) {
            case MP_PLAY_MODE_REPEAT:
                if (mp_playlist_count > 1 && !mp_load_next_az()) {
                    mp_file_pos = mp_data_offset;
                    mp_aring_read = 0;
                    mp_aring_write = 0;
                    mp_aring_count = 0;
                    mp_samples_played = 0;
                    mp_last_output_time = 0;
                    mp_audio_acc_us = 0;
                    if (mp_format == MP_FMT_MP3) mp_mp3_reset();
                } else if (mp_playlist_count <= 1) {
                    mp_file_pos = mp_data_offset;
                    mp_aring_read = 0;
                    mp_aring_write = 0;
                    mp_aring_count = 0;
                    mp_samples_played = 0;
                    mp_last_output_time = 0;
                    mp_audio_acc_us = 0;
                    if (mp_format == MP_FMT_MP3) mp_mp3_reset();
                }
                mp_eof_pending = 0;
                break;

            case MP_PLAY_MODE_ONCE:
                mp_paused = 1;
                mp_eof_pending = 0;
                break;

            case MP_PLAY_MODE_AZ:
                if (!mp_load_next_az()) {
                    mp_paused = 1;
                }
                mp_eof_pending = 0;
                break;

            case MP_PLAY_MODE_SHUFFLE:
                if (!mp_load_shuffle()) {
                    mp_paused = 1;
                }
                mp_eof_pending = 0;
                break;

            default:
                mp_paused = 1;
                mp_eof_pending = 0;
                break;
        }
    }

    // Safety pause if stuck at EOF
    if (!mp_paused && mp_file_pos >= mp_data_offset + mp_data_size && mp_aring_count == 0) {
        mp_paused = 1;
        mp_eof_pending = 0;
    }
}

// v71: Reset audio timing after a long pause (e.g. during image decode)
// This prevents the audio system from trying to output a huge chunk to "catch up"
void mp_reset_audio_timing(void) {
    mp_last_output_time = 0;  // Next call will assume fresh start
    mp_audio_acc_us = 0;
    mp_resample_acc = 0;
}

int mp_handle_input(int up, int down, int left, int right, int a, int b, int start, int l, int r) {
    static int prev_a = 0, prev_b = 0, prev_left = 0, prev_right = 0;
    static int prev_up = 0, prev_down = 0;
    static int prev_l = 0, prev_r = 0, prev_start = 0;

    // B - exit player
    if (prev_b && !b) {
        prev_a = a; prev_b = b; prev_left = left; prev_right = right;
        prev_up = up; prev_down = down;
        prev_l = l; prev_r = r; prev_start = start;
        return 1;
    }

    // Start - toggle credits view
    if (prev_start && !start) {
        if (mp_ui_mode == MP_UI_CREDITS) {
            mp_ui_mode = MP_UI_PLAYING;
        } else {
            mp_ui_mode = MP_UI_CREDITS;
        }
    }

    // A - toggle pause (only in playing mode)
    if (prev_a && !a) {
        if (mp_ui_mode == MP_UI_CREDITS) {
            mp_ui_mode = MP_UI_PLAYING;  // A closes credits
        } else {
            mp_toggle_pause();
        }
    }

    // v71: LEFT/RIGHT - seek 20 seconds
    #define MP_SEEK_SHORT 20
    if (prev_left && !left) {
        // Seek back 20 seconds
        int bytes_per_sec = mp_get_bytes_per_sec();
        uint32_t seek_back = bytes_per_sec * MP_SEEK_SHORT;
        if (mp_file_pos > mp_data_offset + seek_back) {
            mp_file_pos -= seek_back;
        } else {
            mp_file_pos = mp_data_offset;
        }
        // Recalculate samples_played
        uint32_t pos_in_data = mp_file_pos - mp_data_offset;
        if (bytes_per_sec > 0) {
            mp_samples_played = ((uint64_t)pos_in_data * 22050) / bytes_per_sec;
            if (mp_format == MP_FMT_MP3) {
                int ch = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
                int sr = (mp_mp3_detected_samplerate > 0) ? mp_mp3_detected_samplerate : mp_sample_rate;
                if (ch == 2 && (sr == 44100 || sr == 48000)) {
                    mp_samples_played = mp_samples_played / 2;
                }
            }
        }
        mp_aring_read = 0;
        mp_aring_write = 0;
        mp_aring_count = 0;
        mp_last_output_time = 0;
        mp_audio_acc_us = 0;
        mp_audio_mute_samples = MP_AUDIO_MUTE_AFTER_SEEK;
        if (mp_format == MP_FMT_MP3) {
            mp_mp3_reset();
        }
    }

    if (prev_right && !right) {
        // Seek forward 20 seconds
        int bytes_per_sec = mp_get_bytes_per_sec();
        uint32_t seek_fwd = bytes_per_sec * MP_SEEK_SHORT;
        if (mp_file_pos + seek_fwd < mp_data_offset + mp_data_size) {
            mp_file_pos += seek_fwd;
        }
        uint32_t pos_in_data = mp_file_pos - mp_data_offset;
        if (bytes_per_sec > 0) {
            mp_samples_played = ((uint64_t)pos_in_data * 22050) / bytes_per_sec;
            if (mp_format == MP_FMT_MP3) {
                int ch = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
                int sr = (mp_mp3_detected_samplerate > 0) ? mp_mp3_detected_samplerate : mp_sample_rate;
                if (ch == 2 && (sr == 44100 || sr == 48000)) {
                    mp_samples_played = mp_samples_played / 2;
                }
            }
        }
        mp_aring_read = 0;
        mp_aring_write = 0;
        mp_aring_count = 0;
        mp_last_output_time = 0;
        mp_audio_acc_us = 0;
        mp_audio_mute_samples = MP_AUDIO_MUTE_AFTER_SEEK;
        if (mp_format == MP_FMT_MP3) {
            mp_mp3_reset();
        }
    }

    // v73: UP/DOWN - seek 1 minute (UP=forward, DOWN=back)
    #define MP_SEEK_SECONDS 60
    if (prev_up && !up) {
        // Seek forward (1 minute)
        int bytes_per_sec = mp_get_bytes_per_sec();
        uint32_t seek_fwd = bytes_per_sec * MP_SEEK_SECONDS;
        if (mp_file_pos + seek_fwd < mp_data_offset + mp_data_size) {
            mp_file_pos += seek_fwd;
        }
        // Recalculate samples_played based on new position
        // samples_played is in OUTPUT samples (22050 Hz), not source rate
        uint32_t pos_in_data = mp_file_pos - mp_data_offset;
        if (bytes_per_sec > 0) {
            mp_samples_played = ((uint64_t)pos_in_data * 22050) / bytes_per_sec;
            // v58: Workaround for 44.1/48kHz stereo MP3 (same as duration)
            if (mp_format == MP_FMT_MP3) {
                int ch = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
                int sr = (mp_mp3_detected_samplerate > 0) ? mp_mp3_detected_samplerate : mp_sample_rate;
                if (ch == 2 && (sr == 44100 || sr == 48000)) {
                    mp_samples_played = mp_samples_played / 2;
                }
            }
        }
        mp_aring_read = 0;
        mp_aring_write = 0;
        mp_aring_count = 0;
        mp_last_output_time = 0;
        mp_audio_acc_us = 0;
        mp_audio_mute_samples = MP_AUDIO_MUTE_AFTER_SEEK;  // v61: Mute to avoid crackle
        if (mp_format == MP_FMT_MP3) {
            mp_mp3_reset();
        }
    }

    if (prev_down && !down) {
        // Seek back (1 minute)
        int bytes_per_sec = mp_get_bytes_per_sec();
        uint32_t seek_back = bytes_per_sec * MP_SEEK_SECONDS;
        if (mp_file_pos > mp_data_offset + seek_back) {
            mp_file_pos -= seek_back;
        } else {
            mp_file_pos = mp_data_offset;
        }
        // Recalculate samples_played based on new position
        uint32_t pos_in_data = mp_file_pos - mp_data_offset;
        if (bytes_per_sec > 0) {
            mp_samples_played = ((uint64_t)pos_in_data * 22050) / bytes_per_sec;
            if (mp_format == MP_FMT_MP3) {
                int ch = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
                int sr = (mp_mp3_detected_samplerate > 0) ? mp_mp3_detected_samplerate : mp_sample_rate;
                if (ch == 2 && (sr == 44100 || sr == 48000)) {
                    mp_samples_played = mp_samples_played / 2;
                }
            }
        }
        mp_aring_read = 0;
        mp_aring_write = 0;
        mp_aring_count = 0;
        mp_last_output_time = 0;
        mp_audio_acc_us = 0;
        mp_audio_mute_samples = MP_AUDIO_MUTE_AFTER_SEEK;
        if (mp_format == MP_FMT_MP3) {
            mp_mp3_reset();
        }
    }

    // v71: L/R SHOULDER - previous/next track
    if (prev_l && !l) {
        // Previous track
        if (mp_playlist_count > 1) {
            int prev_idx = (mp_playlist_current - 1 + mp_playlist_count) % mp_playlist_count;
            char new_path[MP_MAX_PATH];
            snprintf(new_path, MP_MAX_PATH, "%s/%s", mp_current_dir, mp_playlist[prev_idx]);
            mp_close();
            mp_open(new_path);
        }
    }

    if (prev_r && !r) {
        // Next track
        if (mp_playlist_count > 1) {
            int next_idx = (mp_playlist_current + 1) % mp_playlist_count;
            char new_path[MP_MAX_PATH];
            snprintf(new_path, MP_MAX_PATH, "%s/%s", mp_current_dir, mp_playlist[next_idx]);
            mp_close();
            mp_open(new_path);
        }
    }

    prev_a = a; prev_b = b; prev_left = left; prev_right = right;
    prev_up = up; prev_down = down;
    prev_l = l; prev_r = r; prev_start = start;

    return 0;
}

static void mp_read_and_decode_audio(void) {
    if (!mp_file) return;

    switch (mp_format) {
        case MP_FMT_MP3:
            mp_read_audio_mp3();
            break;
        case MP_FMT_WAV_PCM:
            mp_read_audio_pcm();
            break;
        case MP_FMT_WAV_ADPCM:
        case MP_FMT_RAW_ADPCM:
            mp_read_audio_adpcm();
            break;
    }
}

static void mp_output_audio(void) {
    if (!mp_audio_batch_cb || mp_aring_count == 0) return;

    // v71: Safety checks for ring buffer
    if (!mp_audio_ring) return;
    if (mp_aring_read < 0 || mp_aring_read >= MP_AUDIO_RING_SIZE) {
        mp_aring_read = 0;  // Reset if corrupted
    }
    if (mp_aring_write < 0 || mp_aring_write >= MP_AUDIO_RING_SIZE) {
        mp_aring_write = 0;  // Reset if corrupted
    }
    if (mp_aring_count < 0 || mp_aring_count > MP_AUDIO_RING_SIZE) {
        mp_aring_count = 0;  // Reset if corrupted
    }

    /*
     * v66: TIME-BASED AUDIO OUTPUT (no stretching)
     * Calculate samples based on elapsed time - automatically adapts to actual FPS.
     * At 12 FPS: ~1837 samples/frame. At 30 FPS: ~735 samples/frame.
     * NO STRETCHING - if buffer empty, just skip (natural catch-up next frame).
     */
    #define MP_OUTPUT_RATE 22050

#ifdef SF2000
    uint32_t now = os_get_tick_count();
#else
    uint32_t now = 0;
#endif

    uint32_t delta_ms;
    if (mp_last_output_time == 0) {
        delta_ms = 33;  // First call - assume ~30fps
    } else {
        delta_ms = now - mp_last_output_time;
        if (delta_ms == 0) delta_ms = 1;
        // v71: More aggressive clamping to prevent huge audio chunks after long pause
        if (delta_ms > 100) delta_ms = 100;
    }
    mp_last_output_time = now;

    // Calculate samples: samples = time_ms * sample_rate / 1000
    int output_samples = (delta_ms * MP_OUTPUT_RATE) / 1000;
    if (output_samples <= 0) return;
    if (output_samples > MP_MAX_AUDIO_BUFFER) output_samples = MP_MAX_AUDIO_BUFFER;

    // Get the actual sample rate of the decoded audio
    int source_rate = mp_sample_rate;
    if (mp_format == MP_FMT_MP3 && mp_mp3_detected_samplerate > 0) {
        source_rate = mp_mp3_detected_samplerate;
    }

    // Fixed-point resampling ratio: source_rate / output_rate (scaled by 65536)
    uint32_t ratio_fp = ((uint32_t)source_rate << 16) / MP_OUTPUT_RATE;

    int out = 0;
    while (out < output_samples && mp_aring_count >= 4) {
        // Read current sample from ring buffer
        int16_t left = mp_audio_ring[mp_aring_read] | (mp_audio_ring[mp_aring_read + 1] << 8);
        int16_t right = mp_audio_ring[mp_aring_read + 2] | (mp_audio_ring[mp_aring_read + 3] << 8);

        // Output this sample
        mp_audio_out_buffer[out * 2] = left;
        mp_audio_out_buffer[out * 2 + 1] = right;
        out++;

        // Advance accumulator by ratio
        mp_resample_acc += ratio_fp;

        // Consume source samples based on accumulator overflow
        while (mp_resample_acc >= 65536 && mp_aring_count >= 4) {
            mp_resample_acc -= 65536;
            mp_aring_read = (mp_aring_read + 4) % MP_AUDIO_RING_SIZE;
            mp_aring_count -= 4;
        }
    }

    // Output what we have (no stretching - natural frame-based sync)
    if (out > 0) {
        mp_samples_played += out;

        // v61: Mute samples after seek to avoid crackle
        if (mp_audio_mute_samples > 0) {
            int mute_count = (out < mp_audio_mute_samples) ? out : mp_audio_mute_samples;
            for (int i = 0; i < mute_count * 2; i++) {
                mp_audio_out_buffer[i] = 0;
            }
            mp_audio_mute_samples -= mute_count;
        }

        mp_audio_batch_cb(mp_audio_out_buffer, out);
    }
}

// Draw progress bar inside the window panel
static void mp_draw_progress_bar(uint16_t *fb, int bar_x, int bar_y, int bar_w, int bar_h, uint16_t col_bg, uint16_t col_fg, uint16_t col_border) {
    // Background
    mp_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, col_bg);

    // Progress fill
    if (mp_data_size > 0) {
        uint32_t pos = mp_file_pos - mp_data_offset;
        int progress_w = (int)((uint64_t)pos * (bar_w - 4) / mp_data_size);
        if (progress_w > bar_w - 4) progress_w = bar_w - 4;
        if (progress_w > 0) {
            mp_fill_rect(fb, bar_x + 2, bar_y + 2, progress_w, bar_h - 4, col_fg);
        }
    }

    // Border
    for (int i = 0; i < bar_w; i++) {
        int y1 = bar_y;
        int y2 = bar_y + bar_h - 1;
        if (y1 >= 0 && y1 < SCREEN_HEIGHT && bar_x + i >= 0 && bar_x + i < SCREEN_WIDTH)
            fb[y1 * SCREEN_WIDTH + bar_x + i] = col_border;
        if (y2 >= 0 && y2 < SCREEN_HEIGHT && bar_x + i >= 0 && bar_x + i < SCREEN_WIDTH)
            fb[y2 * SCREEN_WIDTH + bar_x + i] = col_border;
    }
    for (int i = 0; i < bar_h; i++) {
        int y = bar_y + i;
        if (y >= 0 && y < SCREEN_HEIGHT) {
            if (bar_x >= 0 && bar_x < SCREEN_WIDTH)
                fb[y * SCREEN_WIDTH + bar_x] = col_border;
            if (bar_x + bar_w - 1 >= 0 && bar_x + bar_w - 1 < SCREEN_WIDTH)
                fb[y * SCREEN_WIDTH + bar_x + bar_w - 1] = col_border;
        }
    }
}

void mp_render(uint16_t *framebuffer) {
    if (!mp_active) return;

    // v66: Decode MULTIPLE chunks per frame to keep buffer full
    for (int i = 0; i < 8 && !mp_paused && mp_aring_count < MP_AUDIO_RING_SIZE * 3 / 4; i++) {
        mp_read_and_decode_audio();
    }

    // Output audio
    if (!mp_paused) {
        mp_output_audio();
    }

    // Check for end of file (with protection against repeated handling)
    // v57: Universal protection - always pause if something goes wrong
    if (!mp_paused && !mp_eof_pending &&
        mp_file_pos >= mp_data_offset + mp_data_size && mp_aring_count < 256) {

        mp_eof_pending = 1;  // Prevent repeated EOF handling

        // Track ended
        switch (mp_play_mode) {
            case MP_PLAY_MODE_REPEAT:
                // v59: Try to play next track, if no next track exists loop current
                if (mp_playlist_count > 1 && !mp_load_next_az()) {
                    // Failed to load next - loop current track
                    mp_file_pos = mp_data_offset;
                    mp_aring_read = 0;
                    mp_aring_write = 0;
                    mp_aring_count = 0;
                    mp_samples_played = 0;
                    mp_last_output_time = 0;
                    mp_audio_acc_us = 0;
                    if (mp_format == MP_FMT_MP3) mp_mp3_reset();
                } else if (mp_playlist_count <= 1) {
                    // Only one track - loop it
                    mp_file_pos = mp_data_offset;
                    mp_aring_read = 0;
                    mp_aring_write = 0;
                    mp_aring_count = 0;
                    mp_samples_played = 0;
                    mp_last_output_time = 0;
                    mp_audio_acc_us = 0;
                    if (mp_format == MP_FMT_MP3) mp_mp3_reset();
                }
                mp_eof_pending = 0;  // Clear for next play
                break;

            case MP_PLAY_MODE_ONCE:
                mp_paused = 1;
                mp_eof_pending = 0;
                break;

            case MP_PLAY_MODE_AZ:
                if (!mp_load_next_az()) {
                    // Failed to load next - pause instead of freeze
                    mp_paused = 1;
                }
                mp_eof_pending = 0;
                break;

            case MP_PLAY_MODE_SHUFFLE:
                if (!mp_load_shuffle()) {
                    // Failed to load - pause instead of freeze
                    mp_paused = 1;
                }
                mp_eof_pending = 0;
                break;

            default:
                // v57: Unknown mode - safety pause
                mp_paused = 1;
                mp_eof_pending = 0;
                break;
        }
    }

    // v57: Additional safety - if we're somehow stuck at EOF without handling, force pause
    if (!mp_paused && mp_file_pos >= mp_data_offset + mp_data_size && mp_aring_count == 0) {
        mp_paused = 1;
        mp_eof_pending = 0;
    }

    // v64: Draw background with animation first
    render_clear_screen_gfx(framebuffer);

    // v64: Advance and apply animation overlay
    if (gfx_theme_is_animated()) {
        gfx_theme_advance_animation();
    }
    gfx_theme_apply_overlay(framebuffer);

    // Get theme colors (same as video_browser)
    uint16_t col_bg = theme_legend_bg();
    uint16_t col_text = theme_text();
    uint16_t col_accent = theme_select_bg();
    // Make a dimmer version for secondary text
    int r = (col_text >> 11) & 0x1F;
    int g = (col_text >> 5) & 0x3F;
    int b = col_text & 0x1F;
    uint16_t col_dim = ((r * 2 / 3) << 11) | ((g * 2 / 3) << 5) | (b * 2 / 3);

    // Window dimensions (same as video_browser)
    int win_x = MP_WIN_X;
    int win_y = MP_WIN_Y;
    int win_w = MP_WIN_W;
    int win_h = MP_WIN_H;
    int radius = MP_WIN_RADIUS;

    // Draw semi-transparent rounded background (90% opacity like video_browser)
    mp_draw_rounded_rect_alpha(framebuffer, win_x, win_y, win_w, win_h, radius, col_bg, 230);

    // Credits view
    if (mp_ui_mode == MP_UI_CREDITS) {
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 6, "FrogMP - Credits", col_text);

        // Separator line
        mp_fill_rect(framebuffer, win_x + 6, win_y + 22, win_w - 12, 1, col_text);

        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 30, "FrogMP Music Player", col_text);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 46, "by @the_q_dev (Telegram)", col_dim);

        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 66, "Libraries (GPL v2):", col_text);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 82, "* libmad - MPEG Audio", col_dim);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 14, win_y + 98, "Underbit Technologies", col_dim);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 114, "* GSPlayer wrapper", col_dim);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 14, win_y + 130, "Y.Nagamidori", col_dim);

        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 150, "Greetings:", col_text);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 166, "Maciek, Madzia, Tomek", col_dim);

        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + win_h - 14, "A/START: Back", col_dim);
        return;
    }

    // Title
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 6, "FrogMP", col_text);

    // Separator line
    mp_fill_rect(framebuffer, win_x + 6, win_y + 22, win_w - 12, 1, col_text);

    // v57: Filename with scrolling for long names
    // v59: Proper font measurement and end pause
    {
        int title_area_width = win_w - 20;  // 10px padding on each side
        int full_title_width = builtin_measure_text(mp_current_filename);

        if (full_title_width <= title_area_width) {
            // Title fits - no scrolling needed
            builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 30,
                          mp_current_filename, col_text);
            mp_title_scroll_offset = 0;
            mp_title_scroll_delay = 0;
            mp_title_at_end = 0;
        } else {
            // Title doesn't fit - need to scroll
            int title_len = strlen(mp_current_filename);

            // v59: Calculate max scroll using font_measure_text
            // Find the first character position where the remaining text fits
            int max_scroll = 0;
            for (int i = 0; i < title_len; i++) {
                int remaining_width = builtin_measure_text(mp_current_filename + i);
                if (remaining_width <= title_area_width) {
                    max_scroll = i;
                    break;
                }
                max_scroll = i;
            }

            // Update scroll state
            if (mp_title_scroll_delay < MP_TITLE_SCROLL_DELAY) {
                mp_title_scroll_delay++;
            } else if (mp_title_at_end) {
                // v59: Pause at end before resetting
                mp_title_end_timer++;
                if (mp_title_end_timer >= MP_TITLE_SCROLL_END_DELAY) {
                    mp_title_end_timer = 0;
                    mp_title_scroll_offset = 0;
                    mp_title_scroll_delay = 0;
                    mp_title_at_end = 0;
                }
            } else {
                // Scroll every MP_TITLE_SCROLL_SPEED frames
                mp_title_scroll_timer++;
                if (mp_title_scroll_timer >= MP_TITLE_SCROLL_SPEED) {
                    mp_title_scroll_timer = 0;
                    mp_title_scroll_offset++;
                    if (mp_title_scroll_offset >= max_scroll) {
                        // v59: Reached end - pause before resetting
                        mp_title_scroll_offset = max_scroll;
                        mp_title_at_end = 1;
                    }
                }
            }

            // v59: Build display string that fits using font_measure_text
            char display_name[128];
            int start_char = mp_title_scroll_offset;
            if (start_char > title_len) start_char = 0;

            // Copy characters until we exceed the display width
            int display_idx = 0;
            int accum_width = 0;
            for (int c = start_char; c < title_len && display_idx < 127; c++) {
                char ch[2] = {mp_current_filename[c], '\0'};
                int ch_width = builtin_measure_text(ch);
                if (accum_width + ch_width > title_area_width) {
                    break;
                }
                display_name[display_idx++] = mp_current_filename[c];
                accum_width += ch_width;
            }
            display_name[display_idx] = '\0';

            builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 30,
                          display_name, col_text);
        }
    }

    // Format info
    char info[64];
    const char *fmt_name = "Unknown";
    switch (mp_format) {
        case MP_FMT_MP3: fmt_name = "MP3"; break;
        case MP_FMT_WAV_PCM: fmt_name = "WAV PCM"; break;
        case MP_FMT_WAV_ADPCM: fmt_name = "WAV ADPCM"; break;
        case MP_FMT_RAW_ADPCM: fmt_name = "RAW ADPCM"; break;
    }
    snprintf(info, sizeof(info), "%s  %dHz  %s", fmt_name, mp_sample_rate, mp_channels == 1 ? "Mono" : "Stereo");
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 50, info, col_dim);

    // Status (PLAYING/PAUSED) - centered
    const char *status = mp_paused ? "PAUSED" : "PLAYING";
    int status_width = builtin_measure_text(status);
    int status_x = win_x + (win_w - status_width) / 2;
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, status_x, win_y + 80, status, mp_paused ? col_dim : col_accent);

    // Position/Duration - centered
    int pos_sec = mp_get_position_seconds();
    int dur_sec = mp_get_duration_seconds();
    snprintf(info, sizeof(info), "%d:%02d / %d:%02d", pos_sec / 60, pos_sec % 60, dur_sec / 60, dur_sec % 60);
    int time_width = builtin_measure_text(info);
    int time_x = win_x + (win_w - time_width) / 2;
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, time_x, win_y + 100, info, col_text);

    // Progress bar
    int bar_x = win_x + 10;
    int bar_y = win_y + 120;
    int bar_w = win_w - 20;
    int bar_h = 14;
    // Use dark version of bg for bar background, accent for progress
    int bg_r = (col_bg >> 11) & 0x1F;
    int bg_g = (col_bg >> 5) & 0x3F;
    int bg_b = col_bg & 0x1F;
    uint16_t bar_bg = ((bg_r / 2) << 11) | ((bg_g / 2) << 5) | (bg_b / 2);
    mp_draw_progress_bar(framebuffer, bar_x, bar_y, bar_w, bar_h, bar_bg, col_accent, col_text);

    // Playlist info
    if (mp_playlist_count > 1) {
        snprintf(info, sizeof(info), "Track %d/%d", mp_playlist_current + 1, mp_playlist_count);
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 10, win_y + 145, info, col_dim);
    }

    // v69: Winamp-style centered control buttons (no text labels)
    int icon_size = 12;
    int icon_gap = 6;
    int total_icons_width = icon_size * 4 + icon_gap * 3;  // prev, play/pause, next, + one more
    int icon_y = win_y + win_h - 32;
    int ix = win_x + (win_w - total_icons_width) / 2;  // Center icons

    // Previous track (LEFT)
    mp_draw_icon_prev(framebuffer, ix, icon_y, icon_size, col_dim);
    ix += icon_size + icon_gap;

    // Play/Pause (A) - highlight current state
    if (mp_paused) {
        mp_draw_icon_play(framebuffer, ix, icon_y, icon_size, col_accent);
    } else {
        mp_draw_icon_pause(framebuffer, ix, icon_y, icon_size, col_accent);
    }
    ix += icon_size + icon_gap;

    // Next track (RIGHT)
    mp_draw_icon_next(framebuffer, ix, icon_y, icon_size, col_dim);

    // Bottom help text - show actual controls
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, win_x + 6, win_y + win_h - 16,
                      "</>:Trk ^v:Seek A:Play B:X Sel:BG", col_dim);
}

int mp_get_duration_seconds(void) {
    int sr = mp_sample_rate;
    if (mp_format == MP_FMT_MP3 && mp_mp3_detected_samplerate > 0) {
        sr = mp_mp3_detected_samplerate;
    }
    if (sr == 0) return 0;

    // Use bytes_per_sec helper for all formats
    int bytes_per_sec = mp_get_bytes_per_sec();
    if (bytes_per_sec <= 0) return 0;

    int duration = (int)(mp_data_size / bytes_per_sec);

    // v57: Workaround for 44.1/48kHz stereo MP3 showing 2x duration
    // The bitrate detection seems consistently off by 2x for these formats
    if (mp_format == MP_FMT_MP3) {
        int ch = (mp_mp3_detected_channels > 0) ? mp_mp3_detected_channels : mp_channels;
        if (ch == 2 && (sr == 44100 || sr == 48000)) {
            duration = duration / 2;
        }
    }

    return duration;
}

int mp_get_position_seconds(void) {
    // mp_samples_played counts OUTPUT samples at 22050 Hz (SF2000 fixed rate)
    // So we always divide by 22050, not by source sample rate
    #define MP_OUTPUT_SAMPLE_RATE 22050
    return (int)(mp_samples_played / MP_OUTPUT_SAMPLE_RATE);
}

int mp_get_sample_rate(void) {
    return mp_sample_rate;
}

int mp_get_channels(void) {
    return mp_channels;
}

const char *mp_get_format_name(void) {
    switch (mp_format) {
        case MP_FMT_MP3: return "MP3";
        case MP_FMT_WAV_PCM: return "WAV PCM";
        case MP_FMT_WAV_ADPCM: return "WAV ADPCM";
        case MP_FMT_RAW_ADPCM: return "RAW ADPCM";
        default: return "Unknown";
    }
}

const char *mp_get_current_path(void) {
    return mp_current_path;
}

const char *mp_get_current_filename(void) {
    return mp_current_filename;
}

int mp_get_next_track_request(void) {
    return mp_next_track_request;
}

void mp_clear_next_track_request(void) {
    mp_next_track_request = 0;
}

const char *mp_get_current_dir(void) {
    return mp_current_dir;
}

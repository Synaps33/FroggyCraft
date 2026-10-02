/*
 * FrogPMP - Video player for FrogUI
 * - MPEG-4 Part 2 (XviD/DivX) support via Xvid library
 * - MP3/PCM/ADPCM audio support
 * - Supports 3+ hour videos at 30fps (360000 frames)
 * - Full menu UI with seek, color modes, play modes
 * - Visual feedback icons for all controls
 * - Key lock (L+R)
 * by Grzegorz Korycki
 */

#include "video_player.h"
#include "music_player.h"  // v69: For pausing music during video playback
#include "xvid/xvid.h"
#include "libmad/libmad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Directory scanning for play mode A-Z and Shuffle
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

// Maximum frames we can index (supports 3+ hours at 30fps)
#define VP_MAX_FRAMES 360000
#define VP_MAX_AUDIO_CHUNKS 360000

// Maximum frame data size
#define VP_MAX_FRAME_SIZE (480 * 320 * 2)

// Audio settings
#define VP_AUDIO_RING_SIZE (44100 * 4)  // ~1 second at 44kHz stereo
#define VP_AUDIO_REFILL_THRESHOLD (VP_AUDIO_RING_SIZE / 2)
#define VP_MAX_AUDIO_BUFFER 4096

// Audio format constants
#define VP_AUDIO_FMT_PCM    1
#define VP_AUDIO_FMT_ADPCM  2
#define VP_AUDIO_FMT_MP3    3

// Menu items - 8 items
#define VP_MENU_ITEMS 8
#define VP_MENU_GO_TO_POS   0
#define VP_MENU_COLOR_MODE  1
#define VP_MENU_XVID_RANGE  2
#define VP_MENU_PLAY_MODE   3
#define VP_MENU_SHOW_TIME   4
#define VP_MENU_SAVE        5
#define VP_MENU_INSTRUCTIONS 6
#define VP_MENU_ABOUT       7

// Settings file path (separate from pmp123)
#define VP_SETTINGS_FILE "/mnt/sda1/ROMS/.frogpmp.cfg"

// Visual feedback icons
#define VP_ICON_NONE 0
#define VP_ICON_SKIP_LEFT 1
#define VP_ICON_SKIP_RIGHT 2
#define VP_ICON_PAUSE 3
#define VP_ICON_PLAY 4
#define VP_ICON_LOCK 5
#define VP_ICON_UNLOCK 6
#define VP_ICON_SKIP_BACK_1M 7
#define VP_ICON_SKIP_FWD_1M 8
#define VP_ICON_FRAMES 30  // show icon for 1 second

// Color processing modes
#define VP_COLOR_MODE_NORMAL     0
#define VP_COLOR_MODE_LIFTED16   1
#define VP_COLOR_MODE_LIFTED32   2
#define VP_COLOR_MODE_GAMMA_1_2  3
#define VP_COLOR_MODE_GAMMA_1_5  4
#define VP_COLOR_MODE_GAMMA_1_8  5
#define VP_COLOR_MODE_DITHERED   6
#define VP_COLOR_MODE_DITHER2    7
#define VP_COLOR_MODE_WARM       8
#define VP_COLOR_MODE_WARM_PLUS  9
#define VP_COLOR_MODE_NIGHT      10
#define VP_COLOR_MODE_NIGHT_PLUS 11
#define VP_COLOR_MODE_NIGHT_DITHER 12
#define VP_COLOR_MODE_NIGHT_DITHER2 13
#define VP_COLOR_MODE_LEGACY     14
#define VP_COLOR_MODE_COUNT      15

// Xvid black level (output range)
#define VP_XVID_BLACK_TV   0   // Output 0-255: expand source 16-235 to full range
#define VP_XVID_BLACK_PC   1   // Output 16-235: keep source as-is (limited range)

// Play modes
#define VP_PLAY_MODE_REPEAT   0
#define VP_PLAY_MODE_ONCE     1
#define VP_PLAY_MODE_AZ       2
#define VP_PLAY_MODE_SHUFFLE  3
#define VP_PLAY_MODE_COUNT    4

// Key lock timing
#define VP_LOCK_HOLD_FRAMES (30 * 2)  // 2 seconds at 30fps

// AVI state
static FILE *vp_file = NULL;
static uint32_t *vp_frame_offsets = NULL;
static uint32_t *vp_frame_sizes = NULL;
static int vp_total_frames = 0;
static int vp_current_frame = 0;
static int vp_video_width = 0;
static int vp_video_height = 0;

// Audio chunk index
static uint32_t *vp_audio_offsets = NULL;
static uint32_t *vp_audio_sizes = NULL;
static int vp_total_audio_chunks = 0;
static uint32_t vp_total_audio_bytes = 0;

// XVID decoder state
static void *vp_xvid_handle = NULL;
static int vp_xvid_initialized = 0;

// Frame buffers
static uint8_t *vp_frame_buffer = NULL;
static uint8_t *vp_yuv_buffer = NULL;
static uint8_t *vp_yuv_y = NULL;
static uint8_t *vp_yuv_u = NULL;
static uint8_t *vp_yuv_v = NULL;

// MPEG-4 extradata (VOL header)
#define VP_MAX_EXTRADATA_SIZE 256
static uint8_t vp_mpeg4_extradata[VP_MAX_EXTRADATA_SIZE];
static int vp_mpeg4_extradata_size = 0;
static int vp_mpeg4_extradata_sent = 0;

// Player state
static int vp_active = 0;
static int vp_paused = 0;

// Frame timing
static uint32_t vp_us_per_frame = 33333;  // default 30fps
static uint32_t vp_clip_fps = 30;

// Repeat timing (like pmp123 - for 15fps content on 30fps display)
static int vp_repeat_count = 1;
static int vp_repeat_counter = 0;

// YUV->RGB lookup tables
static int16_t vp_yuv_rv_table[256];
static int16_t vp_yuv_gu_table[256];
static int16_t vp_yuv_gv_table[256];
static int16_t vp_yuv_bu_table[256];
static int vp_yuv_tables_initialized = 0;

// 4x4 Bayer dithering matrix
static const int8_t vp_bayer4x4[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 }
};

// Audio state
static int vp_has_audio = 0;
static int vp_audio_format = 0;
static int vp_audio_channels = 0;
static int vp_audio_sample_rate = 0;
static int vp_audio_bits = 0;
static int vp_audio_bytes_per_sample = 0;

// Audio position
static int vp_audio_chunk_idx = 0;
static uint32_t vp_audio_chunk_pos = 0;
static uint64_t vp_audio_samples_sent = 0;

// Audio ring buffer
static uint8_t *vp_audio_ring = NULL;
static int vp_aring_read = 0;
static int vp_aring_write = 0;
static int vp_aring_count = 0;

// v61: Audio mute after seek to avoid crackle (increased from 2048)
static int vp_audio_mute_samples = 0;
#define VP_AUDIO_MUTE_AFTER_SEEK 4096  // ~93ms at 44.1kHz - covers 2+ batches

// Audio output buffer
static int16_t vp_audio_out_buffer[VP_MAX_AUDIO_BUFFER * 2];

// Audio callback
static vp_audio_batch_cb_t vp_audio_batch_cb = NULL;

// ADPCM state
static int vp_adpcm_block_align = 0;
static int vp_adpcm_samples_per_block = 0;
static int vp_adpcm_sample1[2] = {0, 0};
static int vp_adpcm_sample2[2] = {0, 0};
static int vp_adpcm_delta[2] = {0, 0};
static int vp_adpcm_coef_idx[2] = {0, 0};

// MS ADPCM tables
static const int vp_adpcm_adapt_table[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};
static const int vp_adpcm_coef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
static const int vp_adpcm_coef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

// ADPCM decode buffer
#define VP_ADPCM_DECODE_BUF_SIZE 16384
static int16_t vp_adpcm_decode_buf[VP_ADPCM_DECODE_BUF_SIZE];
static uint8_t vp_adpcm_read_buf[8192];

// MP3 decoder state
static void *vp_mp3_handle = NULL;
static int vp_mp3_initialized = 0;
static int vp_mp3_detected_samplerate = 0;
static int vp_mp3_detected_channels = 0;

// MP3 input buffer
#define VP_MP3_INPUT_BUF_SIZE 8192
static uint8_t vp_mp3_input_buf[VP_MP3_INPUT_BUF_SIZE];
static int vp_mp3_input_len = 0;
static int vp_mp3_input_remaining = 0;

// MP3 decode buffer
#define VP_MP3_DECODE_BUF_SIZE 8192
static int16_t vp_mp3_decode_buf[VP_MP3_DECODE_BUF_SIZE];

// Menu state
static int vp_menu_active = 0;
static int vp_menu_selection = 0;
static int vp_seek_position = 0;  // 0-20 (0%, 5%, ... 100%)
static int vp_was_paused_before_menu = 0;
static int vp_submenu_active = 0;  // 0=none, 1=instructions, 2=about
static int vp_color_submenu_active = 0;
static int vp_color_submenu_scroll = 0;
static int vp_save_feedback_timer = 0;
#define VP_SAVE_FEEDBACK_FRAMES 60

// Color mode and settings
static int vp_color_mode = VP_COLOR_MODE_NORMAL;
static int vp_xvid_black_level = VP_XVID_BLACK_TV;
static int vp_play_mode = VP_PLAY_MODE_REPEAT;
static int vp_show_time = 1;
static int vp_show_debug = 0;

// Menu labels - 8 items
static const char *vp_menu_labels[VP_MENU_ITEMS] = {
    "Go to Position",  /* 0 */
    "Color Mode",      /* 1 */
    "Xvid Range",      /* 2 */
    "Play Mode",       /* 3 */
    "Show Time",       /* 4 */
    "Save Settings",   /* 5 */
    "Instructions",    /* 6 */
    "About"            /* 7 */
};

// Color mode names - exact copy from pmp123
static const char *vp_color_mode_names[VP_COLOR_MODE_COUNT] = {
    "Unchanged", "Lift 16", "Lift 32",
    "Gamma 1.2", "Gamma 1.5", "Gamma 1.8",
    "Dithered", "Dither2",
    "Warm", "Warm+", "Night", "Night+",
    "Night+Dith", "Night+Dith2", "Legacy"
};

// Play mode names - exact copy from pmp123
static const char *vp_play_mode_names[VP_PLAY_MODE_COUNT] = {
    "Repeat", "Play Once", "Play A-Z", "Shuffle"
};

// Visual feedback icon state
static int vp_icon_type = VP_ICON_NONE;
static int vp_icon_timer = 0;

// Key lock state
static int vp_is_locked = 0;
static int vp_lock_hold_counter = 0;

// Current video path for play mode (A-Z, Shuffle)
#define VP_MAX_PATH 512
static char vp_current_path[VP_MAX_PATH];
static char vp_current_dir[VP_MAX_PATH];
static int vp_next_video_requested = 0;  // 1 = load next, 2 = load random

// v61: Resume playback - remember last played file and position (in memory)
static char vp_resume_path[VP_MAX_PATH] = {0};
static int vp_resume_frame = 0;

// Simple random for shuffle
static uint32_t vp_shuffle_seed = 12345;
static uint32_t vp_shuffle_rand(void) {
    vp_shuffle_seed = vp_shuffle_seed * 1103515245 + 12345;
    return (vp_shuffle_seed >> 16) & 0x7FFF;
}

// Playlist for A-Z and Shuffle modes
#define VP_MAX_PLAYLIST 256
#define VP_MAX_FILENAME 128
static char vp_playlist[VP_MAX_PLAYLIST][VP_MAX_FILENAME];
static int vp_playlist_count = 0;
static int vp_playlist_current = -1;

// Case-insensitive string comparison
static int vp_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

// Check if filename is a video file (case-insensitive)
static int vp_is_video_file(const char *name) {
    int len = strlen(name);
    if (len < 5) return 0;
    const char *ext = name + len - 4;
    // Check .avi
    if ((ext[0] == '.' || ext[0] == '.') &&
        (ext[1] == 'a' || ext[1] == 'A') &&
        (ext[2] == 'v' || ext[2] == 'V') &&
        (ext[3] == 'i' || ext[3] == 'I')) return 1;
    // Check .pmp
    if ((ext[0] == '.' || ext[0] == '.') &&
        (ext[1] == 'p' || ext[1] == 'P') &&
        (ext[2] == 'm' || ext[2] == 'M') &&
        (ext[3] == 'p' || ext[3] == 'P')) return 1;
    return 0;
}

// Bubble sort playlist case-insensitively
static void vp_sort_playlist(void) {
    for (int i = 0; i < vp_playlist_count - 1; i++) {
        for (int j = 0; j < vp_playlist_count - i - 1; j++) {
            if (vp_strcasecmp(vp_playlist[j], vp_playlist[j + 1]) > 0) {
                char temp[VP_MAX_FILENAME];
                strncpy(temp, vp_playlist[j], VP_MAX_FILENAME - 1);
                strncpy(vp_playlist[j], vp_playlist[j + 1], VP_MAX_FILENAME - 1);
                strncpy(vp_playlist[j + 1], temp, VP_MAX_FILENAME - 1);
            }
        }
    }
}

// Scan directory and build playlist
static void vp_scan_playlist(void) {
    vp_playlist_count = 0;
    vp_playlist_current = -1;

    if (vp_current_dir[0] == '\0') return;

    // Extract current filename from path
    char current_filename[VP_MAX_FILENAME] = "";
    const char *slash = strrchr(vp_current_path, '/');
    if (slash) {
        strncpy(current_filename, slash + 1, VP_MAX_FILENAME - 1);
        current_filename[VP_MAX_FILENAME - 1] = '\0';
    }

#ifdef SF2000
    // SF2000 file system
    union {
        struct {
            uint32_t type;
        };
        struct {
            uint8_t _2[0x22];
            char d_name[0x225];
        };
        uint8_t __[0x428];
    } buffer;

    int dir_fd = fs_opendir(vp_current_dir);
    if (dir_fd < 0) return;

    while (vp_playlist_count < VP_MAX_PLAYLIST) {
        memset(&buffer, 0, sizeof(buffer));
        if (fs_readdir(dir_fd, &buffer) < 0) break;

        // Skip . and ..
        if (buffer.d_name[0] == '.') continue;

        // Skip directories
        if (S_ISDIR(buffer.type)) continue;

        // Check if video file
        if (!vp_is_video_file(buffer.d_name)) continue;

        strncpy(vp_playlist[vp_playlist_count], buffer.d_name, VP_MAX_FILENAME - 1);
        vp_playlist[vp_playlist_count][VP_MAX_FILENAME - 1] = '\0';
        vp_playlist_count++;
    }

    fs_closedir(dir_fd);
#else
    // Standard POSIX
    DIR *dir = opendir(vp_current_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && vp_playlist_count < VP_MAX_PLAYLIST) {
        // Skip . and ..
        if (entry->d_name[0] == '.') continue;

        // Skip directories
        if (entry->d_type == DT_DIR) continue;

        // Check if video file
        if (!vp_is_video_file(entry->d_name)) continue;

        strncpy(vp_playlist[vp_playlist_count], entry->d_name, VP_MAX_FILENAME - 1);
        vp_playlist[vp_playlist_count][VP_MAX_FILENAME - 1] = '\0';
        vp_playlist_count++;
    }

    closedir(dir);
#endif

    // Sort playlist case-insensitively
    vp_sort_playlist();

    // Find current file in playlist
    for (int i = 0; i < vp_playlist_count; i++) {
        if (vp_strcasecmp(vp_playlist[i], current_filename) == 0) {
            vp_playlist_current = i;
            break;
        }
    }
}

// Load next video (A-Z order)
static int vp_load_next_az(void) {
    if (vp_playlist_count <= 1) return 0;
    if (vp_playlist_current < 0) vp_scan_playlist();
    if (vp_playlist_current < 0) return 0;

    // Get next index (wrap around)
    int next_idx = (vp_playlist_current + 1) % vp_playlist_count;

    // Build full path
    char new_path[VP_MAX_PATH];
    snprintf(new_path, VP_MAX_PATH, "%s/%s", vp_current_dir, vp_playlist[next_idx]);

    // Close current and open new
    vp_close();
    return vp_open(new_path);
}

// Load random video (Shuffle)
static int vp_load_shuffle(void) {
    if (vp_playlist_count <= 1) return 0;
    if (vp_playlist_current < 0) vp_scan_playlist();
    if (vp_playlist_count <= 1) return 0;

    // Pick random index different from current
    int new_idx;
    int attempts = 0;
    do {
        new_idx = vp_shuffle_rand() % vp_playlist_count;
        attempts++;
    } while (new_idx == vp_playlist_current && attempts < 20);

    // Build full path
    char new_path[VP_MAX_PATH];
    snprintf(new_path, VP_MAX_PATH, "%s/%s", vp_current_dir, vp_playlist[new_idx]);

    // Close current and open new
    vp_close();
    return vp_open(new_path);
}

// Gamma lookup tables for color modes
static uint8_t vp_gamma_r5[VP_COLOR_MODE_COUNT][32];
static uint8_t vp_gamma_g6[VP_COLOR_MODE_COUNT][64];
static uint8_t vp_gamma_b5[VP_COLOR_MODE_COUNT][32];
static int vp_gamma_tables_initialized = 0;

// Two Y tables for TV/PC range selection
static int16_t vp_yuv_y_table_tv[256];
static int16_t vp_yuv_y_table_pc[256];

// Input edge detection
static int vp_prev_a = 0;
static int vp_prev_b = 0;
static int vp_prev_left = 0;
static int vp_prev_right = 0;
static int vp_prev_start = 0;
static int vp_prev_up = 0;
static int vp_prev_down = 0;
static int vp_prev_l = 0;
static int vp_prev_r = 0;

// 5x7 font (from pmp123)
static const unsigned char vp_font[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},{0x08,0x1C,0x2A,0x08,0x08},
};

// Framebuffer pointer (cached during render)
static uint16_t *vp_fb = NULL;

// Helper functions
static inline uint32_t vp_read_u32_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static inline uint16_t vp_read_u16_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8);
}

static int vp_check4(FILE *fp, const char *tag) {
    char buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    return (buf[0] == tag[0] && buf[1] == tag[1] &&
            buf[2] == tag[2] && buf[3] == tag[3]);
}

static int vp_read32(FILE *fp, uint32_t *val) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, fp) != 4) return -1;
    *val = vp_read_u32_le(buf);
    return 0;
}

static inline int16_t vp_clamp16(int v) {
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return (int16_t)v;
}

// Initialize gamma lookup tables for color modes
static void vp_init_gamma_tables(void) {
    if (vp_gamma_tables_initialized) return;

    // 5-bit tables (R and B)
    for (int i = 0; i < 32; i++) {
        float norm = i / 31.0f;
        int boosted;

        // Normal - identity
        vp_gamma_r5[VP_COLOR_MODE_NORMAL][i] = i;
        vp_gamma_b5[VP_COLOR_MODE_NORMAL][i] = i;
        // Lifted 16
        vp_gamma_r5[VP_COLOR_MODE_LIFTED16][i] = 4 + (i * 27) / 31;
        vp_gamma_b5[VP_COLOR_MODE_LIFTED16][i] = 4 + (i * 27) / 31;
        // Lifted 32
        vp_gamma_r5[VP_COLOR_MODE_LIFTED32][i] = 8 + (i * 23) / 31;
        vp_gamma_b5[VP_COLOR_MODE_LIFTED32][i] = 8 + (i * 23) / 31;
        // Gamma modes - use simple approximation without powf
        vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_2][i] = (int)(31.0f * (norm * 0.833f + norm * norm * 0.167f) + 0.5f);
        vp_gamma_b5[VP_COLOR_MODE_GAMMA_1_2][i] = vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_2][i];
        vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_5][i] = (int)(31.0f * (norm * 0.667f + norm * norm * 0.333f) + 0.5f);
        vp_gamma_b5[VP_COLOR_MODE_GAMMA_1_5][i] = vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_5][i];
        vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_8][i] = (int)(31.0f * (norm * 0.556f + norm * norm * 0.444f) + 0.5f);
        vp_gamma_b5[VP_COLOR_MODE_GAMMA_1_8][i] = vp_gamma_r5[VP_COLOR_MODE_GAMMA_1_8][i];
        // Dithered - identity
        vp_gamma_r5[VP_COLOR_MODE_DITHERED][i] = i;
        vp_gamma_b5[VP_COLOR_MODE_DITHERED][i] = i;
        vp_gamma_r5[VP_COLOR_MODE_DITHER2][i] = i;
        vp_gamma_b5[VP_COLOR_MODE_DITHER2][i] = i;
        // Warm - R boost 15%, B reduced 40%
        boosted = (i * 115) / 100; if (boosted > 31) boosted = 31;
        vp_gamma_r5[VP_COLOR_MODE_WARM][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_WARM][i] = (i * 60) / 100;
        // Warm+ - R boost 30%, B reduced 65%
        boosted = (i * 130) / 100; if (boosted > 31) boosted = 31;
        vp_gamma_r5[VP_COLOR_MODE_WARM_PLUS][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_WARM_PLUS][i] = (i * 35) / 100;
        // Night - warm + dimmed to 63%
        boosted = (i * 73) / 100;
        if (boosted > 31) boosted = 31;
        vp_gamma_r5[VP_COLOR_MODE_NIGHT][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_NIGHT][i] = (i * 38) / 100;
        // Night+ - warm + dimmed to 27%
        boosted = (i * 31) / 100;
        if (boosted > 31) boosted = 31;
        vp_gamma_r5[VP_COLOR_MODE_NIGHT_PLUS][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_NIGHT_PLUS][i] = (i * 16) / 100;
        // Night+Dither - same as Night+
        vp_gamma_r5[VP_COLOR_MODE_NIGHT_DITHER][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_NIGHT_DITHER][i] = (i * 16) / 100;
        vp_gamma_r5[VP_COLOR_MODE_NIGHT_DITHER2][i] = boosted;
        vp_gamma_b5[VP_COLOR_MODE_NIGHT_DITHER2][i] = (i * 16) / 100;
        // Legacy - identity
        vp_gamma_r5[VP_COLOR_MODE_LEGACY][i] = i;
        vp_gamma_b5[VP_COLOR_MODE_LEGACY][i] = i;
    }

    // 6-bit table (G)
    for (int i = 0; i < 64; i++) {
        vp_gamma_g6[VP_COLOR_MODE_NORMAL][i] = i;
        vp_gamma_g6[VP_COLOR_MODE_LIFTED16][i] = 8 + (i * 55) / 63;
        vp_gamma_g6[VP_COLOR_MODE_LIFTED32][i] = 16 + (i * 47) / 63;
        float norm = i / 63.0f;
        vp_gamma_g6[VP_COLOR_MODE_GAMMA_1_2][i] = (int)(63.0f * (norm * 0.833f + norm * norm * 0.167f) + 0.5f);
        vp_gamma_g6[VP_COLOR_MODE_GAMMA_1_5][i] = (int)(63.0f * (norm * 0.667f + norm * norm * 0.333f) + 0.5f);
        vp_gamma_g6[VP_COLOR_MODE_GAMMA_1_8][i] = (int)(63.0f * (norm * 0.556f + norm * norm * 0.444f) + 0.5f);
        vp_gamma_g6[VP_COLOR_MODE_DITHERED][i] = i;
        vp_gamma_g6[VP_COLOR_MODE_DITHER2][i] = i;
        vp_gamma_g6[VP_COLOR_MODE_WARM][i] = (i * 80) / 100;
        vp_gamma_g6[VP_COLOR_MODE_WARM_PLUS][i] = (i * 60) / 100;
        vp_gamma_g6[VP_COLOR_MODE_NIGHT][i] = (i * 50) / 100;
        vp_gamma_g6[VP_COLOR_MODE_NIGHT_PLUS][i] = (i * 19) / 100;
        vp_gamma_g6[VP_COLOR_MODE_NIGHT_DITHER][i] = (i * 19) / 100;
        vp_gamma_g6[VP_COLOR_MODE_NIGHT_DITHER2][i] = (i * 19) / 100;
        vp_gamma_g6[VP_COLOR_MODE_LEGACY][i] = i;
    }

    vp_gamma_tables_initialized = 1;
}

// Initialize YUV->RGB tables (TV and PC range)
static void vp_init_yuv_tables(void) {
    if (vp_yuv_tables_initialized) return;

    for (int i = 0; i < 256; i++) {
        // TV/Limited range (16-235 -> 0-255)
        int y_limited = ((i - 16) * 298) >> 8;
        if (y_limited < 0) y_limited = 0;
        if (y_limited > 255) y_limited = 255;
        vp_yuv_y_table_tv[i] = y_limited;

        // PC/Full range (0-255 as-is)
        vp_yuv_y_table_pc[i] = i;

        // U/V contributions - BT.601 coefficients
        int uv = i - 128;
        vp_yuv_rv_table[i] = (1436 * uv) >> 10;
        vp_yuv_gu_table[i] = (-352 * uv) >> 10;
        vp_yuv_gv_table[i] = (-731 * uv) >> 10;
        vp_yuv_bu_table[i] = (1815 * uv) >> 10;
    }
    vp_yuv_tables_initialized = 1;
}

// Drawing functions for menu
static void vp_draw_char(int x, int y, char c, uint16_t col) {
    if (c < 32 || c > 127) c = '?';
    const unsigned char *g = vp_font[c - 32];
    for (int cx = 0; cx < 5; cx++)
        for (int cy = 0; cy < 7; cy++)
            if (g[cx] & (1 << cy))
                if (x+cx < SCREEN_WIDTH && y+cy < SCREEN_HEIGHT && x+cx >= 0 && y+cy >= 0)
                    vp_fb[(y+cy) * SCREEN_WIDTH + x+cx] = col;
}

static int vp_is_font_pixel(const unsigned char *g, int cx, int cy) {
    if (cx < 0 || cx >= 5 || cy < 0 || cy >= 7) return 0;
    return (g[cx] & (1 << cy)) ? 1 : 0;
}

static void vp_draw_char_outline(int x, int y, char c, uint16_t col) {
    if (c < 32 || c > 127) c = '?';
    const unsigned char *g = vp_font[c - 32];
    const uint16_t outline_col = 0x0000;

    // First pass: draw black outline
    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            if (g[cx] & (1 << cy)) {
                for (int d = 0; d < 8; d++) {
                    int ox = cx + dx[d];
                    int oy = cy + dy[d];
                    if (!vp_is_font_pixel(g, ox, oy)) {
                        int px = x + ox;
                        int py = y + oy;
                        if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                            vp_fb[py * SCREEN_WIDTH + px] = outline_col;
                        }
                    }
                }
            }
        }
    }

    // Second pass: draw font pixels
    for (int cx = 0; cx < 5; cx++)
        for (int cy = 0; cy < 7; cy++)
            if (g[cx] & (1 << cy))
                if (x+cx >= 0 && x+cx < SCREEN_WIDTH && y+cy >= 0 && y+cy < SCREEN_HEIGHT)
                    vp_fb[(y+cy) * SCREEN_WIDTH + x+cx] = col;
}

static void vp_draw_str(int x, int y, const char *s, uint16_t col) {
    while (*s) { vp_draw_char_outline(x, y, *s++, col); x += 6; }
}

static void vp_draw_num(int x, int y, int num, uint16_t col) {
    char buf[12]; int i = 0, neg = 0;
    if (num < 0) { neg = 1; num = -num; }
    if (num == 0) buf[i++] = '0';
    else while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; }
    if (neg) buf[i++] = '-';
    while (i > 0) { vp_draw_char_outline(x, y, buf[--i], col); x += 6; }
}

static int vp_num_width(int num) {
    if (num == 0) return 6;
    int digits = 0;
    if (num < 0) { digits++; num = -num; }
    while (num > 0) { digits++; num /= 10; }
    return digits * 6;
}

static uint16_t vp_darken_pixel(uint16_t p) {
    uint16_t r = (p >> 11) & 0x1F;
    uint16_t g = (p >> 5) & 0x3F;
    uint16_t b = p & 0x1F;
    r = r >> 2;
    g = g >> 2;
    b = b >> 2;
    return (r << 11) | (g << 5) | b;
}

static void vp_draw_dark_rect(int x1, int y1, int x2, int y2) {
    for (int y = y1; y <= y2 && y < SCREEN_HEIGHT; y++) {
        for (int x = x1; x <= x2 && x < SCREEN_WIDTH; x++) {
            if (x >= 0 && y >= 0) {
                int idx = y * SCREEN_WIDTH + x;
                vp_fb[idx] = vp_darken_pixel(vp_fb[idx]);
            }
        }
    }
}

static void vp_draw_fill_rect(int x1, int y1, int x2, int y2, uint16_t col) {
    for (int y = y1; y <= y2 && y < SCREEN_HEIGHT; y++) {
        for (int x = x1; x <= x2 && x < SCREEN_WIDTH; x++) {
            if (x >= 0 && y >= 0) {
                vp_fb[y * SCREEN_WIDTH + x] = col;
            }
        }
    }
}

static void vp_draw_rect(int x1, int y1, int x2, int y2, uint16_t col) {
    for (int x = x1; x <= x2 && x < SCREEN_WIDTH; x++) {
        if (x >= 0) {
            if (y1 >= 0 && y1 < SCREEN_HEIGHT) vp_fb[y1 * SCREEN_WIDTH + x] = col;
            if (y2 >= 0 && y2 < SCREEN_HEIGHT) vp_fb[y2 * SCREEN_WIDTH + x] = col;
        }
    }
    for (int y = y1; y <= y2 && y < SCREEN_HEIGHT; y++) {
        if (y >= 0) {
            if (x1 >= 0 && x1 < SCREEN_WIDTH) vp_fb[y * SCREEN_WIDTH + x1] = col;
            if (x2 >= 0 && x2 < SCREEN_WIDTH) vp_fb[y * SCREEN_WIDTH + x2] = col;
        }
    }
}

// Draw circle outline (Bresenham)
static void vp_draw_circle(int cx, int cy, int r, uint16_t col) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        int px, py;
        px = cx + x; py = cy + y; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx - x; py = cy + y; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx + x; py = cy - y; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx - x; py = cy - y; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx + y; py = cy + x; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx - y; py = cy + x; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx + y; py = cy - x; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        px = cx - y; py = cy - x; if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) vp_fb[py * SCREEN_WIDTH + px] = col;
        if (d < 0) { d += 4 * x + 6; } else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

// Draw filled circle
static void vp_draw_filled_circle(int cx, int cy, int r, uint16_t col) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    vp_fb[py * SCREEN_WIDTH + px] = col;
                }
            }
        }
    }
}

// Draw visual feedback icon (like pmp123)
static void vp_draw_icon(int type) {
    int cx, cy;
    uint16_t bg_col = 0x4208;  // dark gray
    uint16_t fg_col = 0xFFFF;  // white

    if (type == VP_ICON_SKIP_LEFT) {
        cx = 60; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        // Draw << double arrow pointing LEFT
        for (int i = 0; i < 10; i++) {
            int px = cx + 5 - i, py1 = cy - (9-i), py2 = cy + (9-i);
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
            px = cx - 5 - i;
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
        }
        vp_draw_str(cx - 9, cy + 30, "15s", fg_col);
    }
    else if (type == VP_ICON_SKIP_RIGHT) {
        cx = 260; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        // Draw >> double arrow pointing RIGHT
        for (int i = 0; i < 10; i++) {
            int px = cx - 5 + i, py1 = cy - (9-i), py2 = cy + (9-i);
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
            px = cx + 5 + i;
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
        }
        vp_draw_str(cx - 9, cy + 30, "15s", fg_col);
    }
    else if (type == VP_ICON_PAUSE) {
        cx = 160; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        // Draw || pause bars
        vp_draw_fill_rect(cx - 8, cy - 10, cx - 4, cy + 10, fg_col);
        vp_draw_fill_rect(cx + 4, cy - 10, cx + 8, cy + 10, fg_col);
    }
    else if (type == VP_ICON_PLAY) {
        cx = 160; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        // Draw > play triangle
        for (int i = 0; i < 14; i++) {
            int px = cx - 5 + i;
            int h = (14 - i) * 10 / 14;
            for (int dy = -h; dy <= h; dy++) {
                int py = cy + dy;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    vp_fb[py * SCREEN_WIDTH + px] = fg_col;
                }
            }
        }
    }
    else if (type == VP_ICON_LOCK || type == VP_ICON_UNLOCK) {
        cx = 160; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        // Key head
        vp_draw_circle(cx, cy - 8, 7, fg_col);
        vp_draw_circle(cx, cy - 8, 6, fg_col);
        vp_draw_filled_circle(cx, cy - 8, 3, bg_col);
        // Key shaft
        vp_draw_fill_rect(cx - 2, cy - 1, cx + 2, cy + 14, fg_col);
        // Key teeth
        vp_draw_fill_rect(cx + 2, cy + 4, cx + 6, cy + 6, fg_col);
        vp_draw_fill_rect(cx + 2, cy + 9, cx + 8, cy + 11, fg_col);
        // X for unlock
        if (type == VP_ICON_UNLOCK) {
            uint16_t x_col = 0xF800;
            for (int i = -10; i <= 10; i++) {
                int px = cx + i, py = cy + i;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    vp_fb[py * SCREEN_WIDTH + px] = x_col;
                    if (py+1 < SCREEN_HEIGHT) vp_fb[(py+1) * SCREEN_WIDTH + px] = x_col;
                }
            }
            for (int i = -10; i <= 10; i++) {
                int px = cx + i, py = cy - i;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    vp_fb[py * SCREEN_WIDTH + px] = x_col;
                    if (py+1 < SCREEN_HEIGHT) vp_fb[(py+1) * SCREEN_WIDTH + px] = x_col;
                }
            }
        }
    }
    else if (type == VP_ICON_SKIP_BACK_1M) {
        cx = 60; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        for (int i = 0; i < 10; i++) {
            int px = cx + 5 - i, py1 = cy - (9-i), py2 = cy + (9-i);
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
            px = cx - 5 - i;
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
        }
        vp_draw_str(cx - 6, cy + 30, "1m", fg_col);
    }
    else if (type == VP_ICON_SKIP_FWD_1M) {
        cx = 260; cy = 120;
        vp_draw_filled_circle(cx, cy, 25, bg_col);
        vp_draw_circle(cx, cy, 25, fg_col);
        for (int i = 0; i < 10; i++) {
            int px = cx - 5 + i, py1 = cy - (9-i), py2 = cy + (9-i);
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
            px = cx + 5 + i;
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (py1 >= 0 && py1 < SCREEN_HEIGHT) vp_fb[py1 * SCREEN_WIDTH + px] = fg_col;
                if (py2 >= 0 && py2 < SCREEN_HEIGHT) vp_fb[py2 * SCREEN_WIDTH + px] = fg_col;
            }
        }
        vp_draw_str(cx - 6, cy + 30, "1m", fg_col);
    }
}

// Check if data at offset is a valid AVI chunk header
static int vp_check_chunk_header(long offset) {
    if (offset < 0 || !vp_file) return 0;
    uint8_t header[4];
    long saved = ftell(vp_file);
    fseek(vp_file, offset, SEEK_SET);
    int ok = 0;
    if (fread(header, 1, 4, vp_file) == 4) {
        if (header[0] >= '0' && header[0] <= '9' &&
            header[1] >= '0' && header[1] <= '9') {
            char c2 = header[2] | 0x20;
            char c3 = header[3] | 0x20;
            if ((c2 == 'd' && c3 == 'c') || (c2 == 'w' && c3 == 'b')) {
                ok = 1;
            }
        }
    }
    fseek(vp_file, saved, SEEK_SET);
    return ok;
}

// Parse idx1 chunk
static int vp_parse_idx1(long movi_data_start) {
    uint8_t tag[4];
    uint32_t chunk_size;

    while (fread(tag, 1, 4, vp_file) == 4) {
        if (vp_read32(vp_file, &chunk_size) != 0) break;

        if (tag[0]=='i' && tag[1]=='d' && tag[2]=='x' && tag[3]=='1') {
            int num_entries = chunk_size / 16;
            long idx_start = ftell(vp_file);

            // Find first video entry to detect offset format
            uint8_t entry[16];
            uint32_t first_video_offset = 0;
            int found_video = 0;

            for (int i = 0; i < num_entries && i < 100; i++) {
                if (fread(entry, 1, 16, vp_file) != 16) break;
                if ((entry[2]=='d' || entry[2]=='D') && (entry[3]=='c' || entry[3]=='C')) {
                    first_video_offset = vp_read_u32_le(entry + 8);
                    found_video = 1;
                    break;
                }
            }

            if (!found_video) {
                fseek(vp_file, idx_start, SEEK_SET);
                return 0;
            }

            // Auto-detect offset format
            long offset_base = 0;
            int add_header = 8;

            if (vp_check_chunk_header(movi_data_start + first_video_offset)) {
                offset_base = movi_data_start;
            } else if (vp_check_chunk_header(first_video_offset)) {
                offset_base = 0;
            } else if (vp_check_chunk_header(movi_data_start - 4 + first_video_offset)) {
                offset_base = movi_data_start - 4;
            } else {
                offset_base = movi_data_start;
            }

            // Parse all entries
            fseek(vp_file, idx_start, SEEK_SET);

            for (int i = 0; i < num_entries && vp_total_frames < VP_MAX_FRAMES; i++) {
                if (fread(entry, 1, 16, vp_file) != 16) break;

                uint32_t offset = vp_read_u32_le(entry + 8);
                uint32_t fsize = vp_read_u32_le(entry + 12);
                uint32_t abs_offset = offset_base + offset + add_header;

                if ((entry[2]=='d' || entry[2]=='D') && (entry[3]=='c' || entry[3]=='C')) {
                    vp_frame_offsets[vp_total_frames] = abs_offset;
                    vp_frame_sizes[vp_total_frames] = fsize;
                    vp_total_frames++;
                }
                else if ((entry[2]=='w' || entry[2]=='W') && (entry[3]=='b' || entry[3]=='B')) {
                    if (vp_total_audio_chunks < VP_MAX_AUDIO_CHUNKS) {
                        vp_audio_offsets[vp_total_audio_chunks] = abs_offset;
                        vp_audio_sizes[vp_total_audio_chunks] = fsize;
                        vp_total_audio_bytes += fsize;
                        vp_total_audio_chunks++;
                    }
                }
            }

            return (vp_total_frames > 0) ? 1 : 0;
        }

        fseek(vp_file, chunk_size + (chunk_size & 1), SEEK_CUR);
    }

    return 0;
}

// Scan movi list for frame offsets
static void vp_scan_movi(long movi_start, long movi_end) {
    fseek(vp_file, movi_start, SEEK_SET);
    uint8_t header[8];

    while (ftell(vp_file) < movi_end && vp_total_frames < VP_MAX_FRAMES) {
        if (fread(header, 1, 8, vp_file) != 8) break;

        uint32_t size = vp_read_u32_le(header + 4);
        long data_pos = ftell(vp_file);

        if ((header[2] == 'd' || header[2] == 'D') && (header[3] == 'c' || header[3] == 'C')) {
            vp_frame_offsets[vp_total_frames] = data_pos;
            vp_frame_sizes[vp_total_frames] = size;
            vp_total_frames++;
        }
        else if ((header[2] == 'w' || header[2] == 'W') && (header[3] == 'b' || header[3] == 'B')) {
            if (vp_total_audio_chunks < VP_MAX_AUDIO_CHUNKS) {
                vp_audio_offsets[vp_total_audio_chunks] = data_pos;
                vp_audio_sizes[vp_total_audio_chunks] = size;
                vp_total_audio_bytes += size;
                vp_total_audio_chunks++;
            }
        }

        fseek(vp_file, size + (size & 1), SEEK_CUR);
    }
}

// Parse AVI file structure
static int vp_parse_avi(void) {
    uint32_t chunk_size, hsize;
    char tag[4], list_type[4], htag[4];
    long hdrl_end, strl_end;
    long movi_start = 0, movi_end = 0;
    uint8_t buf[64];
    int found_idx1 = 0;
    int strl_type = 0;

    vp_total_frames = 0;
    vp_total_audio_chunks = 0;
    vp_total_audio_bytes = 0;
    vp_video_width = 320;
    vp_video_height = 240;
    vp_mpeg4_extradata_size = 0;
    vp_mpeg4_extradata_sent = 0;
    vp_us_per_frame = 33333;
    vp_clip_fps = 30;
    vp_has_audio = 0;
    vp_audio_format = 0;
    vp_adpcm_block_align = 0;
    vp_adpcm_samples_per_block = 0;

    if (!vp_check4(vp_file, "RIFF")) return 0;
    if (vp_read32(vp_file, &chunk_size) != 0) return 0;
    if (!vp_check4(vp_file, "AVI ")) return 0;

    while (fread(tag, 1, 4, vp_file) == 4) {
        if (vp_read32(vp_file, &chunk_size) != 0) break;

        if (tag[0] == 'L' && tag[1] == 'I' && tag[2] == 'S' && tag[3] == 'T') {
            if (fread(list_type, 1, 4, vp_file) != 4) break;

            if (list_type[0] == 'h' && list_type[1] == 'd' &&
                list_type[2] == 'r' && list_type[3] == 'l') {
                hdrl_end = ftell(vp_file) + chunk_size - 4;

                while (ftell(vp_file) < hdrl_end) {
                    if (fread(htag, 1, 4, vp_file) != 4) break;
                    if (vp_read32(vp_file, &hsize) != 0) break;

                    if (htag[0] == 'a' && htag[1] == 'v' &&
                        htag[2] == 'i' && htag[3] == 'h') {
                        if (hsize >= 4 && fread(buf, 1, (hsize < 56 ? hsize : 56), vp_file) >= 4) {
                            vp_us_per_frame = vp_read_u32_le(buf);
                            if (vp_us_per_frame > 0) {
                                vp_clip_fps = 1000000 / vp_us_per_frame;
                                if (vp_clip_fps == 0) vp_clip_fps = 1;
                            }
                            // Set repeat count based on FPS (exact copy from pmp123)
                            // Host runs at 30fps
                            if (vp_clip_fps >= 25) vp_repeat_count = 1;
                            else if (vp_clip_fps >= 12) vp_repeat_count = 2;
                            else vp_repeat_count = 3;
                            if (hsize > 56) fseek(vp_file, hsize - 56, SEEK_CUR);
                        } else {
                            fseek(vp_file, hsize, SEEK_CUR);
                        }
                    }
                    else if (htag[0] == 'L' && htag[1] == 'I' &&
                        htag[2] == 'S' && htag[3] == 'T') {
                        if (fread(buf, 1, 4, vp_file) != 4) break;
                        if (buf[0] == 's' && buf[1] == 't' &&
                            buf[2] == 'r' && buf[3] == 'l') {
                            strl_end = ftell(vp_file) + hsize - 4;
                            strl_type = 0;

                            while (ftell(vp_file) < strl_end) {
                                if (fread(htag, 1, 4, vp_file) != 4) break;
                                uint32_t shsize;
                                if (vp_read32(vp_file, &shsize) != 0) break;

                                if (htag[0] == 's' && htag[1] == 't' &&
                                    htag[2] == 'r' && htag[3] == 'h') {
                                    if (shsize >= 8 &&
                                        fread(buf, 1, (shsize < 64 ? shsize : 64), vp_file) >= 8) {
                                        if (buf[0] == 'a' && buf[1] == 'u' &&
                                            buf[2] == 'd' && buf[3] == 's') {
                                            strl_type = 2;
                                        }
                                        else if (buf[0] == 'v' && buf[1] == 'i' &&
                                            buf[2] == 'd' && buf[3] == 's') {
                                            strl_type = 1;
                                        }
                                        if (shsize > 64) fseek(vp_file, shsize - 64, SEEK_CUR);
                                    } else {
                                        fseek(vp_file, shsize, SEEK_CUR);
                                    }
                                }
                                else if (htag[0] == 's' && htag[1] == 't' &&
                                         htag[2] == 'r' && htag[3] == 'f') {
                                    if (strl_type == 2 && shsize >= 16) {
                                        // Audio format (WAVEFORMATEX)
                                        if (fread(buf, 1, (shsize < 64 ? shsize : 64), vp_file) >= 16) {
                                            uint16_t fmt = vp_read_u16_le(buf);
                                            vp_audio_channels = vp_read_u16_le(buf + 2);
                                            vp_audio_sample_rate = vp_read_u32_le(buf + 4);
                                            vp_adpcm_block_align = vp_read_u16_le(buf + 12);
                                            vp_audio_bits = vp_read_u16_le(buf + 14);

                                            if (fmt == 1 && vp_audio_channels > 0 && vp_audio_sample_rate > 0) {
                                                // PCM audio
                                                vp_has_audio = 1;
                                                vp_audio_format = VP_AUDIO_FMT_PCM;
                                                vp_audio_bytes_per_sample = (vp_audio_bits / 8) * vp_audio_channels;
                                            }
                                            else if (fmt == 2 && vp_audio_channels > 0 && vp_audio_sample_rate > 0) {
                                                // MS ADPCM audio
                                                vp_has_audio = 1;
                                                vp_audio_format = VP_AUDIO_FMT_ADPCM;
                                                vp_audio_bytes_per_sample = 2 * vp_audio_channels;
                                                if (shsize >= 20) {
                                                    vp_adpcm_samples_per_block = vp_read_u16_le(buf + 18);
                                                } else {
                                                    int header = (vp_audio_channels == 1) ? 7 : 14;
                                                    vp_adpcm_samples_per_block = 2 + (vp_adpcm_block_align - header) * 2 / vp_audio_channels;
                                                }
                                            }
                                            else if (fmt == 0x55 && vp_audio_channels > 0 && vp_audio_sample_rate > 0) {
                                                // MP3 audio
                                                vp_has_audio = 1;
                                                vp_audio_format = VP_AUDIO_FMT_MP3;
                                                vp_audio_bytes_per_sample = 4;  // Stereo 16-bit output
                                            }
                                            if (shsize > 64) fseek(vp_file, shsize - 64, SEEK_CUR);
                                        }
                                    }
                                    else if (strl_type == 1 && shsize >= 40) {
                                        // Video format
                                        if (fread(buf, 1, 40, vp_file) == 40) {
                                            vp_video_width = vp_read_u32_le(buf + 4);
                                            vp_video_height = vp_read_u32_le(buf + 8);

                                            int extradata_len = shsize - 40;
                                            if (extradata_len > 0 && extradata_len <= VP_MAX_EXTRADATA_SIZE) {
                                                if (fread(vp_mpeg4_extradata, 1, extradata_len, vp_file) == (size_t)extradata_len) {
                                                    vp_mpeg4_extradata_size = extradata_len;
                                                }
                                            } else if (extradata_len > VP_MAX_EXTRADATA_SIZE) {
                                                fseek(vp_file, extradata_len, SEEK_CUR);
                                            }
                                        }
                                    } else {
                                        fseek(vp_file, shsize, SEEK_CUR);
                                    }
                                }
                                else {
                                    fseek(vp_file, shsize + (shsize & 1), SEEK_CUR);
                                }
                            }
                        } else {
                            fseek(vp_file, hsize - 4, SEEK_CUR);
                        }
                    }
                    else {
                        fseek(vp_file, hsize + (hsize & 1), SEEK_CUR);
                    }
                }
            }
            else if (list_type[0] == 'm' && list_type[1] == 'o' &&
                     list_type[2] == 'v' && list_type[3] == 'i') {
                movi_start = ftell(vp_file);
                movi_end = movi_start + chunk_size - 4;
                fseek(vp_file, movi_end, SEEK_SET);
                found_idx1 = vp_parse_idx1(movi_start);
                if (!found_idx1) {
                    vp_scan_movi(movi_start, movi_end);
                }
                break;
            }
            else {
                fseek(vp_file, chunk_size - 4, SEEK_CUR);
            }
        }
        else {
            fseek(vp_file, chunk_size + (chunk_size & 1), SEEK_CUR);
        }
    }

    return (vp_total_frames > 0) ? 1 : 0;
}

// Initialize XVID decoder
static int vp_init_xvid(void) {
    if (vp_xvid_initialized) return 1;

    xvid_gbl_init_t xinit;
    memset(&xinit, 0, sizeof(xinit));
    xinit.version = XVID_VERSION;
    xinit.cpu_flags = 0;

    int ret = xvid_global(NULL, XVID_GBL_INIT, &xinit, NULL);
    if (ret < 0) return 0;

    xvid_dec_create_t xcreate;
    memset(&xcreate, 0, sizeof(xcreate));
    xcreate.version = XVID_VERSION;
    xcreate.width = vp_video_width > 0 ? vp_video_width : 320;
    xcreate.height = vp_video_height > 0 ? vp_video_height : 240;

    ret = xvid_decore(NULL, XVID_DEC_CREATE, &xcreate, NULL);
    if (ret < 0) return 0;

    vp_xvid_handle = xcreate.handle;

    int w = vp_video_width > 0 ? vp_video_width : 320;
    int h = vp_video_height > 0 ? vp_video_height : 240;
    int y_size = w * h;
    int uv_size = (w / 2) * (h / 2);

    vp_yuv_buffer = (uint8_t *)malloc(y_size + 2 * uv_size);
    if (!vp_yuv_buffer) {
        xvid_decore(vp_xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
        vp_xvid_handle = NULL;
        return 0;
    }

    memset(vp_yuv_buffer, 0, y_size + 2 * uv_size);
    vp_yuv_y = vp_yuv_buffer;
    vp_yuv_u = vp_yuv_buffer + y_size;
    vp_yuv_v = vp_yuv_buffer + y_size + uv_size;

    vp_xvid_initialized = 1;
    return 1;
}

static void vp_close_xvid(void) {
    if (vp_xvid_handle) {
        xvid_decore(vp_xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
        vp_xvid_handle = NULL;
    }
    if (vp_yuv_buffer) {
        free(vp_yuv_buffer);
        vp_yuv_buffer = NULL;
        vp_yuv_y = NULL;
        vp_yuv_u = NULL;
        vp_yuv_v = NULL;
    }
    vp_xvid_initialized = 0;
}

// Decode a single frame
static int vp_decode_frame(int idx) {
    if (!vp_file || idx >= vp_total_frames) return 0;

    uint32_t offset = vp_frame_offsets[idx];
    uint32_t size = vp_frame_sizes[idx];

    if (size > VP_MAX_FRAME_SIZE || size == 0) return 0;

    if (fseek(vp_file, offset, SEEK_SET) != 0) return 0;
    if (fread(vp_frame_buffer, 1, size, vp_file) != size) return 0;

    if (!vp_xvid_initialized) {
        if (!vp_init_xvid()) return 0;
    }

    // Send extradata first if available
    if (!vp_mpeg4_extradata_sent && vp_mpeg4_extradata_size > 0) {
        xvid_dec_frame_t xvol;
        xvid_dec_stats_t svol;
        memset(&xvol, 0, sizeof(xvol));
        memset(&svol, 0, sizeof(svol));
        xvol.version = XVID_VERSION;
        svol.version = XVID_VERSION;
        xvol.bitstream = vp_mpeg4_extradata;
        xvol.length = vp_mpeg4_extradata_size;
        xvol.output.csp = XVID_CSP_NULL;
        xvid_decore(vp_xvid_handle, XVID_DEC_DECODE, &xvol, &svol);
        vp_mpeg4_extradata_sent = 1;
    }

    int w = vp_video_width > 0 ? vp_video_width : 320;
    int h = vp_video_height > 0 ? vp_video_height : 240;

    uint8_t *bitstream = vp_frame_buffer;
    int remaining = size;
    int ret = 0;
    int loops = 0;
    xvid_dec_stats_t xstats;

    do {
        xvid_dec_frame_t xframe;
        memset(&xframe, 0, sizeof(xframe));
        memset(&xstats, 0, sizeof(xstats));

        xframe.version = XVID_VERSION;
        xstats.version = XVID_VERSION;
        xframe.bitstream = bitstream;
        xframe.length = remaining;
        xframe.output.csp = XVID_CSP_PLANAR;
        xframe.output.plane[0] = vp_yuv_y;
        xframe.output.plane[1] = vp_yuv_u;
        xframe.output.plane[2] = vp_yuv_v;
        xframe.output.stride[0] = w;
        xframe.output.stride[1] = w / 2;
        xframe.output.stride[2] = w / 2;

        ret = xvid_decore(vp_xvid_handle, XVID_DEC_DECODE, &xframe, &xstats);

        if (xstats.type == XVID_TYPE_VOL) {
            if (xstats.data.vol.width > 0) {
                vp_video_width = xstats.data.vol.width;
                w = vp_video_width;
            }
            if (xstats.data.vol.height > 0) {
                vp_video_height = xstats.data.vol.height;
                h = vp_video_height;
            }
        }

        if (ret > 0) {
            bitstream += ret;
            remaining -= ret;
        }

        loops++;
    } while (xstats.type <= 0 && ret > 0 && remaining > 4 && loops < 10);

    return 1;
}

// Convert YUV420P to RGB565 with dithering and color mode
static void vp_yuv_to_rgb565(uint16_t *dst) {
    if (!vp_yuv_tables_initialized) vp_init_yuv_tables();
    if (!vp_gamma_tables_initialized) vp_init_gamma_tables();

    int w = vp_video_width > 0 ? vp_video_width : 320;
    int h = vp_video_height > 0 ? vp_video_height : 240;

    // Center video on screen
    int off_x = (SCREEN_WIDTH - w) / 2;
    int off_y = (SCREEN_HEIGHT - h) / 2;
    if (off_x < 0) off_x = 0;
    if (off_y < 0) off_y = 0;

    // Clear framebuffer (black background)
    memset(dst, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

    // Select Y table based on xvid black level setting (like pmp123)
    const int16_t *y_table = (vp_xvid_black_level == VP_XVID_BLACK_TV) ?
                              vp_yuv_y_table_tv : vp_yuv_y_table_pc;

    // v60: Get color mode gamma tables
    const uint8_t *gamma_r = vp_gamma_r5[vp_color_mode];
    const uint8_t *gamma_g = vp_gamma_g6[vp_color_mode];
    const uint8_t *gamma_b = vp_gamma_b5[vp_color_mode];

    // v60: Check if dithering is enabled for this color mode
    int use_dither = (vp_color_mode == VP_COLOR_MODE_DITHERED ||
                      vp_color_mode == VP_COLOR_MODE_DITHER2 ||
                      vp_color_mode == VP_COLOR_MODE_NIGHT_DITHER ||
                      vp_color_mode == VP_COLOR_MODE_NIGHT_DITHER2 ||
                      vp_color_mode == VP_COLOR_MODE_NORMAL);

    for (int j = 0; j < h && (off_y + j) < SCREEN_HEIGHT; j++) {
        uint8_t *y_row = vp_yuv_y + j * w;
        uint8_t *u_row = vp_yuv_u + (j >> 1) * (w / 2);
        uint8_t *v_row = vp_yuv_v + (j >> 1) * (w / 2);
        uint16_t *dst_row = dst + (off_y + j) * SCREEN_WIDTH + off_x;

        for (int i = 0; i < w && (off_x + i) < SCREEN_WIDTH; i++) {
            int y = y_table[y_row[i]];
            int u_idx = u_row[i >> 1];
            int v_idx = v_row[i >> 1];

            int r = y + vp_yuv_rv_table[v_idx];
            int g = y + vp_yuv_gu_table[u_idx] + vp_yuv_gv_table[v_idx];
            int b = y + vp_yuv_bu_table[u_idx];

            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

            // Bayer dithering (optional based on color mode)
            if (use_dither) {
                int dither = vp_bayer4x4[j & 3][i & 3];
                r = r + dither;
                g = g + dither;
                b = b + dither;
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
            }

            // v60: Apply color mode gamma correction
            int r5 = r >> 3;
            int g6 = g >> 2;
            int b5 = b >> 3;
            dst_row[i] = (gamma_r[r5] << 11) | (gamma_g[g6] << 5) | gamma_b[b5];
        }
    }
}

// ============== AUDIO FUNCTIONS ==============

// Decode one MS ADPCM sample
static inline int16_t vp_decode_adpcm_sample(int nibble, int ch) {
    int unsigned_nibble = nibble & 0xF;
    int pred = ((vp_adpcm_sample1[ch] * vp_adpcm_coef1[vp_adpcm_coef_idx[ch]]) +
                (vp_adpcm_sample2[ch] * vp_adpcm_coef2[vp_adpcm_coef_idx[ch]])) >> 8;
    int signed_nibble = (nibble & 0x8) ? (nibble - 16) : nibble;
    int sample = pred + signed_nibble * vp_adpcm_delta[ch];
    sample = vp_clamp16(sample);
    vp_adpcm_sample2[ch] = vp_adpcm_sample1[ch];
    vp_adpcm_sample1[ch] = sample;
    vp_adpcm_delta[ch] = (vp_adpcm_adapt_table[unsigned_nibble] * vp_adpcm_delta[ch]) >> 8;
    if (vp_adpcm_delta[ch] < 16) vp_adpcm_delta[ch] = 16;
    return sample;
}

// Decode one MS ADPCM block (mono)
static int vp_decode_adpcm_block_mono(uint8_t *src, int src_size, int16_t *dst, int max_samples) {
    if (src_size < 7) return 0;

    vp_adpcm_coef_idx[0] = src[0];
    if (vp_adpcm_coef_idx[0] > 6) vp_adpcm_coef_idx[0] = 0;
    vp_adpcm_delta[0] = (int16_t)(src[1] | (src[2] << 8));
    vp_adpcm_sample1[0] = (int16_t)(src[3] | (src[4] << 8));
    vp_adpcm_sample2[0] = (int16_t)(src[5] | (src[6] << 8));

    int out_idx = 0;
    if (out_idx < max_samples) dst[out_idx++] = vp_adpcm_sample2[0];
    if (out_idx < max_samples) dst[out_idx++] = vp_adpcm_sample1[0];

    for (int i = 7; i < src_size && out_idx < max_samples; i++) {
        dst[out_idx++] = vp_decode_adpcm_sample((src[i] >> 4) & 0xF, 0);
        if (out_idx < max_samples)
            dst[out_idx++] = vp_decode_adpcm_sample(src[i] & 0xF, 0);
    }

    return out_idx;
}

// Decode one MS ADPCM block (stereo)
static int vp_decode_adpcm_block_stereo(uint8_t *src, int src_size, int16_t *dst, int max_samples) {
    if (src_size < 14) return 0;

    vp_adpcm_coef_idx[0] = src[0];
    vp_adpcm_coef_idx[1] = src[1];
    if (vp_adpcm_coef_idx[0] > 6) vp_adpcm_coef_idx[0] = 0;
    if (vp_adpcm_coef_idx[1] > 6) vp_adpcm_coef_idx[1] = 0;

    vp_adpcm_delta[0] = (int16_t)(src[2] | (src[3] << 8));
    vp_adpcm_delta[1] = (int16_t)(src[4] | (src[5] << 8));
    vp_adpcm_sample1[0] = (int16_t)(src[6] | (src[7] << 8));
    vp_adpcm_sample1[1] = (int16_t)(src[8] | (src[9] << 8));
    vp_adpcm_sample2[0] = (int16_t)(src[10] | (src[11] << 8));
    vp_adpcm_sample2[1] = (int16_t)(src[12] | (src[13] << 8));

    int out_idx = 0;
    if (out_idx + 1 < max_samples) {
        dst[out_idx++] = vp_adpcm_sample2[0];
        dst[out_idx++] = vp_adpcm_sample2[1];
    }
    if (out_idx + 1 < max_samples) {
        dst[out_idx++] = vp_adpcm_sample1[0];
        dst[out_idx++] = vp_adpcm_sample1[1];
    }

    for (int i = 14; i < src_size && out_idx + 1 < max_samples; i++) {
        dst[out_idx++] = vp_decode_adpcm_sample((src[i] >> 4) & 0xF, 0);
        dst[out_idx++] = vp_decode_adpcm_sample(src[i] & 0xF, 1);
    }

    return out_idx;
}

// Read raw PCM from disk into ring buffer
static int vp_read_audio_disk_pcm(uint8_t *buf, int bytes_needed) {
    int bytes_read = 0;
    while (bytes_read < bytes_needed && vp_audio_chunk_idx < vp_total_audio_chunks) {
        uint32_t chunk_size = vp_audio_sizes[vp_audio_chunk_idx];
        uint32_t remaining = chunk_size - vp_audio_chunk_pos;
        uint32_t to_read = bytes_needed - bytes_read;
        if (to_read > remaining) to_read = remaining;

        uint32_t file_pos = vp_audio_offsets[vp_audio_chunk_idx] + vp_audio_chunk_pos;
        if (fseek(vp_file, file_pos, SEEK_SET) != 0) break;

        size_t got = fread(buf + bytes_read, 1, to_read, vp_file);
        bytes_read += got;
        vp_audio_chunk_pos += got;

        if (vp_audio_chunk_pos >= chunk_size) {
            vp_audio_chunk_idx++;
            vp_audio_chunk_pos = 0;
        }
        if (got < to_read) break;
    }
    return bytes_read;
}

// Read and decode ADPCM
static int vp_read_audio_disk_adpcm(void) {
    if (vp_adpcm_block_align <= 0 || vp_audio_chunk_idx >= vp_total_audio_chunks) return 0;

    int total_decoded_bytes = 0;
    int free_space = VP_AUDIO_RING_SIZE - vp_aring_count;

    while (free_space > 512 && vp_audio_chunk_idx < vp_total_audio_chunks) {
        uint32_t chunk_size = vp_audio_sizes[vp_audio_chunk_idx];
        uint32_t remaining = chunk_size - vp_audio_chunk_pos;

        int block_size = vp_adpcm_block_align;
        if (block_size > (int)remaining) block_size = remaining;
        if (block_size > (int)sizeof(vp_adpcm_read_buf)) block_size = sizeof(vp_adpcm_read_buf);
        if (block_size < 7) {
            vp_audio_chunk_idx++;
            vp_audio_chunk_pos = 0;
            continue;
        }

        uint32_t file_pos = vp_audio_offsets[vp_audio_chunk_idx] + vp_audio_chunk_pos;
        if (fseek(vp_file, file_pos, SEEK_SET) != 0) break;

        size_t got = fread(vp_adpcm_read_buf, 1, block_size, vp_file);
        if (got < 7) break;

        vp_audio_chunk_pos += got;
        if (vp_audio_chunk_pos >= chunk_size) {
            vp_audio_chunk_idx++;
            vp_audio_chunk_pos = 0;
        }

        int samples;
        if (vp_audio_channels == 1) {
            samples = vp_decode_adpcm_block_mono(vp_adpcm_read_buf, got, vp_adpcm_decode_buf, VP_ADPCM_DECODE_BUF_SIZE);
        } else {
            samples = vp_decode_adpcm_block_stereo(vp_adpcm_read_buf, got, vp_adpcm_decode_buf, VP_ADPCM_DECODE_BUF_SIZE);
        }

        if (samples <= 0) continue;

        int decoded_bytes = samples * 2;
        if (decoded_bytes > free_space) decoded_bytes = free_space;

        uint8_t *src = (uint8_t *)vp_adpcm_decode_buf;
        int written = 0;
        while (written < decoded_bytes) {
            int before_wrap = VP_AUDIO_RING_SIZE - vp_aring_write;
            int to_write = decoded_bytes - written;
            if (to_write > before_wrap) to_write = before_wrap;

            memcpy(vp_audio_ring + vp_aring_write, src + written, to_write);
            vp_aring_write = (vp_aring_write + to_write) % VP_AUDIO_RING_SIZE;
            written += to_write;
        }

        vp_aring_count += decoded_bytes;
        free_space -= decoded_bytes;
        total_decoded_bytes += decoded_bytes;

        if (total_decoded_bytes > 4096) break;
    }

    return total_decoded_bytes;
}

// Initialize MP3 decoder
static void vp_mp3_init(void) {
    if (!vp_mp3_initialized) {
        vp_mp3_handle = mad_init();
        if (vp_mp3_handle) {
            vp_mp3_initialized = 1;
        }
        vp_mp3_input_len = 0;
        vp_mp3_input_remaining = 0;
    }
}

// Reset MP3 decoder
static void vp_mp3_reset(void) {
    if (vp_mp3_initialized && vp_mp3_handle) {
        mad_uninit(vp_mp3_handle);
        vp_mp3_handle = mad_init();
    }
    vp_mp3_input_len = 0;
    vp_mp3_input_remaining = 0;
}

// Fill MP3 input buffer
static int vp_mp3_fill_input_buffer(void) {
    if (vp_mp3_input_remaining > 0 && vp_mp3_input_remaining < vp_mp3_input_len) {
        memmove(vp_mp3_input_buf, vp_mp3_input_buf + (vp_mp3_input_len - vp_mp3_input_remaining), vp_mp3_input_remaining);
        vp_mp3_input_len = vp_mp3_input_remaining;
    } else if (vp_mp3_input_remaining <= 0) {
        vp_mp3_input_len = 0;
    }

    int space = VP_MP3_INPUT_BUF_SIZE - vp_mp3_input_len - 8;
    if (space <= 0) return vp_mp3_input_len;

    while (space > 0 && vp_audio_chunk_idx < vp_total_audio_chunks) {
        uint32_t chunk_size = vp_audio_sizes[vp_audio_chunk_idx];
        uint32_t remaining = chunk_size - vp_audio_chunk_pos;

        if (remaining == 0) {
            vp_audio_chunk_idx++;
            vp_audio_chunk_pos = 0;
            continue;
        }

        int to_read = (space < (int)remaining) ? space : (int)remaining;

        uint32_t file_pos = vp_audio_offsets[vp_audio_chunk_idx] + vp_audio_chunk_pos;
        if (fseek(vp_file, file_pos, SEEK_SET) != 0) break;

        size_t got = fread(vp_mp3_input_buf + vp_mp3_input_len, 1, to_read, vp_file);
        if (got == 0) break;

        vp_mp3_input_len += got;
        vp_audio_chunk_pos += got;
        space -= got;

        if (vp_audio_chunk_pos >= chunk_size) {
            vp_audio_chunk_idx++;
            vp_audio_chunk_pos = 0;
        }
    }

    vp_mp3_input_remaining = vp_mp3_input_len;
    return vp_mp3_input_len;
}

// Read and decode MP3
static int vp_read_audio_disk_mp3(void) {
    if (vp_audio_chunk_idx >= vp_total_audio_chunks && vp_mp3_input_remaining <= 0) return 0;

    vp_mp3_init();
    if (!vp_mp3_handle) return 0;

    int total_decoded_bytes = 0;
    int free_space = VP_AUDIO_RING_SIZE - vp_aring_count;
    int consecutive_errors = 0;

    while (free_space > 512 && consecutive_errors < 100) {
        if (vp_mp3_input_remaining < 2048) {
            if (vp_mp3_fill_input_buffer() <= 0) break;
        }

        if (vp_mp3_input_len <= 0) break;

        int bytes_read = 0;
        int bytes_done = 0;
        int out_buf_size = VP_MP3_DECODE_BUF_SIZE * sizeof(int16_t);

        int result = mad_decode(vp_mp3_handle,
                                (char *)vp_mp3_input_buf, vp_mp3_input_len,
                                (char *)vp_mp3_decode_buf, out_buf_size,
                                &bytes_read, &bytes_done, 16, 0);

        if (result == MAD_OK) {
            consecutive_errors = 0;
            if (vp_mp3_detected_samplerate == 0) {
                int sr = 0, ch = 0;
                if (mad_get_info(vp_mp3_handle, &sr, &ch)) {
                    vp_mp3_detected_samplerate = sr;
                    vp_mp3_detected_channels = ch;
                }
            }
            vp_mp3_input_remaining = vp_mp3_input_len - bytes_read;
            if (vp_mp3_input_remaining > 0 && bytes_read > 0) {
                memmove(vp_mp3_input_buf, vp_mp3_input_buf + bytes_read, vp_mp3_input_remaining);
            }
            vp_mp3_input_len = vp_mp3_input_remaining;
        } else if (result == MAD_NEED_MORE_INPUT) {
            vp_mp3_input_remaining = vp_mp3_input_len - bytes_read;
            if (vp_mp3_input_remaining > 0 && bytes_read > 0) {
                memmove(vp_mp3_input_buf, vp_mp3_input_buf + bytes_read, vp_mp3_input_remaining);
            }
            vp_mp3_input_len = vp_mp3_input_remaining;
            if (vp_mp3_fill_input_buffer() <= 0) break;
            continue;
        } else if (result == MAD_ERR) {
            consecutive_errors++;
            if (bytes_read == 0) bytes_read = 1;
            vp_mp3_input_remaining = vp_mp3_input_len - bytes_read;
            if (vp_mp3_input_remaining > 0) {
                memmove(vp_mp3_input_buf, vp_mp3_input_buf + bytes_read, vp_mp3_input_remaining);
            }
            vp_mp3_input_len = vp_mp3_input_remaining;
            continue;
        } else {
            break;
        }

        if (bytes_done <= 0) continue;

        int actual_channels = (vp_mp3_detected_channels > 0) ? vp_mp3_detected_channels : vp_audio_channels;
        if (actual_channels == 1) {
            int mono_samples = bytes_done / 2;
            int stereo_bytes = mono_samples * 4;

            if (stereo_bytes > free_space) {
                mono_samples = free_space / 4;
                stereo_bytes = mono_samples * 4;
            }

            int16_t *mono_src = (int16_t *)vp_mp3_decode_buf;
            for (int i = 0; i < mono_samples; i++) {
                int16_t sample = mono_src[i];
                vp_audio_ring[vp_aring_write] = sample & 0xFF;
                vp_audio_ring[vp_aring_write + 1] = (sample >> 8) & 0xFF;
                vp_audio_ring[vp_aring_write + 2] = sample & 0xFF;
                vp_audio_ring[vp_aring_write + 3] = (sample >> 8) & 0xFF;
                vp_aring_write = (vp_aring_write + 4) % VP_AUDIO_RING_SIZE;
            }

            vp_aring_count += stereo_bytes;
            free_space -= stereo_bytes;
            total_decoded_bytes += stereo_bytes;
        } else {
            int decoded_bytes = bytes_done;
            if (decoded_bytes > free_space) decoded_bytes = free_space;

            uint8_t *src = (uint8_t *)vp_mp3_decode_buf;
            int written = 0;
            while (written < decoded_bytes) {
                int before_wrap = VP_AUDIO_RING_SIZE - vp_aring_write;
                int to_write = decoded_bytes - written;
                if (to_write > before_wrap) to_write = before_wrap;

                memcpy(vp_audio_ring + vp_aring_write, src + written, to_write);
                vp_aring_write = (vp_aring_write + to_write) % VP_AUDIO_RING_SIZE;
                written += to_write;
            }

            vp_aring_count += decoded_bytes;
            free_space -= decoded_bytes;
            total_decoded_bytes += decoded_bytes;
        }

        if (total_decoded_bytes > 4096) break;
    }

    return total_decoded_bytes;
}

// Refill audio ring buffer
static void vp_refill_audio_ring(void) {
    if (!vp_has_audio || vp_audio_chunk_idx >= vp_total_audio_chunks) return;

    if (vp_audio_format == VP_AUDIO_FMT_ADPCM) {
        vp_read_audio_disk_adpcm();
    } else if (vp_audio_format == VP_AUDIO_FMT_MP3) {
        vp_read_audio_disk_mp3();
    } else {
        // PCM
        int free_space = VP_AUDIO_RING_SIZE - vp_aring_count;
        while (free_space > 0 && vp_audio_chunk_idx < vp_total_audio_chunks) {
            int before_wrap = VP_AUDIO_RING_SIZE - vp_aring_write;
            int to_read = (free_space < before_wrap) ? free_space : before_wrap;
            if (to_read > 4096) to_read = 4096;

            int got = vp_read_audio_disk_pcm(vp_audio_ring + vp_aring_write, to_read);
            if (got <= 0) break;

            vp_aring_write = (vp_aring_write + got) % VP_AUDIO_RING_SIZE;
            vp_aring_count += got;
            free_space -= got;
        }
    }
}

// Read from audio ring buffer
static int vp_read_audio_ring(uint8_t *buf, int bytes_needed) {
    int bytes_read = 0;
    while (bytes_read < bytes_needed && vp_aring_count > 0) {
        int before_wrap = VP_AUDIO_RING_SIZE - vp_aring_read;
        int avail = (vp_aring_count < before_wrap) ? vp_aring_count : before_wrap;
        int to_read = bytes_needed - bytes_read;
        if (to_read > avail) to_read = avail;

        memcpy(buf + bytes_read, vp_audio_ring + vp_aring_read, to_read);
        vp_aring_read = (vp_aring_read + to_read) % VP_AUDIO_RING_SIZE;
        vp_aring_count -= to_read;
        bytes_read += to_read;
    }
    return bytes_read;
}

// Play audio synced to current frame
static void vp_play_audio_for_frame(void) {
    if (!vp_has_audio || !vp_audio_batch_cb || vp_audio_bytes_per_sample == 0) return;

    if (vp_aring_count < VP_AUDIO_REFILL_THRESHOLD) {
        vp_refill_audio_ring();
    }

    int effective_rate = vp_audio_sample_rate;
    if (vp_audio_format == VP_AUDIO_FMT_MP3 && vp_mp3_detected_samplerate > 0) {
        effective_rate = vp_mp3_detected_samplerate;
    }

    int sync_offset = effective_rate / 10;
    uint64_t expected = (uint64_t)vp_current_frame * effective_rate / vp_clip_fps + sync_offset;
    int64_t to_send = expected - vp_audio_samples_sent;

    if (to_send <= 0) return;
    if (to_send > VP_MAX_AUDIO_BUFFER) to_send = VP_MAX_AUDIO_BUFFER;

    int bytes_needed = to_send * vp_audio_bytes_per_sample;
    uint8_t temp[VP_MAX_AUDIO_BUFFER * 4];
    if (bytes_needed > (int)sizeof(temp)) {
        bytes_needed = sizeof(temp);
        to_send = bytes_needed / vp_audio_bytes_per_sample;
    }

    int got_bytes = vp_read_audio_ring(temp, bytes_needed);
    int got_samples = got_bytes / vp_audio_bytes_per_sample;
    if (got_samples <= 0) return;

    int out = 0;
    int effective_bits = (vp_audio_format == VP_AUDIO_FMT_ADPCM || vp_audio_format == VP_AUDIO_FMT_MP3) ? 16 : vp_audio_bits;
    int effective_channels = (vp_audio_format == VP_AUDIO_FMT_MP3) ? 2 : vp_audio_channels;

    if (effective_channels == 1 && effective_bits == 16) {
        int16_t *src = (int16_t *)temp;
        for (int i = 0; i < got_samples && out < VP_MAX_AUDIO_BUFFER; i++) {
            vp_audio_out_buffer[out * 2] = src[i];
            vp_audio_out_buffer[out * 2 + 1] = src[i];
            out++;
        }
    } else if (effective_channels == 2 && effective_bits == 16) {
        int16_t *src = (int16_t *)temp;
        for (int i = 0; i < got_samples && out < VP_MAX_AUDIO_BUFFER; i++) {
            vp_audio_out_buffer[out * 2] = src[i * 2];
            vp_audio_out_buffer[out * 2 + 1] = src[i * 2 + 1];
            out++;
        }
    } else if (effective_bits == 8) {
        for (int i = 0; i < got_samples && out < VP_MAX_AUDIO_BUFFER; i++) {
            int16_t s = ((int16_t)temp[i * effective_channels] - 128) << 8;
            vp_audio_out_buffer[out * 2] = s;
            vp_audio_out_buffer[out * 2 + 1] = s;
            out++;
        }
    }

    if (out > 0) {
        // v60: Mute first samples after seek to avoid audio crackle
        if (vp_audio_mute_samples > 0) {
            int mute_count = (vp_audio_mute_samples < out) ? vp_audio_mute_samples : out;
            for (int i = 0; i < mute_count * 2; i++) {
                vp_audio_out_buffer[i] = 0;
            }
            vp_audio_mute_samples -= mute_count;
        }
        vp_audio_batch_cb(vp_audio_out_buffer, out);
        vp_audio_samples_sent += out;
    }
}

// Seek to frame
static void vp_seek_to_frame(int target_frame) {
    /* Clamp seeking to 2 seconds before end to prevent freeze when seeking */
    int max_seek_frame = vp_total_frames - (vp_clip_fps * 2);
    if (max_seek_frame < 0) max_seek_frame = 0;
    if (target_frame < 0) target_frame = 0;
    if (target_frame > max_seek_frame) target_frame = max_seek_frame;

    vp_current_frame = target_frame;
    vp_repeat_counter = 0;

    if (vp_has_audio && vp_audio_bytes_per_sample > 0) {
        int effective_rate = vp_audio_sample_rate;
        if (vp_audio_format == VP_AUDIO_FMT_MP3 && vp_mp3_detected_samplerate > 0) {
            effective_rate = vp_mp3_detected_samplerate;
        }
        uint64_t time_samples = (uint64_t)target_frame * effective_rate / vp_clip_fps;

        vp_audio_chunk_idx = 0;
        vp_audio_chunk_pos = 0;

        if (vp_audio_format == VP_AUDIO_FMT_MP3) {
            int samples_per_mp3_frame = (effective_rate >= 32000) ? 1152 : 576;
            vp_audio_chunk_idx = time_samples / samples_per_mp3_frame;
            if (vp_audio_chunk_idx >= vp_total_audio_chunks) {
                vp_audio_chunk_idx = vp_total_audio_chunks - 1;
            }
            vp_audio_chunk_pos = 0;
            time_samples = (uint64_t)vp_audio_chunk_idx * samples_per_mp3_frame;
        } else if (vp_audio_format == VP_AUDIO_FMT_ADPCM && vp_adpcm_samples_per_block > 0 && vp_adpcm_block_align > 0) {
            uint64_t target_blocks = time_samples / vp_adpcm_samples_per_block;
            uint64_t target_bytes = target_blocks * vp_adpcm_block_align;
            uint64_t bytes_so_far = 0;

            while (vp_audio_chunk_idx < vp_total_audio_chunks) {
                if (bytes_so_far + vp_audio_sizes[vp_audio_chunk_idx] > target_bytes) {
                    uint32_t pos_in_chunk = target_bytes - bytes_so_far;
                    pos_in_chunk = (pos_in_chunk / vp_adpcm_block_align) * vp_adpcm_block_align;
                    vp_audio_chunk_pos = pos_in_chunk;
                    break;
                }
                bytes_so_far += vp_audio_sizes[vp_audio_chunk_idx];
                vp_audio_chunk_idx++;
            }
        } else {
            uint64_t target_bytes = time_samples * vp_audio_bytes_per_sample;
            uint64_t bytes_so_far = 0;

            while (vp_audio_chunk_idx < vp_total_audio_chunks) {
                if (bytes_so_far + vp_audio_sizes[vp_audio_chunk_idx] > target_bytes) {
                    vp_audio_chunk_pos = target_bytes - bytes_so_far;
                    break;
                }
                bytes_so_far += vp_audio_sizes[vp_audio_chunk_idx];
                vp_audio_chunk_idx++;
            }
        }

        vp_audio_samples_sent = time_samples;
        vp_aring_read = 0;
        vp_aring_write = 0;
        vp_aring_count = 0;

        // v60: Mute first samples after seek to avoid audio crackle
        vp_audio_mute_samples = VP_AUDIO_MUTE_AFTER_SEEK;

        if (vp_audio_format == VP_AUDIO_FMT_MP3) {
            vp_mp3_reset();
        } else {
            vp_refill_audio_ring();
        }
    }

    vp_decode_frame(target_frame);
}

// ============== SETTINGS SAVE/LOAD ==============

static void vp_save_settings(void) {
    FILE *f = fopen(VP_SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "# FrogPMP settings\n");
    fprintf(f, "color_mode=%d\n", vp_color_mode);
    fprintf(f, "xvid_black=%d\n", vp_xvid_black_level);
    fprintf(f, "show_time=%d\n", vp_show_time);
    fprintf(f, "show_debug=%d\n", vp_show_debug);
    fprintf(f, "play_mode=%d\n", vp_play_mode);
    fclose(f);
}

static void vp_load_settings(void) {
    FILE *f = fopen(VP_SETTINGS_FILE, "r");
    if (!f) return;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '#' || buf[0] == '\n') continue;
        char *eq = strchr(buf, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = buf;
        char *val = eq + 1;
        int v = atoi(val);
        if (strcmp(key, "color_mode") == 0 && v >= 0 && v < VP_COLOR_MODE_COUNT) vp_color_mode = v;
        else if (strcmp(key, "xvid_black") == 0) vp_xvid_black_level = v ? VP_XVID_BLACK_PC : VP_XVID_BLACK_TV;
        else if (strcmp(key, "show_time") == 0) vp_show_time = v ? 1 : 0;
        else if (strcmp(key, "show_debug") == 0) vp_show_debug = v ? 1 : 0;
        else if (strcmp(key, "play_mode") == 0 && v >= 0 && v < VP_PLAY_MODE_COUNT) vp_play_mode = v;
    }
    fclose(f);
}

// ============== MENU DRAWING ==============

static void vp_draw_menu(void) {
    int menu_x = 50;
    int menu_y = 10;
    int menu_w = 220;
    int menu_h = 220;  /* Smaller: 10 items instead of 12 */

    /* Colors - Amiga Workbench inspired */
    uint16_t col_bg = 0x0010;
    uint16_t col_border = 0x001F;
    uint16_t col_title = 0xFFFF;
    uint16_t col_titlebar = 0x52AA;
    uint16_t col_text = 0xFFFF;
    uint16_t col_sel = 0x07E0;
    uint16_t col_value = 0x07FF;
    uint16_t col_hint = 0xFBE0;
    uint16_t col_corner = 0x6B5D;

    /* Darken background */
    vp_draw_dark_rect(menu_x - 8, menu_y - 8, menu_x + menu_w + 8, menu_y + menu_h + 8);

    /* Main window fill */
    vp_draw_fill_rect(menu_x, menu_y, menu_x + menu_w, menu_y + menu_h, col_bg);

    /* Amiga-style border with corner cuts */
    vp_draw_fill_rect(menu_x + 6, menu_y - 2, menu_x + menu_w - 6, menu_y, col_border);
    vp_draw_fill_rect(menu_x + 6, menu_y + menu_h, menu_x + menu_w - 6, menu_y + menu_h + 2, col_border);
    vp_draw_fill_rect(menu_x - 2, menu_y + 6, menu_x, menu_y + menu_h - 6, col_border);
    vp_draw_fill_rect(menu_x + menu_w, menu_y + 6, menu_x + menu_w + 2, menu_y + menu_h - 6, col_border);

    /* Corner angles */
    vp_draw_fill_rect(menu_x, menu_y, menu_x + 6, menu_y + 2, col_corner);
    vp_draw_fill_rect(menu_x, menu_y, menu_x + 2, menu_y + 6, col_corner);
    vp_draw_fill_rect(menu_x + menu_w - 6, menu_y, menu_x + menu_w, menu_y + 2, col_corner);
    vp_draw_fill_rect(menu_x + menu_w - 2, menu_y, menu_x + menu_w, menu_y + 6, col_corner);
    vp_draw_fill_rect(menu_x, menu_y + menu_h - 2, menu_x + 6, menu_y + menu_h, col_corner);
    vp_draw_fill_rect(menu_x, menu_y + menu_h - 6, menu_x + 2, menu_y + menu_h, col_corner);
    vp_draw_fill_rect(menu_x + menu_w - 6, menu_y + menu_h - 2, menu_x + menu_w, menu_y + menu_h, col_corner);
    vp_draw_fill_rect(menu_x + menu_w - 2, menu_y + menu_h - 6, menu_x + menu_w, menu_y + menu_h, col_corner);

    /* Title bar */
    vp_draw_fill_rect(menu_x + 4, menu_y + 4, menu_x + menu_w - 4, menu_y + 26, col_titlebar);

    /* Title text */
    vp_draw_str(menu_x + 72, menu_y + 7, "FrogPMP", col_title);
    vp_draw_str(menu_x + 50, menu_y + 17, "by Grzegorz Korycki", col_value);

    /* Item 0: Go to Position (with slider) */
    int go_y = menu_y + 34;
    uint16_t go_col = (vp_menu_selection == VP_MENU_GO_TO_POS) ? col_sel : col_text;
    if (vp_menu_selection == VP_MENU_GO_TO_POS) {
        vp_draw_fill_rect(menu_x + 6, go_y - 1, menu_x + menu_w - 6, go_y + 9, 0x0015);
        vp_draw_str(menu_x + 8, go_y, ">", col_sel);
    }
    vp_draw_str(menu_x + 20, go_y, vp_menu_labels[VP_MENU_GO_TO_POS], go_col);

    /* Seek slider */
    int slider_y = go_y + 14;
    int slider_x = menu_x + 15;
    int slider_w = menu_w - 30;

    vp_draw_fill_rect(slider_x, slider_y, slider_x + slider_w, slider_y + 8, 0x0008);
    vp_draw_fill_rect(slider_x + 1, slider_y + 1, slider_x + slider_w - 1, slider_y + 7, 0x2104);

    for (int p = 0; p <= 20; p += 5) {
        int mark_x = slider_x + (p * slider_w / 20);
        vp_draw_fill_rect(mark_x, slider_y - 2, mark_x + 1, slider_y + 10, col_border);
    }

    int pos_x = slider_x + (vp_seek_position * slider_w / 20);
    vp_draw_fill_rect(pos_x - 4, slider_y - 3, pos_x + 4, slider_y + 11, col_sel);
    vp_draw_fill_rect(pos_x - 2, slider_y - 1, pos_x + 2, slider_y + 9, col_title);

    /* Position info */
    int pct = vp_seek_position * 5;
    int target_frame = (vp_total_frames > 0) ? (vp_seek_position * vp_total_frames / 20) : 0;
    vp_draw_num(slider_x, slider_y + 14, pct, col_hint);
    vp_draw_str(slider_x + 18, slider_y + 14, "%", col_hint);
    vp_draw_str(slider_x + 50, slider_y + 14, "Fr:", col_text);
    vp_draw_num(slider_x + 70, slider_y + 14, target_frame, col_value);
    vp_draw_str(slider_x + 110, slider_y + 14, "/", col_text);
    vp_draw_num(slider_x + 118, slider_y + 14, vp_total_frames, col_value);

    if (vp_menu_selection == VP_MENU_GO_TO_POS) {
        vp_draw_str(menu_x + 52, slider_y + 24, "L/R: Seek", col_hint);
    }

    /* Separator line after Go to Position */
    vp_draw_fill_rect(menu_x + 10, menu_y + 83, menu_x + menu_w - 10, menu_y + 84, col_border);

    /* Menu items 1-9 (Color Mode through About) */
    for (int i = 1; i < VP_MENU_ITEMS; i++) {
        int item_y = menu_y + 89 + (i - 1) * 14;
        uint16_t col = (i == vp_menu_selection) ? col_sel : col_text;

        if (i == vp_menu_selection) {
            vp_draw_fill_rect(menu_x + 6, item_y - 1, menu_x + menu_w - 6, item_y + 9, 0x0015);
            vp_draw_str(menu_x + 8, item_y, ">", col_sel);
        }
        vp_draw_str(menu_x + 20, item_y, vp_menu_labels[i], col);

        /* Show current state for items */
        if (i == VP_MENU_COLOR_MODE) {
            vp_draw_str(menu_x + 120, item_y, vp_color_mode_names[vp_color_mode], col_value);
        } else if (i == VP_MENU_XVID_RANGE) {
            vp_draw_str(menu_x + 110, item_y,
                       vp_xvid_black_level == VP_XVID_BLACK_TV ? "[0-255]" : "[16-235]", col_value);
        } else if (i == VP_MENU_PLAY_MODE) {
            vp_draw_str(menu_x + 110, item_y, vp_play_mode_names[vp_play_mode], col_value);
        } else if (i == VP_MENU_SHOW_TIME) {
            vp_draw_str(menu_x + 150, item_y, vp_show_time ? "[ON]" : "[OFF]", col_value);
        } else if (i == VP_MENU_SAVE) {
            vp_draw_str(menu_x + 150, item_y, "[!]", col_value);
        } else if (i == VP_MENU_INSTRUCTIONS) {
            vp_draw_str(menu_x + 150, item_y, "[>]", col_value);
        } else if (i == VP_MENU_ABOUT) {
            vp_draw_str(menu_x + 155, item_y, "/", 0xFFE0);
        }
    }

    /* Instructions at bottom */
    vp_draw_str(menu_x + 30, menu_y + menu_h - 12, "UP/DOWN:Sel  START:Close", 0x6B5D);

    /* Draw submenu overlay if active */
    if (vp_submenu_active > 0) {
        int sub_x = menu_x + 20;
        int sub_y = menu_y + 40;
        int sub_w = menu_w - 40;
        int sub_h = (vp_submenu_active == 1) ? 124 : 156;

        vp_draw_fill_rect(sub_x, sub_y, sub_x + sub_w, sub_y + sub_h, 0x0008);
        vp_draw_fill_rect(sub_x + 2, sub_y + 2, sub_x + sub_w - 2, sub_y + sub_h - 2, col_bg);

        vp_draw_fill_rect(sub_x, sub_y, sub_x + sub_w, sub_y + 2, col_border);
        vp_draw_fill_rect(sub_x, sub_y + sub_h - 2, sub_x + sub_w, sub_y + sub_h, col_border);
        vp_draw_fill_rect(sub_x, sub_y, sub_x + 2, sub_y + sub_h, col_border);
        vp_draw_fill_rect(sub_x + sub_w - 2, sub_y, sub_x + sub_w, sub_y + sub_h, col_border);

        if (vp_submenu_active == 1) {
            vp_draw_str(sub_x + 40, sub_y + 8, "INSTRUCTIONS", col_title);
            vp_draw_str(sub_x + 10, sub_y + 26, "A: Play/Pause", col_text);
            vp_draw_str(sub_x + 10, sub_y + 38, "L/R: Skip 15 sec", col_text);
            vp_draw_str(sub_x + 10, sub_y + 50, "Up/Down: Skip 1 min", col_text);
            vp_draw_str(sub_x + 10, sub_y + 62, "START: Menu", col_text);
            vp_draw_str(sub_x + 10, sub_y + 74, "L+R Shoulder:", col_text);
            vp_draw_str(sub_x + 20, sub_y + 86, "Lock all keys", col_text);
            vp_draw_str(sub_x + 40, sub_y + 106, "A: Back", col_hint);
        } else if (vp_submenu_active == 2) {
            vp_draw_str(sub_x + 45, sub_y + 6, "ABOUT/CREDITS", col_title);
            vp_draw_str(sub_x + 10, sub_y + 22, "FrogPMP by @the_q_dev", col_text);
            vp_draw_str(sub_x + 10, sub_y + 36, "Libraries (GPL v2):", col_hint);
            vp_draw_str(sub_x + 10, sub_y + 48, "- Xvid MPEG-4 decoder", col_value);
            vp_draw_str(sub_x + 12, sub_y + 58, "Peter Ross, xvid.org", col_value);
            vp_draw_str(sub_x + 10, sub_y + 70, "- libmad MP3 decoder", col_value);
            vp_draw_str(sub_x + 12, sub_y + 80, "Underbit Technologies", col_value);
            vp_draw_str(sub_x + 10, sub_y + 94, "Greetings:", col_text);
            vp_draw_str(sub_x + 10, sub_y + 106, "Maciek,Madzia,Tomek,", col_value);
            vp_draw_str(sub_x + 10, sub_y + 116, "Eliasz,Eliza", col_value);
            vp_draw_str(sub_x + 40, sub_y + 138, "A: Back", col_hint);
        }
    }

    /* Color mode submenu (scrollable) */
    if (vp_color_submenu_active) {
        int csub_x = menu_x + 15;
        int csub_y = menu_y + 35;
        int csub_w = menu_w - 30;
        int csub_h = 130;
        int visible_items = 8;

        vp_draw_fill_rect(csub_x, csub_y, csub_x + csub_w, csub_y + csub_h, 0x0008);
        vp_draw_fill_rect(csub_x + 2, csub_y + 2, csub_x + csub_w - 2, csub_y + csub_h - 2, col_bg);

        vp_draw_fill_rect(csub_x, csub_y, csub_x + csub_w, csub_y + 2, col_border);
        vp_draw_fill_rect(csub_x, csub_y + csub_h - 2, csub_x + csub_w, csub_y + csub_h, col_border);
        vp_draw_fill_rect(csub_x, csub_y, csub_x + 2, csub_y + csub_h, col_border);
        vp_draw_fill_rect(csub_x + csub_w - 2, csub_y, csub_x + csub_w, csub_y + csub_h, col_border);

        vp_draw_str(csub_x + 35, csub_y + 6, "COLOR MODE", col_title);

        if (vp_color_submenu_scroll > 0) {
            vp_draw_str(csub_x + csub_w - 18, csub_y + 6, "^", col_hint);
        }
        if (vp_color_submenu_scroll + visible_items < VP_COLOR_MODE_COUNT) {
            vp_draw_str(csub_x + csub_w - 18, csub_y + csub_h - 16, "v", col_hint);
        }

        for (int i = 0; i < visible_items && (vp_color_submenu_scroll + i) < VP_COLOR_MODE_COUNT; i++) {
            int mode_idx = vp_color_submenu_scroll + i;
            int item_y = csub_y + 20 + i * 12;
            uint16_t item_col = (mode_idx == vp_color_mode) ? col_sel : col_text;

            if (mode_idx == vp_color_mode) {
                vp_draw_fill_rect(csub_x + 6, item_y - 1, csub_x + csub_w - 6, item_y + 9, 0x0015);
                vp_draw_str(csub_x + 8, item_y, ">", col_sel);
            }
            vp_draw_str(csub_x + 20, item_y, vp_color_mode_names[mode_idx], item_col);
        }

        vp_draw_str(csub_x + 15, csub_y + csub_h - 12, "A:Select B:Back", col_hint);
    }

    /* Save feedback popup */
    if (vp_save_feedback_timer > 0) {
        vp_draw_fill_rect(menu_x + 40, menu_y + 100, menu_x + menu_w - 40, menu_y + 130, 0x0008);
        vp_draw_fill_rect(menu_x + 42, menu_y + 102, menu_x + menu_w - 42, menu_y + 128, col_bg);
        vp_draw_str(menu_x + 55, menu_y + 110, "Settings Saved!", col_sel);
    }
}

// ============== PUBLIC API ==============

void vp_init(void) {
    vp_active = 0;
    vp_paused = 0;
    vp_file = NULL;
    vp_frame_buffer = NULL;
    vp_total_frames = 0;
    vp_current_frame = 0;
    vp_repeat_count = 1;
    vp_repeat_counter = 0;
    vp_menu_active = 0;
    vp_menu_selection = 0;

    // Reset input edge detection
    vp_prev_a = 0;
    vp_prev_b = 0;
    vp_prev_left = 0;
    vp_prev_right = 0;
    vp_prev_start = 0;
    vp_prev_up = 0;
    vp_prev_down = 0;
    vp_prev_l = 0;
    vp_prev_r = 0;
}

void vp_set_audio_callback(vp_audio_batch_cb_t cb) {
    vp_audio_batch_cb = cb;
}

int vp_open(const char *path) {
    // v69: Pause background music to save CPU - video needs all the power
    if (mp_is_active() && !mp_is_paused()) {
        mp_toggle_pause();
    }

    // Load saved settings
    vp_load_settings();

    // Store current path for play mode
    strncpy(vp_current_path, path, VP_MAX_PATH - 1);
    vp_current_path[VP_MAX_PATH - 1] = '\0';

    // Extract directory from path
    strncpy(vp_current_dir, path, VP_MAX_PATH - 1);
    vp_current_dir[VP_MAX_PATH - 1] = '\0';
    char *last_slash = strrchr(vp_current_dir, '/');
    if (last_slash) *last_slash = '\0';

    vp_next_video_requested = 0;

    // Scan playlist for A-Z and Shuffle modes
    vp_scan_playlist();

    // Close any existing playback
    if (vp_active) {
        vp_close();
    }

    vp_file = fopen(path, "rb");
    if (!vp_file) return 0;

    // Allocate frame buffer
    vp_frame_buffer = (uint8_t *)malloc(VP_MAX_FRAME_SIZE);
    if (!vp_frame_buffer) {
        fclose(vp_file);
        vp_file = NULL;
        return 0;
    }

    // Allocate frame index arrays
    vp_frame_offsets = (uint32_t *)malloc(VP_MAX_FRAMES * sizeof(uint32_t));
    vp_frame_sizes = (uint32_t *)malloc(VP_MAX_FRAMES * sizeof(uint32_t));
    if (!vp_frame_offsets || !vp_frame_sizes) {
        if (vp_frame_offsets) { free(vp_frame_offsets); vp_frame_offsets = NULL; }
        if (vp_frame_sizes) { free(vp_frame_sizes); vp_frame_sizes = NULL; }
        free(vp_frame_buffer);
        vp_frame_buffer = NULL;
        fclose(vp_file);
        vp_file = NULL;
        return 0;
    }

    // Allocate audio chunk arrays
    vp_audio_offsets = (uint32_t *)malloc(VP_MAX_AUDIO_CHUNKS * sizeof(uint32_t));
    vp_audio_sizes = (uint32_t *)malloc(VP_MAX_AUDIO_CHUNKS * sizeof(uint32_t));
    if (!vp_audio_offsets || !vp_audio_sizes) {
        if (vp_audio_offsets) { free(vp_audio_offsets); vp_audio_offsets = NULL; }
        if (vp_audio_sizes) { free(vp_audio_sizes); vp_audio_sizes = NULL; }
        free(vp_frame_offsets); vp_frame_offsets = NULL;
        free(vp_frame_sizes); vp_frame_sizes = NULL;
        free(vp_frame_buffer); vp_frame_buffer = NULL;
        fclose(vp_file);
        vp_file = NULL;
        return 0;
    }

    // Allocate audio ring buffer
    vp_audio_ring = (uint8_t *)malloc(VP_AUDIO_RING_SIZE);
    if (!vp_audio_ring) {
        free(vp_audio_offsets); vp_audio_offsets = NULL;
        free(vp_audio_sizes); vp_audio_sizes = NULL;
        free(vp_frame_offsets); vp_frame_offsets = NULL;
        free(vp_frame_sizes); vp_frame_sizes = NULL;
        free(vp_frame_buffer); vp_frame_buffer = NULL;
        fclose(vp_file);
        vp_file = NULL;
        return 0;
    }

    // Parse AVI structure
    if (!vp_parse_avi()) {
        free(vp_audio_ring); vp_audio_ring = NULL;
        free(vp_audio_offsets); vp_audio_offsets = NULL;
        free(vp_audio_sizes); vp_audio_sizes = NULL;
        free(vp_frame_offsets); vp_frame_offsets = NULL;
        free(vp_frame_sizes); vp_frame_sizes = NULL;
        free(vp_frame_buffer); vp_frame_buffer = NULL;
        fclose(vp_file);
        vp_file = NULL;
        return 0;
    }

    // Reset state
    vp_current_frame = 0;
    vp_repeat_counter = 0;
    vp_paused = 0;
    vp_active = 1;
    vp_menu_active = 0;
    vp_menu_selection = 0;
    vp_mpeg4_extradata_sent = 0;

    // Reset audio state
    vp_audio_chunk_idx = 0;
    vp_audio_chunk_pos = 0;
    vp_audio_samples_sent = 0;
    vp_aring_read = 0;
    vp_aring_write = 0;
    vp_aring_count = 0;
    vp_mp3_detected_samplerate = 0;
    vp_mp3_detected_channels = 0;
    vp_mp3_input_len = 0;
    vp_mp3_input_remaining = 0;

    // Pre-fill audio buffer
    vp_refill_audio_ring();

    // Decode first frame
    vp_decode_frame(0);

    // v61: Resume playback if same file was played before
    if (vp_resume_path[0] != '\0' && strcmp(path, vp_resume_path) == 0) {
        if (vp_resume_frame > 0 && vp_resume_frame < vp_total_frames) {
            vp_seek_to_frame(vp_resume_frame);
        }
    }

    return 1;
}

void vp_close(void) {
    // v61: Save resume position before closing
    if (vp_active && vp_total_frames > 0) {
        strncpy(vp_resume_path, vp_current_path, VP_MAX_PATH - 1);
        vp_resume_path[VP_MAX_PATH - 1] = '\0';
        vp_resume_frame = vp_current_frame;
    }

    vp_close_xvid();

    if (vp_mp3_initialized && vp_mp3_handle) {
        mad_uninit(vp_mp3_handle);
        vp_mp3_handle = NULL;
        vp_mp3_initialized = 0;
    }

    if (vp_audio_ring) {
        free(vp_audio_ring);
        vp_audio_ring = NULL;
    }

    if (vp_frame_buffer) {
        free(vp_frame_buffer);
        vp_frame_buffer = NULL;
    }

    if (vp_frame_offsets) {
        free(vp_frame_offsets);
        vp_frame_offsets = NULL;
    }

    if (vp_frame_sizes) {
        free(vp_frame_sizes);
        vp_frame_sizes = NULL;
    }

    if (vp_audio_offsets) {
        free(vp_audio_offsets);
        vp_audio_offsets = NULL;
    }

    if (vp_audio_sizes) {
        free(vp_audio_sizes);
        vp_audio_sizes = NULL;
    }

    if (vp_file) {
        fclose(vp_file);
        vp_file = NULL;
    }

    vp_active = 0;
    vp_paused = 0;
    vp_total_frames = 0;
    vp_current_frame = 0;
    vp_repeat_counter = 0;
    vp_mpeg4_extradata_sent = 0;
    vp_menu_active = 0;
}

int vp_is_active(void) {
    return vp_active;
}

int vp_handle_input(int up, int down, int left, int right, int a, int b, int start, int l, int r) {
    if (!vp_active) return 0;

    // L+R together = toggle lock (like pmp123)
    if (l && r && (!vp_prev_l || !vp_prev_r)) {
        vp_is_locked = !vp_is_locked;
        vp_icon_type = vp_is_locked ? VP_ICON_LOCK : VP_ICON_UNLOCK;
        vp_icon_timer = VP_ICON_FRAMES;
    }

    // When locked, ignore all input except L+R
    if (!vp_is_locked) {
        // B button - quick exit (returns to browser immediately)
        if (b && !vp_prev_b) {
            vp_close();
            return 1;
        }

        // START button - toggle menu
        if (start && !vp_prev_start) {
            if (vp_menu_active) {
                vp_menu_active = 0;
                vp_paused = vp_was_paused_before_menu;
                if (!vp_paused) {
                    vp_icon_type = VP_ICON_PLAY;
                    vp_icon_timer = VP_ICON_FRAMES;
                }
            } else {
                vp_menu_active = 1;
                vp_was_paused_before_menu = vp_paused;
                vp_paused = 1;
                if (vp_total_frames > 0) {
                    vp_seek_position = (vp_current_frame * 20) / vp_total_frames;
                    if (vp_seek_position > 20) vp_seek_position = 20;
                }
            }
        }

        if (vp_menu_active) {
            /* Handle submenus first */
            if (vp_submenu_active > 0) {
                /* Close submenu with A or B */
                if ((a && !vp_prev_a) || (b && !vp_prev_b)) {
                    vp_submenu_active = 0;
                }
            } else if (vp_color_submenu_active) {
                /* Color submenu navigation */
                if (up && !vp_prev_up) {
                    vp_color_mode = (vp_color_mode - 1 + VP_COLOR_MODE_COUNT) % VP_COLOR_MODE_COUNT;
                    /* Scroll to keep selection visible */
                    if (vp_color_mode < vp_color_submenu_scroll) {
                        vp_color_submenu_scroll = vp_color_mode;
                    }
                    if (vp_color_mode >= vp_color_submenu_scroll + 8) {
                        vp_color_submenu_scroll = vp_color_mode - 7;
                    }
                }
                if (down && !vp_prev_down) {
                    vp_color_mode = (vp_color_mode + 1) % VP_COLOR_MODE_COUNT;
                    if (vp_color_mode < vp_color_submenu_scroll) {
                        vp_color_submenu_scroll = vp_color_mode;
                    }
                    if (vp_color_mode >= vp_color_submenu_scroll + 8) {
                        vp_color_submenu_scroll = vp_color_mode - 7;
                    }
                }
                /* Close color submenu with A or B */
                if ((a && !vp_prev_a) || (b && !vp_prev_b)) {
                    vp_color_submenu_active = 0;
                }
            } else {
                /* Main menu navigation */
                if (up && !vp_prev_up) {
                    vp_menu_selection = (vp_menu_selection - 1 + VP_MENU_ITEMS) % VP_MENU_ITEMS;
                    vp_save_feedback_timer = 0;
                }
                if (down && !vp_prev_down) {
                    vp_menu_selection = (vp_menu_selection + 1) % VP_MENU_ITEMS;
                    vp_save_feedback_timer = 0;
                }

                /* L/R shoulder buttons for cycling options (exact copy from pmp123) */
                int cycle_prev = (l && !vp_prev_l);
                int cycle_next = (r && !vp_prev_r);

                if (cycle_prev || cycle_next) {
                    switch (vp_menu_selection) {
                        case VP_MENU_COLOR_MODE:
                            if (cycle_next) {
                                vp_color_mode = (vp_color_mode + 1) % VP_COLOR_MODE_COUNT;
                            } else {
                                vp_color_mode = (vp_color_mode - 1 + VP_COLOR_MODE_COUNT) % VP_COLOR_MODE_COUNT;
                            }
                            break;
                        case VP_MENU_PLAY_MODE:
                            if (cycle_next) {
                                vp_play_mode = (vp_play_mode + 1) % VP_PLAY_MODE_COUNT;
                            } else {
                                vp_play_mode = (vp_play_mode - 1 + VP_PLAY_MODE_COUNT) % VP_PLAY_MODE_COUNT;
                            }
                            break;
                        case VP_MENU_SHOW_TIME:
                            vp_show_time = !vp_show_time;
                            break;
                    }
                }

                /* Slider control for Go to Position (item 0) */
                if (vp_menu_selection == VP_MENU_GO_TO_POS) {
                    if (left && !vp_prev_left) {
                        if (vp_seek_position > 0) {
                            vp_seek_position--;
                            int target_frame = (vp_total_frames > 0) ? (vp_seek_position * vp_total_frames / 20) : 0;
                            vp_seek_to_frame(target_frame);
                        }
                    }
                    if (right && !vp_prev_right) {
                        if (vp_seek_position < 20) {
                            vp_seek_position++;
                            int target_frame = (vp_total_frames > 0) ? (vp_seek_position * vp_total_frames / 20) : 0;
                            vp_seek_to_frame(target_frame);
                        }
                    }
                }

                /* Menu action on A */
                if (a && !vp_prev_a) {
                    switch (vp_menu_selection) {
                        case VP_MENU_GO_TO_POS:
                            vp_paused = vp_was_paused_before_menu;
                            vp_menu_active = 0;
                            if (!vp_paused) {
                                vp_icon_type = VP_ICON_PLAY;
                                vp_icon_timer = VP_ICON_FRAMES;
                            }
                            break;
                        case VP_MENU_COLOR_MODE:
                            vp_color_submenu_active = 1;
                            vp_color_submenu_scroll = vp_color_mode - 3;
                            if (vp_color_submenu_scroll < 0) vp_color_submenu_scroll = 0;
                            if (vp_color_submenu_scroll > VP_COLOR_MODE_COUNT - 8)
                                vp_color_submenu_scroll = VP_COLOR_MODE_COUNT - 8;
                            break;
                        case VP_MENU_XVID_RANGE:
                            vp_xvid_black_level = (vp_xvid_black_level == VP_XVID_BLACK_TV) ?
                                                   VP_XVID_BLACK_PC : VP_XVID_BLACK_TV;
                            break;
                        case VP_MENU_PLAY_MODE:
                            vp_play_mode = (vp_play_mode + 1) % VP_PLAY_MODE_COUNT;
                            break;
                        case VP_MENU_SHOW_TIME:
                            vp_show_time = !vp_show_time;
                            break;
                        case VP_MENU_SAVE:
                            vp_save_settings();
                            vp_save_feedback_timer = VP_SAVE_FEEDBACK_FRAMES;
                            break;
                        case VP_MENU_INSTRUCTIONS:
                            vp_submenu_active = 1;
                            break;
                        case VP_MENU_ABOUT:
                            vp_submenu_active = 2;
                            break;
                    }
                }
            }
        } else {
            // Normal playback controls (like pmp123)

            // A button - toggle pause with icon
            if (a && !vp_prev_a) {
                vp_paused = !vp_paused;
                vp_icon_type = vp_paused ? VP_ICON_PAUSE : VP_ICON_PLAY;
                vp_icon_timer = VP_ICON_FRAMES;
            }

            // LEFT - rewind 15 seconds (like pmp123)
            if (left && !vp_prev_left && !vp_paused) {
                int skip = vp_clip_fps * 15;
                vp_seek_to_frame(vp_current_frame - skip);
                vp_icon_type = VP_ICON_SKIP_LEFT;
                vp_icon_timer = VP_ICON_FRAMES;
            }

            // RIGHT - fast forward 15 seconds (like pmp123)
            if (right && !vp_prev_right && !vp_paused) {
                int skip = vp_clip_fps * 15;
                vp_seek_to_frame(vp_current_frame + skip);
                vp_icon_type = VP_ICON_SKIP_RIGHT;
                vp_icon_timer = VP_ICON_FRAMES;
            }

            // UP - fast forward 1 minute (like pmp123)
            if (up && !vp_prev_up && !vp_paused) {
                int skip = vp_clip_fps * 60;
                vp_seek_to_frame(vp_current_frame + skip);
                vp_icon_type = VP_ICON_SKIP_FWD_1M;
                vp_icon_timer = VP_ICON_FRAMES;
            }

            // DOWN - rewind 1 minute (like pmp123)
            if (down && !vp_prev_down && !vp_paused) {
                int skip = vp_clip_fps * 60;
                vp_seek_to_frame(vp_current_frame - skip);
                vp_icon_type = VP_ICON_SKIP_BACK_1M;
                vp_icon_timer = VP_ICON_FRAMES;
            }
        }
    }

    // Save previous button states
    vp_prev_a = a;
    vp_prev_b = b;
    vp_prev_left = left;
    vp_prev_right = right;
    vp_prev_start = start;
    vp_prev_up = up;
    vp_prev_down = down;
    vp_prev_l = l;
    vp_prev_r = r;

    return 0;
}

void vp_render(uint16_t *framebuffer) {
    if (!vp_active || !framebuffer) return;

    vp_fb = framebuffer;  // Cache for drawing functions

    // Frame timing - EXACT copy from pmp123
    if (!vp_paused && !vp_menu_active) {
        // Decode new frame only when repeat_counter == 0
        if (vp_repeat_counter == 0) {
            if (vp_current_frame < vp_total_frames) {
                vp_decode_frame(vp_current_frame);
            }
        }
        // else: same frame displayed again, framebuffer already has it

        vp_repeat_counter++;
        if (vp_repeat_counter >= vp_repeat_count) {
            vp_repeat_counter = 0;
            vp_current_frame++;
        }

        // Play audio synced to frame position
        vp_play_audio_for_frame();

        // Handle end of video based on play mode
        if (vp_current_frame >= vp_total_frames) {
            if (vp_play_mode == VP_PLAY_MODE_REPEAT) {
                // Repeat current video
                vp_current_frame = 0;
                vp_audio_chunk_idx = 0;
                vp_audio_chunk_pos = 0;
                vp_audio_samples_sent = 0;
                vp_aring_read = 0;
                vp_aring_write = 0;
                vp_aring_count = 0;
                if (vp_audio_format == VP_AUDIO_FMT_MP3) {
                    vp_mp3_reset();
                }
                vp_mpeg4_extradata_sent = 0;
                vp_repeat_counter = 0;
                vp_refill_audio_ring();
            } else if (vp_play_mode == VP_PLAY_MODE_ONCE) {
                // Stop at end - pause
                vp_paused = 1;
                vp_current_frame = vp_total_frames - 1;
            } else if (vp_play_mode == VP_PLAY_MODE_AZ) {
                // Load next video alphabetically
                if (vp_playlist_count <= 0) vp_scan_playlist();
                if (!vp_load_next_az()) {
                    // Failed to load next - pause at end
                    vp_paused = 1;
                    vp_current_frame = vp_total_frames - 1;
                }
            } else if (vp_play_mode == VP_PLAY_MODE_SHUFFLE) {
                // Load random video
                if (vp_playlist_count <= 0) vp_scan_playlist();
                if (!vp_load_shuffle()) {
                    // Failed to load random - pause at end
                    vp_paused = 1;
                    vp_current_frame = vp_total_frames - 1;
                }
            }
        }
    }

    // Convert YUV to RGB565 and write to framebuffer
    vp_yuv_to_rgb565(framebuffer);

    // Draw time display (top left)
    int total_secs = (vp_clip_fps > 0) ? (vp_current_frame / vp_clip_fps) : 0;
    int total_duration = (vp_clip_fps > 0 && vp_total_frames > 0) ? (vp_total_frames / vp_clip_fps) : 0;
    int cur_min = total_secs / 60;
    int cur_sec = total_secs % 60;
    int dur_min = total_duration / 60;
    int dur_sec = total_duration % 60;

    int tx = 2;
    vp_draw_num(tx, 2, cur_min, 0xFFFF);
    tx += vp_num_width(cur_min);
    vp_draw_str(tx, 2, ":", 0xFFFF); tx += 6;
    if (cur_sec < 10) { vp_draw_str(tx, 2, "0", 0xFFFF); tx += 6; }
    vp_draw_num(tx, 2, cur_sec, 0xFFFF);
    tx += vp_num_width(cur_sec);
    vp_draw_str(tx, 2, "/", 0x7BEF); tx += 6;
    vp_draw_num(tx, 2, dur_min, 0x7BEF);
    tx += vp_num_width(dur_min);
    vp_draw_str(tx, 2, ":", 0x7BEF); tx += 6;
    if (dur_sec < 10) { vp_draw_str(tx, 2, "0", 0x7BEF); tx += 6; }
    vp_draw_num(tx, 2, dur_sec, 0x7BEF);

    // Draw pause indicator
    if (vp_paused && !vp_menu_active) {
        vp_draw_str(140, 2, "PAUSED", 0xF800);
    }

    // Draw visual feedback icon (skip, pause, lock, etc.)
    if (vp_icon_timer > 0) {
        vp_draw_icon(vp_icon_type);
        vp_icon_timer--;
        if (vp_icon_timer == 0) {
            vp_icon_type = VP_ICON_NONE;
        }
    }

    // Draw lock indicator when locked
    if (vp_is_locked && vp_icon_timer == 0) {
        // Small lock icon in corner
        vp_draw_str(300, 2, "L", 0xF800);
    }

    // Draw menu overlay if active
    if (vp_menu_active) {
        vp_draw_menu();
        // Decrement save feedback timer
        if (vp_save_feedback_timer > 0) {
            vp_save_feedback_timer--;
        }
    }
}

int vp_is_paused(void) {
    return vp_paused;
}

void vp_toggle_pause(void) {
    vp_paused = !vp_paused;
}

int vp_get_total_frames(void) {
    return vp_total_frames;
}

int vp_get_current_frame(void) {
    return vp_current_frame;
}

int vp_get_fps(void) {
    return vp_clip_fps;
}

int vp_get_next_video_request(void) {
    return vp_next_video_requested;
}

void vp_clear_next_video_request(void) {
    vp_next_video_requested = 0;
}

const char *vp_get_current_dir(void) {
    return vp_current_dir;
}

const char *vp_get_current_path(void) {
    return vp_current_path;
}

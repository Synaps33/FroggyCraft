/*
 * FrogUI Image Viewer
 * v56: Full-featured image viewer for comics and photos
 */

#include "image_viewer.h"
#include "render.h"
#include "font.h"
#include "theme.h"
#include "music_player.h"  // v69: For pausing music during heavy image load
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SF2000
#include "../../stockfw.h"
#include "../../dirent.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Screen dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Max image size (limited by 6MB buffer at RGB565 = 3M pixels)
// We use 1732x1732 max which gives ~3MB, leaving room for temp decode buffers
#define MAX_IMAGE_WIDTH 1800
#define MAX_IMAGE_HEIGHT 1800
#define MAX_IMAGE_PIXELS (1732 * 1732)

// Zoom levels (fixed point 8.8 format: 256 = 100%)
#define ZOOM_FP_SHIFT 8
#define ZOOM_100_PERCENT 256
#define ZOOM_MIN 32    // 12.5% minimum zoom
#define ZOOM_MAX 256   // 100% maximum zoom (native size)
#define ZOOM_STEP 16   // ~6% zoom increment

// Pan speed (v57: reduced 3x)
#define PAN_NORMAL 16
#define PAN_FAST_MULT 5    // 2.5x = 16 * 5 / 2 = 40
#define PAN_FAST_DIV 2
#define PAN_SLOW_MULT 2    // 0.4x = 16 * 2 / 5 = 6
#define PAN_SLOW_DIV 5

// Playlist
#define MAX_PLAYLIST 512
#define MAX_PATH_LEN 512
#define MAX_FILENAME_LEN 256

// State
static int iv_active = 0;
static uint16_t *iv_image_data = NULL;  // Points into universal buffer
static int iv_image_width = 0;
static int iv_image_height = 0;

// View state
static int iv_view_x = 0;    // Top-left corner X in source image
static int iv_view_y = 0;    // Top-left corner Y in source image
static int iv_zoom = ZOOM_100_PERCENT;  // Current zoom level (FP 8.8)
static int iv_fit_zoom = ZOOM_100_PERCENT;  // Zoom level for "fit to screen"

// v70: Chunked loading state machine
#define IV_LOAD_IDLE     0
#define IV_LOAD_READING  1
#define IV_LOAD_DECODING 2
#define IV_LOAD_DONE     3
#define IV_LOAD_ERROR    4

#define IV_READ_CHUNK_SIZE (32 * 1024)  // 32KB per frame
#define IV_MAX_FILE_SIZE (4 * 1024 * 1024)  // 4MB max file

static int iv_load_state = IV_LOAD_IDLE;
static FILE *iv_load_file = NULL;
static uint8_t *iv_file_buffer = NULL;
static uint32_t iv_file_size = 0;
static uint32_t iv_file_read = 0;
static int iv_load_format = 0;  // 1=png, 2=jpg, 3=bmp, 4=gif, 5=webp
static int iv_saved_zoom = 0;           // v70: Saved zoom for restoring after chunked load
static int iv_pending_playlist_idx = -1; // v70: Pending playlist index after next/prev

// File info
static char iv_current_path[MAX_PATH_LEN];
static char iv_current_dir[MAX_PATH_LEN];
static char iv_current_filename[MAX_FILENAME_LEN];

// Playlist
static char iv_playlist[MAX_PLAYLIST][MAX_FILENAME_LEN];
static int iv_playlist_count = 0;
static int iv_playlist_current = -1;

// Error state
static int iv_error_timer = 0;
static char iv_error_message[64];

// Previous input state for edge detection
static int iv_prev_input[12] = {0};

// Forward declarations
static int iv_load_image(const char *path);
static void iv_scan_playlist(void);
static int iv_str_ends_with_ci(const char *str, const char *suffix);
static void iv_render_scaled(uint16_t *framebuffer);
static void iv_clamp_view(void);
static int iv_load_next(int direction);
static int iv_start_load(const char *path);  // v70: Start chunked load
static int iv_load_chunk(void);              // v70: Load one chunk
static int iv_decode_from_memory(void);      // v70: Decode from buffer

// Case-insensitive string compare
static int iv_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int iv_str_ends_with_ci(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return iv_strcasecmp(str + str_len - suf_len, suffix) == 0;
}

static int iv_is_image_file(const char *filename) {
    return iv_str_ends_with_ci(filename, ".png") ||
           iv_str_ends_with_ci(filename, ".jpg") ||
           iv_str_ends_with_ci(filename, ".jpeg") ||
           iv_str_ends_with_ci(filename, ".bmp") ||
           iv_str_ends_with_ci(filename, ".gif") ||
           iv_str_ends_with_ci(filename, ".webp");
}

// v70: Start chunked loading - opens file, allocates buffer, begins reading
static int iv_start_load(const char *path) {
    // Clean up any previous load
    if (iv_load_file) { fclose(iv_load_file); iv_load_file = NULL; }
    if (iv_file_buffer) { free(iv_file_buffer); iv_file_buffer = NULL; }
    iv_load_state = IV_LOAD_IDLE;
    iv_file_size = 0;
    iv_file_read = 0;

    // Determine format from extension
    iv_load_format = 0;
    if (iv_str_ends_with_ci(path, ".png")) iv_load_format = 1;
    else if (iv_str_ends_with_ci(path, ".jpg") || iv_str_ends_with_ci(path, ".jpeg")) iv_load_format = 2;
    else if (iv_str_ends_with_ci(path, ".bmp")) iv_load_format = 3;
    else if (iv_str_ends_with_ci(path, ".gif")) iv_load_format = 4;
    else if (iv_str_ends_with_ci(path, ".webp")) iv_load_format = 5;

    if (iv_load_format == 0) {
        strcpy(iv_error_message, "Unknown format");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Open file
    iv_load_file = fopen(path, "rb");
    if (!iv_load_file) {
        strcpy(iv_error_message, "Cannot open file");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Get file size
    fseek(iv_load_file, 0, SEEK_END);
    iv_file_size = ftell(iv_load_file);
    fseek(iv_load_file, 0, SEEK_SET);

    if (iv_file_size > IV_MAX_FILE_SIZE) {
        fclose(iv_load_file);
        iv_load_file = NULL;
        strcpy(iv_error_message, "File too large");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Allocate buffer
    iv_file_buffer = (uint8_t*)malloc(iv_file_size);
    if (!iv_file_buffer) {
        fclose(iv_load_file);
        iv_load_file = NULL;
        strcpy(iv_error_message, "Out of memory");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    iv_file_read = 0;
    iv_load_state = IV_LOAD_READING;
    return 1;
}

// v70: Load one chunk of file data - returns 1 if still reading, 0 if done/error
static int iv_load_chunk(void) {
    if (iv_load_state != IV_LOAD_READING) return 0;
    if (!iv_load_file || !iv_file_buffer) {
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Read one chunk
    uint32_t remaining = iv_file_size - iv_file_read;
    uint32_t to_read = (remaining < IV_READ_CHUNK_SIZE) ? remaining : IV_READ_CHUNK_SIZE;

    if (to_read > 0) {
        size_t read = fread(iv_file_buffer + iv_file_read, 1, to_read, iv_load_file);
        iv_file_read += read;

        if (read < to_read && !feof(iv_load_file)) {
            // Read error
            fclose(iv_load_file);
            iv_load_file = NULL;
            free(iv_file_buffer);
            iv_file_buffer = NULL;
            strcpy(iv_error_message, "Read error");
            iv_error_timer = 120;
            iv_load_state = IV_LOAD_ERROR;
            return 0;
        }
    }

    // Check if done reading
    if (iv_file_read >= iv_file_size) {
        fclose(iv_load_file);
        iv_load_file = NULL;
        iv_load_state = IV_LOAD_DECODING;
        return 0;  // Done reading, need to decode
    }

    return 1;  // Still reading
}

// v70: Decode image from memory buffer
static int iv_decode_from_memory(void) {
    if (iv_load_state != IV_LOAD_DECODING) return 0;
    if (!iv_file_buffer || iv_file_size == 0) {
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // v71: Aggressively pre-fill audio buffer before heavy decode
    // Fill buffer to maximum to survive long decode time
    if (mp_is_active() && !mp_is_paused()) {
        for (int i = 0; i < 32; i++) {  // Much more iterations to fill buffer completely
            mp_update_audio();
        }
        // Reset audio timing so it doesn't try to catch up after decode
        mp_reset_audio_timing();
    }

    uint16_t *loaded_data = NULL;
    int w = 0, h = 0;
    int loaded = 0;

    // Decode from memory based on format
    switch (iv_load_format) {
        case 1:  // PNG
            loaded = load_png_rgb565_mem(iv_file_buffer, iv_file_size, &loaded_data, &w, &h);
            break;
        case 2:  // JPEG
            loaded = load_jpeg_rgb565_mem(iv_file_buffer, iv_file_size, &loaded_data, &w, &h);
            break;
        case 3:  // BMP
            loaded = load_bmp_rgb565_mem(iv_file_buffer, iv_file_size, &loaded_data, &w, &h);
            break;
        case 4:  // GIF
            loaded = load_gif_rgb565_mem(iv_file_buffer, iv_file_size, &loaded_data, &w, &h);
            break;
        case 5:  // WebP
            loaded = load_webp_rgb565_mem(iv_file_buffer, iv_file_size, &loaded_data, &w, &h);
            break;
    }

    // Free file buffer - no longer needed
    free(iv_file_buffer);
    iv_file_buffer = NULL;

    // v71: Reset audio timing after decode so it doesn't try to catch up
    if (mp_is_active()) {
        mp_reset_audio_timing();
        mp_update_audio();  // One update to resume normal operation
    }

    if (!loaded || !loaded_data || w <= 0 || h <= 0) {
        strcpy(iv_error_message, "Decode failed");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Check if image is too large
    if (w > MAX_IMAGE_WIDTH || h > MAX_IMAGE_HEIGHT || (w * h) > MAX_IMAGE_PIXELS) {
        strcpy(iv_error_message, "Image too large");
        iv_error_timer = 120;
        iv_load_state = IV_LOAD_ERROR;
        return 0;
    }

    // Store image data
    iv_image_data = loaded_data;
    iv_image_width = w;
    iv_image_height = h;

    // Calculate fit zoom
    int zoom_x = (SCREEN_WIDTH * ZOOM_100_PERCENT) / w;
    int zoom_y = (SCREEN_HEIGHT * ZOOM_100_PERCENT) / h;
    iv_fit_zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;
    if (iv_fit_zoom > ZOOM_100_PERCENT) iv_fit_zoom = ZOOM_100_PERCENT;
    if (iv_fit_zoom < ZOOM_MIN) iv_fit_zoom = ZOOM_MIN;

    iv_zoom = iv_fit_zoom;
    iv_view_x = 0;
    iv_view_y = 0;

    // v70: Restore saved zoom from prev/next navigation
    if (iv_pending_playlist_idx >= 0) {
        iv_playlist_current = iv_pending_playlist_idx;
        iv_pending_playlist_idx = -1;
        // Restore zoom but clamp to valid range for new image
        if (iv_saved_zoom <= ZOOM_MAX && iv_saved_zoom >= iv_fit_zoom) {
            iv_zoom = iv_saved_zoom;
        }
        iv_clamp_view();
    }

    iv_load_state = IV_LOAD_DONE;
    return 1;
}

void iv_init(void) {
    iv_active = 0;
    iv_image_data = NULL;
    iv_playlist_count = 0;
    iv_playlist_current = -1;
}

static void iv_scan_playlist(void) {
    iv_playlist_count = 0;
    iv_playlist_current = -1;

    DIR *dir = opendir(iv_current_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && iv_playlist_count < MAX_PLAYLIST) {
        if (entry->d_name[0] == '.') continue;

        if (iv_is_image_file(entry->d_name)) {
            strncpy(iv_playlist[iv_playlist_count], entry->d_name, MAX_FILENAME_LEN - 1);
            iv_playlist[iv_playlist_count][MAX_FILENAME_LEN - 1] = '\0';

            // Check if this is current file
            if (iv_strcasecmp(entry->d_name, iv_current_filename) == 0) {
                iv_playlist_current = iv_playlist_count;
            }
            iv_playlist_count++;
        }
    }
    closedir(dir);

    // Sort playlist alphabetically (simple bubble sort - small lists)
    for (int i = 0; i < iv_playlist_count - 1; i++) {
        for (int j = i + 1; j < iv_playlist_count; j++) {
            if (iv_strcasecmp(iv_playlist[i], iv_playlist[j]) > 0) {
                char temp[MAX_FILENAME_LEN];
                strcpy(temp, iv_playlist[i]);
                strcpy(iv_playlist[i], iv_playlist[j]);
                strcpy(iv_playlist[j], temp);
            }
        }
    }

    // Find current file index after sorting
    for (int i = 0; i < iv_playlist_count; i++) {
        if (iv_strcasecmp(iv_playlist[i], iv_current_filename) == 0) {
            iv_playlist_current = i;
            break;
        }
    }
}

static int iv_load_image(const char *path) {
    uint16_t *loaded_data = NULL;
    int w = 0, h = 0;
    int loaded = 0;

    // Try to load based on extension
    if (iv_str_ends_with_ci(path, ".png")) {
        loaded = load_png_rgb565(path, &loaded_data, &w, &h);
    } else if (iv_str_ends_with_ci(path, ".jpg") || iv_str_ends_with_ci(path, ".jpeg")) {
        loaded = load_jpeg_rgb565(path, &loaded_data, &w, &h);
    } else if (iv_str_ends_with_ci(path, ".bmp")) {
        loaded = load_bmp_rgb565(path, &loaded_data, &w, &h);
    } else if (iv_str_ends_with_ci(path, ".gif")) {
        loaded = load_gif_rgb565(path, &loaded_data, &w, &h);
    } else if (iv_str_ends_with_ci(path, ".webp")) {
        loaded = load_webp_rgb565(path, &loaded_data, &w, &h);
    }

    if (!loaded || !loaded_data || w <= 0 || h <= 0) {
        strcpy(iv_error_message, "Failed to load image");
        iv_error_timer = 120;  // 2 seconds at 60fps
        return 0;
    }

    // Check if image is too large
    if (w > MAX_IMAGE_WIDTH || h > MAX_IMAGE_HEIGHT || (w * h) > MAX_IMAGE_PIXELS) {
        strcpy(iv_error_message, "Image too large");
        iv_error_timer = 120;  // 2 seconds at 60fps
        // loaded_data points to universal_buffer, no free needed
        return 0;
    }

    // Store image data (it's already in universal buffer from load functions)
    iv_image_data = loaded_data;
    iv_image_width = w;
    iv_image_height = h;

    // Calculate fit zoom (scale to fit screen while preserving aspect ratio)
    int zoom_x = (SCREEN_WIDTH * ZOOM_100_PERCENT) / w;
    int zoom_y = (SCREEN_HEIGHT * ZOOM_100_PERCENT) / h;
    iv_fit_zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;

    // Clamp fit zoom to valid range
    if (iv_fit_zoom > ZOOM_100_PERCENT) iv_fit_zoom = ZOOM_100_PERCENT;
    if (iv_fit_zoom < ZOOM_MIN) iv_fit_zoom = ZOOM_MIN;

    // Start at fit zoom
    iv_zoom = iv_fit_zoom;
    iv_view_x = 0;
    iv_view_y = 0;

    return 1;
}

int iv_open(const char *path) {
    if (!path || path[0] == '\0') return 0;

    // v70: No longer pause music - we load in chunks respecting retro_run

    // Store path info
    strncpy(iv_current_path, path, MAX_PATH_LEN - 1);
    iv_current_path[MAX_PATH_LEN - 1] = '\0';

    // Extract directory
    strncpy(iv_current_dir, path, MAX_PATH_LEN - 1);
    char *last_slash = strrchr(iv_current_dir, '/');
    if (!last_slash) last_slash = strrchr(iv_current_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
        strncpy(iv_current_filename, last_slash + 1, MAX_FILENAME_LEN - 1);
    } else {
        iv_current_dir[0] = '.';
        iv_current_dir[1] = '\0';
        strncpy(iv_current_filename, path, MAX_FILENAME_LEN - 1);
    }
    iv_current_filename[MAX_FILENAME_LEN - 1] = '\0';

    // v70: Start chunked loading instead of blocking load
    if (!iv_start_load(path)) {
        return 0;
    }

    // Scan directory for playlist
    iv_scan_playlist();

    // Clear previous input state
    memset(iv_prev_input, 0, sizeof(iv_prev_input));

    iv_active = 1;
    iv_error_timer = 0;
    return 1;
}

void iv_close(void) {
    iv_active = 0;
    iv_image_data = NULL;  // Points to universal_buffer, don't free
    iv_image_width = 0;
    iv_image_height = 0;
    iv_playlist_count = 0;
    iv_playlist_current = -1;

    // v70: Clean up any loading state
    if (iv_load_file) { fclose(iv_load_file); iv_load_file = NULL; }
    if (iv_file_buffer) { free(iv_file_buffer); iv_file_buffer = NULL; }
    iv_load_state = IV_LOAD_IDLE;
    iv_file_size = 0;
    iv_file_read = 0;
}

int iv_is_active(void) {
    return iv_active;
}

// v70: Update loading state - call every frame to continue chunked loading
int iv_update(void) {
    if (!iv_active) return 0;

    // Keep music playing while loading
    if (mp_is_active()) {
        mp_update_audio();
    }

    // Handle loading state machine
    switch (iv_load_state) {
        case IV_LOAD_READING:
            // Load one chunk per frame
            iv_load_chunk();
            return 1;  // Still loading

        case IV_LOAD_DECODING:
            // Decode from memory (this is the slow part, but we call mp_update_audio inside)
            iv_decode_from_memory();
            return 0;  // Done loading (or error)

        case IV_LOAD_DONE:
        case IV_LOAD_ERROR:
        case IV_LOAD_IDLE:
        default:
            return 0;  // Not loading
    }
}

static int iv_load_next(int direction) {
    if (iv_playlist_count <= 1) return 0;

    // v70: Don't start new load while already loading
    if (iv_load_state == IV_LOAD_READING || iv_load_state == IV_LOAD_DECODING) return 0;

    int next_idx = iv_playlist_current + direction;
    if (next_idx < 0) next_idx = iv_playlist_count - 1;
    if (next_idx >= iv_playlist_count) next_idx = 0;

    char new_path[MAX_PATH_LEN];
    snprintf(new_path, MAX_PATH_LEN, "%s/%s", iv_current_dir, iv_playlist[next_idx]);

    // v70: Save current zoom and pending index to restore after chunked loading completes
    iv_saved_zoom = iv_zoom;
    iv_pending_playlist_idx = next_idx;

    // v70: Use chunked loading
    if (iv_start_load(new_path)) {
        strncpy(iv_current_path, new_path, MAX_PATH_LEN - 1);
        strncpy(iv_current_filename, iv_playlist[next_idx], MAX_FILENAME_LEN - 1);
        return 1;
    }
    iv_pending_playlist_idx = -1;
    return 0;
}

static void iv_clamp_view(void) {
    // Calculate visible area in source pixels at current zoom
    int visible_w = (SCREEN_WIDTH * ZOOM_100_PERCENT) / iv_zoom;
    int visible_h = (SCREEN_HEIGHT * ZOOM_100_PERCENT) / iv_zoom;

    // Clamp to image bounds
    if (iv_view_x < 0) iv_view_x = 0;
    if (iv_view_y < 0) iv_view_y = 0;

    int max_x = iv_image_width - visible_w;
    int max_y = iv_image_height - visible_h;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    if (iv_view_x > max_x) iv_view_x = max_x;
    if (iv_view_y > max_y) iv_view_y = max_y;
}

int iv_handle_input(int up, int down, int left, int right,
                    int a, int b, int x, int y, int l, int r) {
    if (!iv_active) return 0;

    // v70: Handle loading state - only allow B to cancel
    if (iv_load_state == IV_LOAD_READING || iv_load_state == IV_LOAD_DECODING) {
        // B - cancel loading
        if (iv_prev_input[3] && !b) {
            // Clean up loading state
            if (iv_load_file) { fclose(iv_load_file); iv_load_file = NULL; }
            if (iv_file_buffer) { free(iv_file_buffer); iv_file_buffer = NULL; }
            iv_load_state = IV_LOAD_IDLE;
            iv_close();
            return 0;
        }
        iv_prev_input[3] = b;
        return 1;  // Stay active while loading
    }

    // Handle error display timer
    if (iv_error_timer > 0) {
        iv_error_timer--;
        // B closes error message
        if (iv_prev_input[3] && !b) {
            iv_error_timer = 0;
        }
        // Update prev input
        iv_prev_input[3] = b;
        return 1;
    }

    // B - close viewer
    if (iv_prev_input[3] && !b && !a) {
        iv_close();
        return 0;
    }

    // X - zoom in (on release)
    if (iv_prev_input[9] && !x) {
        if (iv_zoom < ZOOM_MAX) {
            iv_zoom += ZOOM_STEP;
            if (iv_zoom > ZOOM_MAX) iv_zoom = ZOOM_MAX;
            iv_clamp_view();
        }
    }

    // Y - zoom out step by step (on release) - v58: gradual like X
    if (iv_prev_input[10] && !y) {
        if (iv_zoom > iv_fit_zoom) {
            iv_zoom -= ZOOM_STEP;
            if (iv_zoom < iv_fit_zoom) iv_zoom = iv_fit_zoom;
            iv_clamp_view();
        }
    }

    // L - previous image (on release)
    if (iv_prev_input[4] && !l) {
        iv_load_next(-1);
    }

    // R - next image (on release)
    if (iv_prev_input[5] && !r) {
        iv_load_next(1);
    }

    // D-PAD panning (continuous while held)
    // v58: A = slower pan, B = exit only (no pan modifier)
    int pan_speed = PAN_NORMAL;
    if (a) {
        // A held - slower pan
        pan_speed = (PAN_NORMAL * PAN_SLOW_MULT) / PAN_SLOW_DIV;
    }

    // Convert screen pan speed to source pixels based on zoom
    int src_pan = (pan_speed * ZOOM_100_PERCENT) / iv_zoom;
    if (src_pan < 1) src_pan = 1;

    if (up) iv_view_y -= src_pan;
    if (down) iv_view_y += src_pan;
    if (left) iv_view_x -= src_pan;
    if (right) iv_view_x += src_pan;

    iv_clamp_view();

    // Update previous input state
    iv_prev_input[0] = up;
    iv_prev_input[1] = down;
    iv_prev_input[2] = a;
    iv_prev_input[3] = b;
    iv_prev_input[4] = l;
    iv_prev_input[5] = r;
    iv_prev_input[7] = left;
    iv_prev_input[8] = right;
    iv_prev_input[9] = x;
    iv_prev_input[10] = y;

    return 1;
}

// Bilinear interpolation with integer math (fixed point 16.16)
static inline uint16_t iv_bilinear_sample(int src_x_fp, int src_y_fp) {
    int x0 = src_x_fp >> 16;
    int y0 = src_y_fp >> 16;
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Clamp to image bounds
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= iv_image_width) x1 = iv_image_width - 1;
    if (y1 >= iv_image_height) y1 = iv_image_height - 1;
    if (x0 >= iv_image_width) x0 = iv_image_width - 1;
    if (y0 >= iv_image_height) y0 = iv_image_height - 1;

    // Fractional parts (0-65535)
    int fx = src_x_fp & 0xFFFF;
    int fy = src_y_fp & 0xFFFF;
    int ifx = 65536 - fx;
    int ify = 65536 - fy;

    // Get four corner pixels
    uint16_t p00 = iv_image_data[y0 * iv_image_width + x0];
    uint16_t p10 = iv_image_data[y0 * iv_image_width + x1];
    uint16_t p01 = iv_image_data[y1 * iv_image_width + x0];
    uint16_t p11 = iv_image_data[y1 * iv_image_width + x1];

    // Extract RGB components (RGB565)
    int r00 = (p00 >> 11) & 0x1F, g00 = (p00 >> 5) & 0x3F, b00 = p00 & 0x1F;
    int r10 = (p10 >> 11) & 0x1F, g10 = (p10 >> 5) & 0x3F, b10 = p10 & 0x1F;
    int r01 = (p01 >> 11) & 0x1F, g01 = (p01 >> 5) & 0x3F, b01 = p01 & 0x1F;
    int r11 = (p11 >> 11) & 0x1F, g11 = (p11 >> 5) & 0x3F, b11 = p11 & 0x1F;

    // Bilinear interpolation for each channel
    // Using 32-bit intermediate to avoid overflow
    int r = (r00 * ifx + r10 * fx) >> 16;
    int r_top = r;
    r = (r01 * ifx + r11 * fx) >> 16;
    int r_bot = r;
    r = (r_top * ify + r_bot * fy) >> 16;

    int g = (g00 * ifx + g10 * fx) >> 16;
    int g_top = g;
    g = (g01 * ifx + g11 * fx) >> 16;
    int g_bot = g;
    g = (g_top * ify + g_bot * fy) >> 16;

    int b = (b00 * ifx + b10 * fx) >> 16;
    int b_top = b;
    b = (b01 * ifx + b11 * fx) >> 16;
    int b_bot = b;
    b = (b_top * ify + b_bot * fy) >> 16;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void iv_render_scaled(uint16_t *framebuffer) {
    if (!iv_image_data) return;

    // Calculate source step per destination pixel (fixed point 16.16)
    // step = (1 / zoom) in source pixels per screen pixel
    // zoom is 8.8 fixed point (256 = 100%)
    // step_fp = (65536 * 256) / zoom = 16777216 / zoom
    int step_fp = 16777216 / iv_zoom;

    // Starting position in source image (fixed point 16.16)
    int src_x_start = iv_view_x << 16;
    int src_y = iv_view_y << 16;

    for (int dy = 0; dy < SCREEN_HEIGHT; dy++) {
        int src_x = src_x_start;
        int dst_idx = dy * SCREEN_WIDTH;

        for (int dx = 0; dx < SCREEN_WIDTH; dx++) {
            // Check bounds (integer part)
            int sx = src_x >> 16;
            int sy = src_y >> 16;

            if (sx >= 0 && sx < iv_image_width && sy >= 0 && sy < iv_image_height) {
                framebuffer[dst_idx + dx] = iv_bilinear_sample(src_x, src_y);
            } else {
                // Black for out-of-bounds
                framebuffer[dst_idx + dx] = 0x0000;
            }

            src_x += step_fp;
        }
        src_y += step_fp;
    }
}

void iv_render(uint16_t *framebuffer) {
    if (!iv_active) return;

    // v71: Show loading progress ON TOP of current image (if any)
    if (iv_load_state == IV_LOAD_READING || iv_load_state == IV_LOAD_DECODING) {
        // First render current image as background (if we have one)
        if (iv_image_data && iv_image_width > 0 && iv_image_height > 0) {
            iv_render_scaled(framebuffer);
            // Darken the image slightly for overlay visibility
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                uint16_t c = framebuffer[i];
                int r = ((c >> 11) & 0x1F) >> 1;
                int g = ((c >> 5) & 0x3F) >> 1;
                int b = (c & 0x1F) >> 1;
                framebuffer[i] = (r << 11) | (g << 5) | b;
            }
        } else {
            // No image yet - use black background
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                framebuffer[i] = 0x0000;
            }
        }

        // Calculate loading percentage
        int percent = 0;
        const char *status_text;
        if (iv_load_state == IV_LOAD_READING && iv_file_size > 0) {
            percent = (iv_file_read * 100) / iv_file_size;
            status_text = "Loading...";
        } else {
            percent = 100;
            status_text = "Decoding...";
        }

        // Draw semi-transparent box for text
        int box_x = 60, box_y = 90, box_w = 200, box_h = 60;
        for (int y = box_y; y < box_y + box_h; y++) {
            for (int x = box_x; x < box_x + box_w; x++) {
                uint16_t c = framebuffer[y * SCREEN_WIDTH + x];
                int r = ((c >> 11) & 0x1F) >> 1;
                int g = ((c >> 5) & 0x3F) >> 1;
                int b = (c & 0x1F) >> 1;
                framebuffer[y * SCREEN_WIDTH + x] = (r << 11) | (g << 5) | b;
            }
        }

        // Draw loading text
        char load_text[64];
        snprintf(load_text, sizeof(load_text), "%s %d%%", status_text, percent);
        int text_w = font_measure_text(load_text);
        int text_x = (SCREEN_WIDTH - text_w) / 2;
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, 100, load_text, 0xFFFF);

        // Draw progress bar
        int bar_w = 180;
        int bar_h = 14;
        int bar_x = (SCREEN_WIDTH - bar_w) / 2;
        int bar_y = 125;

        // Bar background (dark gray)
        for (int y = bar_y; y < bar_y + bar_h; y++) {
            for (int x = bar_x; x < bar_x + bar_w; x++) {
                framebuffer[y * SCREEN_WIDTH + x] = 0x4208;
            }
        }

        // Bar fill (green)
        int fill_w = (bar_w * percent) / 100;
        for (int y = bar_y + 2; y < bar_y + bar_h - 2; y++) {
            for (int x = bar_x + 2; x < bar_x + 2 + fill_w - 4 && x < bar_x + bar_w - 2; x++) {
                framebuffer[y * SCREEN_WIDTH + x] = 0x07E0;
            }
        }

        return;
    }

    // Show error message if any
    if (iv_error_timer > 0) {
        // Draw semi-transparent background
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            uint16_t c = framebuffer[i];
            int r = ((c >> 11) & 0x1F) >> 1;
            int g = ((c >> 5) & 0x3F) >> 1;
            int b = (c & 0x1F) >> 1;
            framebuffer[i] = (r << 11) | (g << 5) | b;
        }

        // Draw error box
        int box_w = 200;
        int box_h = 50;
        int box_x = (SCREEN_WIDTH - box_w) / 2;
        int box_y = (SCREEN_HEIGHT - box_h) / 2;

        // Red background
        for (int y = box_y; y < box_y + box_h; y++) {
            for (int x = box_x; x < box_x + box_w; x++) {
                framebuffer[y * SCREEN_WIDTH + x] = 0xF800;  // Red
            }
        }

        // Draw error message
        int text_x = box_x + 10;
        int text_y = box_y + 18;
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, text_y,
                      iv_error_message, 0xFFFF);  // White text
        return;
    }

    // Render scaled image
    iv_render_scaled(framebuffer);

    // Draw info overlay at bottom
    char info[64];
    int zoom_percent = (iv_zoom * 100) / ZOOM_100_PERCENT;
    snprintf(info, sizeof(info), "%dx%d  %d%%  [%d/%d]",
             iv_image_width, iv_image_height, zoom_percent,
             iv_playlist_current + 1, iv_playlist_count);

    // Semi-transparent bar at bottom
    for (int y = SCREEN_HEIGHT - 20; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint16_t c = framebuffer[y * SCREEN_WIDTH + x];
            int r = ((c >> 11) & 0x1F) >> 1;
            int g = ((c >> 5) & 0x3F) >> 1;
            int b = (c & 0x1F) >> 1;
            framebuffer[y * SCREEN_WIDTH + x] = (r << 11) | (g << 5) | b;
        }
    }

    // Draw info text centered
    int text_w = font_measure_text(info);
    int text_x = (SCREEN_WIDTH - text_w) / 2;
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, SCREEN_HEIGHT - 16,
                  info, 0xFFFF);
}

int iv_get_image_width(void) {
    return iv_image_width;
}

int iv_get_image_height(void) {
    return iv_image_height;
}

int iv_get_zoom_percent(void) {
    return (iv_zoom * 100) / ZOOM_100_PERCENT;
}

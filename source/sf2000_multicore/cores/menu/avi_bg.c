/*
 * avi_bg.c - Animated AVI background support for FrogUI
 *
 * Simplified AVI/XVID decoder for background animations.
 * Based on pmp123 video player code, stripped down for video-only playback.
 */

#include "avi_bg.h"
#include "xvid/xvid.h"
#include "xvid/image/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Stub for image_printf - not needed for decoding, just for debug output */
void image_printf(IMAGE *img, int edged_width, int height, int x, int y, char *fmt, ...) {
    (void)img; (void)edged_width; (void)height; (void)x; (void)y; (void)fmt;
}

/* Maximum frames we can index */
#define MAX_FRAMES 4096

/* Maximum frame data size */
#define MAX_FRAME_SIZE (320 * 240 * 2)

/* AVI state */
static FILE *avi_file = NULL;
static uint32_t frame_offsets[MAX_FRAMES];
static uint32_t frame_sizes[MAX_FRAMES];
static int total_frames = 0;
static int current_frame = 0;
static int video_width = 0;
static int video_height = 0;

/* XVID decoder state */
static void *xvid_handle = NULL;
static int xvid_initialized = 0;
static int xvid_global_initialized = 0;  /* v22: Never reset - tracks global XVID lib init */

/* Frame buffers */
static uint8_t *frame_buffer = NULL;
static uint8_t *yuv_buffer = NULL;
static uint8_t *yuv_y = NULL;
static uint8_t *yuv_u = NULL;
static uint8_t *yuv_v = NULL;
static uint16_t *rgb_buffer = NULL;

/* MPEG-4 extradata (VOL header) */
#define MAX_EXTRADATA_SIZE 256
static uint8_t mpeg4_extradata[MAX_EXTRADATA_SIZE];
static int mpeg4_extradata_size = 0;
static int mpeg4_extradata_sent = 0;

/* Animation state */
static bool is_active = false;
static bool is_paused = false;

/* Frame timing from AVI header */
static uint32_t us_per_frame = 66666;  /* microseconds per frame (default 15fps) */
static uint32_t clip_fps = 15;          /* frames per second */

/* Repeat timing - COPIED FROM pmp123 (15fps content on 30/60fps display) */
static int repeat_count = 2;    /* decode every N retro_run calls */
static int repeat_counter = 0;  /* current counter */

/* v14: DEBUG COUNTERS */
static int dbg_advance_calls = 0;      /* avi_bg_advance_frame() calls */
static int dbg_decode_calls = 0;       /* decode_frame() calls */
static int dbg_decode_success = 0;     /* decode with xstats.type > 0 */
static int dbg_yuv_convert = 0;        /* yuv_to_rgb565() calls */
static int dbg_last_frame = -1;        /* last decoded frame index */
static int dbg_last_xstats_type = 0;   /* last xstats.type value */

/* 4x4 Bayer dithering matrix - COPIED EXACTLY FROM pmp123 */
static const int8_t bayer4x4[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 }
};

/* YUV->RGB lookup tables - EXACTLY like pmp123 */
static int16_t yuv_y_table[256];
static int16_t yuv_rv_table[256];
static int16_t yuv_gu_table[256];
static int16_t yuv_gv_table[256];
static int16_t yuv_bu_table[256];
static int yuv_tables_initialized = 0;

/* Helper functions */
static inline uint32_t read_u32_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static inline uint16_t read_u16_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8);
}

static int check4(FILE *fp, const char *tag) {
    char buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    return (buf[0] == tag[0] && buf[1] == tag[1] &&
            buf[2] == tag[2] && buf[3] == tag[3]);
}

static int read32(FILE *fp, uint32_t *val) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, fp) != 4) return -1;
    *val = read_u32_le(buf);
    return 0;
}

/* Initialize YUV->RGB tables - COPIED FROM pmp123 (TV range expansion) */
static void init_yuv_tables(void) {
    if (yuv_tables_initialized) return;

    for (int i = 0; i < 256; i++) {
        /* TV/Limited range (16-235 -> 0-255) - XVID outputs TV range!
         * Formula: Y' = (Y - 16) * 255 / 219 = (Y - 16) * 298 / 256 */
        int y_limited = ((i - 16) * 298) >> 8;
        if (y_limited < 0) y_limited = 0;
        if (y_limited > 255) y_limited = 255;
        yuv_y_table[i] = y_limited;

        /* U/V contributions - BT.601 coefficients
         * R = Y' + 1.402 * (V-128)
         * G = Y' - 0.344 * (U-128) - 0.714 * (V-128)
         * B = Y' + 1.772 * (U-128) */
        int uv = i - 128;
        yuv_rv_table[i] = (1436 * uv) >> 10;   /* 1.402 * 1024 = 1436 */
        yuv_gu_table[i] = (-352 * uv) >> 10;   /* -0.344 * 1024 = -352 */
        yuv_gv_table[i] = (-731 * uv) >> 10;   /* -0.714 * 1024 = -731 */
        yuv_bu_table[i] = (1815 * uv) >> 10;   /* 1.772 * 1024 = 1815 */
    }
    yuv_tables_initialized = 1;
}

/* v16: COPIED FROM pmp123 - Check if data at offset is a valid AVI chunk header */
static int check_chunk_header(long offset) {
    if (offset < 0) return 0;
    uint8_t header[4];
    long saved = ftell(avi_file);
    fseek(avi_file, offset, SEEK_SET);
    int ok = 0;
    if (fread(header, 1, 4, avi_file) == 4) {
        /* Check for valid stream chunk: ##dc (video) or ##wb (audio) */
        if (header[0] >= '0' && header[0] <= '9' &&
            header[1] >= '0' && header[1] <= '9') {
            char c2 = header[2] | 0x20;  /* to lowercase */
            char c3 = header[3] | 0x20;
            if ((c2 == 'd' && c3 == 'c') ||  /* video */
                (c2 == 'w' && c3 == 'b')) {  /* audio */
                ok = 1;
            }
        }
    }
    fseek(avi_file, saved, SEEK_SET);
    return ok;
}

/* v16: COPIED FROM pmp123 - Parse idx1 with auto-detect offset format */
static int parse_idx1(long movi_data_start) {
    uint8_t tag[4];
    uint32_t chunk_size;

    /* Look for idx1 chunk */
    while (fread(tag, 1, 4, avi_file) == 4) {
        if (read32(avi_file, &chunk_size) != 0) break;

        if (tag[0]=='i' && tag[1]=='d' && tag[2]=='x' && tag[3]=='1') {
            /* Found idx1! */
            int num_entries = chunk_size / 16;
            long idx_start = ftell(avi_file);

            /* Find first video entry to detect offset format */
            uint8_t entry[16];
            uint32_t first_video_offset = 0;
            int found_video = 0;

            for (int i = 0; i < num_entries && i < 100; i++) {
                if (fread(entry, 1, 16, avi_file) != 16) break;
                if ((entry[2]=='d' || entry[2]=='D') && (entry[3]=='c' || entry[3]=='C')) {
                    first_video_offset = read_u32_le(entry + 8);
                    found_video = 1;
                    break;
                }
            }

            if (!found_video) {
                fseek(avi_file, idx_start, SEEK_SET);
                return 0;
            }

            /* v16: Auto-detect offset format - COPIED FROM pmp123 */
            long offset_base = 0;
            int add_header = 8;
            int format_found = 0;

            /* Variant 1: relative to movi */
            if (check_chunk_header(movi_data_start + first_video_offset)) {
                offset_base = movi_data_start;
                add_header = 8;
                format_found = 1;
            }
            /* Variant 2: absolute offset */
            else if (check_chunk_header(first_video_offset)) {
                offset_base = 0;
                add_header = 8;
                format_found = 1;
            }
            /* Variant 3: relative to movi-4 (some encoders) */
            else if (check_chunk_header(movi_data_start - 4 + first_video_offset)) {
                offset_base = movi_data_start - 4;
                add_header = 8;
                format_found = 1;
            }

            if (!format_found) {
                /* Fallback: assume movi-relative with +8 header */
                offset_base = movi_data_start;
                add_header = 8;
            }

            /* Go back and parse all entries with detected format */
            fseek(avi_file, idx_start, SEEK_SET);

            for (int i = 0; i < num_entries && total_frames < MAX_FRAMES; i++) {
                if (fread(entry, 1, 16, avi_file) != 16) break;

                /* Case-insensitive check for video chunks (00dc, 00DC, etc.) */
                if ((entry[2]=='d' || entry[2]=='D') && (entry[3]=='c' || entry[3]=='C')) {
                    uint32_t offset = read_u32_le(entry + 8);
                    uint32_t fsize = read_u32_le(entry + 12);
                    /* v16: Use detected offset_base and add_header */
                    uint32_t abs_offset = offset_base + offset + add_header;

                    frame_offsets[total_frames] = abs_offset;
                    frame_sizes[total_frames] = fsize;
                    total_frames++;
                }
            }

            return (total_frames > 0) ? 1 : 0;
        }

        fseek(avi_file, chunk_size + (chunk_size & 1), SEEK_CUR);
    }

    return 0;
}

/* Scan movi list for frame offsets */
static void scan_movi(long movi_start, long movi_end) {
    fseek(avi_file, movi_start, SEEK_SET);
    char tag[4];
    uint32_t size;

    while (ftell(avi_file) < movi_end && total_frames < MAX_FRAMES) {
        if (fread(tag, 1, 4, avi_file) != 4) break;
        if (read32(avi_file, &size) != 0) break;

        /* Case-insensitive check for video chunks (00dc, 00DC, etc.) */
        if (((tag[2] == 'd' || tag[2] == 'D') && (tag[3] == 'c' || tag[3] == 'C'))) {
            frame_offsets[total_frames] = ftell(avi_file);
            frame_sizes[total_frames] = size;
            total_frames++;
        }

        fseek(avi_file, size + (size & 1), SEEK_CUR);
    }
}

/* Parse AVI file structure */
static int parse_avi(void) {
    uint32_t chunk_size, hsize;
    char tag[4], list_type[4], htag[4];
    long hdrl_end, strl_end;
    long movi_start = 0, movi_end = 0;
    uint8_t buf[64];
    int found_idx1 = 0;

    total_frames = 0;
    video_width = 320;
    video_height = 240;
    mpeg4_extradata_size = 0;
    mpeg4_extradata_sent = 0;

    /* v13: Reset frame timing - decode every frame */
    us_per_frame = 66666;  /* default 15fps */
    clip_fps = 15;
    repeat_count = 1;  /* ALWAYS decode every frame */
    repeat_counter = 0;

    if (!check4(avi_file, "RIFF")) return 0;
    if (read32(avi_file, &chunk_size) != 0) return 0;
    if (!check4(avi_file, "AVI ")) return 0;

    while (fread(tag, 1, 4, avi_file) == 4) {
        if (read32(avi_file, &chunk_size) != 0) break;

        if (tag[0] == 'L' && tag[1] == 'I' && tag[2] == 'S' && tag[3] == 'T') {
            if (fread(list_type, 1, 4, avi_file) != 4) break;

            if (list_type[0] == 'h' && list_type[1] == 'd' &&
                list_type[2] == 'r' && list_type[3] == 'l') {
                hdrl_end = ftell(avi_file) + chunk_size - 4;

                while (ftell(avi_file) < hdrl_end) {
                    if (fread(htag, 1, 4, avi_file) != 4) break;
                    if (read32(avi_file, &hsize) != 0) break;

                    /* Parse avih chunk for FPS - COPIED FROM pmp123 */
                    if (htag[0] == 'a' && htag[1] == 'v' &&
                        htag[2] == 'i' && htag[3] == 'h') {
                        if (hsize >= 4 && fread(buf, 1, (hsize < 56 ? hsize : 56), avi_file) >= 4) {
                            us_per_frame = read_u32_le(buf);
                            if (us_per_frame > 0) {
                                clip_fps = 1000000 / us_per_frame;
                                if (clip_fps == 0) clip_fps = 1;
                            }
                            /* v13: ALWAYS decode every frame - no skipping */
                            repeat_count = 1;
                            if (hsize > 56) fseek(avi_file, hsize - 56, SEEK_CUR);
                        } else {
                            fseek(avi_file, hsize, SEEK_CUR);
                        }
                    }
                    else if (htag[0] == 'L' && htag[1] == 'I' &&
                        htag[2] == 'S' && htag[3] == 'T') {
                        if (fread(buf, 1, 4, avi_file) != 4) break;
                        if (buf[0] == 's' && buf[1] == 't' &&
                            buf[2] == 'r' && buf[3] == 'l') {
                            strl_end = ftell(avi_file) + hsize - 4;
                            int is_video = 0;

                            while (ftell(avi_file) < strl_end) {
                                if (fread(htag, 1, 4, avi_file) != 4) break;
                                uint32_t shsize;
                                if (read32(avi_file, &shsize) != 0) break;

                                if (htag[0] == 's' && htag[1] == 't' &&
                                    htag[2] == 'r' && htag[3] == 'h') {
                                    if (shsize >= 8 &&
                                        fread(buf, 1, (shsize < 64 ? shsize : 64), avi_file) >= 8) {
                                        if (buf[0] == 'v' && buf[1] == 'i' &&
                                            buf[2] == 'd' && buf[3] == 's') {
                                            is_video = 1;
                                        }
                                        if (shsize > 64) fseek(avi_file, shsize - 64, SEEK_CUR);
                                    } else {
                                        fseek(avi_file, shsize, SEEK_CUR);
                                    }
                                }
                                else if (htag[0] == 's' && htag[1] == 't' &&
                                         htag[2] == 'r' && htag[3] == 'f') {
                                    if (is_video && shsize >= 40) {
                                        if (fread(buf, 1, 40, avi_file) == 40) {
                                            video_width = read_u32_le(buf + 4);
                                            video_height = read_u32_le(buf + 8);

                                            int extradata_len = shsize - 40;
                                            if (extradata_len > 0 && extradata_len <= MAX_EXTRADATA_SIZE) {
                                                if (fread(mpeg4_extradata, 1, extradata_len, avi_file) == (size_t)extradata_len) {
                                                    mpeg4_extradata_size = extradata_len;
                                                }
                                            } else if (extradata_len > MAX_EXTRADATA_SIZE) {
                                                fseek(avi_file, extradata_len, SEEK_CUR);
                                            }
                                        }
                                    } else {
                                        fseek(avi_file, shsize, SEEK_CUR);
                                    }
                                }
                                else {
                                    fseek(avi_file, shsize + (shsize & 1), SEEK_CUR);
                                }
                            }
                        } else {
                            fseek(avi_file, hsize - 4, SEEK_CUR);
                        }
                    }
                    else {
                        fseek(avi_file, hsize + (hsize & 1), SEEK_CUR);
                    }
                }
            }
            else if (list_type[0] == 'm' && list_type[1] == 'o' &&
                     list_type[2] == 'v' && list_type[3] == 'i') {
                movi_start = ftell(avi_file);
                movi_end = movi_start + chunk_size - 4;
                fseek(avi_file, movi_end, SEEK_SET);
                found_idx1 = parse_idx1(movi_start);  /* FIX: pass movi_start directly */
                if (!found_idx1) {
                    scan_movi(movi_start, movi_end);
                }
                break;
            }
            else {
                fseek(avi_file, chunk_size - 4, SEEK_CUR);
            }
        }
        else {
            fseek(avi_file, chunk_size + (chunk_size & 1), SEEK_CUR);
        }
    }

    return (total_frames > 0) ? 1 : 0;
}

/* Initialize XVID decoder */
static int init_xvid(void) {
    if (xvid_initialized) return 1;

    /* v22: Only initialize XVID global state once (never reset) */
    if (!xvid_global_initialized) {
        xvid_gbl_init_t xinit;
        memset(&xinit, 0, sizeof(xinit));
        xinit.version = XVID_VERSION;
        xinit.cpu_flags = 0;

        int ret = xvid_global(NULL, XVID_GBL_INIT, &xinit, NULL);
        if (ret < 0) return 0;
        xvid_global_initialized = 1;
    }

    xvid_dec_create_t xcreate;
    memset(&xcreate, 0, sizeof(xcreate));
    xcreate.version = XVID_VERSION;
    xcreate.width = video_width > 0 ? video_width : 320;
    xcreate.height = video_height > 0 ? video_height : 240;

    int ret = xvid_decore(NULL, XVID_DEC_CREATE, &xcreate, NULL);
    if (ret < 0) return 0;

    xvid_handle = xcreate.handle;

    int w = video_width > 0 ? video_width : 320;
    int h = video_height > 0 ? video_height : 240;
    int y_size = w * h;
    int uv_size = (w / 2) * (h / 2);

    yuv_buffer = (uint8_t *)malloc(y_size + 2 * uv_size);
    if (!yuv_buffer) {
        xvid_decore(xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
        xvid_handle = NULL;
        return 0;
    }

    memset(yuv_buffer, 0, y_size + 2 * uv_size);
    yuv_y = yuv_buffer;
    yuv_u = yuv_buffer + y_size;
    yuv_v = yuv_buffer + y_size + uv_size;

    xvid_initialized = 1;
    return 1;
}

static void close_xvid(void) {
    if (xvid_handle) {
        xvid_decore(xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
        xvid_handle = NULL;
    }
    if (yuv_buffer) {
        free(yuv_buffer);
        yuv_buffer = NULL;
        yuv_y = NULL;
        yuv_u = NULL;
        yuv_v = NULL;
    }
    xvid_initialized = 0;
}

/* Decode a single frame using XVID */
static int decode_frame(int idx) {
    dbg_decode_calls++;  /* v14: count decode attempts */

    if (!avi_file || idx >= total_frames) return 0;

    uint32_t offset = frame_offsets[idx];
    uint32_t size = frame_sizes[idx];

    if (size > MAX_FRAME_SIZE || size == 0) return 0;

    if (fseek(avi_file, offset, SEEK_SET) != 0) return 0;
    if (fread(frame_buffer, 1, size, avi_file) != size) return 0;

    if (!xvid_initialized) {
        if (!init_xvid()) return 0;
    }

    if (!mpeg4_extradata_sent && mpeg4_extradata_size > 0) {
        xvid_dec_frame_t xvol;
        xvid_dec_stats_t svol;
        memset(&xvol, 0, sizeof(xvol));
        memset(&svol, 0, sizeof(svol));
        xvol.version = XVID_VERSION;
        svol.version = XVID_VERSION;
        xvol.bitstream = mpeg4_extradata;
        xvol.length = mpeg4_extradata_size;
        xvol.output.csp = XVID_CSP_NULL;
        xvid_decore(xvid_handle, XVID_DEC_DECODE, &xvol, &svol);
        mpeg4_extradata_sent = 1;
    }

    int w = video_width > 0 ? video_width : 320;
    int h = video_height > 0 ? video_height : 240;

    uint8_t *bitstream = frame_buffer;
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
        xframe.output.plane[0] = yuv_y;
        xframe.output.plane[1] = yuv_u;
        xframe.output.plane[2] = yuv_v;
        xframe.output.stride[0] = w;
        xframe.output.stride[1] = w / 2;
        xframe.output.stride[2] = w / 2;

        ret = xvid_decore(xvid_handle, XVID_DEC_DECODE, &xframe, &xstats);

        /* If VOL decoded, update dimensions */
        if (xstats.type == XVID_TYPE_VOL) {
            if (xstats.data.vol.width > 0) {
                video_width = xstats.data.vol.width;
                w = video_width;
            }
            if (xstats.data.vol.height > 0) {
                video_height = xstats.data.vol.height;
                h = video_height;
            }
        }

        /* Advance bitstream pointer for next iteration - UNCONDITIONALLY */
        if (ret > 0) {
            bitstream += ret;
            remaining -= ret;
        }

        loops++;
    } while (xstats.type <= 0 && ret > 0 && remaining > 4 && loops < 10);

    /* Check if we got a frame */
    dbg_last_xstats_type = xstats.type;  /* v14: track last xstats.type */

    if (xstats.type <= 0) {
        /* No frame decoded yet (maybe needs more data) */
        return 1;  /* Return success to avoid breaking playback */
    }

    dbg_decode_success++;  /* v14: count successful decodes (type > 0) */
    return 1;
}

/* YUV420P to RGB565 with DITHER2 - COPIED EXACTLY FROM pmp123
 * Applies Bayer 4x4 dithering on 8-bit values BEFORE converting to RGB565
 * This improves quality on low-color displays by reducing banding */
static void yuv_to_rgb565(void) {
    if (!yuv_tables_initialized) init_yuv_tables();

    int w = video_width > 0 ? video_width : 320;
    int h = video_height > 0 ? video_height : 240;

    /* For 320x240 video on 320x240 screen - direct 1:1 copy, no scaling */
    if (w == 320 && h == 240) {
        uint16_t *dst = rgb_buffer;

        for (int j = 0; j < 240; j++) {
            uint8_t *y_row = yuv_y + j * 320;
            uint8_t *u_row = yuv_u + (j >> 1) * 160;
            uint8_t *v_row = yuv_v + (j >> 1) * 160;

            for (int i = 0; i < 320; i++) {
                int y = yuv_y_table[y_row[i]];
                int u_idx = u_row[i >> 1];
                int v_idx = v_row[i >> 1];

                int r = y + yuv_rv_table[v_idx];
                int g = y + yuv_gu_table[u_idx] + yuv_gv_table[v_idx];
                int b = y + yuv_bu_table[u_idx];

                /* Clamp to 0-255 */
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;

                /* DITHER2 - Apply Bayer dithering to ALL pixels including black
                 * COPIED EXACTLY FROM pmp123 yuv420p_to_rgb565() */
                int dither = bayer4x4[j & 3][i & 3];
                r = r + dither;
                g = g + dither;
                b = b + dither;
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;

                /* Convert to RGB565 */
                *dst++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    }
    else {
        /* Fallback for non-320x240 video */
        int off_x = (320 - w) / 2;
        int off_y = (240 - h) / 2;
        if (off_x < 0) off_x = 0;
        if (off_y < 0) off_y = 0;

        memset(rgb_buffer, 0, 320 * 240 * sizeof(uint16_t));

        for (int j = 0; j < h && (off_y + j) < 240; j++) {
            uint8_t *y_row = yuv_y + j * w;
            uint8_t *u_row = yuv_u + (j >> 1) * (w / 2);
            uint8_t *v_row = yuv_v + (j >> 1) * (w / 2);
            uint16_t *dst = rgb_buffer + (off_y + j) * 320 + off_x;

            for (int i = 0; i < w && (off_x + i) < 320; i++) {
                int y = yuv_y_table[y_row[i]];
                int u_idx = u_row[i >> 1];
                int v_idx = v_row[i >> 1];

                int r = y + yuv_rv_table[v_idx];
                int g = y + yuv_gu_table[u_idx] + yuv_gv_table[v_idx];
                int b = y + yuv_bu_table[u_idx];

                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;

                /* DITHER2 - Apply Bayer dithering to ALL pixels */
                int dither = bayer4x4[j & 3][i & 3];
                r = r + dither;
                g = g + dither;
                b = b + dither;
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;

                *dst++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    }

    /* v19: Debug overlay removed for production */
}

/* Public API */

void avi_bg_init(void) {
    init_yuv_tables();

    if (!frame_buffer) {
        frame_buffer = (uint8_t *)malloc(MAX_FRAME_SIZE);
    }
    if (!rgb_buffer) {
        rgb_buffer = (uint16_t *)malloc(AVI_SCREEN_WIDTH * AVI_SCREEN_HEIGHT * sizeof(uint16_t));
    }
}

void avi_bg_shutdown(void) {
    avi_bg_close();

    if (frame_buffer) {
        free(frame_buffer);
        frame_buffer = NULL;
    }
    if (rgb_buffer) {
        free(rgb_buffer);
        rgb_buffer = NULL;
    }
}

int avi_bg_load(const char *path) {
    avi_bg_close();
    avi_bg_init();

    if (!frame_buffer || !rgb_buffer) return 0;

    avi_file = fopen(path, "rb");
    if (!avi_file) return 0;

    if (!parse_avi()) {
        fclose(avi_file);
        avi_file = NULL;
        return 0;
    }

    current_frame = 0;
    if (!decode_frame(0)) {
        fclose(avi_file);
        avi_file = NULL;
        close_xvid();
        return 0;
    }

    yuv_to_rgb565();

    is_active = true;
    is_paused = false;
    return 1;
}

void avi_bg_close(void) {
    if (avi_file) {
        fclose(avi_file);
        avi_file = NULL;
    }
    close_xvid();
    total_frames = 0;
    current_frame = 0;
    is_active = false;
    is_paused = false;
    mpeg4_extradata_size = 0;
    mpeg4_extradata_sent = 0;
    repeat_counter = 0;  /* Reset frame timing */
    /* v22: Reset video dimensions for clean state */
    video_width = 0;
    video_height = 0;
}

bool avi_bg_is_active(void) {
    return is_active;
}

uint16_t* avi_bg_get_frame(void) {
    if (!is_active || !rgb_buffer) return NULL;
    return rgb_buffer;
}

/* Advance frame with repeat timing - COPIED FROM pmp123 retro_run()
 * Only decode new frame when repeat_counter == 0
 * This allows 15fps video to play correctly on 30/60fps retro_run */
int avi_bg_advance_frame(void) {
    dbg_advance_calls++;  /* v14: count advance calls */

    if (!is_active || is_paused) return 0;

    /* Only decode when repeat_counter == 0 - EXACTLY like pmp123 */
    if (repeat_counter == 0) {
        /* New source frame needed - decode it */
        dbg_last_frame = current_frame;  /* v14: track which frame we're decoding */
        if (!decode_frame(current_frame)) {
            return 0;
        }
        dbg_yuv_convert++;  /* v14: count yuv conversions */
        yuv_to_rgb565();
    }
    /* else: same frame displayed again (repeat), rgb_buffer already has it */

    /* Advance repeat counter - EXACTLY like pmp123 */
    repeat_counter++;
    if (repeat_counter >= repeat_count) {
        repeat_counter = 0;
        current_frame++;
        if (current_frame >= total_frames) {
            current_frame = 0;
            mpeg4_extradata_sent = 0;  /* Reset for loop - decoder may need VOL again */
        }
    }

    return 1;
}

void avi_bg_reset(void) {
    if (!is_active) return;
    current_frame = 0;
    repeat_counter = 0;  /* Reset frame timing */
    mpeg4_extradata_sent = 0;  /* Reset for proper restart */
    decode_frame(0);
    yuv_to_rgb565();
}

void avi_bg_pause(void) {
    is_paused = true;
}

void avi_bg_resume(void) {
    is_paused = false;
}

bool avi_bg_is_paused(void) {
    return is_paused;
}

int avi_bg_get_width(void) {
    return video_width;
}

int avi_bg_get_height(void) {
    return video_height;
}

int avi_bg_get_total_frames(void) {
    return total_frames;
}

int avi_bg_get_current_frame(void) {
    return current_frame;
}

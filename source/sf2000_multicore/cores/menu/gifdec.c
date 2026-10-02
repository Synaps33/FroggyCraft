// gifdec - fast memory-buffered GIF decoder
// Modified for SF2000: loads entire file to memory first for speed
// Original by lecram (public domain)

#include "gifdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

typedef struct Entry {
    uint16_t length;
    uint16_t prefix;
    uint8_t  suffix;
} Entry;

typedef struct Table {
    int bulk;
    int nentries;
    Entry *entries;
} Table;

// Memory buffer instead of file descriptor
static uint8_t *gif_buffer = NULL;
static size_t gif_buffer_size = 0;
static size_t gif_pos = 0;

static inline void buf_read(void *dest, size_t n) {
    if (gif_pos + n <= gif_buffer_size) {
        memcpy(dest, gif_buffer + gif_pos, n);
        gif_pos += n;
    }
}

static inline void buf_seek(size_t pos) {
    gif_pos = pos;
}

static inline void buf_skip(size_t n) {
    gif_pos += n;
}

static inline size_t buf_tell(void) {
    return gif_pos;
}

static uint16_t read_num(void) {
    uint8_t bytes[2];
    buf_read(bytes, 2);
    return bytes[0] + (((uint16_t) bytes[1]) << 8);
}

gd_GIF *gd_open_gif(const char *fname) {
    uint8_t sigver[3];
    uint16_t width, height, depth;
    uint8_t fdsz, bgidx, aspect;
    int i;
    uint8_t *bgcolor;
    int gct_sz;
    gd_GIF *gif;

    // Load entire file into memory
    FILE *fp = fopen(fname, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    gif_buffer_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    gif_buffer = (uint8_t*)malloc(gif_buffer_size);
    if (!gif_buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(gif_buffer, 1, gif_buffer_size, fp) != gif_buffer_size) {
        free(gif_buffer);
        gif_buffer = NULL;
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    gif_pos = 0;

    /* Header */
    buf_read(sigver, 3);
    if (memcmp(sigver, "GIF", 3) != 0) {
        goto fail;
    }
    /* Version - accept both 87a and 89a */
    buf_read(sigver, 3);
    if (memcmp(sigver, "89a", 3) != 0 && memcmp(sigver, "87a", 3) != 0) {
        goto fail;
    }
    /* Width x Height */
    width  = read_num();
    height = read_num();
    /* FDSZ */
    buf_read(&fdsz, 1);
    /* Presence of GCT */
    if (!(fdsz & 0x80)) {
        goto fail;
    }
    /* Color Space's Depth */
    depth = ((fdsz >> 4) & 7) + 1;
    /* GCT Size */
    gct_sz = 1 << ((fdsz & 0x07) + 1);
    /* Background Color Index */
    buf_read(&bgidx, 1);
    /* Aspect Ratio */
    buf_read(&aspect, 1);
    /* Create gd_GIF Structure. */
    gif = calloc(1, sizeof(*gif));
    if (!gif) goto fail;
    gif->fd = 0; // Not used anymore
    gif->width  = width;
    gif->height = height;
    gif->depth  = depth;
    /* Read GCT */
    gif->gct.size = gct_sz;
    buf_read(gif->gct.colors, 3 * gif->gct.size);
    gif->palette = &gif->gct;
    gif->bgindex = bgidx;
    gif->frame = calloc(4, width * height);
    if (!gif->frame) {
        free(gif);
        goto fail;
    }
    gif->canvas = &gif->frame[width * height];
    if (gif->bgindex)
        memset(gif->frame, gif->bgindex, gif->width * gif->height);
    bgcolor = &gif->palette->colors[gif->bgindex*3];
    if (bgcolor[0] || bgcolor[1] || bgcolor [2])
        for (i = 0; i < gif->width * gif->height; i++)
            memcpy(&gif->canvas[i*3], bgcolor, 3);
    gif->anim_start = buf_tell();
    return gif;

fail:
    if (gif_buffer) {
        free(gif_buffer);
        gif_buffer = NULL;
    }
    return NULL;
}

static void discard_sub_blocks(void) {
    uint8_t size;
    do {
        buf_read(&size, 1);
        buf_skip(size);
    } while (size);
}

static void read_plain_text_ext(gd_GIF *gif) {
    if (gif->plain_text) {
        uint16_t tx, ty, tw, th;
        uint8_t cw, ch, fg, bg;
        size_t sub_block;
        buf_skip(1); /* block size = 12 */
        tx = read_num();
        ty = read_num();
        tw = read_num();
        th = read_num();
        buf_read(&cw, 1);
        buf_read(&ch, 1);
        buf_read(&fg, 1);
        buf_read(&bg, 1);
        sub_block = buf_tell();
        gif->plain_text(gif, tx, ty, tw, th, cw, ch, fg, bg);
        buf_seek(sub_block);
    } else {
        buf_skip(13);
    }
    discard_sub_blocks();
}

static void read_graphic_control_ext(gd_GIF *gif) {
    uint8_t rdit;
    buf_skip(1);
    buf_read(&rdit, 1);
    gif->gce.disposal = (rdit >> 2) & 3;
    gif->gce.input = rdit & 2;
    gif->gce.transparency = rdit & 1;
    gif->gce.delay = read_num();
    buf_read(&gif->gce.tindex, 1);
    buf_skip(1);
}

static void read_comment_ext(gd_GIF *gif) {
    if (gif->comment) {
        size_t sub_block = buf_tell();
        gif->comment(gif);
        buf_seek(sub_block);
    }
    discard_sub_blocks();
}

static void read_application_ext(gd_GIF *gif) {
    char app_id[8];
    char app_auth_code[3];

    buf_skip(1);
    buf_read(app_id, 8);
    buf_read(app_auth_code, 3);
    if (!strncmp(app_id, "NETSCAPE", sizeof(app_id))) {
        buf_skip(2);
        gif->loop_count = read_num();
        buf_skip(1);
    } else if (gif->application) {
        size_t sub_block = buf_tell();
        gif->application(gif, app_id, app_auth_code);
        buf_seek(sub_block);
        discard_sub_blocks();
    } else {
        discard_sub_blocks();
    }
}

static void read_ext(gd_GIF *gif) {
    uint8_t label;
    buf_read(&label, 1);
    switch (label) {
    case 0x01:
        read_plain_text_ext(gif);
        break;
    case 0xF9:
        read_graphic_control_ext(gif);
        break;
    case 0xFE:
        read_comment_ext(gif);
        break;
    case 0xFF:
        read_application_ext(gif);
        break;
    }
}

static Table *new_table(int key_size) {
    int key;
    int init_bulk = MAX(1 << (key_size + 1), 0x100);
    Table *table = malloc(sizeof(*table) + sizeof(Entry) * init_bulk);
    if (table) {
        table->bulk = init_bulk;
        table->nentries = (1 << key_size) + 2;
        table->entries = (Entry *) &table[1];
        for (key = 0; key < (1 << key_size); key++)
            table->entries[key] = (Entry) {1, 0xFFF, key};
    }
    return table;
}

static int add_entry(Table **tablep, uint16_t length, uint16_t prefix, uint8_t suffix) {
    Table *table = *tablep;
    if (table->nentries == table->bulk) {
        table->bulk *= 2;
        table = realloc(table, sizeof(*table) + sizeof(Entry) * table->bulk);
        if (!table) return -1;
        table->entries = (Entry *) &table[1];
        *tablep = table;
    }
    table->entries[table->nentries] = (Entry) {length, prefix, suffix};
    table->nentries++;
    if ((table->nentries & (table->nentries - 1)) == 0)
        return 1;
    return 0;
}

static uint16_t get_key(int key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte) {
    int bits_read;
    int rpad;
    int frag_size;
    uint16_t key;

    key = 0;
    for (bits_read = 0; bits_read < key_size; bits_read += frag_size) {
        rpad = (*shift + bits_read) % 8;
        if (rpad == 0) {
            if (*sub_len == 0) {
                buf_read(sub_len, 1);
                if (*sub_len == 0)
                    return 0x1000;
            }
            buf_read(byte, 1);
            (*sub_len)--;
        }
        frag_size = MIN(key_size - bits_read, 8 - rpad);
        key |= ((uint16_t) ((*byte) >> rpad)) << bits_read;
    }
    key &= (1 << key_size) - 1;
    *shift = (*shift + key_size) % 8;
    return key;
}

static int interlaced_line_index(int h, int y) {
    int p;
    p = (h - 1) / 8 + 1;
    if (y < p)
        return y * 8;
    y -= p;
    p = (h - 5) / 8 + 1;
    if (y < p)
        return y * 8 + 4;
    y -= p;
    p = (h - 3) / 4 + 1;
    if (y < p)
        return y * 4 + 2;
    y -= p;
    return y * 2 + 1;
}

static int read_image_data(gd_GIF *gif, int interlace) {
    uint8_t sub_len, shift, byte;
    int init_key_size, key_size, table_is_full = 0;
    int frm_off, frm_size, str_len = 0, i, p, x, y;
    uint16_t key, clear, stop;
    int ret = 0;
    Table *table;
    Entry entry = {0, 0, 0};
    size_t start, end;

    buf_read(&byte, 1);
    key_size = (int) byte;
    if (key_size < 2 || key_size > 8)
        return -1;

    start = buf_tell();
    discard_sub_blocks();
    end = buf_tell();
    buf_seek(start);

    clear = 1 << key_size;
    stop = clear + 1;
    table = new_table(key_size);
    if (!table) return -1;

    key_size++;
    init_key_size = key_size;
    sub_len = shift = 0;
    key = get_key(key_size, &sub_len, &shift, &byte);
    frm_off = 0;
    frm_size = gif->fw * gif->fh;

    while (frm_off < frm_size) {
        if (key == clear) {
            key_size = init_key_size;
            table->nentries = (1 << (key_size - 1)) + 2;
            table_is_full = 0;
        } else if (!table_is_full) {
            ret = add_entry(&table, str_len + 1, key, entry.suffix);
            if (ret == -1) {
                free(table);
                return -1;
            }
            if (table->nentries == 0x1000) {
                ret = 0;
                table_is_full = 1;
            }
        }
        key = get_key(key_size, &sub_len, &shift, &byte);
        if (key == clear) continue;
        if (key == stop || key == 0x1000) break;
        if (ret == 1) key_size++;
        entry = table->entries[key];
        str_len = entry.length;
        for (i = 0; i < str_len; i++) {
            p = frm_off + entry.length - 1;
            x = p % gif->fw;
            y = p / gif->fw;
            if (interlace)
                y = interlaced_line_index((int) gif->fh, y);
            gif->frame[(gif->fy + y) * gif->width + gif->fx + x] = entry.suffix;
            if (entry.prefix == 0xFFF)
                break;
            else
                entry = table->entries[entry.prefix];
        }
        frm_off += str_len;
        if (key < table->nentries - 1 && !table_is_full)
            table->entries[table->nentries - 1].suffix = entry.suffix;
    }
    free(table);
    if (key == stop)
        buf_read(&sub_len, 1);
    buf_seek(end);
    return 0;
}

static int read_image(gd_GIF *gif) {
    uint8_t fisrz;
    int interlace;

    gif->fx = read_num();
    gif->fy = read_num();

    if (gif->fx >= gif->width || gif->fy >= gif->height)
        return -1;

    gif->fw = read_num();
    gif->fh = read_num();

    gif->fw = MIN(gif->fw, gif->width - gif->fx);
    gif->fh = MIN(gif->fh, gif->height - gif->fy);

    buf_read(&fisrz, 1);
    interlace = fisrz & 0x40;

    if (fisrz & 0x80) {
        gif->lct.size = 1 << ((fisrz & 0x07) + 1);
        buf_read(gif->lct.colors, 3 * gif->lct.size);
        gif->palette = &gif->lct;
    } else {
        gif->palette = &gif->gct;
    }
    return read_image_data(gif, interlace);
}

static void render_frame_rect(gd_GIF *gif, uint8_t *buffer) {
    int i, j, k;
    uint8_t index, *color;
    i = gif->fy * gif->width + gif->fx;
    for (j = 0; j < gif->fh; j++) {
        for (k = 0; k < gif->fw; k++) {
            index = gif->frame[(gif->fy + j) * gif->width + gif->fx + k];
            color = &gif->palette->colors[index*3];
            if (!gif->gce.transparency || index != gif->gce.tindex)
                memcpy(&buffer[(i+k)*3], color, 3);
        }
        i += gif->width;
    }
}

static void dispose(gd_GIF *gif) {
    int i, j, k;
    uint8_t *bgcolor;
    switch (gif->gce.disposal) {
    case 2:
        bgcolor = &gif->palette->colors[gif->bgindex*3];
        i = gif->fy * gif->width + gif->fx;
        for (j = 0; j < gif->fh; j++) {
            for (k = 0; k < gif->fw; k++)
                memcpy(&gif->canvas[(i+k)*3], bgcolor, 3);
            i += gif->width;
        }
        break;
    case 3:
        break;
    default:
        render_frame_rect(gif, gif->canvas);
    }
}

int gd_get_frame(gd_GIF *gif) {
    char sep;

    dispose(gif);
    buf_read(&sep, 1);
    while (sep != ',') {
        if (sep == ';')
            return 0;
        if (sep == '!')
            read_ext(gif);
        else return -1;
        buf_read(&sep, 1);
    }
    if (read_image(gif) == -1)
        return -1;
    return 1;
}

void gd_render_frame(gd_GIF *gif, uint8_t *buffer) {
    memcpy(buffer, gif->canvas, gif->width * gif->height * 3);
    render_frame_rect(gif, buffer);
}

int gd_is_bgcolor(gd_GIF *gif, uint8_t color[3]) {
    return !memcmp(&gif->palette->colors[gif->bgindex*3], color, 3);
}

void gd_rewind(gd_GIF *gif) {
    buf_seek(gif->anim_start);
}

void gd_close_gif(gd_GIF *gif) {
    if (gif_buffer) {
        free(gif_buffer);
        gif_buffer = NULL;
        gif_buffer_size = 0;
        gif_pos = 0;
    }
    free(gif->frame);
    free(gif);
}

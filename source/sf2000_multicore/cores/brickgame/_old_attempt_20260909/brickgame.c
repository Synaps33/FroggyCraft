#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <boolean.h>
#include <libretro.h>
#include <stdarg.h>

#include "ht4bit_cpu.h"
#include "render.h"

#define RETRO_API_VERSION 1
#define RETRO_DEVICE_JOYPAD 1

#define RETRO_DEVICE_ID_JOYPAD_UP      4
#define RETRO_DEVICE_ID_JOYPAD_DOWN    5
#define RETRO_DEVICE_ID_JOYPAD_LEFT    6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT   7
#define RETRO_DEVICE_ID_JOYPAD_A       0
#define RETRO_DEVICE_ID_JOYPAD_B       1
#define RETRO_DEVICE_ID_JOYPAD_START   3
#define RETRO_DEVICE_ID_JOYPAD_SELECT  2

static cpu_state_t cpu;
static uint8_t rom_data[0x1000];
static uint16_t framebuffer[SCREEN_W * SCREEN_H];

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_audio_sample_batch_t audio_batch_cb;

static int key_state;
static unsigned frame_count = 0;

static FILE *dbg_log = NULL;

static void dbg_init(void) {
    if (!dbg_log) {
        dbg_log = fopen("/mnt/sda1/log.txt", "w");
        if (dbg_log) {
            fprintf(dbg_log, "brickgame: dbg_init OK\n");
            fflush(dbg_log);
        }
    }
}

static void dbg(const char *fmt, ...) {
    if (!dbg_log) dbg_init();
    if (dbg_log) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(dbg_log, fmt, ap);
        va_end(ap);
        fflush(dbg_log);
    }
}

void retro_init(void) {
    dbg_init();
    dbg("retro_init: called\n");
    ht4bit_init(&cpu);
    key_state = 0;
    dbg("retro_init: cpu initialized\n");
}

void retro_deinit(void) {
    dbg("retro_deinit: called\n");
    if (dbg_log) { fclose(dbg_log); dbg_log = NULL; }
}

unsigned retro_api_version(void) {
    dbg_init();
    dbg("retro_api_version: returning %d\n", RETRO_API_VERSION);
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
    dbg_init();
    dbg("retro_get_system_info: called\n");
    memset(info, 0, sizeof(*info));
    info->library_name     = "BrickGame";
    info->library_version  = "1.0";
    info->need_fullpath    = 0;
    info->valid_extensions = "bin";
    dbg("retro_get_system_info: name=%s ver=%s\n", info->library_name, info->library_version);
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    dbg("retro_get_system_av_info: called\n");
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = SCREEN_W;
    info->geometry.base_height  = SCREEN_H;
    info->geometry.max_width    = SCREEN_W;
    info->geometry.max_height   = SCREEN_H;
    info->geometry.aspect_ratio = (float)SCREEN_W / SCREEN_H;
    info->timing.fps           = 60.0;
    info->timing.sample_rate   = 0.0;
    dbg("retro_get_system_av_info: %dx%d @ 60fps\n", SCREEN_W, SCREEN_H);
}

void retro_set_environment(retro_environment_t cb) {
    dbg_init();
    dbg("retro_set_environment: called\n");
    environ_cb = cb;
    int no_content = 1;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);
    dbg("retro_set_environment: SET_SUPPORT_NO_GAME done\n");
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    dbg("retro_set_video_refresh: cb=%p\n", (void*)(uintptr_t)cb);
    video_cb = cb;
}
void retro_set_audio_sample(retro_audio_sample_t cb) {
    dbg("retro_set_audio_sample: cb=%p\n", (void*)(uintptr_t)cb);
    (void)cb;
}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    dbg("retro_set_audio_sample_batch: cb=%p\n", (void*)(uintptr_t)cb);
    audio_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb) {
    dbg("retro_set_input_poll: cb=%p\n", (void*)(uintptr_t)cb);
    input_poll_cb = cb;
}
void retro_set_input_state(retro_input_state_t cb) {
    dbg("retro_set_input_state: cb=%p\n", (void*)(uintptr_t)cb);
    input_state_cb = cb;
}
void retro_set_controller_port_device(unsigned port, unsigned device) {
    dbg("retro_set_controller_port_device: port=%u device=%u\n", port, device);
}

void retro_reset(void) {
    dbg("retro_reset: called\n");
    ht4bit_init(&cpu);
}

void retro_run(void) {
    unsigned pp = 0, ps = 0;
    int i;

    if (frame_count == 0) {
        dbg("retro_run: first frame, video_cb=%p input_poll=%p input_state=%p\n",
            (void*)(uintptr_t)video_cb,
            (void*)(uintptr_t)input_poll_cb,
            (void*)(uintptr_t)input_state_cb);
    }

    if (input_poll_cb) input_poll_cb();

    if (input_state_cb) {
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))    pp |= 1;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))  pp |= 2;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) pp |= 4;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))  pp |= 8;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))     pp |= 1;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))     pp |= 1;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))  ps |= 1;
        if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT)) ps |= 2;
    }

    cpu.mem[0] = pp;
    cpu.mem[1] = ps;

    ht4bit_run(&cpu, rom_data, 1000);

    render_frame(framebuffer, cpu.mem, SCREEN_W * 2);

    if (video_cb) {
        video_cb(framebuffer, SCREEN_W, SCREEN_H, SCREEN_W * 2);
    } else {
        dbg("retro_run: video_cb is NULL!\n");
    }

    frame_count++;
    if (frame_count <= 5 || (frame_count % 60 == 0)) {
        dbg("retro_run: frame=%u pc=%03x a=%x mem0=%x mem1=%x pp=%x ps=%x\n",
            frame_count, cpu.pc, cpu.a, cpu.mem[0], cpu.mem[1], pp, ps);
    }
}

size_t retro_serialize_size(void) {
    dbg("retro_serialize_size: %u bytes\n", (unsigned)sizeof(cpu_state_t));
    return sizeof(cpu_state_t);
}

bool retro_serialize(void *data, size_t size) {
    dbg("retro_serialize: size=%u\n", (unsigned)size);
    if (size < sizeof(cpu_state_t)) return false;
    memcpy(data, &cpu, sizeof(cpu_state_t));
    return true;
}

bool retro_unserialize(const void *data, size_t size) {
    dbg("retro_unserialize: size=%u\n", (unsigned)size);
    if (size < sizeof(cpu_state_t)) return false;
    memcpy(&cpu, data, sizeof(cpu_state_t));
    return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index; (void)enabled; (void)code;
}

bool retro_load_game(const struct retro_game_info *info) {
    dbg_init();
    dbg("retro_load_game: called\n");

    if (info) {
        dbg("retro_load_game: path=%s data=%p size=%u\n",
            info->path ? info->path : "NULL",
            info->data, (unsigned)info->size);

        if (info->data && info->size > 0) {
            size_t copy_size = info->size < 0x1000 ? info->size : 0x1000;
            memcpy(rom_data, info->data, copy_size);
            dbg("retro_load_game: loaded %u bytes from data\n", (unsigned)copy_size);
        } else if (info->path) {
            FILE *f = fopen(info->path, "rb");
            if (f) {
                size_t n = fread(rom_data, 1, 0x1000, f);
                fclose(f);
                dbg("retro_load_game: loaded %u bytes from file\n", (unsigned)n);
            } else {
                dbg("retro_load_game: FAILED to open %s\n", info->path);
            }
        }
    } else {
        dbg("retro_load_game: info is NULL, using NOP ROM\n");
    }

    ht4bit_init(&cpu);
    dbg("retro_load_game: ROM[0..3] = %02x %02x %02x %02x\n",
        rom_data[0], rom_data[1], rom_data[2], rom_data[3]);
    dbg("retro_load_game: returning true\n");
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num_info) {
    (void)type; (void)info; (void)num_info;
    return false;
}

void retro_unload_game(void) {
    dbg("retro_unload_game: called\n");
}

unsigned retro_get_region(void) {
    return 0;
}

void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}

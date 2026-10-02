#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include "libretro.h"

static uint16_t s_last_frame[320 * 240];
static int s_frame_count = 0;
static uint32_t s_input_mask = 0;

static void cb_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;
    s_frame_count++;

    const uint8_t *src = (const uint8_t*)data;
    for (unsigned y = 0; y < height && y < 240; y++) {
        memcpy(&s_last_frame[y * 320], src + y * pitch, (width < 320 ? width : 320) * sizeof(uint16_t));
    }
}

static void cb_audio_sample(int16_t left, int16_t right) {
    (void)left; (void)right;
}

static size_t cb_audio_sample_batch(const int16_t *data, size_t frames) {
    (void)data;
    return frames;
}

static void cb_input_poll(void) {}

static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    return (s_input_mask & (1 << id)) ? 1 : 0;
}

static bool cb_environment(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format *fmt = (enum retro_pixel_format*)data;
            if (*fmt == RETRO_PIXEL_FORMAT_RGB565) return true;
            return false;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            return false;
        case RETRO_ENVIRONMENT_SHUTDOWN:
            printf("[TEST] Core requested shutdown.\n");
            return true;
        default:
            return false;
    }
}

static void save_ppm(const char *filename, const uint16_t *fb, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t p = fb[y * width + x];
            uint8_t r = (p >> 11) << 3;
            uint8_t g = ((p >> 5) & 0x3F) << 2;
            uint8_t b = (p & 0x1F) << 3;
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    }
    fclose(f);
    printf("[TEST] Saved screenshot: %s\n", filename);
}

static int count_non_black_pixels(const uint16_t *fb, int count) {
    int nb = 0;
    for (int i = 0; i < count; i++) {
        if (fb[i] != 0) nb++;
    }
    return nb;
}

int main(int argc, char *argv[]) {
    const char *core_path = "./fheroes2_libretro.so";
    const char *rom_path = "/tmp/h2demo/DATA/HEROES2.AGG";
    if (argc > 1) core_path = argv[1];
    if (argc > 2) rom_path = argv[2];

    printf("============================================\n");
    printf("=== FHEROES2 LIBRETRO CORE TESTER ==========\n");
    printf("============================================\n");
    printf("[TEST] Loading core: %s\n", core_path);

    void *handle = dlopen(core_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "[TEST ERROR] dlopen failed: %s\n", dlerror());
        return 1;
    }

    void (*retro_init)(void) = (void (*)(void))dlsym(handle, "retro_init");
    void (*retro_deinit)(void) = (void (*)(void))dlsym(handle, "retro_deinit");
    void (*retro_set_environment)(retro_environment_t) = (void (*)(retro_environment_t))dlsym(handle, "retro_set_environment");
    void (*retro_set_video_refresh)(retro_video_refresh_t) = (void (*)(retro_video_refresh_t))dlsym(handle, "retro_set_video_refresh");
    void (*retro_set_audio_sample)(retro_audio_sample_t) = (void (*)(retro_audio_sample_t))dlsym(handle, "retro_set_audio_sample");
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t) = (void (*)(retro_audio_sample_batch_t))dlsym(handle, "retro_set_audio_sample_batch");
    void (*retro_set_input_poll)(retro_input_poll_t) = (void (*)(retro_input_poll_t))dlsym(handle, "retro_set_input_poll");
    void (*retro_set_input_state)(retro_input_state_t) = (void (*)(retro_input_state_t))dlsym(handle, "retro_set_input_state");
    void (*retro_get_system_av_info)(struct retro_system_av_info*) = (void (*)(struct retro_system_av_info*))dlsym(handle, "retro_get_system_av_info");
    bool (*retro_load_game)(const struct retro_game_info*) = (bool (*)(const struct retro_game_info*))dlsym(handle, "retro_load_game");
    void (*retro_run)(void) = (void (*)(void))dlsym(handle, "retro_run");

    if (!retro_init || !retro_load_game || !retro_run) {
        fprintf(stderr, "[TEST ERROR] dlsym failed for core functions!\n");
        return 1;
    }

    retro_set_environment(cb_environment);
    retro_set_video_refresh(cb_video_refresh);
    retro_set_audio_sample(cb_audio_sample);
    retro_set_audio_sample_batch(cb_audio_sample_batch);
    retro_set_input_poll(cb_input_poll);
    retro_set_input_state(cb_input_state);

    printf("[TEST] Initializing core...\n");
    retro_init();

    struct retro_system_av_info av_info;
    retro_get_system_av_info(&av_info);
    printf("[TEST] Video geometry: %ux%u @ %.1f FPS\n",
           av_info.geometry.base_width, av_info.geometry.base_height,
           av_info.timing.fps);

    printf("[TEST] Loading game file: %s\n", rom_path);
    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.path = rom_path;
    if (!retro_load_game(&game_info)) {
        fprintf(stderr, "[TEST ERROR] retro_load_game failed!\n");
        return 1;
    }
    printf("[TEST] Game loaded!\n");

    printf("[TEST] Running 10 frames for Main Menu...\n");
    for (int i = 0; i < 10; i++) {
        retro_run();
    }
    save_ppm("/home/Sajnaps/gb300/fheroes2_menu.ppm", s_last_frame, 320, 240);

    printf("[TEST] Moving cursor UP towards 'New Game' (15 frames)...\n");
    s_input_mask = (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    for (int i = 0; i < 15; i++) {
        retro_run();
    }

    printf("[TEST] Clicking A button (LPM) on 'New Game'...\n");
    s_input_mask = (1 << RETRO_DEVICE_ID_JOYPAD_A);
    for (int i = 0; i < 3; i++) {
        retro_run();
    }
    s_input_mask = 0;
    for (int i = 0; i < 15; i++) {
        retro_run();
    }

    save_ppm("/home/Sajnaps/gb300/fheroes2_action.ppm", s_last_frame, 320, 240);

    printf("[TEST] Total frames received: %d\n", s_frame_count);
    int non_black = count_non_black_pixels(s_last_frame, 320 * 240);
    printf("[TEST] Non-black pixels: %d / %d (%.2f%%)\n",
           non_black, 320 * 240, (double)non_black * 100.0 / (320 * 240));

    return (non_black > 1000) ? 0 : 2;
}

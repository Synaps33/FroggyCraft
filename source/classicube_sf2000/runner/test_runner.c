/*
Standalone libretro test harness for the ClassiCube SF2000 port.
Loads classicube_libretro.so via dlopen, runs a number of frames with a
scripted joypad input, and dumps the framebuffer to PPM/PNG for inspection.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <time.h>

#include "libretro.h"

#define WIDTH  320
#define HEIGHT 240

static uint16_t s_frame[WIDTH * HEIGHT];
static int s_frame_count;
static int s_last_width, s_last_height;
static uint32_t s_pad_mask;
static int s_shutdown_requested;

static uint16_t conv(uint16_t p) { return p; }

static void cb_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
	if (!data) return;
	unsigned y, x;
	s_last_width = width;
	s_last_height = height;
	if (width > WIDTH) width = WIDTH;
	if (height > HEIGHT) height = HEIGHT;

	for (y = 0; y < height; y++) {
		const uint8_t* src = (const uint8_t*)data + y * pitch;
		uint16_t* dst = &s_frame[y * WIDTH];
		for (x = 0; x < width; x++) dst[x] = conv(*(const uint16_t*)(src + x * 2));
	}
	s_frame_count++;
}

static void cb_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t cb_audio_sample_batch(const int16_t* d, size_t f) { (void)d; return f; }
static void cb_input_poll(void) { }

static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void)port; (void)index;
	if (device != RETRO_DEVICE_JOYPAD) return 0;
	return (s_pad_mask & (1u << id)) ? 1 : 0;
}

static bool cb_environment(unsigned cmd, void* data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		enum retro_pixel_format* fmt = (enum retro_pixel_format*)data;
		printf("[TEST] pixel format requested = %d\n", (int)*fmt);
		return *fmt == RETRO_PIXEL_FORMAT_RGB565;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		return false;
	case RETRO_ENVIRONMENT_SHUTDOWN:
		s_shutdown_requested = 1;
		return true;
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
		return false;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool*)data = false;
		return true;
	default:
		return false;
	}
}

static int count_non_black(const uint16_t* fb, int count) {
	int i, nb = 0;
	for (i = 0; i < count; i++) if (fb[i] != 0) nb++;
	return nb;
}

static int count_distinct(const uint16_t* fb, int count) {
	static uint16_t seen[4096];
	int i, n = 0;
	memset(seen, 0, sizeof(seen));
	for (i = 0; i < count; i++) {
		uint16_t v = fb[i];
		if (v == 0) continue;
		uint16_t h = (uint16_t)((v * 2654435761u) >> 20);
		seen[h & 4095] = 1;
	}
	for (i = 0; i < 4096; i++) if (seen[i]) n++;
	return n;
}

static void save_ppm(const char* path) {
	FILE* f = fopen(path, "wb");
	int y, x;
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) {
			uint16_t p = s_frame[y * WIDTH + x];
			fputc((p >> 11) << 3, f);
			fputc(((p >> 5) & 0x3F) << 2, f);
			fputc((p & 0x1F) << 3, f);
		}
	}
	fclose(f);
	printf("[TEST] wrote %s\n", path);
}

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void* g_handle;

/* Resolve a libretro entry point from the shared object */
static void* sym(const char* name) {
	void* p = dlsym(g_handle, name);
	if (!p) { fprintf(stderr, "missing symbol %s\n", name); exit(1); }
	return p;
}

int main(int argc, char** argv) {
	void* handle;
	struct retro_system_info sys;
	struct retro_system_av_info av;
	struct retro_game_info game;
	const char* so_path = (argc > 1) ? argv[1] : "./classicube_libretro.so";
	int total_frames = (argc > 2) ? atoi(argv[2]) : 240;
	int i, phase;

	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info*);
	void (*retro_get_system_av_info)(struct retro_system_av_info*);
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	void (*retro_init)(void);
	void (*retro_deinit)(void);
	bool (*retro_load_game)(const struct retro_game_info*);
	void (*retro_run)(void);
	void (*retro_unload_game)(void);
	unsigned (*retro_get_region)(void);

	handle = dlopen(so_path, RTLD_NOW);
	if (!handle) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }
	g_handle = handle;

	*(void**)&retro_api_version            = sym("retro_api_version");
	*(void**)&retro_get_system_info        = sym("retro_get_system_info");
	*(void**)&retro_get_system_av_info     = sym("retro_get_system_av_info");
	*(void**)&retro_set_environment        = sym("retro_set_environment");
	*(void**)&retro_set_video_refresh      = sym("retro_set_video_refresh");
	*(void**)&retro_set_audio_sample       = sym("retro_set_audio_sample");
	*(void**)&retro_set_audio_sample_batch = sym("retro_set_audio_sample_batch");
	*(void**)&retro_set_input_poll         = sym("retro_set_input_poll");
	*(void**)&retro_set_input_state        = sym("retro_set_input_state");
	*(void**)&retro_init                   = sym("retro_init");
	*(void**)&retro_deinit                 = sym("retro_deinit");
	*(void**)&retro_load_game              = sym("retro_load_game");
	*(void**)&retro_run                    = sym("retro_run");
	*(void**)&retro_unload_game            = sym("retro_unload_game");
	*(void**)&retro_get_region             = sym("retro_get_region");

	printf("[TEST] API version = %u\n", retro_api_version());

	retro_set_environment(cb_environment);
	retro_set_video_refresh(cb_video_refresh);
	retro_set_audio_sample(cb_audio_sample);
	retro_set_audio_sample_batch(cb_audio_sample_batch);
	retro_set_input_poll(cb_input_poll);
	retro_set_input_state(cb_input_state);

	retro_get_system_info(&sys);
	printf("[TEST] library: %s %s, ext='%s'\n", sys.library_name, sys.library_version, sys.valid_extensions);

	retro_get_system_av_info(&av);
	printf("[TEST] av: %ux%u fps=%f\n", av.geometry.base_width, av.geometry.base_height, av.timing.fps);

	retro_init();

	memset(&game, 0, sizeof(game));
	if (!retro_load_game(&game)) {
		fprintf(stderr, "retro_load_game failed\n");
		retro_deinit();
		return 1;
	}
	printf("[TEST] game loaded\n");

	/* Input script:
	   phase 0 (frames 0-29)   : idle
	   phase 1 (frames 30-89)  : walk forward + jump
	   phase 2 (frames 90-149) : look around
	   phase 3 (frames 150-209): place/break blocks
	   phase 4 (frames 210+)   : open inventory / menus */
	for (i = 0; i < total_frames; i++) {
		s_pad_mask = 0;
		phase = i / 30;

		switch (phase) {
		case 1:
			s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
			if ((i % 30) == 10) s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
			break;
		case 2:
			s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT) | (1u << RETRO_DEVICE_ID_JOYPAD_Y);
			break;
		case 3:
			s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
			if ((i % 15) == 0) s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
			if ((i % 15) == 7) s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_X);
			break;
		case 4:
			if ((i % 30) == 5) s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);
			if ((i % 30) == 15) s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
			break;
		default:
			break;
		}

		double t0 = now_ms();
		retro_run();
		double ms = now_ms() - t0;

		if (i % 30 == 0) {
			{ char nm[64]; snprintf(nm, sizeof(nm), "frames/frame_%03d.ppm", i); save_ppm(nm); }
			int nb = count_non_black(s_frame, WIDTH * HEIGHT);
			int distinct = count_distinct(s_frame, WIDTH * HEIGHT);
			printf("[TEST] frame %3d: %5.2f ms | frames=%d nonblack=%d colors=%d (%dx%d)\n",
				i, ms, s_frame_count, nb, distinct, s_last_width, s_last_height);
			fflush(stdout);
		}
		if (s_shutdown_requested) { printf("[TEST] shutdown requested\n"); break; }
	}

	printf("[TEST] total frames presented: %d\n", s_frame_count);
#ifdef RAWHEX
	{ int i; for (i = 0; i < 12; i++) {
		int y = 150 + (i/4)*8, x = 20 + (i%4)*8;
		printf("  raw(%d,%d)=0x%04x\n", x, y, s_frame[y*WIDTH+x]); } }
#endif
	retro_unload_game();
	retro_deinit();
	dlclose(handle);
	printf("[TEST] done\n");
	return 0;
}
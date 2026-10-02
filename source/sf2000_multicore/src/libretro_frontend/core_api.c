#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <reent.h>
#include <ctype.h>

#include <libretro.h>
#include <file/file_path.h>
#include <file/config_file.h>

#include <core_api.h>
#include <core.h>
#include <debug.h>
#include <deimos.h>
#include <file.h>
#include <images.h>
#include <input.h>
#include <logging.h>
#include <options.h>
#include <stockfw.h>
#include <video_sf2000.h>

#define MIPS_J(pfunc)    (2 << 26) | (uint32_t)pfunc >> 2 & ((1 << 26) - 1)
#define MIPS_JAL(pfunc)  (3 << 26) | (uint32_t)pfunc >> 2 & ((1 << 26) - 1)

#define PATCH_J(target, hook)    *(uint32_t*)(target) = MIPS_J(hook)
#define PATCH_JAL(target, hook)  *(uint32_t*)(target) = MIPS_JAL(hook)

#define MAX_CONTENT_INFO_OVERRIDES 3

#define fw_fps_counter_enable   ((int *)__fw_fps_counter_enable)
#define fw_fps_counter          ((int *)__fw_fps_counter)
#define fw_fps_counter_format   ((char *)__fw_fps_counter_format)

static void wrap_retro_init(void);
static void wrap_retro_deinit(void);
static void wrap_retro_set_environment(retro_environment_t cb);
static void wrap_retro_run(void);
static bool wrap_retro_load_game(const struct retro_game_info* info);
static void wrap_retro_unload_game(void);

static void wrap_video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch);
static void xrgb8888_video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch);
static bool wrap_environ_cb(unsigned cmd, void *data);
static int16_t wrap_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id);

static retro_audio_buffer_status_callback_t	audio_buff_status_cb;
static size_t mono_mix_audio_batch_cb(const int16_t *data, size_t frames);
static void mono_mix_audio_sample_cb(int16_t left, int16_t right);

static void frameskip_cb(BOOL flag);
static void failed_retro_run(void);
static void enable_xrgb8888_support();
static void convert_xrgb8888_to_rgb565(void* buffer, unsigned width, unsigned height, size_t stride);

// Buffer vars
static uint32_t state_fb_height, state_fb_width;
static size_t state_framebuffer_size;
static uint16_t *state_framebuffer = NULL, *rgb565_darken_buffer = NULL;
static uint16_t* s_rgb565_convert_buffer = NULL; // TODO: Should clean up all the buffers
static unsigned geom_prev_width = 0, geom_prev_height = 0;
static int buffer_prev_width = 0, buffer_prev_height = 0;

// State flags
static bool g_xrgb888 = false;
bool variable_update_flag = false, g_fps_counter = false;

// Libretro vars
static const struct retro_system_content_info_override* content_info_overrides[MAX_CONTENT_INFO_OVERRIDES];
static size_t content_info_override_count = 0;
config_file_t *s_core_config = NULL;
struct retro_game_info saved_game_info;
struct retro_system_info sysinfo;
struct retro_game_info_ext game_info_ext;
char assets_dir[MAXPATH];

struct retro_core_t core_exports = {
   .retro_init = wrap_retro_init,
   .retro_deinit = wrap_retro_deinit,
   .retro_api_version = retro_api_version,
   .retro_get_system_info = retro_get_system_info,
   .retro_get_system_av_info = retro_get_system_av_info,
   .retro_set_environment = wrap_retro_set_environment,
   .retro_set_video_refresh = retro_set_video_refresh,
   .retro_set_audio_sample = retro_set_audio_sample,
   .retro_set_audio_sample_batch = retro_set_audio_sample_batch,
   .retro_set_input_poll = retro_set_input_poll,
   .retro_set_input_state = retro_set_input_state,
   .retro_set_controller_port_device = retro_set_controller_port_device,
   .retro_reset = retro_reset,
   .retro_run = wrap_retro_run,
   .retro_serialize_size = retro_serialize_size,
   .retro_serialize = retro_serialize,
   .retro_unserialize = retro_unserialize,
   .retro_cheat_reset = retro_cheat_reset,
   .retro_cheat_set = retro_cheat_set,
   .retro_load_game = wrap_retro_load_game,
   .retro_load_game_special = retro_load_game_special,
   .retro_unload_game = wrap_retro_unload_game,
   .retro_get_region = retro_get_region,
   .retro_get_memory_data = retro_get_memory_data,
   .retro_get_memory_size = retro_get_memory_size,
};

void wrap_retro_run(void) {
	static int s_run_count = 0;
	if (s_run_count < 10) {
		xlog("[FRONTEND] wrap_retro_run frame %d\n", s_run_count++);
	}
	// Disable the osd message after 2 seconds
	if (gb_temporary_osd) {
		if (os_get_tick_count() - g_osd_time > 1000) {
			if (!g_fps_counter) *fw_fps_counter_enable = 0; // Don't change fps if fps is enabled
			gb_temporary_osd = false;
		}
	}

	frontend_check_hotkeys();
	retro_run(); 
}

void wrap_retro_unload_game(void){
	if (g_enable_cheats_hotkey) unload_cheats();
	save_srm(0);
	if (g_auto_save_load) {
		if (state_framebuffer) state_save(g_auto_save_load_slot, state_framebuffer, state_fb_width, state_fb_height);
		else state_save(g_auto_save_load_slot, gp_run_osd_data, g_run_osd_width, g_run_osd_height);
		if (g_per_state_srm) save_srm(g_auto_save_load_slot);
	}
	retro_unload_game();
}

static void clear_bss()
{
	extern void *__bss_start;
	extern void *_end;

    void *start = &__bss_start;
    void *end = &_end;

	memset(start, 0, end - start);

	// xlog("clear_bss: start=%p end=%p\n", &__bss_start, &_end);
}

// call_ctors currently is not being used since __libc_init_array will handle that instead.
// but leave it here for now if there would be a need to debug a crash during the static init phase.
static void call_ctors()
{
	typedef void (*func_ptr) (void);
	extern func_ptr __init_array_start;
	extern func_ptr __init_array_end;

	xlog("call_ctors: start=%p end=%p\n", &__init_array_start, &__init_array_end);

	// call ctors from last to first
	for (func_ptr *pfunc = &__init_array_end - 1; pfunc >= &__init_array_start; --pfunc) {
		xlog("pfunc=%p func=%p\n", pfunc, *pfunc);
		(*pfunc)();
	}
}

// TODO: need a place to call dtors as well. maybe when retro_deinit is called?
static void call_dtors()
{
	typedef void (*func_ptr) (void);
	extern func_ptr __fini_array_start;
	extern func_ptr __fini_array_end;

	// dtors are called in reverse order of ctors
	for (func_ptr *pfunc = &__fini_array_start; pfunc < &__fini_array_end; ++pfunc) {
		(*pfunc)();
	}
}

#define LOADER_ADDR 0x80001500
typedef void (*loader_func_t)(const char *path, int load_state);
static const loader_func_t direct_loader = (loader_func_t)LOADER_ADDR;

static inline void clear_snd_task_flags(void) {
	g_snd_task_flags = 0;
#if defined(GB300V2)
	*(volatile int *)0x80c5ab64 = 0;
#else
	*(volatile int *)0x80c0b574 = 0;
#endif
}

// Wrap run_game for FrogUI or other menu cores so we can unload game and deinit
void wrap_run_game(const char *filename, int load_state) {
	wrap_retro_unload_game();
	wrap_retro_deinit();
	clear_snd_task_flags();
	direct_loader(filename, load_state);
	while (1) {
		dly_tsk(100);
	}
}

static void get_menu_rom_info(const char **out_name, const char **out_path) {
	// The stock run_game() dispatches on the path's extension, so the menu
	// must be launched through a ".gba"-suffixed stub (as the original
	// multicore code did with "menu;p.gba"). On this card the stub exists
	// as /mnt/sda1/ROMS/menu;m.gba and resolves to cores/menu/core_87000000.
	const char *menu_file = "menu;m.gba";
	const char *menu_path = "/mnt/sda1/ROMS/menu;m.gba";

	if (fs_access(menu_path, 0) != 0) {
		// Fallbacks without the .gba stub
		menu_file = "menu;menu;m.gba";
		menu_path = "/mnt/sda1/ROMS/menu/m";

		if (fs_access(menu_path, 0) != 0) {
			if (fs_access("/mnt/sda1/roms/menu/m", 0) == 0) {
				menu_file = "menu;menu;m.gba";
				menu_path = "/mnt/sda1/roms/menu/m";
			}
			else if (fs_access("/mnt/sda1/ROMS/menu;p.gba", 0) == 0) {
				menu_file = "menu;p.gba";
				menu_path = "/mnt/sda1/ROMS/menu;p.gba";
			}
		}
	}

	if (out_name) *out_name = menu_file;
	if (out_path) *out_path = menu_path;
}

// Run the Menu core as a method of exiting.
// This is the official multicore exit path: unload the game, deinit the core
// and launch the FrogUI menu core through the stock run_game() launcher.
// Must only be called from firmware context (correct $gp), e.g. from
// dummy_run_emulator_menu() or after JavaTask has been unwound.
void shutdown_game(void) {
	static bool s_in_shutdown = false;
	if (s_in_shutdown) {
		xlog("[SHUTDOWN] Already in shutdown, skipping\n");
		return;
	}
	s_in_shutdown = true;

	// Unload the game before anything else to preserve framebuffer
	wrap_retro_unload_game();

	// Black out the screen before loading the menu
	size_t framebuffer_size = 640 * 480 * sizeof(uint16_t);
	uint16_t *framebuffer = (uint16_t *)malloc(framebuffer_size);
	if (framebuffer) {
		memset(framebuffer, 0, framebuffer_size);
		run_screen_write(framebuffer, 640, 480, 640 * sizeof(uint16_t));
		free(framebuffer);
	}

	// Prepare the loader to load the menu core
	const char *menu_name = NULL;
	const char *menu_path = NULL;
	get_menu_rom_info(&menu_name, &menu_path);
	strcpy(ptr_gs_run_game_file, menu_name);
	strcpy(ptr_gs_run_game_name, "FrogUI");

	// Deinit before running the menu
	wrap_retro_deinit();
	clear_snd_task_flags();

	// Clear any pending joy state: if the firmware sees SELECT+START (9) still
	// held while the menu core starts, it immediately triggers the pause/quit
	// flow and the menu ends up reloading in a loop with a black screen.
	g_joy_task_state = 0;
	g_joy_state = 0;

	xlog("[SHUTDOWN] Launching menu core: %s (joy=0x%08x)\n", menu_path, g_joy_task_state);
	run_game(menu_path, 0);

	// run_game() should never return; safety net just in case
	while (1) {
		dly_tsk(100);
	}
}

// Patch pause menu
//
// History: this hook used to call run_emulator_menu() (the firmware pause
// menu) after saving the SRM and snapshotting the framebuffer, exactly as
// upstream does. At some point the run_emulator_menu() call was dropped and
// replaced by a direct shutdown_game(), which made SELECT+START exit to FrogUI
// immediately on every core instead of opening a menu. The comment claimed
// this was "J2ME core only" but nothing ever checked library_name, so it
// applied to all cores - fheroes2 included.
//
// Restored for fheroes2 only: the firmware pause menu is what
// state_framebuffer below exists for, and without it save states were being
// written from a NULL buffer (wrap_state_save and the autosave path both
// no-op on `if (state_framebuffer)`).
/* Opens the stock firmware pause menu (Resume / Reboot / Save / Load).
 *
 * Exported so that cores which handle SELECT+START on their own (the J2ME
 * core unwinds its Java task before the firmware ever reaches
 * dummy_run_emulator_menu) get the exact same behaviour, including the
 * framebuffer snapshot that state_save() needs. Returning 0 means "resume".
 */
int frontend_open_pause_menu(void) {
	unsigned int response;

	save_srm(0); // Save before loading the pause menu (in case of crashes)

	// Snapshot the framebuffer. run_emulator_menu() draws over it, and this
	// copy is what state_save() writes out later - so it must be taken before
	// the menu opens. Also fixes the autosave path, which previously found
	// state_framebuffer NULL here and skipped saving entirely.
	state_fb_width = g_run_osd_width;
	state_fb_height = g_run_osd_height;
	state_framebuffer_size = state_fb_width * state_fb_height * 2;
	free(state_framebuffer);
	state_framebuffer = malloc(state_framebuffer_size);
	if (state_framebuffer) {
		memcpy(state_framebuffer, gp_run_osd_data, state_framebuffer_size);
	}
	else {
		xlog("[MENU] malloc(%lu) for framebuffer copy failed\n",
		     (unsigned long)state_framebuffer_size);
	}

	response = (unsigned int)run_emulator_menu();
	xlog("[MENU] pause menu response = %u\n", response);

	free(state_framebuffer);
	state_framebuffer = NULL;

	if (g_enable_frogui_patch && response == 1) {
		shutdown_game();	// "quit" button -> FrogUI
	}
	return (int)response;
}

int dummy_run_emulator_menu(void) {
	// Every core opens the firmware pause menu (Resume / Reboot / Save / Load).
	// Previously this hook was hardcoded to fheroes2 only and every other core
	// exited straight to FrogUI, so SELECT+START never showed the menu
	// anywhere else.
	xlog("[MENU] %s: SELECT+START -> opening firmware pause menu\n",
	     sysinfo.library_name ? sysinfo.library_name : "(unknown core)");
	return frontend_open_pause_menu();
}

// __core_entry__ must be placed at a known location in the binary (at the beginning)
// so that when the loader actually loads the binary into mem address 0x87000000,
// then __core_entry__ will be the first function there for the loader to call.
struct retro_core_t *__core_entry__(void) __attribute__((section(".init.core_entry")));

struct retro_core_t *__core_entry__(void)
{
	os_disable_interrupt();
	// https://gitlab.com/kobily/sf2000_multicore/-/commit/328bce4173316a6ea0afe4c360ee4f3d5b951c19 condensed to core's side with $gp value fetched from _start
	// Values are the same between gb300 V2 and SF2000
	*(unsigned *)0x80049744 = *(unsigned *)0x80001270; // lui	$gp
	*(unsigned *)0x80049748 = *(unsigned *)0x80001274; // addiu	$gp
	__builtin___clear_cache((void *)0x80049744, (void *)0x8004974c);

	// Patch Pause Menu
	PATCH_JAL((uintptr_t)&jal_run_emulator_menu, dummy_run_emulator_menu);
	__builtin___clear_cache(&jal_run_emulator_menu, &jal_run_emulator_menu+4);
	os_enable_interrupt();
	clear_bss();

	xlog("[FRONTEND] __core_entry__ bss cleared\n");

	extern void __sinit (struct _reent *);
	extern void __libc_init_array (void);

	_REENT_INIT_PTR(_REENT);
	__sinit(_REENT);
	__libc_init_array();

	xlog("libc initialized\n");

	return &core_exports;
}

static bool str_eq_ci(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

static bool extension_in_list_ci(const char *ext, const char *list) {
    if (!ext || !list) return false;

    const char *start = list;
    const char *end;

    while (*start)
    {
        end = strchr(start, '|');
        if (!end) end = start + strlen(start);

        size_t len = end - start;

        if (strlen(ext) == len && str_eq_ci(ext, start, len))
            return true;

        if (*end == '\0')
            break;
        start = end + 1;
    }

    return false;
}

bool extension_supports_no_fullpath(const char *ext) {
    for (size_t i = 0; i < content_info_override_count; i++) {
        const struct retro_system_content_info_override *ovr = content_info_overrides[i];

        if (!ovr->need_fullpath && extension_in_list_ci(ext, ovr->extensions))
            return true;
    }
    return false;
}

// Unzip a file into a buffer
void unzip_file(const char* path, void* buffer, bool is_wqw, uint32_t preview_size) {
	uint32_t unwqw_init_response = unwqw_init(path,preview_size,2);
    unwqw_decompress(unwqw_init_response,0,buffer,0,3);
	unwqw_free(unwqw_init_response);
}

// Check if a file is .zip or WQW (.zfc, .zsf, .zmd, .zgb, .zpc)
bool is_zip_wqw_file(const char* path, bool* is_wqw) {
	*is_wqw = false;

    size_t len = strlen(path);
    if (len <= 4) return false;

    const char* ext = path + len - 4;

    if (str_eq_ci(ext, ".zip", 4)) {
        return true;
    }

    // WQW extensions
    const char* wqw_exts[] = { ".zfc", ".zsf", ".zmd", ".zgb", ".zpc" };
    for (size_t i = 0; i < sizeof(wqw_exts)/sizeof(wqw_exts[0]); i++) {
        if (str_eq_ci(ext, wqw_exts[i], 4)) {
            *is_wqw = true;
            return true;
        }
    }

    return false;
}

void dump_zip_header(const char *zip_path, bool is_wqw, uint32_t preview_size, zip_local_file_header *zip_header) {
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        xlog("Failed to open ZIP file\n");
        return;
    }

	// If this is a WQW file, skip preview bytes
    if (is_wqw && preview_size > 0) {
        if (fseek(zip_file, preview_size, SEEK_SET) != 0) {
            xlog("Failed to skip preview bytes: %u\n", preview_size);
            fclose(zip_file);
            return;
        }
        xlog("Skipped %u preview bytes for WQW file\n", preview_size);
    }

    // Read fields from the header
	fread(&zip_header->signature, sizeof(uint32_t), 1, zip_file);
    fread(&zip_header->version_needed, sizeof(uint16_t), 1, zip_file);
    fread(&zip_header->general_flag, sizeof(uint16_t), 1, zip_file);
    fread(&zip_header->compression_method, sizeof(uint16_t), 1, zip_file);
    fread(&zip_header->last_mod_time, sizeof(uint16_t), 1, zip_file);
    fread(&zip_header->last_mod_date, sizeof(uint16_t), 1, zip_file);
    fread(&zip_header->crc32, sizeof(uint32_t), 1, zip_file);
    fread(&zip_header->compressed_size, sizeof(uint32_t), 1, zip_file);
    fread(&zip_header->uncompressed_size, sizeof(uint32_t), 1, zip_file);
    fread(&zip_header->filename_length, sizeof(uint16_t), 1, zip_file);
	fread(&zip_header->extra_field_length, sizeof(uint16_t), 1, zip_file);

    // Read the filename
    zip_header->filename = (char *)malloc(zip_header->filename_length + 1);  // +1 for null terminator
    fread(zip_header->filename, sizeof(char), zip_header->filename_length, zip_file);
    zip_header->filename[zip_header->filename_length] = '\0';  // Null-terminate the string

	// If the signature is 0x57515703 (0x03575157 little endian) (obfuscated WQW), undo XOR 0xE5 on the filename
    if (zip_header->signature == 0x03575157) {
        for (int i = 0; i < zip_header->filename_length; i++) {
            zip_header->filename[i] ^= 0xE5;
        }
    }

    fclose(zip_file);
}

/* Content stubs for cores that do not need a real ROM file. The console's
   loader derives the ROM path from its own bookkeeping rather than from the
   file we placed on the SD card, so the name it hands us is not always the one
   we deployed. Instead of bailing out (which leaves the console on a black
   screen forever), fall back to the stub shipped next to the core. */
static const char *fallback_rom_path = ROMS_DIRECTORY "/classicube/classicube.gba";

bool wrap_retro_load_game(const struct retro_game_info* info) {
	memcpy(&saved_game_info, info, sizeof(struct retro_game_info)); // Save the retro_game_info
	xlog("core=%s-%s need_fullpath=%d\n", sysinfo.library_name, sysinfo.library_version, sysinfo.need_fullpath); // Log info

	const char *rom_path = info->path;

	// Check if the file exists (if the core expects it to exist)
	FILE *hfile = fopen(rom_path, "rb");
	if (!hfile) {
		xlog("[core] rom not found (%s), trying fallback stub\n", rom_path);
		hfile = fopen(fallback_rom_path, "rb");
		if (hfile) {
			xlog("[core] using fallback stub %s\n", fallback_rom_path);
			rom_path = fallback_rom_path;
		} else {
			xlog("[core] fallback stub missing too, continuing with empty content\n");
		}
	}

	// setup load/save state handlers
	gfn_state_load = wrap_state_load;
	gfn_state_save = wrap_state_save;
	gfn_frameskip = NULL;

	// install custom input handler to filter out all requests for non-joypad devices
	retro_set_input_state(wrap_input_state_cb);

	// intercept audio output to mix stereo into mono
	retro_set_audio_sample(mono_mix_audio_sample_cb);
	retro_set_audio_sample_batch(mono_mix_audio_batch_cb);

	void *buffer = NULL;
	long size = 0;
	char *dir = NULL;
    char *filename = NULL;
    char *extension = NULL;
	bool is_wqw = false;
	bool core_supports_rom_in_buffer = false;
	zip_local_file_header zip_header;

	extract_path_components(rom_path, &dir, &filename, &extension);
	core_supports_rom_in_buffer = extension_supports_no_fullpath(extension);

	if (!hfile) {
		/* No content at all: hand the core empty buffers and let it decide. */
		game_info_ext.file_in_archive = false;
	} else if (is_zip_wqw_file(rom_path, &is_wqw)) {
		g_preview_size = 0;
		if (is_wqw) g_preview_size = g_preview_height * g_preview_width * 2;
		dump_zip_header(rom_path, is_wqw, g_preview_size, &zip_header);
		size = zip_header.uncompressed_size;
		// Extended game info
		game_info_ext.file_in_archive = true;
		game_info_ext.archive_path = rom_path;
		game_info_ext.archive_file = zip_header.filename;
		extract_extension(zip_header.filename, &extension);
		core_supports_rom_in_buffer = extension_supports_no_fullpath(extension);
		if (!sysinfo.need_fullpath || core_supports_rom_in_buffer) {
			buffer = malloc(size);
			unzip_file(rom_path, buffer, is_wqw, g_preview_size);
		}
	} else {
		fseeko(hfile, 0, SEEK_END);
		size = ftell(hfile);
		fseeko(hfile, 0, SEEK_SET);
		if (!sysinfo.need_fullpath || core_supports_rom_in_buffer) {
			buffer = malloc(size);
			fread(buffer, 1, size, hfile);
		}
		fclose(hfile);
		// Extended game info
		game_info_ext.file_in_archive = false;
	}

	if (sysinfo.need_fullpath && !core_supports_rom_in_buffer) xlog("core loads content directly from file\n");

	struct retro_game_info gameinfo;
	gameinfo.path = rom_path;
	if ((!sysinfo.need_fullpath || core_supports_rom_in_buffer) && buffer) {
		gameinfo.data = buffer;
		xlog("game loaded into temp buffer. size=%u\n", size);
	} else gameinfo.data = info->data;
	gameinfo.size = size;

	// Extended game info
	game_info_ext.persistent_data = false;
	game_info_ext.full_path = rom_path;
	if (!sysinfo.need_fullpath || core_supports_rom_in_buffer) game_info_ext.data = buffer;
	game_info_ext.size = size;
	game_info_ext.dir = dir;
	game_info_ext.name = filename;
	game_info_ext.ext = extension;

	bool ret = retro_load_game(&gameinfo);

	if (!sysinfo.need_fullpath || core_supports_rom_in_buffer) free(buffer);
	free(dir);
    free(filename);
    free(extension);

	if (!ret) {
		xlog("retro_load_game failed\n");
		gfn_retro_run = failed_retro_run;
		return false;
	}

	xlog("retro_load_game ok\n");

	// Make sure the first two controllers are configured as gamepads
	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);

	load_srm(0);

	if (g_auto_save_load) {
		state_load(g_auto_save_load_slot);
		if (g_per_state_srm) load_srm(g_auto_save_load_slot);
	}

	return true;
}

void wrap_retro_set_environment(retro_environment_t cb)
{
	retro_set_environment(wrap_environ_cb);
}

bool wrap_environ_cb(unsigned cmd, void *data)
{
	switch (cmd)
	{
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		{
			struct retro_log_callback *cb = (struct retro_log_callback*)data;
			cb->log = core_log_cb;
			return true;
		}

		case RETRO_ENVIRONMENT_SET_MESSAGE:
		case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
		{
			const struct retro_message *msg = (const struct retro_message*)data;
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"SET_MESSAGE: %s\n", msg->msg);
			return true;
		}

		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		{
			const char *dir = SYSTEM_DIRECTORY;
			*(const char**)data = dir;
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"SYSTEM_DIRECTORY: \"%s\"\n", dir);
			return true;
		}

		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		{
			const char *dir = SAVE_DIRECTORY;
			*(const char**)data = dir;
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"SAVE_DIRECTORY: \"%s\"\n", dir);
			return true;
		}


		case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
		{
			if (!assets_dir) return false;
			*(const char**)data = assets_dir;
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"ASSETS_DIRECTORY: \"%s\"\n", assets_dir);
			return true;
		}

		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*(bool*)data = true;
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"GET_CAN_DUPE: true\n");
			return true;

		case RETRO_ENVIRONMENT_GET_VARIABLE:
		{
			struct retro_variable *var = (struct retro_variable*)data;
			bool ret = config_get_var(&s_core_config, var);
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"GET_VARIABLE: %s=%s\n", var->key, ret ? var->value : "");
			return ret;
		}

		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		{
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"SET_MEMORY_MAPS\n");
			break;
		}

		case RETRO_ENVIRONMENT_SET_GEOMETRY:
		{
    		const struct retro_game_geometry *geom = (const struct retro_game_geometry*)data;
    		frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"SET_GEOMETRY: %ux%u, Aspect: %.3f.\n", geom->base_width, geom->base_height, geom->aspect_ratio);

    		// Check if resolution actually changed
    		if (geom->base_width != geom_prev_width || geom->base_height != geom_prev_height) {
				extern double g_ratio;
				extern scaling_mode_enum scaling_mode;
				struct retro_system_av_info info;
				if (scaling_mode == CORE_PROVIDED) {
					retro_get_system_av_info(&info);
					if (info.geometry.aspect_ratio <= 0.1f)
						g_ratio = 1.0 * info.geometry.base_width / info.geometry.base_height;
					else
						g_ratio = info.geometry.aspect_ratio;
				}
        		geom_prev_width = geom->base_width;
        		geom_prev_height = geom->base_height;
    		}
    		break;
		}

		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		{
			enum retro_pixel_format fmt = *(enum retro_pixel_format*)data;
			if (fmt == RETRO_PIXEL_FORMAT_XRGB8888)
			{
				enable_xrgb8888_support();
				return true;
			}
			break;
		}

		case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
		{
			struct retro_audio_buffer_status_callback *buff_status_cb = (struct retro_audio_buffer_status_callback*)data;
			if (buff_status_cb)
			{
				audio_buff_status_cb = buff_status_cb->callback;
				gfn_frameskip = frameskip_cb;
			}
			else
				gfn_frameskip = NULL;

			xlog("support for auto frameskipping %s\n", buff_status_cb ? "enabled" : "disabled" );
			return true;
		}

		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		{
            *(bool*)data = variable_update_flag;
            variable_update_flag = false;
            return true;
		}

		case RETRO_ENVIRONMENT_SHUTDOWN:
        {
            frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"RETRO_ENVIRONMENT_SHUTDOWN\n");
			shutdown_game();
            return true;
        }

		case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
    	{
			frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"GET_GAME_INFO_EXT\n");
        	*(struct retro_game_info_ext**)data = &game_info_ext;
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Full Path: %s\n", game_info_ext.full_path ? game_info_ext.full_path : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Archive Path: %s\n", game_info_ext.archive_path ? game_info_ext.archive_path : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Archive File: %s\n", game_info_ext.archive_file ? game_info_ext.archive_file : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Directory: %s\n", game_info_ext.dir ? game_info_ext.dir : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Name: %s\n", game_info_ext.name ? game_info_ext.name : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Extension: %s\n", game_info_ext.ext ? game_info_ext.ext : "N/A");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Data: %p\n", game_info_ext.data);
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Size: %d bytes\n", game_info_ext.size);
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"File in Archive: %s\n", game_info_ext.file_in_archive ? "Yes" : "No");
        	frontend_log_cb(RETRO_LOG_INFO, "ENVIRON" ,"Persistent Data: %s\n", game_info_ext.persistent_data ? "Yes" : "No");
        	return true;
    	}

		case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
		{
    		const struct retro_system_content_info_override *overrides = (const struct retro_system_content_info_override *)data;
    		content_info_override_count = 0;

    		for (size_t i = 0; overrides[i].extensions; i++) {
        		if (content_info_override_count >= MAX_CONTENT_INFO_OVERRIDES) {
            		frontend_log_cb(RETRO_LOG_WARN, "ENVIRON" ,"SET_CONTENT_INFO_OVERRIDE: too many overrides, truncating at %zu\n", (size_t)MAX_CONTENT_INFO_OVERRIDES);
            		break;
        		}
        		content_info_overrides[content_info_override_count++] = &overrides[i];
    		}

    		return true;
		}
		
	}
	return retro_environment_cb(cmd, data);
}

void build_state_filepath(char *state_filepath, size_t size, const char *game_filepath, int save_slot) {
	//	"/mnt/sda1/ROMS/pce/Alien Crush.pce"	->
	//	"/mnt/sda1/saves/savestates/[core]/Alien Crush.state[slot]"
	char basename[MAXPATH];
	char directory[MAXPATH];
	fill_pathname_base(basename, game_filepath, sizeof(basename));
	path_remove_extension(basename);
	create_dir(SAVESTATE_DIRECTORY); // Make sure SAVESTATE_DIRECTORY exists
	snprintf(directory, size, "%s", SAVESTATE_DIRECTORY);
	create_dir(directory); // Make sure SAVESTATE_DIRECTORY exists
	strcat(directory, "/");
	strcat(directory, sysinfo.library_name);
	create_dir(directory); // Make sure SAVESTATE_DIRECTORY/sysinfo.library_name exists
	snprintf(state_filepath, size, "%s/%s.state%d", directory, basename, save_slot);
}

int extract_slot(const char *path) {
    int save_slot = slot_state;
    size_t len = strlen(path);

    size_t i = len;
    while (i > 0 && isdigit((unsigned char)path[i - 1])) {
        i--;
    }

    if (i < len) {
        save_slot = atoi(&path[i]);
    }

    return save_slot;
}

int wrap_state_load(const char *frontend_state_filepath) {
    return state_load(extract_slot(frontend_state_filepath));
}

int wrap_state_save(const char *frontend_state_filepath) {
	// Make sure frontend_state_filepath directory exists
	if (strlen(frontend_state_filepath) != 0) { 
		char frontend_state_directory[MAXPATH];
		strcpy(frontend_state_directory, frontend_state_filepath);
		char *last_slash = strrchr(frontend_state_directory, '/');
		*last_slash = '\0';
		create_dir(frontend_state_directory);
	}
    return state_save(extract_slot(frontend_state_filepath), state_framebuffer, state_fb_width, state_fb_height);
}

int state_load(int save_slot) {
	char state_filepath[MAXPATH];
	build_state_filepath(state_filepath, sizeof(state_filepath), saved_game_info.path, save_slot);
	xlog("state_load: file=%s\n", state_filepath);

	FILE *file = fopen(state_filepath, "rb");
	if (!file)
		return 0;

	fseeko(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseeko(file, 0, SEEK_SET);

	void *data = malloc(size);

	fread(data, 1, size, file);
	fclose(file);

	retro_unserialize(data, size);

	free(data);

	if(g_per_state_srm) load_srm(save_slot);
	return 1;
}

int state_save(int save_slot, uint16_t* emu_fb, uint32_t fb_width, uint32_t fb_height) {
	char state_filepath[MAXPATH];
	build_state_filepath(state_filepath, sizeof(state_filepath), saved_game_info.path, save_slot);
	xlog("state_save: file=%s\n", state_filepath);

	FILE *file = fopen(state_filepath, "wb");
	if (!file)
		return 0;

	size_t size = retro_serialize_size();
	void *data = calloc(size, 1);

	retro_serialize(data, size);

	fwrite(data, size, 1, file);
	fclose(file);

	free(data);

	fs_sync(state_filepath);

	if(g_per_state_srm) save_srm(save_slot);

	// Save an rgb565 image of the state framebuffer
	char bmp_filepath[MAXPATH];
    snprintf(bmp_filepath, sizeof(bmp_filepath), "%s.bmp", state_filepath);
	save_bmp_image(emu_fb, fb_width, fb_height, bmp_filepath, g_xrgb888);

	return 1;
}

void wrap_retro_init(void) {	
	xlog("[FRONTEND] wrap_retro_init enter\n");
	retro_get_system_info(&sysinfo);
	snprintf(assets_dir, sizeof(assets_dir), "%s/%s", ASSETS_DIRECTORY, sysinfo.library_name);
	config_load(&s_core_config, ptr_gs_run_game_file, sysinfo.library_name); // Load configs
	multicore_options_load(&s_core_config); // Load settings
	load_keymap(ptr_gs_run_game_file); // Load keymap
	if (g_enable_cheats_hotkey) load_cheats(ptr_gs_run_game_file); // load cheats
	retro_set_video_refresh(wrap_video_refresh_cb);
	retro_init();
}

void wrap_retro_deinit(void) {
	if (g_fps_counter) *fw_fps_counter_enable = 0;
	video_cleanup();
	retro_deinit();
	config_free(&s_core_config);

	free(state_framebuffer);
	state_framebuffer = NULL;
	free(rgb565_darken_buffer);
	rgb565_darken_buffer = NULL;
	free(s_rgb565_convert_buffer);
	s_rgb565_convert_buffer = NULL;
}

size_t mono_mix_audio_batch_cb(const int16_t *data, size_t frames)
{
	// TODO: is data always assumed to be s16bit dual channel buffer?
	for (size_t i=0; i < frames*2; i+=2)
	{
		// for single speaker output, mix to mono both channels into the first channel
		((int16_t*)data)[i] = (data[i] >> 1) + (data[i+1] >> 1);
		// leave the second channel as is because it is not heard anyway
		//((int16_t*)data)[i+1] = 0;
	}

	// NOTE: stock frontend audio_batch_cb always return 0!
	retro_audio_sample_batch_cb(data, frames);
	// return `frames` as if all data was consumed
	return frames;
}

void mono_mix_audio_sample_cb(int16_t left, int16_t right)
{
	int16_t mixed = (left >> 1) + (right >> 1);
	int16_t data[2] = {mixed, right};
	retro_audio_sample_batch_cb(data, 1);
}

void convert_xrgb8888_to_rgb565(void* buffer, unsigned width, unsigned height, size_t stride)
{
	uint32_t* xrgb8888_buffer = (uint32_t*)buffer;
	uint16_t* rgb565_buffer = s_rgb565_convert_buffer;

    for (int y = 0; y < height; y++)
	{
        for (int x = 0; x < width; x++)
		{
            int index = y * stride / sizeof(uint32_t) + x;

            uint32_t xrgbPixel = xrgb8888_buffer[index];

            uint16_t rgb565Pixel = ((xrgbPixel >> 8) & 0xF800) | ((xrgbPixel >> 5) & 0x07E0) | ((xrgbPixel >> 3) & 0x001F);

            rgb565_buffer[index] = rgb565Pixel;
        }
    }
}

void xrgb8888_video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
	convert_xrgb8888_to_rgb565((void*)data, width, height, pitch);

	retro_video_refresh_cb(s_rgb565_convert_buffer, width, height, width * 2);		// each pixel is now 16bit, so pass the pitch as width*2
}

static void enable_xrgb8888_support()
{
	xlog("support for XRGB8888 enabled\n");

	struct retro_system_av_info av_info;
	retro_get_system_av_info(&av_info);

	s_rgb565_convert_buffer = (uint16_t*)malloc(av_info.geometry.max_width * av_info.geometry.max_height * sizeof(uint16_t));

	xlog("created rgb565_convert_buffer=%p width=%u height=%u\n",
		s_rgb565_convert_buffer, av_info.geometry.max_width, av_info.geometry.max_height);

	g_xrgb888 = true;
}

static int16_t wrap_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if ((port == 0 || port == 1) && (device == RETRO_DEVICE_JOYPAD)) {
		unsigned logical_port = (g_swap_controllers && (port <= 1)) ? (port ^ 1) : port;
		return retro_input_state_cb(logical_port, device, index, id);
	} else
		return 0;
}

static void frameskip_cb(BOOL flag) {
	audio_buff_status_cb(flag == 1 /*active*/, 0 /*occupancy*/, true /*underrun_likely*/);
}

static void failed_retro_run(void) {
	shutdown_game();
}

void darken_rgb565_buffer(const void* buffer, unsigned width, unsigned height, size_t pitch_bytes, uint8_t darken_percentage) {
    const uint16_t* src = (const uint16_t*)buffer;
    uint16_t* dst = rgb565_darken_buffer;

    // Convert darken_percentage (0-100) to darken_factor_256 (0-255)
    uint8_t darken_factor_256 = ((100 - darken_percentage) * 255) / 100;

    for (unsigned y = 0; y < height; y++) {
        const uint16_t* src_row = (const uint16_t*)((const uint8_t*)src + y * pitch_bytes);
        uint16_t* dst_row = dst + y * width;

        for (unsigned x = 0; x < width; x++) {
            uint16_t pixel = src_row[x];

            // Extract RGB components
            uint8_t r5 = (pixel >> 11) & 0x1F;
            uint8_t g6 = (pixel >> 5) & 0x3F;
            uint8_t b5 = pixel & 0x1F;

            // Darken components
            r5 = (r5 * darken_factor_256) >> 8;
            g6 = (g6 * darken_factor_256) >> 8;
            b5 = (b5 * darken_factor_256) >> 8;

            // Repack components
            dst_row[x] = (r5 << 11) | (g6 << 5) | b5;
        }
    }
}

static void handle_rgb565_darken(const void *data, unsigned width, unsigned height, size_t pitch) {
	darken_rgb565_buffer(data, width, height, pitch, g_darken_percentage);
	retro_video_refresh_cb(rgb565_darken_buffer, width, height, width * 2);
}

void wrap_video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
	if (g_show_fps) {
		*fw_fps_counter_enable = 1;
		if (!g_fps_counter) g_fps_counter = true; 
		static uint32_t prev_msec = 0;
		static int count_all = 0;
		static int count_not_skipped = 0;

		uint32_t curr_msec = os_get_tick_count();

		++count_all;
		if (data)
			++count_not_skipped;

		if (curr_msec - prev_msec > 1000) {
			// im not sure that using floats math will calc the fps much more accurately
			// float sec = ((curr_msec - prev_msec) / 1000.0f);
			// *fw_fps_counter1 = count_not_skipped / sec;
			// fps_counter2 = count_all / sec;

			if (os_get_tick_count() - g_osd_time > 1000) sprintf(fw_fps_counter_format, "%2d/%2d", count_not_skipped, count_all);

			prev_msec = curr_msec;
			count_all = 0;
			count_not_skipped = 0;
		}
	}
	
	if (!rgb565_darken_buffer || width != buffer_prev_width || height != buffer_prev_height) {
    	if (rgb565_darken_buffer) free(rgb565_darken_buffer);

    	rgb565_darken_buffer = malloc(width * height * 2);
    	buffer_prev_width = width;
    	buffer_prev_height = height;
	}

	if (g_xrgb888) { //TODO: add darkening filter support
		convert_xrgb8888_to_rgb565((void*)data, width, height, pitch);
		// each pixel is now 16bit, so pass the pitch as width*2
		if (data && g_enable_darken_filter) handle_rgb565_darken(s_rgb565_convert_buffer, width, height, width * 2);
		else retro_video_refresh_cb(s_rgb565_convert_buffer, width, height, width * 2);
	} else {
		if (data && g_enable_darken_filter) handle_rgb565_darken(data, width, height, pitch);
		else retro_video_refresh_cb(data, width, height, pitch);
	}
}

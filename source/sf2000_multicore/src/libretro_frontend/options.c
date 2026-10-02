#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <debug.h>
#include <deimos.h>
#include <file.h>
#include <libretro.h>
#include <file/file_path.h>
#include <file/config_file.h>
#include <options.h>
#include <stockfw.h>
#include <video_sf2000.h>

#define KEYMAP_SIZE 12
#define MAX_CHEAT_LENGTH 200
#define MAX_CHEATS 100

static char *gc_cheats[MAX_CHEATS];  // Array of strings to store lines
static int gi_cheat_count = 0;  // Keeps track of the number of lines read
bool gb_cheats_enabled = false;

bool g_show_fps = false;
bool g_per_state_srm = false;
bool g_per_core_srm = false;
bool g_enable_savestate_hotkeys = true;
bool g_enable_screenshot_hotkey = true;
bool g_enable_darken_filter = true;
bool g_enable_darken_hotkey = true;
int g_darken_percentage = 0;
bool g_swap_controllers = false;
bool g_swap_gameboy = true;
bool g_enable_ctrlswp_hotkey = true;
bool g_enable_osd = true;
bool g_osd_small_messages = false;
bool g_enable_frogui_patch = true;
bool g_auto_save_load = false;
int g_auto_save_load_slot = 10;
bool g_enable_cheats_hotkey = false;

/*		Configs		*/
static void config_add_file(config_file_t **config_file, const char *filepath) {
	bool ret = config_append_file(*config_file, filepath);
	xlog("config_load: %s %s\n", filepath, ret ? "loaded" : "not found");
}

void config_free(config_file_t **config_file) {
	config_file_free(*config_file);
	*config_file = NULL;
}

void config_load(config_file_t **config_file, const char *game_filepath, const char *library_name) {
	if (config_file) config_free(config_file);
	*config_file = config_file_new_alloc();

	// load global multicore options
	config_add_file(config_file, CONFIG_DIRECTORY "/multicore.opt");

	// load per core options
	char config_filepath[MAXPATH];
	snprintf(config_filepath, sizeof(config_filepath), "%s/%s/%s.opt", CONFIG_DIRECTORY, library_name, library_name);
	config_add_file(config_file, config_filepath);

	// load per game options
	snprintf(config_filepath, sizeof(config_filepath), "%s/%s/options/%s.opt", CONFIG_DIRECTORY, library_name, game_filepath);
	config_add_file(config_file, config_filepath);
}

bool config_get_var(config_file_t **config_file, struct retro_variable *var) {
	if (!config_file) return false;

	const struct config_entry_list *entry = config_get_entry(*config_file, var->key);
	if (!entry) return false;

	var->value = entry->value;
	return true;
}

bool config_entry_exists(config_file_t **config_file, const char *key) {
	struct config_entry_list *entry;
    for (entry = (*config_file)->entries; entry; entry = entry->next)
    {
        if (strcmp(entry->key, key) == 0)
            return true;
    }

    return false;
}

void multicore_options_load(config_file_t **config_file) {
	if (!config_file) return;

	// Video settings in video_sf2000
	video_options(*config_file);
	
	// Frogui pause menu patch
	config_get_bool(*config_file, "sf2000_enable_frogui_patch", &g_enable_frogui_patch);

	// show FPS
	config_get_bool(*config_file, "sf2000_show_fps", &g_show_fps);

	// Auto load save states
	config_get_bool(*config_file, "sf2000_auto_save_load", &g_auto_save_load);
	config_get_uint(*config_file, "sf2000_auto_save_load_slot", &g_auto_save_load_slot);

	config_get_bool(*config_file, "sf2000_per_state_srm", &g_per_state_srm); // per state srm
	config_get_bool(*config_file, "sf2000_per_core_srm", &g_per_core_srm); // per core srm

	// Hotkey settings
	config_get_bool(*config_file, "sf2000_enable_savestate_hotkeys", &g_enable_savestate_hotkeys);
	config_get_bool(*config_file, "sf2000_enable_screenshot_hotkey", &g_enable_screenshot_hotkey);
	config_get_bool(*config_file, "sf2000_enable_osd", &g_enable_osd);
	config_get_bool(*config_file, "sf2000_osd_small_messages", &g_osd_small_messages);
		
	// Darkening filter
	config_get_bool(*config_file, "sf2000_enable_darken_filter", &g_enable_darken_filter);
	config_get_bool(*config_file, "sf2000_enable_darken_hotkey", &g_enable_darken_hotkey);
	config_get_uint(*config_file, "sf2000_darken_percentage", &g_darken_percentage);

	// Controller swap
	config_get_bool(*config_file, "sf2000_swap_controllers", &g_swap_controllers);
	config_get_bool(*config_file, "sf2000_swap_gameboy", &g_swap_gameboy);
	config_get_bool(*config_file, "sf2000_enable_ctrlswp_hotkey", &g_enable_ctrlswp_hotkey);

	// Cheats 
	config_get_bool(*config_file, "sf2000_enable_cheats_hotkey", &g_enable_cheats_hotkey);
}

/*		Keymaps		*/
void load_keymap(const char *s_game_filepath) {
	char kmp_filepath[MAXPATH];

	// Rom keymap
	snprintf(kmp_filepath, sizeof(kmp_filepath), "%s/%s/keymaps/%s.kmp", CONFIG_DIRECTORY, sysinfo.library_name, s_game_filepath);
	xlog("Checking keymap in %s...\n", kmp_filepath);

	// if ROM keymap doesn't exist load Core keymap
	if (fs_access(kmp_filepath, 0) != 0) {
		snprintf(kmp_filepath, sizeof(kmp_filepath), "%s/%s/%s.kmp", CONFIG_DIRECTORY, sysinfo.library_name, sysinfo.library_name);
		xlog("Checking keymap in %s...\n", kmp_filepath);
	}

	// if Core keymap doesn't exist load Multicore keymap
	if (fs_access(kmp_filepath, 0) != 0) {
		snprintf(kmp_filepath, sizeof(kmp_filepath), "%s/multicore.kmp", CONFIG_DIRECTORY);
		xlog("Checking keymap in %s...\n", kmp_filepath);
	}

	// if no keymap, just return
	if (fs_access(kmp_filepath, 0) != 0) return;

	uint32_t keymap[KEYMAP_SIZE];
	FILE *h_file = NULL;
	h_file = fopen(kmp_filepath, "rb");

	size_t elements_read = fread(keymap, sizeof(uint32_t), KEYMAP_SIZE, h_file);
	fclose(h_file);

    set_keymap(keymap, 8);
	xlog("Keymap file %s loaded\n", kmp_filepath);
}

/*		Cheats		*/
void load_cheats(const char *s_game_filepath) {
	char cht_filepath[MAXPATH];

	snprintf(cht_filepath, sizeof(cht_filepath), "%s/%s/cheats/%s.cht", CONFIG_DIRECTORY, sysinfo.library_name, s_game_filepath);
	xlog("Checking for cheat codes in %s...\n", cht_filepath);

	FILE *h_file = fopen(cht_filepath, "r");
	if (!h_file) return;

	char buffer[MAX_CHEAT_LENGTH]; // Temporary buffer
	while (fgets(buffer, MAX_CHEAT_LENGTH, h_file) && gi_cheat_count < MAX_CHEATS) {
		// Remove newline character at the end if exists
		buffer[strcspn(buffer, "\n\r")] = 0;

		// Skip empty lines and comments
		if (buffer[0] == '\0' || buffer[0] == '#' || buffer[0] == ';')
    		continue;

		// Allocate memory for the line and store it in the global array
		gc_cheats[gi_cheat_count] = (char *)malloc(strlen(buffer) + 1);
		if (gc_cheats[gi_cheat_count]) {
			strcpy(gc_cheats[gi_cheat_count], buffer);
			gi_cheat_count++;
		}
	}
	fclose(h_file);
	xlog("Cheat codes loaded\n");
}

void unload_cheats(void) {
	for (int i = 0; i < gi_cheat_count; i++) {
        free(gc_cheats[i]);
		gc_cheats[i] = NULL;
    }
	gi_cheat_count = 0;
}

void toggle_cheat(void) {
	gb_cheats_enabled = !gb_cheats_enabled;
	if (gb_cheats_enabled) {
		retro_cheat_reset();
		for (int i = 0; i < gi_cheat_count; i++) {
			retro_cheat_set(i, true, gc_cheats[i]);
		}
	} else retro_cheat_reset();
}
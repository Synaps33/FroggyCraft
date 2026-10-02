#ifndef __OPTIONS_H
#define __OPTIONS_H

#include <stdbool.h>
#include <libretro.h>
#include <file/file_path.h>
#include <file/config_file.h>

extern bool gb_cheats_enabled;

extern void config_load(config_file_t **config_file, const char *game_filepath, const char *library_name);
extern void config_free(config_file_t **config_file);
extern bool config_get_var(config_file_t **config_file, struct retro_variable *var);
extern bool config_entry_exists(config_file_t **config_file, const char *key);
extern void multicore_options_load(config_file_t **config_file);

extern void load_keymap(const char *s_game_filepath);

extern void load_cheats(const char *s_game_filepath);
extern void unload_cheats(void);
extern void toggle_cheat(void);

extern bool g_show_fps;                 // sf2000_show_fps                  Default: False
extern bool g_per_state_srm;            // sf2000_per_state_srm             Default: False
extern bool g_per_core_srm;             // sf2000_per_core_srm              Default: False
extern bool g_enable_savestate_hotkeys; // sf2000_enable_savestate_hotkeys  Default: True
extern bool g_enable_screenshot_hotkey; // sf2000_enable_screenshot_hotkey  Default: True
extern bool g_enable_darken_filter;     // sf2000_enable_darken_filter      Default: True
extern bool g_enable_darken_hotkey;     // sf2000_enable_darken_hotkey      Default: True
extern int g_darken_percentage;         // sf2000_darken_percentage         Default: 0
extern bool g_swap_controllers;         // sf2000_swap_controllers          Default: False
extern bool g_swap_gameboy;             // sf2000_swap_gameboy              Default: True
extern bool g_enable_ctrlswp_hotkey;    // sf2000_enable_ctrlswp_hotkey     Default: True
extern bool g_enable_osd;               // sf2000_enable_osd                Default: True
extern bool g_osd_small_messages;       // sf2000_osd_small_messages        Default: False
extern bool g_enable_frogui_patch;      // sf2000_enable_frogui_patch       Default: True
extern bool g_auto_save_load;           // sf2000_auto_save_load            Default: False
extern int g_auto_save_load_slot;       // sf2000_auto_save_load_slot       Default: 10
extern bool g_enable_cheats_hotkey;     // sf2000_enable_cheats_hotkey      Default: False

#endif //__OPTIONS_H
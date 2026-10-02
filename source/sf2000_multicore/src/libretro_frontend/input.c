#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <libretro.h>

#include <deimos.h>
#include <file.h>
#include <images.h>
#include <input.h>
#include <logging.h>
#include <options.h>

// Hotkeys
// Up = 10, Down = 40, Left = 80, Right = 20
// Select = 1, Start = 8
// A = 2000, B = 4000. X = 400, Y = 800, L = 1000, R = 8000
#define HOTKEYSCREENSHOT 0x9008 // press L + R + Start
#define HOTKEYCHEATS 0x9001 // press L + R + Select
#define HOTKEYSAVESTATE 0x9400 // press L + R + X
#define HOTKEYLOADSTATE 0x9800 // press L + R + Y
#define HOTKEYINCREASESTATE 0x9020 // press L + R + Right
#define HOTKEYDECREASESTATE 0x9080 // press L + R + LEFT
#define HOTKEYINCREASEDARKEN 0x9010 // press L + R + UP
#define HOTKEYDECREASEDARKEN 0x9040 // press L + R + DOWN
#define HOTKEYSWAPPLAYER 45056 // press L + R + A

static int screenshot_counter = 1; // Counter for screenshot filenames
int slot_state = 0;

// Do not use retro_input_state_cb to avoid key remapping issues
// Use osd delay to prevent spamming hotkeys

bool hotkey_screenshot(void) {
    if (os_get_tick_count() - g_osd_time < 1000 || !g_enable_screenshot_hotkey) return false;
    char filename[MAXPATH];
	char basename[MAXPATH];
	fill_pathname_base(basename, saved_game_info.path, sizeof(basename));
	path_remove_extension(basename);
    sprintf(filename, "/mnt/sda1/%s_screenshot_%d.bmp", basename, screenshot_counter);
	save_bmp_image(gp_run_osd_data, g_run_osd_width, g_run_osd_height, filename, false);
	screenshot_counter++;
	g_osd_small_messages ? sprintf(osd_message, "Saved") : sprintf(osd_message, "Screenshot Saved");
	return true;
}

bool hotkey_cheats(void) {
    if (os_get_tick_count() - g_osd_time < 1000 || !g_enable_cheats_hotkey) return false;
    toggle_cheat();
	g_osd_small_messages ? sprintf(osd_message, "C:%c", gb_cheats_enabled ? 'E' : 'D') : sprintf(osd_message, "Cheats: %s", gb_cheats_enabled ? "Enabled" : "Disabled");
    return true;
}

bool hotkey_save_state(void) {
    if (os_get_tick_count() - g_osd_time < 1000 || !g_enable_savestate_hotkeys) return false;
    state_save(slot_state, gp_run_osd_data, g_run_osd_width, g_run_osd_height);
    g_osd_small_messages ? sprintf(osd_message, "S:%d", slot_state) : sprintf(osd_message, "Save: %d", slot_state);
    return true;
}

bool hotkey_load_state(void) {
    if (os_get_tick_count() - g_osd_time < 1000 || !g_enable_savestate_hotkeys) return false;
    state_load(slot_state);
    g_osd_small_messages ? sprintf(osd_message, "L:%d", slot_state) : sprintf(osd_message, "Load: %d", slot_state);
    return true;
}

bool hotkey_increase_state(void) {
    if (os_get_tick_count() - g_osd_time < 100 || !g_enable_savestate_hotkeys) return false;
    if (slot_state < 9) slot_state += 1;
    else slot_state = 0;
    g_osd_small_messages ? sprintf(osd_message, "SLT:%d", slot_state) : sprintf(osd_message, "Slot: %d", slot_state);
    return true;
}

bool hotkey_decrease_state(void) {
    if (os_get_tick_count() - g_osd_time < 100 || !g_enable_savestate_hotkeys) return false;
    if (slot_state > 0) slot_state -= 1;
    else slot_state = 9;
    g_osd_small_messages ? sprintf(osd_message, "SLT:%d", slot_state) : sprintf(osd_message, "Slot: %d", slot_state);
    return true;
}

bool hotkey_increase_darken(void) {
    if (os_get_tick_count() - g_osd_time < 100 || !g_enable_darken_filter || !g_enable_darken_hotkey) return false;
    if (g_darken_percentage < 100) g_darken_percentage += 1;
    else g_darken_percentage = 0;
    g_osd_small_messages ? sprintf(osd_message, "DF:%d", g_darken_percentage) : sprintf(osd_message, "Darken: %d", g_darken_percentage);
    return true;
}

bool hotkey_decrease_darken(void) {
    if (os_get_tick_count() - g_osd_time < 100 || !g_enable_darken_filter || !g_enable_darken_hotkey) return false;
    if (g_darken_percentage > 0) g_darken_percentage -= 1;
    else g_darken_percentage = 100;
    g_osd_small_messages ? sprintf(osd_message, "DF:%d", g_darken_percentage) : sprintf(osd_message, "Darken: %d", g_darken_percentage);
    return true;
}

bool hotkey_swap_player(void) {
    if (os_get_tick_count() - g_osd_time < 1000 || !g_enable_ctrlswp_hotkey) return false;
    bool is_dcgb = (strcmp(sysinfo.library_name, "DoubleCherryGB") == 0);
    const char *single_screen_mp = is_dcgb ? "dcgb_single_screen_mp" : "tgbdual_single_screen_mp";
    const char *audio_output     = is_dcgb ? "dcgb_audio_output"     : "tgbdual_audio_output";

    g_swap_controllers = !g_swap_controllers;

    if (g_swap_gameboy && (is_dcgb || strcmp(sysinfo.library_name, "TGB Dual") == 0)) {
        if (config_entry_exists(&s_core_config, single_screen_mp)) {
            config_set_string(s_core_config, single_screen_mp, g_swap_controllers ? "player 2 only" : "player 1 only");
            config_set_string(s_core_config, audio_output,     g_swap_controllers ? "Game Boy #2"   : "Game Boy #1");
            variable_update_flag = true;
        }
    }

    g_osd_small_messages ? sprintf(osd_message, "P%d", g_swap_controllers + 1) : sprintf(osd_message, "Player %d", g_swap_controllers + 1);
    return true;
}

static const hotkey_entry_t hotkeys[] = {
    { HOTKEYSCREENSHOT, hotkey_screenshot },
    { HOTKEYCHEATS, hotkey_cheats },
    { HOTKEYSAVESTATE, hotkey_save_state },
    { HOTKEYLOADSTATE, hotkey_load_state },
    { HOTKEYINCREASESTATE, hotkey_increase_state },
    { HOTKEYDECREASESTATE, hotkey_decrease_state },
    { HOTKEYINCREASEDARKEN, hotkey_increase_darken },
    { HOTKEYDECREASEDARKEN, hotkey_decrease_darken },
    { HOTKEYSWAPPLAYER, hotkey_swap_player },
};

void frontend_check_hotkeys(void) {
    for (size_t i = 0; i < sizeof(hotkeys)/sizeof(hotkeys[0]); ++i) {
        if (g_joy_task_state == hotkeys[i].mask) {
            g_joy_state = 0; // Reset g_joy_state to avoid button presses
            bool ret = hotkeys[i].action();
            if (ret) show_osd_message(osd_message);
        }
    }
}
#ifndef __DEIMOS_H
#define __DEIMOS_H

#include <stdint.h>

#include <libretro.h>
#include <file/file_path.h>
#include <file/config_file.h>

extern struct retro_game_info saved_game_info;
extern struct retro_system_info sysinfo;
extern config_file_t *s_core_config;

extern bool variable_update_flag;
extern bool g_fps_counter;

extern int state_load(int save_slot);
extern int state_save(int save_slot, uint16_t* emu_fb, uint32_t fb_width, uint32_t fb_height);
extern int wrap_state_load(const char *frontend_state_filepath);
extern int wrap_state_save(const char *frontend_state_filepath);
extern void shutdown_game(void);

#endif //__DEIMOS_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <debug.h>
#include <deimos.h>
#include <file.h>
#include <libretro.h>
#include <logging.h>
#include <options.h>
#include <stockfw.h>

#define fw_fps_counter_enable   ((int *)__fw_fps_counter_enable)
#define fw_fps_counter          ((int *)__fw_fps_counter)
#define fw_fps_counter_format   ((char *)__fw_fps_counter_format)

// OSD vars
bool gb_temporary_osd = false;
char osd_message[MAXPATH];
uint32_t g_osd_time = 0;

// Show osd message
int show_osd_message(const char *message) {
	if (g_enable_osd) {
		gb_temporary_osd = true;
		if (!g_fps_counter) *fw_fps_counter_enable = 1; // Don't change fps if fps is enabled
		sprintf(fw_fps_counter_format, message);
		g_osd_time = os_get_tick_count();
	}
}

void frontend_log_cb(enum retro_log_level level, const char *tag, const char *fmt, ...) {
    char buffer[500];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Determine the log level string
	// TODO: Add a setting to block logs as a whole and specific log levels
	// Users will probably only want WARN and ERROR logs
    const char *level_str = "";
    switch (level) {
        case RETRO_LOG_DEBUG: level_str = "DEBUG"; break;
        case RETRO_LOG_INFO: level_str = "INFO"; break;
        case RETRO_LOG_WARN: level_str = "WARN"; break;
        case RETRO_LOG_ERROR: level_str = "ERROR"; break;
        default: break;
    }
    xlog("[%s][%s] %s", tag, level_str, buffer);
}

void core_log_cb(enum retro_log_level level, const char *fmt, ...) {
    char buffer[500];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Determine the log level string
	// TODO: Add a setting to block logs as a whole and specific log levels
	// Users will probably only want WARN and ERROR logs
    const char *level_str = "";
    switch (level) {
        case RETRO_LOG_DEBUG: level_str = "DEBUG"; break;
        case RETRO_LOG_INFO: level_str = "INFO"; break;
        case RETRO_LOG_WARN: level_str = "WARN"; break;
        case RETRO_LOG_ERROR: level_str = "ERROR"; break;
        default: break;
    }
    xlog("[CORE][%s] %s", level_str, buffer);
}
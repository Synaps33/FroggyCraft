#ifndef __LOGGING_H
#define __LOGGING_H

#include <stdbool.h>
#include <stdint.h>

#include <stockfw.h>

extern bool gb_temporary_osd;
extern char osd_message[MAXPATH];
extern uint32_t g_osd_time;

int show_osd_message(const char *message);
void frontend_log_cb(enum retro_log_level level, const char *tag, const char *fmt, ...);
void core_log_cb(enum retro_log_level level, const char *fmt, ...);

#endif //__LOGGING_H
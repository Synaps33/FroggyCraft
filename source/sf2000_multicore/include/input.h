#ifndef __INPUT_H
#define __INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef bool (*hotkey_action_t)(void);

typedef struct {
    uint16_t mask;
    hotkey_action_t action;
} hotkey_entry_t;

extern int slot_state;
extern void frontend_check_hotkeys(void);

#endif //__INPUT_H
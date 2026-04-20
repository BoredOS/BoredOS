#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    KEYMAP_QWERTY = 0,
    KEYMAP_AZERTY = 1,
} keymap_id_t;

void keymap_init(void);
void keymap_set_current(keymap_id_t id);
keymap_id_t keymap_get_current(void);

char keymap_translate(uint8_t scancode, bool shift, bool caps);

#endif

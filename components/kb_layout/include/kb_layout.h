#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef enum {
    KB_LAYOUT_US = 0,
    KB_LAYOUT_UK,
    KB_LAYOUT_DE,
    KB_LAYOUT_FR,
    KB_LAYOUT_COUNT,
} kb_layout_id_t;

/* Returns the UTF-8 string for a given HID keycode + modifier.
 * The returned pointer is valid until the next call.
 * Returns NULL if the keycode does not produce a character. */
const char *kb_layout_translate(uint8_t keycode, uint8_t modifier);

/* Set the active keyboard layout */
void kb_layout_set(kb_layout_id_t layout);

/* Get the active keyboard layout */
kb_layout_id_t kb_layout_get(void);

/* Get the display name for a layout (e.g. "US", "UK", "DE", "FR") */
const char *kb_layout_name(kb_layout_id_t layout);

/* Cycle to the next layout and return its id */
kb_layout_id_t kb_layout_next(void);

#ifdef __cplusplus
}
#endif

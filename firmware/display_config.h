#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H
#include "core.h"
#include "layout.h"
/* Loader uses bytes 0..19 of the metadata page. Never erase this journal. */
#define CONFIG_BASE (META_BASE + 32u)
#define CONFIG_RECORD_BYTES 16u
#define CONFIG_SLOTS ((FLASH_END - CONFIG_BASE) / CONFIG_RECORD_BYTES)
#define DISPLAY_DEFAULT 41u
#define BUTTON_COUNT 5u
#define BUTTON_RADIUS 32u
#define BUTTON_GAP 32u
#define BUTTON_RELEASE_MAX 32u
/* Persistent order is LEFT, PLUS, MINUS, RIGHT, OK. Never infer pin roles. */
enum { KEY_UNKNOWN, KEY_RELEASED, KEY_RIGHT, KEY_MINUS, KEY_OK, KEY_PLUS, KEY_LEFT };
typedef struct {
    uint8_t (*read)(void *context, uint32_t address);
    bool (*program)(void *context, uint32_t address, uint16_t value);
    void *context;
} ConfigIo;
typedef struct {
    ConfigIo io;
    uint32_t index, slot, cursor, candidate, write_slots;
    char label, candidate_label;
    uint16_t buttons[BUTTON_COUNT], candidate_buttons[BUTTON_COUNT];
    uint8_t record[2u*CONFIG_RECORD_BYTES];
    bool writing, fault, calibrated;
} DisplayConfig;
bool config_buttons_valid(const uint16_t values[BUTTON_COUNT]);
uint32_t config_button_code(const DisplayConfig *c, uint32_t raw);
void config_init(DisplayConfig *c, ConfigIo io);
uint32_t config_remaining(const DisplayConfig *c);
/* No-op for already selected values. False means invalid/full/busy/unavailable. */
bool config_save(DisplayConfig *c, uint32_t index);
bool config_save_label(DisplayConfig *c, char label);
bool config_save_buttons(DisplayConfig *c, const uint16_t values[BUTTON_COUNT]);
/* At most one halfword per call; commit last. No erase callback exists. */
void config_poll(DisplayConfig *c);
void config_cancel(DisplayConfig *c);
#endif

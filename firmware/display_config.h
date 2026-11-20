#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H
#include "core.h"
#include "layout.h"
/* Loader uses bytes 0..19 of the metadata page. Never erase this journal. */
#define CONFIG_BASE (META_BASE + 32u)
#define CONFIG_RECORD_BYTES 16u
#define CONFIG_SLOTS ((FLASH_END - CONFIG_BASE) / CONFIG_RECORD_BYTES)
#define DISPLAY_DEFAULT 41u
typedef struct {
    uint8_t (*read)(void *context, uint32_t address);
    bool (*program)(void *context, uint32_t address, uint16_t value);
    void *context;
} ConfigIo;
typedef struct {
    ConfigIo io;
    uint32_t index, slot, cursor, candidate;
    uint8_t record[CONFIG_RECORD_BYTES];
    bool writing, fault;
} DisplayConfig;
void config_init(DisplayConfig *c, ConfigIo io);
uint32_t config_remaining(const DisplayConfig *c);
/* No-op for an already selected value. False means full, busy or unavailable. */
bool config_save(DisplayConfig *c, uint32_t index);
/* At most one halfword per call; commit last. No erase callback exists. */
void config_poll(DisplayConfig *c);
void config_cancel(DisplayConfig *c);
#endif

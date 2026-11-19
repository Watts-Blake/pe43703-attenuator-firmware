#ifndef BOOT_CORE_H
#define BOOT_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "layout.h"
typedef struct {
    void *context;
    uint8_t (*read)(void *, uint32_t);
    bool (*erase)(void *, uint32_t);
    bool (*program)(void *, uint32_t, uint16_t);
} Flash;
typedef struct {
    Flash flash;
    uint32_t size, crc, received;
    size_t used;
    bool active, discard, run;
    char line[160];
} Update;
uint32_t bl_crc(Flash flash, uint32_t address, uint32_t size);
bool bl_valid(Flash flash);
void bl_init(Update *update, Flash flash);
size_t bl_feed(Update *update, char byte, bool rx_error, char out[64]);
#endif

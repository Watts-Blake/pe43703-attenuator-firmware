#ifndef DISPLAY_ID_H
#define DISPLAY_ID_H
#include "display_config.h"
#include "screen.h"
#define DISPLAY_IDLE_MS 120000u
#define DISPLAY_SESSION_MS 600000u
typedef struct {
    DisplayConfig config;
    uint32_t trial, started, touched, result;
    bool active, sweep, saving;
} DisplayId;
void display_id_init(DisplayId *d, Screen *s, ConfigIo io);
void display_id_cancel(DisplayId *d, Screen *s);
void display_id_expire(DisplayId *d, Screen *s, uint32_t now_ms);
void display_id_poll(DisplayId *d, Screen *s, uint32_t now_ms);
void display_id_next(DisplayId *d, Screen *s, uint32_t now_ms);
/* Returns zero only for non-display commands. No unsolicited UART output. */
size_t display_id_command(DisplayId *d, Screen *s, const char *command,
                          uint32_t now_ms, bool busy, char out[16]);
#endif

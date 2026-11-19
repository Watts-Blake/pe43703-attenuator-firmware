#ifndef OLED_H
#define OLED_H
#include "adc.h"
#define OLED_MASK (15u << 4)
#define OLED_STEPS 96u
#define OLED_LIMIT_MS 210000u
typedef struct {
    void (*begin)(void *context, uint32_t idle);
    void (*pins)(void *context, uint32_t levels);
    void (*end)(void *context);
    void (*delay_us)(void *context, uint32_t us);
    void *context;
} OledBus;
typedef struct {
    OledBus bus;
    uint32_t index, phase, cursor, start_ms, step_ms, clk, si, dc, control;
    bool active, configured;
} Oled;
void oled_init(Oled *p, OledBus bus);
bool oled_mapping(uint32_t index, uint8_t pins[4]); /* SCK,MOSI,DC,CS/RES */
bool oled_start(Oled *p, uint32_t now_ms);
void oled_cancel(Oled *p);
/* One byte/control transition per call; never waits for a hold deadline.
   can_report requires16 free TX bytes. Deadline cleanup runs even when false. */
size_t oled_poll(Oled *p, uint32_t now_ms, char out[16], bool can_report);
size_t probe_console_feed(Line *line, Controller *c, const AdcIo *adc, Oled *p,
                          uint32_t now_ms, char ch, bool rx_error, char out[16]);
#endif

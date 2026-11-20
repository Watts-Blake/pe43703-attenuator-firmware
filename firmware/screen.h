#ifndef SCREEN_H
#define SCREEN_H
#include "oled.h"
typedef struct {
    Oled wire;
    uint32_t phase, cursor, test_start, hold_start, index;
    char lines[8][22], large[6];
    bool dirty, testing, holding, done, identifying;
} Screen;
void screen_init(Screen *s, OledBus bus);
void screen_show(Screen *s, uint32_t applied, uint32_t pending, uint32_t step,
                 uint32_t source, bool locked);
void screen_suspend(Screen *s);
void screen_test(Screen *s, uint32_t now_ms);
void screen_stop_test(Screen *s);
void screen_config(Screen *s, uint32_t index);
void screen_identify(Screen *s, uint32_t index);
/* At most one 25us byte or one20us reset transition. No framebuffer. */
void screen_poll(Screen *s, uint32_t now_ms);
uint8_t screen_image(const Screen *s, uint32_t x, uint32_t page);
#endif

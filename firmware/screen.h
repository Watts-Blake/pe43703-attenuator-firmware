#ifndef SCREEN_H
#define SCREEN_H
#include "oled.h"
/* Applied knowledge comes from completed controller transactions, never requests. */
typedef struct {
    uint32_t applied, pending, step, source;
    bool known, error, locked, feedback;
    char label;
} ScreenState;
enum { SCREEN_CALM, SCREEN_PENDING, SCREEN_LOCKED, SCREEN_ERROR };
typedef struct {
    Oled wire;
    uint32_t phase, cursor, test_start, hold_start, index;
    char lines[8][22], large[8];
    uint32_t message, step_index;
    bool locked, known;
    char label;
    bool dirty, testing, holding, done, identifying, calibrating;
} Screen;
void screen_init(Screen *s, OledBus bus);
void screen_show(Screen *s, const ScreenState *state);
void screen_suspend(Screen *s);
void screen_test(Screen *s, uint32_t now_ms);
void screen_stop_test(Screen *s);
void screen_config(Screen *s, uint32_t index);
void screen_identify(Screen *s, uint32_t index);
void screen_calibration(Screen *s, const char *prompt, char key, uint32_t raw,
                        bool captured, const char *footer);
/* At most one 25us byte or one20us reset transition. No framebuffer. */
void screen_poll(Screen *s, uint32_t now_ms);
uint8_t screen_image(const Screen *s, uint32_t x, uint32_t page);
#endif

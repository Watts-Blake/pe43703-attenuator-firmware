#ifndef BUTTON_CAL_H
#define BUTTON_CAL_H
#include "display_config.h"
#include "screen.h"
#define BUTTON_IDLE_MS 120000u
#define BUTTON_SESSION_MS 600000u
#define BUTTON_CAPTURE_MS 3000u
#define BUTTON_SAMPLES 31u
enum { CAL_RELEASE, CAL_HOLD, CAL_CAPTURE, CAL_VALUE, CAL_CONFIRM, CAL_HELD, CAL_SAVING };
enum { CAL_IDLE, CAL_SAVED, CAL_TIMEOUT, CAL_NOISE, CAL_BANDS, CAL_SAMPLE, CAL_SAVE_ERROR, CAL_HOLD_ERROR };
typedef struct {
    uint16_t values[BUTTON_COUNT], samples[BUTTON_SAMPLES];
    uint32_t phase, result, index, count, started, touched, phase_at, last_sample, stable_at;
    bool active, sampled, confirming;
} ButtonCal;
void button_cal_init(ButtonCal *b);
void button_cal_cancel(ButtonCal *b, DisplayConfig *c, Screen *s);
void button_cal_error(ButtonCal *b, DisplayConfig *c, Screen *s, uint32_t result);
void button_cal_expire(ButtonCal *b, DisplayConfig *c, Screen *s, uint32_t now);
void button_cal_poll(ButtonCal *b, DisplayConfig *c, Screen *s, uint32_t now);
void button_cal_sample(ButtonCal *b, DisplayConfig *c, Screen *s, uint32_t raw, uint32_t now);
size_t button_cal_command(ButtonCal *b, DisplayConfig *c, Screen *s, const char *command,
                          uint32_t now, bool busy, char out[16]);
#endif

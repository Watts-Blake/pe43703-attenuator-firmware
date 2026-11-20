#ifndef UI_H
#define UI_H
#include "display_id.h"
#include "button_cal.h"
#define PROFILE_LEASE_MS 10000u
typedef struct {
    Screen screen;
    DisplayId identification;
    ButtonCal calibration;
    AdcAsync sampler;
    uint32_t pending, step_index, seen, token, lease_at, sample_at_us;
    uint32_t samples[5], used, next, candidate, candidate_at, stable, last_sample;
    bool locked, armed, dirty;
} Ui;
void ui_init(Ui *u, Controller *c, OledBus bus);
void ui_configure(Ui *u, ConfigIo io);
uint32_t button_code(uint32_t raw);
void ui_button_sample(Ui *u, Controller *c, uint32_t raw, uint32_t now_ms);
void ui_expire(Ui *u, Controller *c, uint32_t now_ms);
void ui_sync(Ui *u, Controller *c);
size_t ui_console_feed(Ui *u, Line *line, Controller *c, const AdcIo *adc,
                       Oled *probe, uint32_t now_ms, char ch, bool rx_error, char out[16]);
size_t ui_poll(Ui *u, Controller *c, const AdcIo *adc, Oled *probe,
               uint32_t now_ms, uint32_t now_us, char out[16], bool can_report);
#endif

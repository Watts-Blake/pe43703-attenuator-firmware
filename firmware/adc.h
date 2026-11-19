#ifndef ADC_H
#define ADC_H
#include "core.h"
/* ADC1 is exclusively owned here and disabled outside explicit requests.
   Callbacks permit the actual register sequence to run in offline tests. */
typedef struct {
    uint32_t (*read)(void *context, uint32_t address);
    void (*write)(void *context, uint32_t address, uint32_t value);
    void (*delay_us)(void *context, uint32_t us);
    void *context;
} AdcIo;
bool adc_sample(const AdcIo *io, uint32_t *sample);
size_t console_feed(Line *line, Controller *c, const AdcIo *io,
                    char ch, bool rx_error, char out[16]);
#endif

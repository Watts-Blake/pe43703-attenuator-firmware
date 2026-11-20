#include "adc.h"
#define RCC_CFGR UINT32_C(0x40021004)
#define RCC_APB2RSTR UINT32_C(0x4002100c)
#define RCC_APB2ENR UINT32_C(0x40021018)
#define GPIOA_CRL UINT32_C(0x40010800)
#define ADC_SR UINT32_C(0x40012400)
#define ADC_CR2 UINT32_C(0x40012408)
#define ADC_SMPR2 UINT32_C(0x40012410)
#define ADC_SQR3 UINT32_C(0x40012434)
#define ADC_DR UINT32_C(0x4001244c)
#define ADC1_BIT (1u << 9)
#define ADON 1u
#define CAL (1u << 2)
#define RSTCAL (1u << 3)
#define EOC (1u << 1)
static uint32_t read(const AdcIo *io, uint32_t address) {
    return io->read(io->context, address);
}
static void write(const AdcIo *io, uint32_t address, uint32_t value) {
    io->write(io->context, address, value);
}
static bool wait(const AdcIo *io, uint32_t address, uint32_t mask, uint32_t wanted) {
    for (uint32_t i = 0; i < 100; ++i) {
        if ((read(io, address) & mask) == wanted) return true;
        io->delay_us(io->context, 1);
    }
    return false;
}
void adc_cancel(AdcAsync *a, const AdcIo *io) {
    if (!a->phase) return;
    write(io,ADC_CR2,0);
    write(io,RCC_APB2RSTR,a->resets|ADC1_BIT); write(io,RCC_APB2RSTR,a->resets);
    /* OLED mode changes may occur between sampler phases; restore PA1 only. */
    write(io,GPIOA_CRL,(read(io,GPIOA_CRL)&~0xf0u)|a->pa1);
    write(io,RCC_APB2ENR,a->enables); a->phase=0;
}
bool adc_begin(AdcAsync *a, const AdcIo *io, uint32_t now_us) {
    if (a->phase) return false;
    uint32_t en=read(io,RCC_APB2ENR), reset=read(io,RCC_APB2RSTR);
    if ((en&ADC1_BIT) || !(en&(1u<<2)) || (reset&ADC1_BIT) ||
        (read(io,RCC_CFGR)&(3u<<14))!=(2u<<14)) return false;
    a->enables=en; a->resets=reset; a->pa1=read(io,GPIOA_CRL)&0xf0u;
    write(io,RCC_APB2ENR,en|ADC1_BIT); (void)read(io,RCC_APB2ENR);
    write(io,RCC_APB2RSTR,reset|ADC1_BIT); write(io,RCC_APB2RSTR,reset);
    write(io,GPIOA_CRL,read(io,GPIOA_CRL)&~0xf0u);
    write(io,ADC_SMPR2,7u<<3); write(io,ADC_SQR3,1); write(io,ADC_CR2,ADON);
    a->phase=1; a->since_us=now_us; return true;
}
int adc_poll(AdcAsync *a, const AdcIo *io, uint32_t now_us, uint32_t *sample) {
    uint32_t elapsed=now_us-a->since_us;
    if (!a->phase) return 0;
    if (elapsed>=100u) { adc_cancel(a,io); return -1; }
    if (a->phase==1) {
        if (elapsed<2u) return 0;
        write(io,ADC_CR2,ADON|RSTCAL); a->phase=2;
    } else if (a->phase==2) {
        if (read(io,ADC_CR2)&RSTCAL) return 0;
        write(io,ADC_CR2,ADON|CAL); a->phase=3;
    } else if (a->phase==3) {
        if (read(io,ADC_CR2)&CAL) return 0;
        (void)read(io,ADC_DR); write(io,ADC_SR,0); write(io,ADC_CR2,ADON); a->phase=4;
    } else {
        if (!(read(io,ADC_SR)&EOC)) return 0;
        *sample=read(io,ADC_DR)&0xfffu; adc_cancel(a,io); return 1;
    }
    a->since_us=now_us; return 0;
}
bool adc_sample(const AdcIo *io, uint32_t *sample) {
    /* App clock contract: PCLK2=72MHz, ADCPRE=/6 gives12MHz (max14MHz).
       Refuse unexpected ownership/clock state without modifying anything. */
    uint32_t enables = read(io, RCC_APB2ENR), resets = read(io, RCC_APB2RSTR);
    if ((enables & ADC1_BIT) || !(enables & (1u << 2)) || (resets & ADC1_BIT) ||
        (read(io, RCC_CFGR) & (3u << 14)) != (2u << 14)) return false;
    uint32_t gpio = read(io, GPIOA_CRL);
    write(io, RCC_APB2ENR, enables | ADC1_BIT);
    (void)read(io, RCC_APB2ENR); /* peripheral clock enable readback delay */
    write(io, RCC_APB2RSTR, resets | ADC1_BIT);
    write(io, RCC_APB2RSTR, resets);
    write(io, GPIOA_CRL, gpio & ~0xf0u); /* PA1 analog, no pull/output */
    /* Reset gives CR1=0 (no scan/interrupts), SQR1=0 (one conversion),
       SQR2=0, CR2=0 (no continuous/DMA/external trigger/internal channels). */
    write(io, ADC_SMPR2, 7u << 3); /* channel1,239.5-cycle acquisition */
    write(io, ADC_SQR3, 1);
    write(io, ADC_CR2, ADON);
    io->delay_us(io->context, 2); /* tSTAB and >=2 ADC clocks before calibration */
    bool ok = false;
    write(io, ADC_CR2, ADON | RSTCAL);
    if (!wait(io, ADC_CR2, RSTCAL, 0)) goto done;
    write(io, ADC_CR2, ADON | CAL);
    if (!wait(io, ADC_CR2, CAL, 0)) goto done;
    (void)read(io, ADC_DR); /* calibration code is not a conversion result */
    write(io, ADC_SR, 0);
    write(io, ADC_CR2, ADON); /* same ADON=1, no other changed bits: single start */
    if (!wait(io, ADC_SR, EOC, EOC)) goto done;
    *sample = read(io, ADC_DR) & 0xfffu;
    ok = true;
done:
    write(io, ADC_CR2, 0); /* power down even on timeout */
    write(io, RCC_APB2RSTR, resets | ADC1_BIT);
    write(io, RCC_APB2RSTR, resets);
    write(io, GPIOA_CRL, gpio);
    write(io, RCC_APB2ENR, enables);
    return ok;
}
size_t console_feed(Line *line, Controller *c, const AdcIo *io,
                    char ch, bool rx_error, char out[16]) {
    size_t n = line_feed(line, c, ch, rx_error, out);
    if (!line->adc) return n;
    line->adc = false;
    uint32_t sample;
    if (!adc_sample(io, &sample)) {
        const char error[] = "err adc\r\n";
        for (size_t i = 0; i < sizeof(error); ++i) out[i] = error[i];
        return sizeof(error) - 1;
    }
    out[0]='a'; out[1]='d'; out[2]='c'; out[3]=' ';
    for (uint32_t i = 0, divisor = 1000; i < 4; ++i, divisor /= 10)
        out[4+i] = (char)('0' + (sample / divisor) % 10u);
    out[8]='\r'; out[9]='\n'; out[10]=0;
    return 10;
}

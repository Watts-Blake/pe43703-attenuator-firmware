/* Production UI/config/serial parser behind a PTY. No physical device access. */
#include "ui.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    Ui ui; Controller c; Oled probe; Line line; uint32_t now;
    uint32_t raw, enables, gpio;
    bool automatic, future;
    uint8_t flash[1024];
} Model;
static void quiet(void *ctx,uint32_t v) { (void)ctx;(void)v; }
static void end(void *ctx) { (void)ctx; }
static uint32_t read_adc(void *ctx,uint32_t a) {
    Model *m=ctx;
    if(a==0x40021004u)return 2u<<14;
    if(a==0x40021018u)return m->enables;
    if(a==0x40010800u)return m->gpio;
    if(a==0x40012400u)return 2; /* EOC */
    if(a==0x4001244cu)return m->raw;
    return 0; /* reset/calibration flags self-clear */
}
static void write_adc(void *ctx,uint32_t a,uint32_t v) {
    Model *m=ctx;
    if(a==0x40021018u)m->enables=v;
    if(a==0x40010800u)m->gpio=v;
}
static uint8_t read_flash(void *ctx,uint32_t a) {
    Model *m=ctx;assert(a>=CONFIG_BASE && a<FLASH_END);return m->flash[a-META_BASE];
}
static bool program(void *ctx,uint32_t a,uint16_t v) {
    Model *m=ctx;assert(a>=CONFIG_BASE && a<=FLASH_END-2 && !(a&1u));
    assert(m->flash[a-META_BASE]==255 && m->flash[a-META_BASE+1u]==255);
    m->flash[a-META_BASE]=(uint8_t)v;m->flash[a-META_BASE+1u]=(uint8_t)(v>>8);return true;
}
void *model_new(void) {
    Model *m=calloc(1,sizeof(*m));assert(m);memset(m->flash,255,sizeof(m->flash));
    m->enables=4; m->gpio=0x44444444u;
    Bus rf={quiet,quiet,0};OledBus bus={quiet,quiet,end,quiet,0};
    controller_boot(&m->c,rf);ui_init(&m->ui,&m->c,bus);oled_init(&m->probe,bus);
    ConfigIo io={read_flash,program,m};ui_configure(&m->ui,io);return m;
}
void model_free(void *ctx) { free(ctx); }
void model_hold(void *ctx,uint32_t raw,uint32_t duration) {
    Model *m=ctx;m->raw=raw;AdcIo adc={read_adc,write_adc,quiet,m};char out[16];
    for(uint32_t i=0;i<duration;++i) {
        ++m->now;
        for(uint32_t us=0;us<1000;us+=20)
            (void)ui_poll(&m->ui,&m->c,&adc,&m->probe,m->now,m->now*1000u+us,out,false);
    }
}
void model_automatic(void *ctx) { ((Model *)ctx)->automatic=true; }
void model_future(void *ctx) { ((Model *)ctx)->future=true; }
void model_reboot(void *ctx) {
    Model *m=ctx;screen_suspend(&m->ui.screen);
    Bus rf={quiet,quiet,0};OledBus bus={quiet,quiet,end,quiet,0};
    controller_boot(&m->c,rf);ui_init(&m->ui,&m->c,bus);oled_init(&m->probe,bus);
    memset(&m->line,0,sizeof(m->line));ConfigIo io={read_flash,program,m};ui_configure(&m->ui,io);
}
uint32_t model_pending(void *ctx) { return ((Model *)ctx)->ui.pending; }
void model_command(void *ctx,const char *s,char *out) {
    Model *m=ctx;AdcIo adc={read_adc,write_adc,quiet,m};out[0]=0;
    bool future_query=m->future && !strcmp(s,"buttons\n");
    if(m->automatic && !strcmp(s,"buttons status\n") && m->ui.calibration.active) {
        static const uint32_t values[BUTTON_COUNT]={3900,2500,1700,500,1100};
        ButtonCal *b=&m->ui.calibration;
        if(b->phase==CAL_RELEASE || b->phase==CAL_VALUE)model_hold(m,0,120);
        else if(b->phase==CAL_HOLD || b->phase==CAL_CAPTURE)model_hold(m,values[b->index],350);
    }
    while(*s)(void)ui_console_feed(&m->ui,&m->line,&m->c,&adc,&m->probe,m->now,*s++,false,out);
    char ignored[16];
    /* Finish bounded frames/saves with the automatic sample clock paused. */
    m->ui.sample_at_us=m->now*1000u;
    for(uint32_t i=0;i<2500;++i)(void)ui_poll(&m->ui,&m->c,&adc,&m->probe,m->now,m->now*1000u,ignored,true);
    if(future_query)strcpy(out,"buttons 2\r\n");
    ++m->now;
}
void model_legacy_command(void *ctx,const char *s,char *out) {
    Model *m=ctx;out[0]=0;
    while(*s)(void)line_feed(&m->line,&m->c,*s++,false,out);
}
uint32_t model_applied(void *ctx) { return ((Model *)ctx)->c.qdb; }

/* Scheduling boundaries around real ADC/UI confirmation, before journal poll. */
void model_captured(void *ctx) {
    Model *m=ctx;
    assert(m->ui.calibration.phase==CAL_HOLD);
    static const uint32_t values[5]={3900,2500,1700,500,1100};
    for(uint32_t i=0;i<5;++i) {
        model_hold(m,values[i],350);model_hold(m,0,120);
    }
    assert(m->ui.calibration.phase==CAL_CONFIRM);
}
void model_confirm(void *ctx) {
    Model *m=ctx;
    assert(m->ui.calibration.phase==CAL_CONFIRM);
    for(uint32_t i=0;i<101;++i) {m->now+=10;ui_button_sample(&m->ui,&m->c,1100,m->now);}
    for(uint32_t i=0;i<11;++i) {m->now+=10;ui_button_sample(&m->ui,&m->c,0,m->now);}
    assert(m->ui.calibration.phase==CAL_SAVING);
    assert(m->ui.identification.config.writing && !m->ui.identification.config.calibrated);
}

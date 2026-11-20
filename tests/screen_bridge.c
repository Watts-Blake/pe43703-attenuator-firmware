/* Host-only access to production rendering and UI transitions. No raster code. */
#include "ui.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    Ui ui; Controller c; Oled probe; Line line; uint32_t now;
    uint8_t flash[1024];
} Model;
static void quiet(void *ctx,uint32_t v) { (void)ctx;(void)v; }
static void end(void *ctx) { (void)ctx; }
static uint32_t read_adc(void *ctx,uint32_t a) { (void)ctx;(void)a;return 0; }
static void write_adc(void *ctx,uint32_t a,uint32_t v) { (void)ctx;(void)a;(void)v;assert(false); }
static uint8_t read_flash(void *ctx,uint32_t a) {
    Model *m=ctx;assert(a>=CONFIG_BASE && a<FLASH_END);return m->flash[a-META_BASE];
}
static bool program(void *ctx,uint32_t a,uint16_t v) {
    Model *m=ctx;assert(a>=CONFIG_BASE && a<=FLASH_END-2u && !(a&1u));
    assert(read_flash(m,a)==255 && read_flash(m,a+1u)==255);
    m->flash[a-META_BASE]=(uint8_t)v;m->flash[a-META_BASE+1u]=(uint8_t)(v>>8);return true;
}
void *screen_model_new(uint32_t boot) {
    Model *m=calloc(1,sizeof(*m));assert(m);memset(m->flash,255,sizeof(m->flash));
    Bus rf={quiet,quiet,0};OledBus bus={quiet,quiet,end,quiet,0};
    if(boot) controller_boot(&m->c,rf);
    else { m->c.bus=rf;m->c.qdb=73;m->c.source=SOURCE_BOOT; } /* poison unknown numeric field */
    ui_init(&m->ui,&m->c,bus);oled_init(&m->probe,bus);
    ConfigIo io={read_flash,program,m};ui_configure(&m->ui,io);return m;
}
void screen_model_free(void *ctx) { free(ctx); }
void screen_model_poll(void *ctx) {
    Model *m=ctx;AdcIo adc={read_adc,write_adc,quiet,0};char out[16];
    /* Pause automatic ADC clock; buttons are fed through production debounce. */
    (void)ui_poll(&m->ui,&m->c,&adc,&m->probe,m->now,0,out,false);
}
void screen_model_command(void *ctx,const char *input,char out[16]) {
    Model *m=ctx;AdcIo adc={read_adc,write_adc,quiet,0};out[0]=0;
    while(*input)(void)ui_console_feed(&m->ui,&m->line,&m->c,&adc,&m->probe,m->now,*input++,false,out);
}
void screen_model_hold(void *ctx,uint32_t raw,uint32_t ms) {
    Model *m=ctx;
    for(uint32_t i=0;i<ms;i+=5) { m->now+=5;ui_button_sample(&m->ui,&m->c,raw,m->now); }
}
void screen_model_advance(void *ctx,uint32_t ms) {
    Model *m=ctx;m->now+=ms;screen_model_poll(m);
}
void screen_model_reject(void *ctx) {
    Model *m=ctx;Mailbox box;mailbox_init(&box);
    box.seq=2;box.command_magic=MB_MAGIC;box.command_version=MB_VERSION;
    box.op=OP_SET;box.arg=128;box.seq_inverse=~box.seq;
    box.check=MB_CHECK^box.seq^box.command_magic^box.command_version^box.op^box.arg;
    assert(mailbox_poll(&box,&m->c) && box.status==ST_RANGE && box.ack==2);
}
void screen_model_frame(void *ctx,uint8_t out[1024]) {
    Model *m=ctx;screen_model_poll(m);
    for(uint32_t page=0;page<8;++page)for(uint32_t x=0;x<128;++x)
        out[page*128u+x]=screen_image(&m->ui.screen,x,page);
}
void screen_direct_frame(uint32_t applied,uint32_t pending,uint32_t step,uint32_t source,
                         uint32_t flags,char label,uint8_t out[1024]) {
    Screen s;OledBus bus={quiet,quiet,end,quiet,0};screen_init(&s,bus);
    ScreenState v={applied,pending,step,source,(flags&1u)!=0,(flags&2u)!=0,
                   (flags&4u)!=0,(flags&8u)!=0,label};screen_show(&s,&v);
    for(uint32_t page=0;page<8;++page)for(uint32_t x=0;x<128;++x)
        out[page*128u+x]=screen_image(&s,x,page);
    assert(!screen_image(&s,128,0) && !screen_image(&s,0,8));
}

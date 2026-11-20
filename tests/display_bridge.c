/* Production UI/config/serial parser behind a PTY. No physical device access. */
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
    Model *m=ctx;assert(a>=CONFIG_BASE && a<=FLASH_END-2 && !(a&1u));
    assert(m->flash[a-META_BASE]==255 && m->flash[a-META_BASE+1u]==255);
    m->flash[a-META_BASE]=(uint8_t)v;m->flash[a-META_BASE+1u]=(uint8_t)(v>>8);return true;
}
void *model_new(void) {
    Model *m=calloc(1,sizeof(*m));assert(m);memset(m->flash,255,sizeof(m->flash));
    Bus rf={quiet,quiet,0};OledBus bus={quiet,quiet,end,quiet,0};
    controller_boot(&m->c,rf);ui_init(&m->ui,&m->c,bus);oled_init(&m->probe,bus);
    ConfigIo io={read_flash,program,m};ui_configure(&m->ui,io);return m;
}
void model_free(void *ctx) { free(ctx); }
void model_command(void *ctx,const char *s,char *out) {
    Model *m=ctx;AdcIo adc={read_adc,write_adc,quiet,0};out[0]=0;
    while(*s)(void)ui_console_feed(&m->ui,&m->line,&m->c,&adc,&m->probe,m->now,*s++,false,out);
    char ignored[16];
    for(uint32_t i=0;i<2500;++i)(void)ui_poll(&m->ui,&m->c,&adc,&m->probe,m->now,0,ignored,true);
    ++m->now;
}
void model_legacy_command(void *ctx,const char *s,char *out) {
    Model *m=ctx;out[0]=0;
    while(*s)(void)line_feed(&m->line,&m->c,*s++,false,out);
}
uint32_t model_applied(void *ctx) { return ((Model *)ctx)->c.qdb; }

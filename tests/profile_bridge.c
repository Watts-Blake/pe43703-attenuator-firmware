/* Host-only bridge: exercise the production console and local button path. */
#include "ui.h"
#include <assert.h>
#include <stdlib.h>
typedef struct { Ui ui; Controller c; Oled probe; Line line; uint32_t now; } Model;
static void quiet(void *ctx, uint32_t value) { (void)ctx; (void)value; }
static uint32_t read_adc(void *ctx, uint32_t address) { (void)ctx; (void)address; assert(false); return 0; }
static void write_adc(void *ctx, uint32_t address, uint32_t value) { (void)ctx; (void)address; (void)value; assert(false); }
void *model_new(void) {
    Model *m=calloc(1,sizeof(*m)); assert(m);
    Bus bus={quiet,quiet,0}; OledBus display={quiet,quiet,0,quiet,0};
    controller_boot(&m->c,bus); ui_init(&m->ui,&m->c,display); oled_init(&m->probe,display);
    return m;
}
void model_free(void *ctx) { free(ctx); }
void model_command(void *ctx, const char *s, char *out) {
    Model *m=ctx; AdcIo adc={read_adc,write_adc,quiet,0}; out[0]=0;
    while (*s) (void)ui_console_feed(&m->ui,&m->line,&m->c,&adc,&m->probe,m->now,*s++,false,out);
}
void model_legacy_command(void *ctx, const char *s, char *out) {
    Model *m=ctx; out[0]=0;
    /* The unchanged core parser is the legacy SET/GET/unknown-command path. */
    while (*s) (void)line_feed(&m->line,&m->c,*s++,false,out);
}
static void hold(Model *m, uint32_t raw) {
    for (uint32_t n=0;n<16;++n) { m->now+=5; ui_button_sample(&m->ui,&m->c,raw,m->now); }
}
void model_pause_local(void *ctx) {
    Model *m=ctx; m->now+=PROFILE_LEASE_MS; ui_expire(&m->ui,&m->c,m->now);
    assert(!m->ui.locked);
    hold(m,0); hold(m,2048); hold(m,0); hold(m,1352);
    assert(m->c.qdb==1 && m->c.source==SOURCE_LOCAL);
}
uint32_t model_applied(void *ctx) { return ((Model *)ctx)->c.qdb; }

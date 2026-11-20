#include "button_cal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t bytes[1024]; uint32_t writes; } Memory;
static uint8_t read_byte(void *ctx,uint32_t a) {
    assert(a>=CONFIG_BASE && a<FLASH_END); return ((Memory *)ctx)->bytes[a-META_BASE];
}
static bool program(void *ctx,uint32_t a,uint16_t v) {
    Memory *m=ctx;assert(a>=CONFIG_BASE && a<=FLASH_END-2u && !(a&1u));
    assert(read_byte(ctx,a)==255 && read_byte(ctx,a+1u)==255);
    m->bytes[a-META_BASE]=(uint8_t)v;m->bytes[a-META_BASE+1u]=(uint8_t)(v>>8);++m->writes;return true;
}
static void quiet(void *ctx,uint32_t v) { (void)ctx;(void)v; }
static void end(void *ctx) { (void)ctx; }
static void setup(ButtonCal *b,DisplayConfig *c,Screen *s,Memory *m) {
    memset(m,0,sizeof(*m));memset(m->bytes,255,sizeof(m->bytes));
    ConfigIo io={read_byte,program,m};config_init(c,io);
    OledBus bus={quiet,quiet,end,quiet,0};screen_init(s,bus);button_cal_init(b);
}
static void command(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t now,const char *op,const char *expected) {
    char out[16];size_t n=button_cal_command(b,c,s,op,now,false,out);
    assert(n==strlen(expected) && !strcmp(out,expected));
}
static void samples(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t *now,uint32_t raw,uint32_t n) {
    for(uint32_t i=0;i<n;++i) { *now+=10;button_cal_sample(b,c,s,raw,*now); }
}
static const uint16_t values[BUTTON_COUNT]={3900,2500,1700,500,1100};
static void capture(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t *now,uint32_t i) {
    assert(b->phase==CAL_HOLD && b->index==i);
    for(uint32_t n=0;n<BUTTON_SAMPLES;++n) {
        *now+=10;uint32_t raw=values[i]+n%3u-1u;button_cal_sample(b,c,s,raw,*now);
    }
    assert(b->active && b->phase==CAL_VALUE && b->values[i]==values[i]);
    assert(s->calibrating && !strcmp(s->lines[2],"CAPTURED"));
    samples(b,c,s,now,values[i],100); /* no repeat or next key without release */
    assert(b->index==i && b->phase==CAL_VALUE);
    samples(b,c,s,now,0,11);
}
static void all(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t *now) {
    command(b,c,s,*now,"buttons start","ok buttons\r\n");
    samples(b,c,s,now,values[0],50);assert(b->phase==CAL_RELEASE);
    samples(b,c,s,now,0,11);
    for(uint32_t i=0;i<BUTTON_COUNT;++i) capture(b,c,s,now,i);
    assert(b->phase==CAL_CONFIRM && b->index==BUTTON_COUNT && b->active);
    assert(!c->calibrated && !c->writing);
}
static void test_complete_and_confirm(void) {
    for(uint32_t local=0;local<2;++local) {
        ButtonCal b;DisplayConfig c;Screen s;Memory m;setup(&b,&c,&s,&m);uint32_t now=UINT32_MAX-500u;
        all(&b,&c,&s,&now);assert(!m.writes);
        command(&b,&c,&s,now,"buttons status","confirm\r\n");
        if(local) {
            samples(&b,&c,&s,&now,values[4],50);samples(&b,&c,&s,&now,0,11);assert(b.phase==CAL_CONFIRM && !c.writing);
            samples(&b,&c,&s,&now,values[3],120);samples(&b,&c,&s,&now,0,11);assert(b.phase==CAL_CONFIRM && !c.writing);
            samples(&b,&c,&s,&now,values[4],101);assert(b.phase==CAL_HELD && !c.writing);
            samples(&b,&c,&s,&now,0,11);
        } else command(&b,&c,&s,now,"buttons save","ok saving\r\n");
        assert(b.phase==CAL_SAVING && c.writing);
        command(&b,&c,&s,now,"buttons save","ok saving\r\n");
        assert(!m.writes && c.writing);
        for(uint32_t i=0;i<16;++i)button_cal_poll(&b,&c,&s,now);
        assert(!b.active && b.result==CAL_SAVED && !s.calibrating && c.calibrated && m.writes==16);
        assert(!memcmp(c.buttons,values,sizeof(values)) && c.index==41 && config_remaining(&c)==60);
        command(&b,&c,&s,now,"buttons stored L","value L 3900\r\n");
        command(&b,&c,&s,now,"buttons value O","value O 1100\r\n");
        command(&b,&c,&s,now,"buttons save","ok saving\r\n");
        command(&b,&c,&s,now,"buttons status","saved\r\n");
        assert(m.writes==16 && !c.writing);
        DisplayConfig reboot;ConfigIo io={read_byte,program,&m};config_init(&reboot,io);
        assert(reboot.calibrated && !memcmp(reboot.buttons,values,sizeof(values)));
    }
}
static void test_cancel_partial_and_errors(void) {
    for(uint32_t stage=0;stage<6;++stage) {
        ButtonCal b;DisplayConfig c;Screen s;Memory m;setup(&b,&c,&s,&m);uint32_t now=0;
        command(&b,&c,&s,now,"buttons start","ok buttons\r\n");samples(&b,&c,&s,&now,0,11);
        for(uint32_t i=0;i<stage && i<BUTTON_COUNT;++i)capture(&b,&c,&s,&now,i);
        command(&b,&c,&s,now,"buttons cancel","ok cancel\r\n");assert(!b.active && !m.writes && !c.calibrated);
        command(&b,&c,&s,now,"buttons save","err buttons\r\n");assert(!c.writing);
    }
    for(uint32_t kind=0;kind<6;++kind) {
        ButtonCal b;DisplayConfig c;Screen s;Memory m;setup(&b,&c,&s,&m);uint32_t now=0;
        command(&b,&c,&s,now,"buttons start","ok buttons\r\n");samples(&b,&c,&s,&now,0,11);
        if(kind==0) { samples(&b,&c,&s,&now,3900,10);samples(&b,&c,&s,&now,0,1);assert(b.result==CAL_HOLD_ERROR); }
        if(kind==1) {
            for(uint32_t i=0;i<31;++i) { now+=10;button_cal_sample(&b,&c,&s,3900+i%2u*25u,now); }
            assert(b.result==CAL_NOISE);
        }
        if(kind==2) { samples(&b,&c,&s,&now,96,31);assert(b.result==CAL_BANDS); }
        if(kind==3) { capture(&b,&c,&s,&now,0);samples(&b,&c,&s,&now,values[0]-96u,31);assert(b.result==CAL_BANDS); }
        if(kind==4) { samples(&b,&c,&s,&now,4096,1);assert(b.result==CAL_SAMPLE); }
        if(kind==5) { samples(&b,&c,&s,&now,3900,1);now+=31;button_cal_sample(&b,&c,&s,3900,now);assert(b.result==CAL_SAMPLE); }
        assert(!b.active && !c.calibrated && !m.writes && !c.writing);
    }
}
static void test_deadlines_and_release(void) {
    ButtonCal b;DisplayConfig c;Screen s;Memory m;setup(&b,&c,&s,&m);uint32_t now=UINT32_MAX-1000u;
    command(&b,&c,&s,now,"buttons start","ok buttons\r\n");
    samples(&b,&c,&s,&now,0,10);assert(b.phase==CAL_RELEASE);
    samples(&b,&c,&s,&now,50,1);samples(&b,&c,&s,&now,0,10);assert(b.phase==CAL_RELEASE);
    samples(&b,&c,&s,&now,0,1);assert(b.phase==CAL_HOLD);
    samples(&b,&c,&s,&now,3900,1);
    button_cal_expire(&b,&c,&s,now+BUTTON_CAPTURE_MS);assert(!b.active && b.result==CAL_HOLD_ERROR);
    command(&b,&c,&s,now,"buttons start","ok buttons\r\n");
    for(uint32_t i=1;i<120;++i)command(&b,&c,&s,now+i*1000u,"buttons status","release L\r\n");
    button_cal_expire(&b,&c,&s,now+BUTTON_IDLE_MS);assert(!b.active && b.result==CAL_TIMEOUT);
    command(&b,&c,&s,now,"buttons start","ok buttons\r\n");
    /* Independent total deadline even when meaningful progress last occurred recently. */
    b.touched=now+BUTTON_SESSION_MS-1u;button_cal_expire(&b,&c,&s,now+BUTTON_SESSION_MS);
    assert(!b.active && b.result==CAL_TIMEOUT && !m.writes);
    setup(&b,&c,&s,&m);now=0;all(&b,&c,&s,&now);
    command(&b,&c,&s,now,"buttons save","ok saving\r\n");
    button_cal_poll(&b,&c,&s,now);button_cal_cancel(&b,&c,&s);
    assert(!c.writing && !c.calibrated && m.writes==1);
    ConfigIo io={read_byte,program,&m};config_init(&c,io);assert(!c.calibrated);
}
int main(void) {
    test_complete_and_confirm();test_cancel_partial_and_errors();test_deadlines_and_release();
    puts("Button calibration: stable median/release, explicit local/host confirmation, noise/bands/cancel and wrap deadlines passed");return 0;
}

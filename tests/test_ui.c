#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint32_t levels, word, bits, bytes, commands, data, begins, ends, us;
    uint8_t ram[8][132];
    uint32_t page, column;
    bool active;
} Display;
static void begin(void *ctx,uint32_t idle) {
    Display *d=ctx; assert(!d->active && !idle); d->active=true; d->levels=idle; ++d->begins;
}
static void pins(void *ctx,uint32_t high) {
    Display *d=ctx; assert(d->active && !(high&~OLED_MASK));
    if ((high&(1u<<6)) && !(d->levels&(1u<<6))) {
        assert(high&(1u<<4));
        d->word=(d->word<<1)|((high&(1u<<7)) ? 1u : 0u);
        if (++d->bits==8) {
            uint8_t value=(uint8_t)d->word;
            if (high&(1u<<5)) {
                ++d->data;
                if (d->page<8 && d->column<132) d->ram[d->page][d->column++]=value;
            } else {
                ++d->commands;
                if (value>=0xb0 && value<=0xb7) d->page=value-0xb0u;
                if (value<=0x0f) d->column=(d->column&0xf0u)|value;
                if (value>=0x10 && value<=0x1f) d->column=(d->column&0x0fu)|((uint32_t)(value&15u)<<4);
            }
            ++d->bytes; d->bits=0; d->word=0;
        }
    }
    d->levels=high;
}
static void end(void *ctx) { Display *d=ctx; assert(d->active); d->active=false; ++d->ends; }
static void delay(void *ctx,uint32_t us) { Display *d=ctx; d->us+=us; }
static OledBus bus(Display *d) { OledBus b={begin,pins,end,delay,d}; return b; }
static void quiet(void *ctx,uint32_t value) { (void)ctx; (void)value; }
static Bus rf(void) { Bus b={quiet,quiet,0}; return b; }
static uint32_t no_read(void *ctx,uint32_t address) { (void)ctx;(void)address;assert(false);return 0; }
static void no_write(void *ctx,uint32_t address,uint32_t value) { (void)ctx;(void)address;(void)value;assert(false); }
static void command(Ui *u,Controller *c,Oled *probe,const char *input,uint32_t now,const char *expected) {
    AdcIo adc={no_read,no_write,quiet,0}; Line line={0}; char out[16]={0}; size_t n=0;
    while (*input) { size_t next=ui_console_feed(u,&line,c,&adc,probe,now,*input++,false,out); if(next) n=next; }
    assert(n==strlen(expected) && !strcmp(out,expected));
}
static void hold(Ui *u,Controller *c,uint32_t raw,uint32_t *now,uint32_t duration) {
    for (uint32_t i=0;i<duration;i+=5) { *now+=5; ui_button_sample(u,c,raw,*now); }
}
static void press(Ui *u,Controller *c,uint32_t raw,uint32_t *now) {
    hold(u,c,0,now,80); hold(u,c,raw,now,80);
}
static void test_buttons(void) {
    Display d={0}; Controller c; controller_boot(&c,rf()); Ui u; ui_init(&u,&c,bus(&d)); uint32_t now=0;
    for (uint32_t raw=0;raw<5000;++raw) {
        uint32_t expected=raw<=32 ? KEY_RELEASED : raw>=780 && raw<=850 ? KEY_RIGHT :
            raw>=970 && raw<=1060 ? KEY_MINUS : raw>=1310 && raw<=1410 ? KEY_OK :
            raw>=1980 && raw<=2110 ? KEY_PLUS : raw>=4050 && raw<=4095 ? KEY_LEFT : KEY_UNKNOWN;
        assert(button_code(raw)==expected);
    }
    hold(&u,&c,2048,&now,100); assert(!u.pending); /* require released startup */
    press(&u,&c,2028,&now); assert(u.pending==1 && c.qdb==0 && c.issued==1);
    hold(&u,&c,2048,&now,2000); assert(u.pending==1); /* no autorepeat */
    hold(&u,&c,1352,&now,100); assert(c.issued==1); /* no key without release */
    press(&u,&c,1092,&now); assert(c.issued==1 && u.pending==1);
    press(&u,&c,1352,&now); assert(c.qdb==1 && c.source==SOURCE_LOCAL && c.issued==2);
    press(&u,&c,809,&now); assert(u.step_index==1);
    press(&u,&c,820,&now); assert(u.step_index==2);
    press(&u,&c,2048,&now); assert(u.pending==21 && c.qdb==1);
    press(&u,&c,4093,&now); assert(u.step_index==1);
    press(&u,&c,1016,&now); assert(u.pending==17);
    press(&u,&c,4092,&now); assert(u.step_index==0);
    for (uint32_t i=0;i<140;++i) press(&u,&c,2040,&now);
    assert(u.pending==127 && c.qdb==1);
    for (uint32_t i=0;i<140;++i) press(&u,&c,1021,&now);
    assert(u.pending==0 && c.qdb==1);
    hold(&u,&c,0,&now,80); hold(&u,&c,2048,&now,10); hold(&u,&c,0,&now,80);
    assert(!u.pending); /* median and stable-time reject short press */
    (void)controller_set(&c,80); ui_sync(&u,&c); assert(u.pending==80 && c.source==SOURCE_USB);
    hold(&u,&c,1352,&now,100); assert(c.issued==3); /* held OK cannot overwrite remote */
}
static void test_lease(void) {
    Display d={0}; Controller c; controller_boot(&c,rf()); Ui u; ui_init(&u,&c,bus(&d)); Oled probe; oled_init(&probe,bus(&d));
    uint32_t now=UINT32_MAX-500u;
    command(&u,&c,&probe,"profile\n",now,"profile 2\r\n");
    command(&u,&c,&probe,"profile 1234abcd\n",now,"ok profile\r\n"); assert(u.locked && c.issued==1);
    command(&u,&c,&probe,"profile 11111111\n",now,"err busy\r\n");
    command(&u,&c,&probe,"profile keep 11111111\n",now,"err lock\r\n");
    command(&u,&c,&probe,"profile end 11111111\n",now,"err lock\r\n"); assert(u.locked);
    press(&u,&c,2048,&now); press(&u,&c,1368,&now); assert(!u.pending && c.issued==1);
    command(&u,&c,&probe,"set 22\n",now,"ok 22.00\r\n"); assert(u.locked && u.pending==88);
    command(&u,&c,&probe,"profile keep 1234abcd\n",now,"ok keep\r\n");
    ui_expire(&u,&c,now+9999u); assert(u.locked);
    ui_expire(&u,&c,now+10000u); assert(!u.locked && c.qdb==88);
    now+=10000u;
    command(&u,&c,&probe,"profile keep 1234abcd\n",now,"err lock\r\n");
    hold(&u,&c,1368,&now,100); assert(c.issued==2); /* release required after expiry */
    command(&u,&c,&probe,"set 0\n",now,"ok 0.00\r\n"); assert(!u.locked);
    command(&u,&c,&probe,"profile 00000000\n",now,"err\r\n");
    command(&u,&c,&probe,"profile abcd\n",now,"err\r\n");
    command(&u,&c,&probe,"profile 11111111\n",now,"ok profile\r\n");
    command(&u,&c,&probe,"profile end 11111111\n",now,"ok end\r\n"); assert(!u.locked);
    assert(c.issued==3);
}
static void test_atomic_profile_set(void) {
    Display d={0}; Controller c; controller_boot(&c,rf()); Ui u; ui_init(&u,&c,bus(&d));
    Oled probe; oled_init(&probe,bus(&d)); uint32_t now=0;
    command(&u,&c,&probe,"profile set 1234abcd 20.00\n",now,"err lock\r\n");
    command(&u,&c,&probe,"profile 1234abcd\n",now,"ok profile\r\n");
    command(&u,&c,&probe,"profile set 11111111 20.00\n",now,"err lock\r\n");
    const char *bad[]={"profile set 1234abcd 32\n","profile set 1234abcd 0.1\n",
        "profile set 00000000 1\n","profile set 1234ABCd 1\n","profile set abcd 1\n",
        "profile set 1234abcd\n","profile set 1234abcd 1 extra\n"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) command(&u,&c,&probe,bad[i],now,"err\r\n");
    assert(c.issued==1 && u.locked);
    command(&u,&c,&probe,"oledprobe\n",now,"ok oledprobe\r\n"); assert(probe.active);
    command(&u,&c,&probe,"profile set 11111111 20\n",now,"err lock\r\n"); assert(probe.active);
    command(&u,&c,&probe,"profile set 1234abcd 31.75\n",now,"ok 31.75\r\n");
    assert(c.qdb==127 && c.issued==2 && u.pending==127 && c.source==SOURCE_USB && !probe.active);
    command(&u,&c,&probe,"profile keep 1234abcd\n",1,"ok keep\r\n");
    /* The lease can expire even while a command is arriving byte by byte. */
    Line line={0}; AdcIo adc={no_read,no_write,quiet,0}; char out[16];
    const char *partial="profile set 1234abcd 20.00";
    while (*partial) assert(!ui_console_feed(&u,&line,&c,&adc,&probe,10000,*partial++,false,out));
    assert(ui_console_feed(&u,&line,&c,&adc,&probe,10001,'\n',false,out)==10);
    assert(!strcmp(out,"err lock\r\n") && !u.locked && c.issued==2);
    now=10001;
    press(&u,&c,1020,&now); press(&u,&c,1360,&now);
    assert(c.qdb==126 && c.source==SOURCE_LOCAL && c.issued==3);
    command(&u,&c,&probe,"profile set 1234abcd 20.00\n",now,"err lock\r\n");
    assert(c.qdb==126 && c.issued==3);
    command(&u,&c,&probe,"profile 22222222\n",now,"ok profile\r\n");
    command(&u,&c,&probe,"profile set 1234abcd 20.00\n",now,"err lock\r\n");
    command(&u,&c,&probe,"profile keep 1234abcd\n",now,"err lock\r\n");
    command(&u,&c,&probe,"profile end 1234abcd\n",now,"err lock\r\n");
    assert(c.qdb==126 && c.issued==3 && u.locked);
    command(&u,&c,&probe,"profile set 22222222 0\n",now,"ok 0.00\r\n");
    command(&u,&c,&probe,"profile end 22222222\n",now,"ok end\r\n");
    command(&u,&c,&probe,"profile set 22222222 20\n",now,"err lock\r\n");
    assert(!c.qdb && c.issued==4 && !u.locked);
    command(&u,&c,&probe,"set 1.25\n",now,"ok 1.25\r\n");
    command(&u,&c,&probe,"get\n",now,"1.25\r\n"); assert(c.issued==5);
}
static void frame(Screen *s,Display *d,uint32_t now) {
    for(uint32_t n=0;n<2400;++n) {
        uint32_t before=d->us; screen_poll(s,now); assert(d->us-before<=25);
    }
    assert(s->phase==4 && !s->dirty);
}
static void test_display(void) {
    Display d={0}; Screen s; screen_init(&s,bus(&d));
    screen_show(&s,127,80,4,SOURCE_USB,true); frame(&s,&d,0);
    assert(d.data==2080 && !strcmp(s.large,"31.75") && !strcmp(s.lines[6],"SOURCE USB") && !strcmp(s.lines[7],"USB LOCK"));
    bool nonzero=false;
    for(uint32_t page=0;page<8;++page) for(uint32_t x=0;x<128;++x) {
        assert(d.ram[page][x]==screen_image(&s,x,page)); nonzero|=d.ram[page][x]!=0;
    }
    assert(nonzero);
    screen_show(&s,1,2,20,SOURCE_LOCAL,true); frame(&s,&d,1);
    assert(!strcmp(s.large,"0.25") && !strcmp(s.lines[6],"SOURCE LOCAL") && !strcmp(s.lines[7],"USB LOCK"));
    uint32_t bytes=d.bytes; frame(&s,&d,2); assert(d.bytes==bytes); /* idle has no writes */
    screen_test(&s,UINT32_MAX-100u); frame(&s,&d,UINT32_MAX-100u);
    for(uint32_t page=0;page<8;++page) {
        assert(d.ram[page][0]==255 && d.ram[page][127]==255);
        for(uint32_t x=0;x<128;++x) assert(d.ram[page][x]==screen_image(&s,x,page));
    }
    screen_poll(&s,9898u); assert(s.testing);
    screen_poll(&s,9899u); assert(!s.testing && s.done);
    frame(&s,&d,9901); assert(!strcmp(s.large,"0.25"));
    screen_test(&s,0); screen_poll(&s,12000); assert(!s.testing); /* hard deadline */
    screen_suspend(&s); assert(!d.active && d.ends==1);
    frame(&s,&d,13000); assert(d.begins==2);
}
typedef struct { Controller *c; uint32_t expected, source; bool checking; } Latch;
static void latch_pins(void *ctx,uint32_t value) {
    Latch *l=ctx; (void)value;
    if(l->checking) assert(l->c->qdb==l->expected && l->c->source==l->source);
}
static void test_post_latch(void) {
    Controller c; Latch l={&c,0,SOURCE_BOOT,false}; Bus b={latch_pins,quiet,&l}; controller_boot(&c,b);
    l.checking=true; assert(controller_set_from(&c,100,SOURCE_LOCAL));
    assert(c.qdb==100 && c.source==SOURCE_LOCAL && c.issued==2);
    l.expected=100; l.source=SOURCE_LOCAL; assert(controller_set(&c,0)); assert(!c.qdb && c.source==SOURCE_USB);
}
static uint32_t unavailable_adc(void *ctx,uint32_t address) { (void)ctx;(void)address;return 0; }
typedef struct { uint8_t bytes[1024]; uint32_t writes; bool fail; } ConfigMemory;
static uint8_t config_read(void *ctx,uint32_t a) {
    ConfigMemory *m=ctx; assert(a>=CONFIG_BASE && a<FLASH_END); return m->bytes[a-META_BASE];
}
static bool config_write(void *ctx,uint32_t a,uint16_t v) {
    ConfigMemory *m=ctx;assert(a>=CONFIG_BASE && a<=FLASH_END-2u);++m->writes;
    if(m->fail)return false;
    m->bytes[a-META_BASE]=(uint8_t)v;m->bytes[a-META_BASE+1u]=(uint8_t)(v>>8);return true;
}
static void test_identification(void) {
    Display d={0};Controller c;controller_boot(&c,rf());Ui u;ui_init(&u,&c,bus(&d));
    Oled probe;oled_init(&probe,bus(&d));ConfigMemory m={0};memset(m.bytes,255,sizeof(m.bytes));
    ConfigIo io={config_read,config_write,&m};ui_configure(&u,io);
    AdcIo adc={unavailable_adc,no_write,quiet,0};char out[16];uint32_t now=UINT32_MAX-500u;
    command(&u,&c,&probe,"display\n",now,"display 1\r\n");
    command(&u,&c,&probe,"display get\n",now,"config 41 62\r\n");
    command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
    command(&u,&c,&probe,"display status\n",now,"trial 41 W\r\n");
    frame(&u.screen,&d,now);
    command(&u,&c,&probe,"display status\n",now,"trial 41 R\r\n");
    command(&u,&c,&probe,"display start\n",now,"err busy\r\n");
    assert(u.identification.active && !m.writes);
    command(&u,&c,&probe,"display next\n",now,"ok next\r\n");
    assert(u.screen.index==89);frame(&u.screen,&d,now);
    command(&u,&c,&probe,"display status\n",now,"trial 89 R\r\n");
    command(&u,&c,&probe,"display save 89\n",now,"ok saving\r\n");
    assert(!m.writes); /* dispatch does not write */
    for(uint32_t i=0;i<8;++i) {
        uint32_t before=m.writes;
        (void)ui_poll(&u,&c,&adc,&probe,now,0,out,false);assert(m.writes==before+1u);
    }
    command(&u,&c,&probe,"display status\n",now,"saved 89\r\n");
    command(&u,&c,&probe,"display get\n",now,"config 89 61\r\n");
    assert(!u.identification.active && !u.screen.identifying && u.screen.index==89 && c.issued==1);
    /* Legacy fixed test still reports completion after restoring a nondefault config. */
    command(&u,&c,&probe,"oledtest\n",now,"ok oledtest\r\n");frame(&u.screen,&d,now);
    assert(u.screen.index==41);
    (void)ui_poll(&u,&c,&adc,&probe,now+12000u,0,out,false);
    assert(u.screen.index==89 && u.screen.done);
    assert(ui_poll(&u,&c,&adc,&probe,now+12001u,0,out,true)==15);
    assert(!strcmp(out,"oledtest done\r\n"));
    /* Cancel and malformed/error lines keep the existing configuration. */
    const char *errors[]={"display cancel\n","display finish\n","display try 96\n","set 32\n"};
    const char *answers[]={"ok cancel\r\n","ok finish\r\n","err display\r\n","err\r\n"};
    for(uint32_t i=0;i<4;++i) {
        command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
        command(&u,&c,&probe,errors[i],now,answers[i]);
        assert(!u.identification.active && u.screen.index==89 && m.writes==8 && c.issued==1);
    }
    /* RX error / overlong input abandon the session and require release. */
    for(uint32_t error=0;error<2;++error) {
        command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
        hold(&u,&c,0,&now,80);assert(u.armed);
        Line line={0};const char *bad="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n";
        if(error) (void)ui_console_feed(&u,&line,&c,&adc,&probe,now,'x',true,out);
        else while(*bad)(void)ui_console_feed(&u,&line,&c,&adc,&probe,now,*bad++,false,out);
        assert(!u.identification.active && !u.armed && u.screen.index==89);
    }
    /* Explicit SET and profile acquire cancel; no extra RF transaction. */
    command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
    command(&u,&c,&probe,"set 1\n",now,"ok 1.00\r\n");assert(c.issued==2 && !u.identification.active);
    command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
    command(&u,&c,&probe,"profile 1234abcd\n",now,"ok profile\r\n");assert(!u.identification.active);
    command(&u,&c,&probe,"display start\n",now,"err busy\r\n");assert(u.locked && c.issued==2);
    command(&u,&c,&probe,"profile set 1234abcd 2\n",now,"ok 2.00\r\n");
    command(&u,&c,&probe,"profile end 1234abcd\n",now,"ok end\r\n");
    /* Any released/debounced key advances only; no local pending or SET. */
    command(&u,&c,&probe,"display start\n",now,"ok display\r\n");frame(&u.screen,&d,now);
    uint32_t pending=u.pending,issued=c.issued;
    press(&u,&c,1352,&now);assert(u.identification.trial==89 && c.issued==issued && u.pending==pending);
    frame(&u.screen,&d,now);hold(&u,&c,1352,&now,1000);assert(u.identification.trial==89);
    /* GET/status do not extend the inactivity or total deadlines. */
    ui_expire(&u,&c,now+DISPLAY_IDLE_MS);assert(!u.identification.active && !u.armed && c.issued==issued);
    command(&u,&c,&probe,"display status\n",now+DISPLAY_IDLE_MS,"err timeout\r\n");
    now=0;command(&u,&c,&probe,"display start\n",now,"ok display\r\n");
    for(uint32_t n=1;n<6;++n) {
        frame(&u.screen,&d,now);now=n*100000u;
        command(&u,&c,&probe,"display next\n",now,"ok next\r\n");
    }
    (void)ui_poll(&u,&c,&adc,&probe,DISPLAY_SESSION_MS,0,out,false);
    assert(!u.identification.active && u.screen.index==89 && m.writes==8);
    /* Cancellation before the final halfword leaves the prior record valid. */
    now=0;command(&u,&c,&probe,"display start\n",now,"ok display\r\n");frame(&u.screen,&d,now);
    command(&u,&c,&probe,"display save 41\n",now,"ok saving\r\n");
    for(uint32_t i=0;i<7;++i)(void)ui_poll(&u,&c,&adc,&probe,now,0,out,false);
    command(&u,&c,&probe,"display cancel\n",now,"ok cancel\r\n");
    DisplayConfig reboot;config_init(&reboot,io);assert(reboot.index==89);
    /* Store error ends the session with the saved mapping intact. */
    m.fail=true;command(&u,&c,&probe,"display start\n",now,"ok display\r\n");frame(&u.screen,&d,now);
    command(&u,&c,&probe,"display save 41\n",now,"ok saving\r\n");
    (void)ui_poll(&u,&c,&adc,&probe,now,0,out,false);
    command(&u,&c,&probe,"display status\n",now,"err save\r\n");assert(u.screen.index==89);
    assert(c.issued==issued);
}
static void test_console_and_scheduling(void) {
    Display d={0}; Controller c; controller_boot(&c,rf()); Ui u; ui_init(&u,&c,bus(&d));
    Oled probe; oled_init(&probe,bus(&d));
    AdcIo adc={unavailable_adc,no_write,quiet,0}; Line line={0}; TxQueue tx={0};
    char out[16], wire[256]; uint32_t used=0, now=0, next_rx=0, next_tx=0;
    const char *input="oledtest\nset 31.75\nget\n";
    while (*input || tx.used || now<100000u) {
        uint32_t before=d.us;
        if (*input && now>=next_rx && tx.used<=48) {
            size_t n=ui_console_feed(&u,&line,&c,&adc,&probe,now/1000u,*input++,false,out);
            assert(tx_push(&tx,out,n)); next_rx+=87;
        }
        size_t n=ui_poll(&u,&c,&adc,&probe,now/1000u,now,out,tx.used<=48);
        assert(tx_push(&tx,out,n)); assert(d.us-before<=25);
        if (now>=next_tx && tx.used) {
            assert(used<sizeof(wire)-1u); assert(tx_pop(&tx,&wire[used++])); next_tx=now+87u;
        }
        now+=d.us-before+5u;
    }
    wire[used]=0;
    assert(!strcmp(wire,"ok oledtest\r\nok 31.75\r\n31.75\r\n"));
    assert(!u.screen.testing && c.qdb==127 && !strcmp(u.screen.large,"31.75"));
    command(&u,&c,&probe,"oledtest\n",now/1000u,"ok oledtest\r\n");
    command(&u,&c,&probe,"set 32\n",now/1000u,"err\r\n"); assert(u.screen.testing);
    uint32_t issued=c.issued;
    (void)ui_poll(&u,&c,&adc,&probe,now/1000u+12000u,now+12000000u,out,false);
    assert(!u.screen.testing && u.screen.done && c.issued==issued); /* deadline independent of TX */
    assert(ui_poll(&u,&c,&adc,&probe,now/1000u+12001u,now+12001000u,out,true)==15);
    assert(!strcmp(out,"oledtest done\r\n"));
    command(&u,&c,&probe,"oledprobe\n",0,"ok oledprobe\r\n");
    assert(!d.active && probe.active);
    uint32_t bytes=d.bytes;
    assert(!ui_poll(&u,&c,&adc,&probe,0,0,out,true) && d.bytes==bytes);
    oled_cancel(&probe); frame(&u.screen,&d,0); assert(d.active);
}
int main(void) {
    test_buttons(); test_lease(); test_atomic_profile_set(); test_display(); test_post_latch(); test_console_and_scheduling();test_identification();
    puts("UI: guarded ladder, median/debounce/release, clamping, post-latch state, leases/wrap, fixed OLED bytes and bounded alignment passed");
    return 0;
}

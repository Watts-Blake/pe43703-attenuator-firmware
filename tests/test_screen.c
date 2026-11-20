#include "screen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void quiet(void *ctx,uint32_t v) { (void)ctx;(void)v; }
static void end(void *ctx) { (void)ctx; }
static bool pixel(const Screen *s,uint32_t x,uint32_t y) {
    return (screen_image(s,x,y/8u)&(1u<<(y%8u)))!=0;
}
int main(void) {
    OledBus bus={quiet,quiet,end,quiet,0};Screen s;screen_init(&s,bus);
    assert(!s.known && !strcmp(s.large,"UNKNOWN"));
    for(uint32_t q=0;q<128;++q)for(uint32_t flags=0;flags<16;++flags)
    for(uint32_t step=0;step<3;++step)for(uint32_t label=0;label<2;++label) {
        const uint32_t steps[]={1,4,20};
        ScreenState v={q,127u-q,steps[step],SOURCE_USB,(flags&1u)!=0,(flags&2u)!=0,
                       (flags&4u)!=0,(flags&8u)!=0,label ? 'B' : 'A'};
        screen_show(&s,&v);
        for(uint32_t x=0;x<128;++x) {
            assert(pixel(&s,x,8) && pixel(&s,x,41));
            for(uint32_t page=0;page<8;++page)(void)screen_image(&s,x,page);
        }
        assert(!screen_image(&s,128,0) && !screen_image(&s,0,8));
        assert(s.known==v.known && s.locked==v.locked && s.label==v.label);
        if(!v.known)assert(!strcmp(s.large,"UNKNOWN") && s.message==SCREEN_ERROR);
        if(v.error)assert(s.message==SCREEN_ERROR);
        if(v.locked && !v.error && v.known)assert(s.message==((flags&8u) ? SCREEN_LOCKED : SCREEN_CALM));
    }
    /* Setup pages retain all eight text rows and do not acquire main chrome. */
    screen_calibration(&s,"HOLD PLUS",'+',2500,true,"RELEASE KEY");
    assert(s.calibrating && !strcmp(s.lines[2],"HOLD PLUS") && !strcmp(s.lines[4],"+ ADC 2500"));
    for(uint32_t x=0;x<128;++x)assert(!pixel(&s,x,8) && !pixel(&s,x,41));
    ScreenState v={127,0,20,SOURCE_LOCAL,true,false,false,false,'B'};
    screen_show(&s,&v);assert(!s.calibrating && s.message==SCREEN_PENDING && !strcmp(s.large,"31.75"));
    puts("Screen: all values, steps, labels, knowledge/ownership flags, bounds and calibration restoration passed");
    return 0;
}

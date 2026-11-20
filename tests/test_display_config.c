#include "display_config.h"
#include "boot_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t bytes[65536]; uint32_t writes, fail, tear; } Memory;
static uint8_t read_byte(void *ctx,uint32_t a) {
    Memory *m=ctx; assert(a>=FLASH_BASE && a<FLASH_END); return m->bytes[a-FLASH_BASE];
}
static bool program(void *ctx,uint32_t a,uint16_t value) {
    Memory *m=ctx; assert(a>=CONFIG_BASE && a<=FLASH_END-2 && !(a&1u));
    assert(read_byte(ctx,a)==255 && read_byte(ctx,a+1)==255);
    ++m->writes;
    if (m->writes==m->fail) {
        /* A failed program may leave no bits, partial bits, or all bits written. */
        uint16_t torn=(uint16_t)(value | (uint16_t)m->tear);
        m->bytes[a-FLASH_BASE]=(uint8_t)torn; m->bytes[a-FLASH_BASE+1]=(uint8_t)(torn>>8);
        return false;
    }
    m->bytes[a-FLASH_BASE]=(uint8_t)value; m->bytes[a-FLASH_BASE+1]=(uint8_t)(value>>8); return true;
}
static ConfigIo io(Memory *m) { ConfigIo f={read_byte,program,m}; return f; }
static void empty(Memory *m) { memset(m,0,sizeof(*m)); memset(m->bytes,255,sizeof(m->bytes)); }
static void complete(DisplayConfig *c) { for(uint32_t i=0;i<8;++i)config_poll(c); assert(!c->writing); }
static void test_interruptions(void) {
    for (uint32_t stop=0;stop<=8;++stop) {
        Memory m; empty(&m); DisplayConfig c; config_init(&c,io(&m));
        assert(c.index==41 && config_remaining(&c)==62 && !m.writes);
        assert(config_save(&c,89));
        for(uint32_t n=0;n<stop;++n)config_poll(&c);
        DisplayConfig reboot; config_init(&reboot,io(&m));
        assert(reboot.index==(stop==8 ? 89u : 41u));
        if(stop<8) { config_cancel(&c); assert(!c.writing && c.index==41); }
        assert(config_save(&reboot,42)); complete(&reboot);
        config_init(&c,io(&m)); assert(c.index==42);
    }
    for(uint32_t fail=1;fail<=8;++fail) for(uint32_t tear=0;tear<4;++tear) {
        Memory m;empty(&m);DisplayConfig c;config_init(&c,io(&m));
        assert(config_save(&c,42));complete(&c); /* known-good preceding record */
        m.fail=m.writes+fail; m.tear=tear==0 ? 0 : tear==1 ? 0xffffu : tear==2 ? 0xffu : 0xff00u;
        assert(config_save(&c,89));complete(&c);assert(c.fault && c.index==42);
        uint32_t writes=m.writes;assert(!config_save(&c,43));config_poll(&c);assert(m.writes==writes);
        config_init(&c,io(&m));assert(c.index==42 || (fail==8 && c.index==89));
    }
}
static void test_corruption_and_bounds(void) {
    Memory base;empty(&base);DisplayConfig c;config_init(&c,io(&base));
    assert(config_save(&c,42));complete(&c);assert(config_save(&c,89));complete(&c);
    for(uint32_t bit=0;bit<128;++bit) {
        Memory m=base;m.bytes[CONFIG_BASE-FLASH_BASE+16u+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        config_init(&c,io(&m));assert(c.index==42 && c.slot==2);
    }
    Memory m;empty(&m);config_init(&c,io(&m));
    assert(!config_save(&c,96) && !config_save(&c,UINT32_MAX));
    assert(config_save(&c,41) && !c.writing && !m.writes);
    for(uint32_t n=0;n<62;++n) {
        assert(config_save(&c,n%2u ? 42u : 89u));
        assert(!config_save(&c,43));complete(&c);
    }
    assert(!config_remaining(&c) && m.writes==496);
    assert(!config_save(&c,43) && config_save(&c,c.index));
    config_init(&c,io(&m));assert(c.index==42 && !config_remaining(&c));
    /* No bytes outside the metadata tail can have changed. */
    for(uint32_t a=FLASH_BASE;a<CONFIG_BASE;++a)assert(read_byte(&m,a)==255);
    /* A non-erased corrupt slot is consumed and can never become a pin map. */
    empty(&m);m.bytes[CONFIG_BASE-FLASH_BASE]=0;config_init(&c,io(&m));
    assert(c.index==41 && c.slot==1);
    ConfigIo none={0};config_init(&c,none);assert(c.index==41 && !config_save(&c,89));
}
static void put(Memory *m,uint32_t a,uint32_t v) {
    for(uint32_t n=0;n<4;++n)m->bytes[a-FLASH_BASE+n]=(uint8_t)(v>>(8u*n));
}
static void test_valid_crc_invalid_fields(void) {
    Memory base;empty(&base);DisplayConfig c;config_init(&c,io(&base));
    assert(config_save(&c,42));complete(&c);assert(config_save(&c,89));complete(&c);
    for(uint32_t field=0;field<5;++field) {
        Memory m=base;uint32_t a=CONFIG_BASE+16u;
        uint8_t *p=m.bytes+a-FLASH_BASE;
        if(field==0)p[0]=2; /* future record version */
        else if(field==1) { p[2]=96;p[4]=(uint8_t)(p[2]^255u); }
        else if(field==2)p[3]=1; /* trial is u16, never truncate into a pin map */
        else if(field==3)p[6]=1; /* reserved */
        else p[4]^=1; /* complement */
        Flash f={&m,read_byte,0,0};put(&m,a+8,bl_crc(f,a,8));
        config_init(&c,io(&m));assert(c.index==42 && c.slot==2);
    }
}
static bool erase(void *ctx,uint32_t a) {
    Memory *m=ctx;assert(a>=APP_BASE && a<FLASH_END && !(a&1023u));
    memset(m->bytes+a-FLASH_BASE,255,PAGE_BYTES);return true;
}
static void test_loader_compatibility(void) {
    Memory m;empty(&m);
    put(&m,APP_BASE,BOOT_RAM);put(&m,APP_BASE+4,APP_BASE+9);
    Flash f={&m,read_byte,erase,program};uint32_t checksum=bl_crc(f,APP_BASE,256);
    put(&m,META_BASE,APP_COMMIT);put(&m,META_BASE+4,256);put(&m,META_BASE+8,checksum);
    put(&m,META_BASE+12,~256u);put(&m,META_BASE+16,~checksum);assert(bl_valid(f));
    uint8_t before[CONFIG_BASE-FLASH_BASE];memcpy(before,m.bytes,sizeof(before));
    DisplayConfig c;config_init(&c,io(&m));assert(config_save(&c,89));complete(&c);
    assert(config_save_buttons(&c,(const uint16_t[]){4095,2500,1700,500,1100}));
    for(uint32_t i=0;i<16;++i)config_poll(&c);
    assert(config_save_label(&c,'B'));complete(&c);
    assert(!memcmp(before,m.bytes,sizeof(before)) && bl_valid(f));
    Update u;bl_init(&u,f);char out[64];const char *hello="hello\n";
    while(*hello)(void)bl_feed(&u,*hello++,false,out);
    assert(!strcmp(out,"boot 1 08002000 0000dc00 00000040\n"));
    const char *begin="begin 00000100 00000000\n";
    while(*begin)(void)bl_feed(&u,*begin++,false,out);
    assert(!strcmp(out,"ok\n") && !bl_valid(f));
    config_init(&c,io(&m));assert(c.index==41 && !c.calibrated && c.label=='A' && config_remaining(&c)==62);
}
static const uint16_t buttons[BUTTON_COUNT]={4095,2500,1700,500,1100};
static void finish_buttons(DisplayConfig *c) {
    for(uint32_t i=0;i<16;++i)config_poll(c);
    assert(!c->writing);
}
static void test_button_records(void) {
    for(uint32_t stop=0;stop<=16;++stop) {
        Memory m;empty(&m);DisplayConfig c;config_init(&c,io(&m));
        assert(config_save(&c,89));complete(&c);
        assert(config_save_buttons(&c,buttons));
        for(uint32_t i=0;i<stop;++i)config_poll(&c);
        DisplayConfig reboot;config_init(&reboot,io(&m));
        assert(reboot.index==89 && reboot.calibrated==(stop==16));
        if(stop<16)config_cancel(&c);
        /* Display and button saves choose the latest value independently. */
        assert(config_save(&reboot,42));complete(&reboot);
        assert(config_save_buttons(&reboot,buttons));finish_buttons(&reboot);
        config_init(&c,io(&m));assert(c.index==42 && c.calibrated && !memcmp(c.buttons,buttons,sizeof(buttons)));
        /* A v0.3 reader ignores both halves of every v2 button record. */
        uint32_t legacy=41;
        for(uint32_t slot=0;slot<CONFIG_SLOTS;++slot) {
            uint8_t *p=m.bytes+CONFIG_BASE-FLASH_BASE+slot*16u;
            Flash f={&m,read_byte,0,0};
            if(p[0]==1 && p[1]==0 && p[2]<96 && p[3]==0 && p[4]==(uint8_t)(p[2]^255u) &&
               p[5]==255 && !p[6] && !p[7] &&
               bl_crc(f,CONFIG_BASE+slot*16u,8)==(uint32_t)(p[8]|((uint32_t)p[9]<<8)|((uint32_t)p[10]<<16)|((uint32_t)p[11]<<24)) &&
               !memcmp(p+12,"1PSD",4)) legacy=p[2];
        }
        assert(legacy==42);
    }
    for(uint32_t fail=1;fail<=16;++fail)for(uint32_t tear=0;tear<4;++tear) {
        Memory m;empty(&m);DisplayConfig c;config_init(&c,io(&m));
        assert(config_save_buttons(&c,buttons));finish_buttons(&c);
        uint16_t other[BUTTON_COUNT];memcpy(other,buttons,sizeof(other));other[1]=2700;
        m.fail=m.writes+fail;m.tear=tear==0 ? 0 : tear==1 ? 0xffffu : tear==2 ? 0xffu : 0xff00u;
        assert(config_save_buttons(&c,other));finish_buttons(&c);
        assert(c.fault && c.calibrated && c.buttons[1]==2500 && !config_save(&c,89));
        config_init(&c,io(&m));assert(c.calibrated && (c.buttons[1]==2500 || (fail==16 && c.buttons[1]==2700)));
    }
    Memory base;empty(&base);DisplayConfig c;config_init(&c,io(&base));
    assert(config_save_buttons(&c,buttons));finish_buttons(&c);
    assert(config_save(&c,89));complete(&c);
    uint16_t other[BUTTON_COUNT];memcpy(other,buttons,sizeof(other));other[1]=2700;
    assert(config_save_buttons(&c,other));finish_buttons(&c);
    for(uint32_t bit=0;bit<256;++bit) {
        Memory m=base;m.bytes[CONFIG_BASE-FLASH_BASE+48u+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        config_init(&c,io(&m));assert(c.index==89 && c.calibrated && !memcmp(c.buttons,buttons,sizeof(buttons)));
    }
    for(uint32_t field=0;field<8;++field) {
        Memory m=base;uint32_t a=CONFIG_BASE+48u;uint8_t *p=m.bytes+a-FLASH_BASE;
        if(field==0)p[0]=3; /* future */
        if(field==1)p[1]=1; /* exact u16 version */
        if(field==2)p[2]=1; /* reserved */
        if(field==3)p[14]=1;
        if(field==4) { p[4]=32;p[5]=0; } /* release collision */
        if(field==5) { p[4]=255;p[5]=255; } /* out of ADC range */
        if(field==6) { p[4]=p[6];p[5]=p[7]; } /* overlap */
        if(field==7)p[16]^=1; /* continuation tag */
        Flash f={&m,read_byte,0,0};uint32_t sum=bl_crc(f,a,20);put(&m,a+20,sum);put(&m,a+24,~sum);
        config_init(&c,io(&m));assert(c.index==89 && c.calibrated && !memcmp(c.buttons,buttons,sizeof(buttons)));
    }
    Memory m;empty(&m);config_init(&c,io(&m));
    for(uint32_t i=0;i<61;++i) { assert(config_save(&c,i%2u ? 42u : 89u));complete(&c); }
    assert(config_remaining(&c)==1 && !config_save_buttons(&c,buttons));
    assert(!c.calibrated && !c.writing && m.writes==61u*8u);
    empty(&m);config_init(&c,io(&m));
    for(uint32_t i=0;i<31;++i) {
        other[1]=(uint16_t)(i%2u ? 2700 : 2500);assert(config_save_buttons(&c,other));finish_buttons(&c);
    }
    assert(!config_remaining(&c) && config_save_buttons(&c,other) && !c.writing);
    other[1]=2800;assert(!config_save_buttons(&c,other));
    for(uint32_t a=FLASH_BASE;a<CONFIG_BASE;++a)assert(read_byte(&m,a)==255);
    for(uint32_t raw=0;raw<5000;++raw) {
        static const uint32_t keys[BUTTON_COUNT]={KEY_LEFT,KEY_PLUS,KEY_MINUS,KEY_RIGHT,KEY_OK};
        uint32_t expected=raw<=32 ? KEY_RELEASED : KEY_UNKNOWN;
        for(uint32_t i=0;i<BUTTON_COUNT;++i)if(raw<=4095 && raw+32u>=c.buttons[i] && raw<=(uint32_t)c.buttons[i]+32u)expected=keys[i];
        assert(config_button_code(&c,raw)==expected);
    }
}
static void test_label_records(void) {
    for(uint32_t stop=0;stop<=8;++stop) {
        Memory m;empty(&m);DisplayConfig c;config_init(&c,io(&m));
        assert(config_save(&c,89));complete(&c);
        assert(config_save_buttons(&c,buttons));finish_buttons(&c);
        assert(config_save_label(&c,'B'));
        for(uint32_t n=0;n<stop;++n)config_poll(&c);
        DisplayConfig reboot;config_init(&reboot,io(&m));
        assert(reboot.label==(stop==8 ? 'B' : 'A') && reboot.index==89 && reboot.calibrated);
        assert(!memcmp(reboot.buttons,buttons,sizeof(buttons)));
        config_cancel(&c);
        assert(config_save(&reboot,42));complete(&reboot);
        assert(config_save_label(&reboot,'B'));complete(&reboot);
        config_init(&c,io(&m));assert(c.label=='B' && c.index==42 && c.calibrated);
    }
    for(uint32_t fail=1;fail<=8;++fail)for(uint32_t tear=0;tear<4;++tear) {
        Memory m;empty(&m);DisplayConfig c;config_init(&c,io(&m));
        assert(config_save_label(&c,'B'));complete(&c);
        m.fail=m.writes+fail;m.tear=tear==0 ? 0 : tear==1 ? 65535u : tear==2 ? 255u : 65280u;
        assert(config_save_label(&c,'A'));complete(&c);assert(c.fault && c.label=='B');
        assert(!config_save_label(&c,'A'));config_init(&c,io(&m));
        assert(c.label=='B' || (fail==8 && c.label=='A'));
    }
    Memory base;empty(&base);DisplayConfig c;config_init(&c,io(&base));
    assert(!config_save_label(&c,'C') && !config_save_label(&c,0));
    assert(config_save_label(&c,'A') && !c.writing && !base.writes);
    assert(config_save_label(&c,'B'));complete(&c);
    for(uint32_t bit=0;bit<128;++bit) {
        Memory m=base;m.bytes[CONFIG_BASE-FLASH_BASE+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        config_init(&c,io(&m));assert(c.label=='A' && c.index==41 && !c.calibrated);
    }
    for(uint32_t field=0;field<7;++field) {
        Memory m=base;uint8_t *p=m.bytes+CONFIG_BASE-FLASH_BASE;
        if(field==0)p[0]=4;
        if(field==1)p[1]=1;
        if(field==2){p[2]='C';p[4]=(uint8_t)(p[2]^255u);}
        if(field==3)p[3]=1;
        if(field==4)p[4]^=1;
        if(field==5)p[5]=0;
        if(field==6)p[6]=1;
        Flash f={&m,read_byte,0,0};put(&m,CONFIG_BASE+8,bl_crc(f,CONFIG_BASE,8));
        config_init(&c,io(&m));assert(c.label=='A');
    }
    Memory m;empty(&m);config_init(&c,io(&m));
    for(uint32_t i=0;i<CONFIG_SLOTS;++i){assert(config_save_label(&c,i%2u ? 'A' : 'B'));complete(&c);}
    assert(!config_remaining(&c) && config_save_label(&c,'A') && !config_save_label(&c,'B'));
    for(uint32_t a=FLASH_BASE;a<CONFIG_BASE;++a)assert(read_byte(&m,a)==255);
}
int main(void) {
    test_interruptions();test_corruption_and_bounds();test_valid_crc_invalid_fields();test_loader_compatibility();test_button_records();test_label_records();
    puts("Display config: interrupted/torn halfwords, bit corruption, append bounds and unchanged loader geometry passed");return 0;
}

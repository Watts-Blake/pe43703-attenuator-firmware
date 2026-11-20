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
    assert(!memcmp(before,m.bytes,sizeof(before)) && bl_valid(f));
    Update u;bl_init(&u,f);char out[64];const char *hello="hello\n";
    while(*hello)(void)bl_feed(&u,*hello++,false,out);
    assert(!strcmp(out,"boot 1 08002000 0000dc00 00000040\n"));
    const char *begin="begin 00000100 00000000\n";
    while(*begin)(void)bl_feed(&u,*begin++,false,out);
    assert(!strcmp(out,"ok\n") && !bl_valid(f));
    config_init(&c,io(&m));assert(c.index==41 && config_remaining(&c)==62);
}
int main(void) {
    test_interruptions();test_corruption_and_bounds();test_valid_crc_invalid_fields();test_loader_compatibility();
    puts("Display config: interrupted/torn halfwords, bit corruption, append bounds and unchanged loader geometry passed");return 0;
}

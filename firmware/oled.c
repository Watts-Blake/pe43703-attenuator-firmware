#include "oled.h"
enum { ANNOUNCE, BEGIN, INIT, WHITE, WHITE_HOLD, PATTERN, PATTERN_HOLD, FINISH };
/* 128x64 page addressing, low contrast, internal pump explicitly enabled.
   Command meanings: SSD1306 Rev1.1 + charge-pump appendix; SH1106 V2.6. */
static const uint8_t ssd_init[] = {
    0xae,0xd5,0x80,0xa8,0x3f,0xd3,0x00,0x40,0xa1,0xc8,0xda,0x12,
    0x81,0x10,0xd9,0xf1,0xdb,0x20,0xa4,0xa6,0x20,0x02,0x8d,0x14
};
static const uint8_t sh_init[] = {
    0xae,0xd5,0x80,0xa8,0x3f,0xd3,0x00,0x40,0xa1,0xc8,0xda,0x12,
    0x81,0x10,0xd9,0x22,0xdb,0x35,0xa4,0xa6,0xad,0x8b
};
static const uint8_t digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};
bool oled_mapping(uint32_t index, uint8_t pins[4]) {
    if (index >= OLED_STEPS) return false;
    uint32_t wanted = index % 24u, n = 0;
    for (uint8_t a = 4; a < 8; ++a) for (uint8_t b = 4; b < 8; ++b)
        for (uint8_t c = 4; c < 8; ++c) {
            if (a == b || a == c || b == c) continue;
            if (n++ == wanted) {
                pins[0]=a; pins[1]=b; pins[2]=c;
                pins[3]=(uint8_t)(4u ^ 5u ^ 6u ^ 7u ^ a ^ b ^ c);
                return true;
            }
        }
    return false;
}
static bool reset_mode(const Oled *p) { return (p->index / 24u) % 2u != 0; }
static void levels(Oled *p, uint32_t value) { p->bus.pins(p->bus.context,value); }
static void byte(Oled *p, uint8_t value, bool data) {
    uint32_t base = (reset_mode(p) ? p->control : 0) | (data ? p->dc : 0);
    for (uint32_t bit = 0; bit < 8; ++bit) {
        uint32_t v = base | ((value & (0x80u >> bit)) ? p->si : 0);
        levels(p,v); p->bus.delay_us(p->bus.context,1);
        levels(p,v|p->clk); p->bus.delay_us(p->bus.context,1);
        levels(p,v); p->bus.delay_us(p->bus.context,1);
    }
    levels(p,base|p->control); /* CS inactive, or RES remains high */
    p->bus.delay_us(p->bus.context,1);
}
void oled_write(Oled *p, uint8_t value, bool data) { byte(p,value,data); }
void oled_fixed_begin(Oled *p) {
    p->index=41; p->clk=1u<<6; p->si=1u<<7; p->dc=1u<<5; p->control=1u<<4;
    p->bus.begin(p->bus.context,0); p->configured=true;
    p->bus.delay_us(p->bus.context,10); levels(p,p->control);
    p->bus.delay_us(p->bus.context,10);
}
bool oled_fixed_init(Oled *p, uint32_t cursor) {
    if (cursor>=sizeof(ssd_init)) return true;
    byte(p,ssd_init[cursor],false);
    return cursor+1u==sizeof(ssd_init);
}
void oled_init(Oled *p, OledBus bus) {
    p->bus=bus; p->active=false; p->configured=false; p->index=0;
}
bool oled_start(Oled *p, uint32_t now_ms) {
    if (p->active) return false;
    p->active=true; p->phase=ANNOUNCE; p->index=0; p->start_ms=now_ms;
    return true;
}
void oled_cancel(Oled *p) {
    if (p->configured) {
        byte(p,0xae,false); /* attempt display-off on current trial mapping */
        p->bus.end(p->bus.context); p->configured=false;
    }
    p->active=false;
}
static size_t reply(char out[16], const char *text) {
    size_t n=0;
    while (*text) out[n++]=*text++;
    out[n]=0; return n;
}
static uint8_t pattern(uint32_t index, uint32_t x, uint32_t page) {
    uint8_t result=0;
    for (uint32_t b=0; b<8; ++b) {
        uint32_t y=page*8u+b;
        bool on = x==0 || x==127 || y==0 || y==63;
        if (x>=44 && x<84 && y>=20 && y<40) {
            uint32_t column=(x-44u)%20u, digit=(x-44u)/20u;
            uint32_t value=digit ? index%10u : index/10u;
            on=column<12 && (digits[value][(y-20u)/4u] & (4u>>(column/4u)));
        }
        if (on) result |= (uint8_t)(1u<<b);
    }
    return result;
}
/* Each page has three positioning commands followed by128 data bytes. */
static bool image_byte(Oled *p, bool white) {
    uint32_t page=p->cursor/131u, column=p->cursor%131u;
    if (column==0) byte(p,(uint8_t)(0xb0u+page),false);
    else if (column==1) byte(p,p->index<48 ? 0x00 : 0x02,false);
    else if (column==2) byte(p,0x10,false);
    else byte(p,white ? 0xff : pattern(p->index,column-3u,page),true);
    return ++p->cursor==8u*131u;
}
size_t oled_poll(Oled *p, uint32_t now_ms, char out[16], bool can_report) {
    if (!p->active) return 0;
    if ((uint32_t)(now_ms-p->start_ms)>=OLED_LIMIT_MS) {
        oled_cancel(p); return can_report ? reply(out,"oled timeout\r\n") : 0;
    }
    if (!can_report && (p->phase==ANNOUNCE || p->phase==FINISH)) return 0;
    switch (p->phase) {
    case ANNOUNCE: {
        uint8_t pins[4]; (void)oled_mapping(p->index,pins);
        p->clk=1u<<pins[0]; p->si=1u<<pins[1]; p->dc=1u<<pins[2]; p->control=1u<<pins[3];
        p->step_ms=now_ms; p->phase=BEGIN;
        reply(out,"oled 00 S C\r\n"); out[5]=(char)('0'+p->index/10u); out[6]=(char)('0'+p->index%10u);
        out[8]=p->index<48 ? 'S' : 'H'; out[10]=reset_mode(p) ? 'R' : 'C'; return 13;
    }
    case BEGIN:
        p->bus.begin(p->bus.context,reset_mode(p) ? 0 : p->control); p->configured=true;
        if (reset_mode(p)) {
            p->bus.delay_us(p->bus.context,10); levels(p,p->control);
            p->bus.delay_us(p->bus.context,10);
        }
        p->cursor=0; p->phase=INIT; break;
    case INIT: {
        const uint8_t *init=p->index<48 ? ssd_init : sh_init;
        uint32_t length=p->index<48 ? sizeof(ssd_init) : sizeof(sh_init);
        byte(p,init[p->cursor++],false);
        if (p->cursor==length) { p->cursor=0; p->phase=WHITE; }
        break;
    }
    case WHITE:
        if (image_byte(p,true)) p->phase=WHITE_HOLD;
        break;
    case WHITE_HOLD:
        /* Display ON only after white RAM is complete; separate bounded call. */
        byte(p,0xaf,false); p->cursor=now_ms; p->phase=PATTERN; break;
    case PATTERN:
        if ((uint32_t)(now_ms-p->cursor)<100u) break;
        p->cursor=0; p->phase=PATTERN_HOLD; break;
    case PATTERN_HOLD:
        if (p->cursor<8u*131u) { (void)image_byte(p,false); break; }
        if ((uint32_t)(now_ms-p->step_ms)<2000u) break;
        byte(p,0xae,false); p->bus.end(p->bus.context); p->configured=false;
        p->phase=++p->index==OLED_STEPS ? FINISH : ANNOUNCE; break;
    case FINISH:
        p->active=false; return reply(out,"oled done\r\n");
    default:
        oled_cancel(p); return reply(out,"oled error\r\n");
    }
    return 0;
}
size_t probe_console_feed(Line *line, Controller *c, const AdcIo *adc, Oled *p,
                          uint32_t now_ms, char ch, bool rx_error, char out[16]) {
    uint32_t issued=c->issued;
    size_t n=console_feed(line,c,adc,ch,rx_error,out);
    if (c->issued!=issued) oled_cancel(p); /* valid SET/bootloader is authoritative */
    if (!line->oledprobe) return n;
    line->oledprobe=false;
    return reply(out,oled_start(p,now_ms) ? "ok oledprobe\r\n" : "err busy\r\n");
}

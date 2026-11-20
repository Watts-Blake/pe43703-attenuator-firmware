#include "screen.h"
/* Five columns, top pixel in bit0. Small fixed alphabet, six-pixel cell. */
static const uint8_t font[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};
static uint8_t glyph(char ch, uint32_t col) {
    if (col>=5) return 0;
    if (ch>='0' && ch<='9') return font[(uint32_t)(ch-'0')][col];
    if (ch>='A' && ch<='Z') return font[10u+(uint32_t)(ch-'A')][col];
    if (ch=='+') return col==2 ? 0x3e : 8;
    if (ch=='-') return 8;
    return ch=='.' && col==2 ? 0x60 : 0;
}
static void text(char *out, const char *in) {
    uint32_t i=0;
    while (in[i] && i<21) { out[i]=in[i]; ++i; }
    while (i<22) out[i++]=0;
}
static void value(char *out, const char *label, uint32_t qdb) {
    text(out,label);
    uint32_t i=0; while (out[i]) ++i;
    format_db(qdb,out+i);
}
void screen_init(Screen *s, OledBus bus) {
    oled_init(&s->wire,bus); s->phase=0; s->cursor=0;
    s->dirty=true; s->testing=false; s->holding=false; s->done=false;
    s->identifying=false; s->calibrating=false; s->index=41;
    for (uint32_t i=0;i<8;++i) text(s->lines[i],"");
    format_db(0,s->large);
}
void screen_show(Screen *s, uint32_t applied, uint32_t pending, uint32_t step,
                 uint32_t source, bool locked) {
    text(s->lines[0],"APPLIED DB"); format_db(applied,s->large);
    value(s->lines[4],"PENDING ",pending); value(s->lines[5],"STEP ",step);
    text(s->lines[6],source==SOURCE_LOCAL ? "SOURCE LOCAL" : source==SOURCE_BOOT ? "SOURCE BOOT" : "SOURCE USB");
    text(s->lines[7],locked ? "USB LOCK" : "UNLOCKED"); s->dirty=true;
}
void screen_suspend(Screen *s) {
    oled_cancel(&s->wire); s->phase=0; s->dirty=true;
}
void screen_test(Screen *s, uint32_t now_ms) {
    if (s->index!=41) screen_config(s,41); /* legacy factory-mapping diagnostic */
    s->testing=true; s->holding=false; s->done=false;
    s->test_start=now_ms; s->dirty=true;
}
void screen_config(Screen *s, uint32_t index) {
    screen_suspend(s); s->index=index<96 ? index : 41;
    s->identifying=false; s->calibrating=false; s->testing=false; s->holding=false; s->done=false;
}
void screen_identify(Screen *s, uint32_t index) {
    screen_config(s,index); s->identifying=true;
}
void screen_calibration(Screen *s,const char *prompt,char key,uint32_t raw,
                        bool captured,const char *footer) {
    for (uint32_t i=0;i<8;++i) text(s->lines[i],"");
    text(s->lines[0],"BUTTON CALIBRATION"); text(s->lines[2],prompt);
    if (captured) {
        text(s->lines[4],"L ADC 0000"); s->lines[4][0]=key;
        for (uint32_t j=0,divisor=1000;j<4;++j,divisor/=10)
            s->lines[4][6u+j]=(char)('0'+(raw/divisor)%10u);
    }
    text(s->lines[6],footer); s->calibrating=true; s->dirty=true;
}
void screen_stop_test(Screen *s) {
    if (s->testing) { s->testing=false; s->holding=false; s->dirty=true; s->done=false; }
}
uint8_t screen_image(const Screen *s, uint32_t x, uint32_t page) {
    if (x>=128 || page>=8) return 0;
    if (s->testing || s->identifying) {
        uint8_t bits=0;
        for (uint32_t b=0;b<8;++b) {
            uint32_t y=page*8u+b;
            bool on=x==0 || x==127 || y==0 || y==63;
            /* Distinct inner edge columns must not hide a clipped outer border. */
            if (((x==1 || x==2) && y>=8 && y<16) ||
                ((x==125 || x==126) && y>=48 && y<56)) on=true;
            /* Asymmetric left/right markers and a labeled page number. */
            if ((x==8 && y>=8 && y<24) || (y==24 && x>=8 && x<20)) on=true;
            if ((x==119 && y>=40 && y<56) || (y==40 && x>=108 && x<120)) on=true;
            if (on) bits|=(uint8_t)(1u<<b);
        }
        if (x>=32 && x<74 && page==3) bits|=glyph((s->index<48 ? "SSD1306" : "SH1106 ")[(x-32u)/6u],(x-32u)%6u);
        if (s->identifying && x>=50 && x<62 && page==5)
            bits|=glyph((char)('0'+((x-50u)/6u ? s->index%10u : s->index/10u)),(x-50u)%6u);
        if (x>=24 && x<29) bits|=glyph((char)('0'+page),x-24u);
        return bits;
    }
    if (!s->calibrating && (page==1 || page==2)) {
        uint32_t column=x/12u;
        uint32_t length=0; while (s->large[length]) ++length;
        if (column>=length) return 0;
        uint8_t col=glyph(s->large[column],(x%12u)/2u), result=0;
        for (uint32_t bit=0;bit<8;++bit)
            if (col & (1u<<((page-1u)*4u+bit/2u))) result|=(uint8_t)(1u<<bit);
        return result;
    }
    uint32_t column=x/6u;
    return column<21 ? glyph(s->lines[page][column],x%6u) : 0;
}
void screen_poll(Screen *s, uint32_t now_ms) {
    if (s->testing && ((uint32_t)(now_ms-s->test_start)>=12000u ||
        (s->holding && (uint32_t)(now_ms-s->hold_start)>=10000u))) {
        screen_stop_test(s); s->done=true;
    }
    if (!s->wire.configured) { oled_config_begin(&s->wire,s->index); s->phase=1; s->cursor=0; return; }
    if (s->phase==1) {
        if (oled_config_init(&s->wire,s->cursor++)) { s->phase=5; s->cursor=0; }
        return;
    }
    if (s->phase==5) {
        /* Clear all 132 possible RAM columns, so a prior trial cannot supply
           a missing edge. SSD1306 wraps the four extra zero bytes harmlessly. */
        uint32_t page=s->cursor/135u, col=s->cursor%135u;
        if (col==0) oled_write(&s->wire,(uint8_t)(0xb0u+page),false);
        else if (col==1) oled_write(&s->wire,0,false);
        else if (col==2) oled_write(&s->wire,0x10,false);
        else oled_write(&s->wire,0,true);
        if (++s->cursor==1080) { s->phase=2; s->cursor=0; }
        return;
    }
    if (s->dirty) { s->phase=2; s->cursor=0; s->dirty=false; }
    if (s->phase==2) {
        uint32_t page=s->cursor/131u, col=s->cursor%131u;
        if (col==0) oled_write(&s->wire,(uint8_t)(0xb0u+page),false);
        else if (col==1) oled_write(&s->wire,s->index<48 ? 0 : 2,false);
        else if (col==2) oled_write(&s->wire,0x10,false);
        else oled_write(&s->wire,screen_image(s,col-3u,page),true);
        if (++s->cursor==1048) s->phase=3;
    } else if (s->phase==3) {
        oled_write(&s->wire,0xaf,false); s->phase=4;
        if (s->testing && !s->holding) { s->holding=true; s->hold_start=now_ms; }
    }
}

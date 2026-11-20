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
void screen_init(Screen *s, OledBus bus) {
    oled_init(&s->wire,bus); s->phase=0; s->cursor=0;
    s->dirty=true; s->testing=false; s->holding=false; s->done=false;
    s->identifying=false; s->calibrating=false; s->index=41;
    for (uint32_t i=0;i<8;++i) text(s->lines[i],"");
    ScreenState state={0,0,1,SOURCE_BOOT,false,false,false,false,'A'};
    screen_show(s,&state);
}
void screen_show(Screen *s, const ScreenState *v) {
    s->calibrating=false; s->known=v->known && v->applied<=127;
    s->locked=v->locked; s->label=v->label=='B' ? 'B' : 'A';
    s->step_index=v->step==20 ? 2u : v->step==4 ? 1u : 0u;
    bool error=v->error || !s->known;
    text(s->lines[0],error ? "ERROR" : v->source==SOURCE_USB ? "USB" : "LOCAL");
    if (s->known) format_db(v->applied,s->large);
    else { const char unknown[]="UNKNOWN"; for (uint32_t i=0;i<8;++i) s->large[i]=unknown[i]; }
    text(s->lines[5],s->step_index==2 ? "STEP 5.00" : s->step_index==1 ? "STEP 1.00" : "STEP 0.25");
    s->message=error ? SCREEN_ERROR : v->locked ?
        (v->feedback ? SCREEN_LOCKED : SCREEN_CALM) :
        v->pending!=v->applied && v->pending<=127 ? SCREEN_PENDING : SCREEN_CALM;
    text(s->lines[7],"");
    if (s->message==SCREEN_PENDING) {
        char number[6]; format_db(v->pending,number);
        uint32_t n=0; while (number[n]) { s->lines[7][n]=number[n]; ++n; }
        const char suffix[]="  OK TO APPLY";
        for (uint32_t i=0;i<sizeof(suffix);++i) s->lines[7][n+i]=suffix[i];
    } else if (s->message==SCREEN_LOCKED) text(s->lines[7],"LOCKED  USB OWNS");
    else if (s->message==SCREEN_ERROR) text(s->lines[7],"ERR NO LATCH");
    s->dirty=true;
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
/* Query only the requested pixel: no 1 KiB framebuffer or dynamic allocation. */
static bool text_pixel(const char *p, uint32_t x, uint32_t y, uint32_t scale) {
    uint32_t column=x/(6u*scale);
    for (uint32_t i=0;i<column;++i) if (!p[i]) return false;
    return p[column] && y<7u*scale &&
        (glyph(p[column],(x/scale)%6u)&(1u<<(y/scale)))!=0;
}
static bool main_pixel(const Screen *s, uint32_t x, uint32_t y) {
    static const uint8_t lock[8]={0x3c,0x42,0x42,0x7e,0xff,0xe7,0xe7,0xff};
    if (y==8 || y==41) return true;
    if (y<8) {
        if (x<8) return s->locked && (lock[y]&(1u<<(7u-x)));
        if (x>=117 && x<125) {
            bool ink=x>=118 && (glyph(s->label,x-118u)&(1u<<y));
            return !ink;
        }
        return x>=10 && text_pixel(s->lines[0],x-10u,y,1);
    }
    if (y<41) return x>=4 && text_pixel(s->large,x-4u,y-(s->known ? 9u : 17u),s->known ? 4u : 2u);
    if (y>=45 && y<52) {
        if (x>=68 && x<84 && y>=47 && y<51) {
            uint32_t i=(x-68u)/6u, dx=(x-68u)%6u;
            return dx<4 && (i==s->step_index || dx==0 || dx==3 || y==47 || y==50);
        }
        return x>=4 && text_pixel(s->lines[5],x-4u,y-45u,1);
    }
    if (y>=54 && x>=2 && x<126 && s->message!=SCREEN_CALM) {
        bool ink=x>=5 && y>=55 && text_pixel(s->lines[7],x-5u,y-55u,1);
        if (s->message==SCREEN_ERROR) return x==2 || x==125 || y==54 || y==63 || ink;
        return !ink;
    }
    return false;
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
    if (!s->calibrating) {
        uint8_t bits=0;
        for (uint32_t b=0;b<8;++b)
            if (main_pixel(s,x,page*8u+b)) bits|=(uint8_t)(1u<<b);
        return bits;
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

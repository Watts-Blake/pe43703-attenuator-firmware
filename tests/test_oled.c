#include "oled.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint64_t us, white_on;
    uint32_t levels, clk, si, dc, control, rise, change, reset_low, reset_high;
    uint32_t index, bits, word, count, begins, ends, rf;
    uint16_t stream[2200];
    bool active, reset, validate;
} Trace;
static void validate(Trace *t) {
    const uint8_t ssd[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0xa1,0xc8,0xda,0x12,0x81,0x10,0xd9,0xf1,0xdb,0x20,0xa4,0xa6,0x20,2,0x8d,0x14};
    const uint8_t sh[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0xa1,0xc8,0xda,0x12,0x81,0x10,0xd9,0x22,0xdb,0x35,0xa4,0xa6,0xad,0x8b};
    const uint8_t *init=t->index<48 ? ssd : sh;
    uint32_t length=t->index<48 ? (uint32_t)sizeof(ssd) : (uint32_t)sizeof(sh);
    assert(t->count==length+2u*1048u+2u); uint32_t n=0;
    for (;n<length;++n) assert(t->stream[n]==init[n]);
    uint32_t black=0,white=0;
    for (uint32_t image=0;image<2;++image) {
        for (uint32_t page=0;page<8;++page) {
            assert(t->stream[n++]==0xb0u+page);
            assert(t->stream[n++]==(t->index<48 ? 0u : 2u)); assert(t->stream[n++]==0x10);
            for (uint32_t x=0;x<128;++x) {
                uint16_t value=t->stream[n++];assert(value & 0x100u);
                if (!image) assert(value==0x1ffu);
                else { if (value!=0x1ffu) ++black; if (value!=0x100u) ++white; }
            }
        }
        if (!image) assert(t->stream[n++]==0xaf);
    }
    assert(t->stream[n++]==0xae && n==t->count && black>0 && white>0);
    const uint8_t font[10][5]={{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    uint32_t pattern_start=length+1048u+1u;
    for(uint32_t digit=0;digit<2;++digit) for(uint32_t row=0;row<5;++row) for(uint32_t col=0;col<3;++col) {
        uint32_t x=44u+digit*20u+col*4u+1u,y=20u+row*4u+1u;
        uint16_t data=t->stream[pattern_start+(y/8u)*131u+3u+x];
        uint32_t value=digit ? t->index%10u : t->index/10u;
        assert(!!(data & (1u<<(y%8u)))==!!(font[value][row] & (4u>>col)));
    }
}
static void begin(void *context,uint32_t idle) {
    Trace *t=context; assert(!t->active);t->active=true;
    uint8_t pins[4];assert(oled_mapping(t->index,pins));
    t->clk=1u<<pins[0];t->si=1u<<pins[1];t->dc=1u<<pins[2];t->control=1u<<pins[3];
    t->reset=(t->index/24u)%2u!=0;t->levels=idle;t->bits=0;t->word=0;t->count=0;t->white_on=0;
    assert(idle==(t->reset ? 0 : t->control));t->reset_low=(uint32_t)t->us;++t->begins;
}
static void pins(void *context,uint32_t levels) {
    Trace *t=context; assert(t->active && !(levels & ~OLED_MASK));
    uint32_t now=(uint32_t)t->us,changed=t->levels^levels;
    if (t->reset && (changed&t->control) && (levels&t->control)) {
        assert(now-t->reset_low>=10);t->reset_high=now;
    }
    if (changed & (t->si|t->dc)) t->change=now;
    if ((changed & t->clk) && (levels & t->clk)) {
        assert(now-t->change>=1);
        if (t->reset) assert((levels&t->control) && now-t->reset_high>=10);
        else assert(!(levels&t->control));
        t->word=(t->word<<1)|((levels&t->si) ? 1u : 0u);++t->bits;t->rise=now;
        if (t->bits==8) {
            if (!(levels&t->dc) && t->word==0xaf) t->white_on=t->us;
            if ((levels&t->dc) && t->white_on) assert(t->us-t->white_on>=100000u);
            assert(t->count<2200);t->stream[t->count++]=(uint16_t)(t->word|((levels&t->dc) ? 0x100u : 0));
            t->bits=0;t->word=0;
        }
    }
    if ((changed&t->clk) && !(levels&t->clk)) assert(now-t->rise>=1);
    t->levels=levels;
}
static void end(void *context) {
    Trace *t=context;assert(t->active && !t->bits);if(t->validate) validate(t);
    t->active=false;++t->ends;
}
static void delay(void *context,uint32_t us) { Trace *t=context;assert(us==1 || us==10);t->us+=us; }
static void rf_pins(void *context,uint32_t value) { Trace *t=context;(void)value;++t->rf; }
static void rf_delay(void *context,uint32_t us) { (void)context;(void)us; }
static uint32_t no_adc_read(void *context,uint32_t address) { (void)context;(void)address;assert(false);return 0; }
static void no_adc_write(void *context,uint32_t address,uint32_t value) { (void)context;(void)address;(void)value;assert(false); }
static OledBus bus(Trace *t) { OledBus b={begin,pins,end,delay,t};return b; }
static void test_mapping(void) {
    bool seen[256]={0};
    for(uint32_t i=0;i<24;++i) {
        uint8_t p[4];assert(oled_mapping(i,p));uint32_t mask=0,key=0;
        for(uint32_t n=0;n<4;++n) { assert(p[n]>=4 && p[n]<=7);mask|=1u<<p[n];key=(key<<2)|(uint32_t)(p[n]-4u); }
        assert(mask==OLED_MASK && !seen[key]);seen[key]=true;
        for(uint32_t group=1;group<4;++group) { uint8_t q[4];assert(oled_mapping(i+group*24,q));assert(!memcmp(p,q,4)); }
    }
    uint8_t p[4];assert(!oled_mapping(96,p) && !oled_mapping(UINT32_MAX,p));
}
static void test_sweep(void) {
    Trace t={0};t.validate=true;Oled p;oled_init(&p,bus(&t));char out[16];
    assert(!oled_poll(&p,0,out,true) && !t.begins);
    uint32_t start=UINT32_MAX-10000u;assert(oled_start(&p,start));assert(!oled_start(&p,start));
    uint32_t announces=0,done=0,calls=0;
    while(p.active) {
        uint64_t before=t.us;t.index=p.index;
        size_t n=oled_poll(&p,start+(uint32_t)(t.us/1000u),out,true);
        assert(t.us-before<=25); /* at most one SPI byte or20us reset pulse */
        if(n) {
            assert(n==strlen(out) && n<16);
            if(!strcmp(out,"oled done\r\n")) ++done;
            else {
                char expected[16];snprintf(expected,sizeof(expected),"oled %02u %c %c\r\n",announces,announces<48?'S':'H',(announces/24)%2?'R':'C');
                assert(!strcmp(expected,out));++announces;
            }
        }
        t.us+=t.us==before ? 1000u : 1u;assert(++calls<500000);
    }
    assert(announces==96 && done==1 && t.begins==96 && t.ends==96);
    assert(t.us>=192000000u && t.us<210000000u && !t.rf);
    uint64_t old=t.us;assert(!oled_poll(&p,0,out,true) && t.us==old);
    t.index=0;assert(oled_start(&p,10));oled_poll(&p,10,out,true);oled_poll(&p,10,out,true);
    t.validate=false;assert(oled_poll(&p,10+OLED_LIMIT_MS,out,true)==14 && !strcmp(out,"oled timeout\r\n"));assert(!p.active && !t.active);
    assert(oled_start(&p,0));
    assert(!oled_poll(&p,0,out,false) && !t.active); /* no index space: no outputs */
    assert(!oled_poll(&p,OLED_LIMIT_MS,out,false) && !p.active); /* hard bound despite TX backpressure */
}
static void test_uart_cancel(void) {
    Trace t={0};Oled p;oled_init(&p,bus(&t));Controller c;Bus rf={rf_pins,rf_delay,&t};controller_boot(&c,rf);
    AdcIo adc={no_adc_read,no_adc_write,rf_delay,NULL};Line line={0};TxQueue q={0};char out[16];
    const char *command="oledprobe\n";
    while(*command) { size_t n=probe_console_feed(&line,&c,&adc,&p,0,*command++,false,out);assert(tx_push(&q,out,n)); }
    assert(p.active && c.qdb==0 && c.issued==1 && !t.begins);
    uint32_t rf_before=t.rf;const char *set="set 1.25\n";uint64_t rx_at=100,tx_at=0;char wire[64];uint32_t used=0;
    while(*set || q.used || p.active) {
        if(*set && t.us>=rx_at) {
            assert(t.us-rx_at<86);size_t n=probe_console_feed(&line,&c,&adc,&p,(uint32_t)(t.us/1000u),*set++,false,out);
            assert(tx_push(&q,out,n));rx_at+=87;
            if(!*set) assert(!p.active && !t.active && c.qdb==5 && c.issued==2);
        }
        if(q.used<=48) { size_t n=oled_poll(&p,(uint32_t)(t.us/1000u),out,true);assert(tx_push(&q,out,n)); }
        if(t.us>=tx_at) { char ch;if(tx_pop(&q,&ch)) { assert(used<sizeof(wire)-1);wire[used++]=ch;tx_at=t.us+87; } }
        ++t.us;assert(t.us<10000);
    }
    wire[used]=0;assert(!strcmp(wire,"ok oledprobe\r\noled 00 S C\r\nok 1.25\r\n"));assert(t.rf>rf_before);
    uint32_t count=t.begins;uint64_t old=t.us;assert(!oled_poll(&p,1000,out,true) && t.begins==count && t.us==old);
    command="oledprobe extra\n";
    while(*command) (void)probe_console_feed(&line,&c,&adc,&p,0,*command++,false,out);
    assert(!p.active && !strcmp(out,"err\r\n"));
}
static void test_queue(void) {
    TxQueue q={0};char data[64],ch;for(uint32_t i=0;i<64;++i)data[i]=(char)i;
    assert(!tx_pop(&q,&ch));
    for(uint32_t round=0;round<100;++round) {
        assert(tx_push(&q,data,64) && !tx_push(&q,"x",1));
        for(uint32_t i=0;i<64;++i)assert(tx_pop(&q,&ch) && ch==data[i]);
        assert(!q.used && !tx_pop(&q,&ch));
    }
}
static void test_cancel_each_mapping(void) {
    for(uint32_t index=0;index<96;++index) {
        Trace t={0};t.index=index;Oled p;oled_init(&p,bus(&t));
        Controller c;Bus rf={rf_pins,rf_delay,&t};controller_boot(&c,rf);
        AdcIo adc={no_adc_read,no_adc_write,rf_delay,NULL};Line line={0};char out[16];
        assert(oled_start(&p,0));p.index=index;
        assert(oled_poll(&p,0,out,true)==13);(void)oled_poll(&p,0,out,true);
        for(uint32_t i=0;i<10;++i)(void)oled_poll(&p,0,out,true);
        const char *busy="oledprobe\n";
        while(*busy)(void)probe_console_feed(&line,&c,&adc,&p,0,*busy++,false,out);
        assert(!strcmp(out,"err busy\r\n") && p.index==index && p.active);
        const char *set="set 0\n";
        while(*set)(void)probe_console_feed(&line,&c,&adc,&p,0,*set++,false,out);
        assert(!strcmp(out,"ok 0.00\r\n") && c.qdb==0 && c.issued==2 && !p.active && !t.active && t.ends==1);
        uint32_t count=t.count;assert(!oled_poll(&p,100,out,true) && t.count==count);
    }
    Trace t={0};Oled p;oled_init(&p,bus(&t));char out[16];
    assert(oled_start(&p,0));(void)oled_poll(&p,0,out,true);(void)oled_poll(&p,0,out,true);
    assert(t.active);assert(!oled_poll(&p,OLED_LIMIT_MS,out,false) && !t.active && !p.active);
}
int main(void) {
    test_mapping();test_sweep();test_uart_cancel();test_queue();test_cancel_each_mapping();
    puts("OLED: all96 trials, actualSPI bytes/timing/white+pattern, wrap/deadline, inertboot, boundedUART queue and SET cancellation passed");return 0;
}

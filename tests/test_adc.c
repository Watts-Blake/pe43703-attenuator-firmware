#include "adc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { CFGR=0x40021004, RSTR=0x4002100c, ENR=0x40021018, GPIO=0x40010800,
       SR=0x40012400, CR2=0x40012408, SMPR2=0x40012410, SQR3=0x40012434, DR=0x4001244c };
enum { READY, RESET_STUCK, CAL_STUCK, EOC_STUCK };
typedef struct {
    uint32_t cfg, resets, enables, gpio, sr, cr2, smpr2, sqr3;
    uint32_t time, powered, triggered, reset_at, cal_at, sample, conversions;
    uint32_t accesses, writes, dr_reads, pe_events, failure;
    bool calibrated, cleared;
} Model;
#define ADC_BIT (1u << 9)
#define ENABLES ((1u << 2) | (1u << 3) | (1u << 14))
#define GPIO_INITIAL UINT32_C(0x44444444)
static void init(Model *m) {
    memset(m,0,sizeof(*m)); m->cfg=2u<<14; m->enables=ENABLES; m->gpio=GPIO_INITIAL;
}
static uint32_t read_reg(void *context, uint32_t address) {
    Model *m=context; ++m->accesses;
    if (address==CFGR) return m->cfg;
    if (address==RSTR) return m->resets;
    if (address==ENR) return m->enables;
    if (address==GPIO) return m->gpio;
    assert(m->enables & ADC_BIT); assert(!(m->resets & ADC_BIT));
    if (address==CR2) {
        if ((m->cr2 & 8u) && m->time-m->reset_at>=3 && m->failure!=RESET_STUCK) m->cr2 &= ~8u;
        if ((m->cr2 & 4u) && m->time-m->cal_at>=5 && m->failure!=CAL_STUCK) {
            m->cr2 &= ~4u; m->calibrated=true;
        }
        return m->cr2;
    }
    if (address==SR) {
        if (m->conversions && m->time-m->triggered>=21 && m->failure!=EOC_STUCK) m->sr |= 2u;
        return m->sr;
    }
    if (address==DR) {
        ++m->dr_reads;
        if (!m->conversions) { assert(m->calibrated); return 0x1234; }
        assert(m->sr & 2u); m->sr &= ~2u; return m->sample;
    }
    assert(false && "unexpected register read"); return 0;
}
static void write_reg(void *context, uint32_t address, uint32_t value) {
    Model *m=context; ++m->accesses; ++m->writes;
    if (address==ENR) { assert((value & ~ADC_BIT)==ENABLES); m->enables=value; return; }
    if (address==RSTR) {
        assert(!(value & ~ADC_BIT)); m->resets=value;
        if (value & ADC_BIT) { m->cr2=0; m->sr=0; m->smpr2=0; m->sqr3=0; }
        return;
    }
    if (address==GPIO) {
        assert((value & ~0xf0u)==(GPIO_INITIAL & ~0xf0u));
        assert(value==GPIO_INITIAL || value==(GPIO_INITIAL & ~0xf0u));
        m->gpio=value; return;
    }
    assert(m->enables & ADC_BIT); assert(!(m->resets & ADC_BIT));
    if (address==SMPR2) { assert(value==(7u<<3)); m->smpr2=value; return; }
    if (address==SQR3) { assert(value==1); m->sqr3=value; return; }
    if (address==SR) { assert(value==0); assert(m->dr_reads==1); m->sr=0; m->cleared=true; return; }
    if (address==CR2) {
        assert(value==0 || value==1 || value==9 || value==5);
        if (value==9) { assert(m->cr2==1 && m->time-m->powered>=2); m->reset_at=m->time; }
        if (value==5) { assert(m->cr2==1 && m->time-m->reset_at>=3); m->cal_at=m->time; }
        if (value==1) {
            assert(m->gpio==(GPIO_INITIAL & ~0xf0u) && m->smpr2==56 && m->sqr3==1);
            if (!m->cr2) m->powered=m->time;
            else {
                assert(m->cr2==1 && m->calibrated && m->cleared && m->dr_reads==1);
                m->triggered=m->time; ++m->conversions;
            }
        }
        m->cr2=value; return;
    }
    assert(false && "unexpected register write");
}
static void delay(void *context, uint32_t us) {
    Model *m=context; assert(us==1 || us==2); m->time+=us;
}
static void pe_pins(void *context, uint32_t mask) { Model *m=context; (void)mask; ++m->pe_events; }
static void pe_delay(void *context, uint32_t us) { (void)context; (void)us; }
static AdcIo io(Model *m) { AdcIo result={read_reg,write_reg,delay,m}; return result; }
static void clean(const Model *m) {
    assert(m->enables==ENABLES && m->resets==0 && m->gpio==GPIO_INITIAL);
    assert(m->cr2==0 && m->sr==0 && m->smpr2==0 && m->sqr3==0);
    assert(m->time<=302); /* <=100 one-us waits per phase plus startup */
}
static void feed(Line *l, Controller *c, Model *m, const char *input, const char *expected) {
    AdcIo bus=io(m); char out[16]={0}; size_t n=0;
    while (*input) { size_t next=console_feed(l,c,&bus,*input++,false,out); if (next) n=next; }
    assert(n==strlen(expected) && strcmp(out,expected)==0); assert(!l->adc);
}
static void test_values(void) {
    for (uint32_t sample=0; sample<=4095; ++sample) {
        Model m; init(&m); m.sample=sample; AdcIo bus=io(&m); uint32_t got=9999;
        assert(adc_sample(&bus,&got) && got==sample);
        assert(m.conversions==1 && m.dr_reads==2 && m.time==31); clean(&m);
    }
}
static void test_console(void) {
    Model m; init(&m); Controller c; Bus bus={pe_pins,pe_delay,&m}; controller_boot(&c,bus);
    assert(c.qdb==0 && c.issued==1 && m.accesses==0);
    Line l={0}; feed(&l,&c,&m,"get\r\n","0.00\r\n");
    feed(&l,&c,&m,"version\n","version 0.1.1\r\n"); assert(m.accesses==0 && c.issued==1);
    feed(&l,&c,&m,"set 20\n","ok 20.00\r\n"); assert(m.accesses==0);
    uint32_t events=m.pe_events, issued=c.issued;
    const char *bad[]={"ADC\n","adc \n","adc 1\n","diag adc 1\n","diag\n",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxadc\n","adc\001\n"};
    for (size_t i=0; i<sizeof(bad)/sizeof(*bad); ++i) feed(&l,&c,&m,bad[i],"err\r\n");
    assert(m.accesses==0 && m.pe_events==events && c.issued==issued);
    AdcIo adc=io(&m); char out[16]; assert(!console_feed(&l,&c,&adc,'a',true,out));
    feed(&l,&c,&m,"dc\n","err\r\n"); assert(m.accesses==0);
    m.sample=4095; feed(&l,&c,&m,"adc\r\n","adc 4095\r\n"); clean(&m);
    assert(m.conversions==1 && c.qdb==80 && c.issued==issued && m.pe_events==events);
    uint32_t count=m.accesses; feed(&l,&c,&m,"get\n","20.00\r\n"); assert(m.accesses==count);
    init(&m); m.sample=7; feed(&l,&c,&m,"diag adc\n","adc 0007\r\n"); clean(&m);
    feed(&l,&c,&m,"set 0\n","ok 0.00\r\n"); assert(c.qdb==0 && c.issued==issued+1);
    count=m.accesses; feed(&l,&c,&m,"bootloader\n","ok bootloader\r\n"); assert(l.bootloader && m.accesses==count);
}
static void test_failures(void) {
    for (uint32_t failure=RESET_STUCK; failure<=EOC_STUCK; ++failure) {
        Model m; init(&m); m.failure=failure;
        Controller c; Bus bus={pe_pins,pe_delay,&m}; controller_boot(&c,bus); Line l={0};
        uint32_t events=m.pe_events;
        feed(&l,&c,&m,"adc\n","err adc\r\n"); clean(&m);
        assert(m.time>=102 && m.pe_events==events && c.issued==1 && c.qdb==0);
        feed(&l,&c,&m,"set 1.25\n","ok 1.25\r\n"); assert(c.issued==2 && c.qdb==5);
        init(&m); m.sample=0; feed(&l,&c,&m,"adc\n","adc 0000\r\n"); clean(&m);
        assert(c.issued==2 && c.qdb==5);
    }
    for (uint32_t fault=0; fault<4; ++fault) {
        Model m; init(&m);
        if (fault==0) m.enables |= ADC_BIT;
        if (fault==1) m.enables &= ~(1u<<2);
        if (fault==2) m.resets=ADC_BIT;
        if (fault==3) m.cfg=0;
        AdcIo bus=io(&m); uint32_t result=123;
        assert(!adc_sample(&bus,&result) && result==123 && m.writes==0);
    }
}
int main(void) {
    test_values(); test_console(); test_failures();
    puts("ADC: actual single-shot register sequence, all4096 values, inactive default, bounded faults, UART recovery and unchanged RF state passed");
    return 0;
}

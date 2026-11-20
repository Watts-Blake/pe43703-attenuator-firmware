#include "button_cal.h"
static const char codes[]="LPMRO";
static const char *const prompts[BUTTON_COUNT]={"HOLD LEFT","HOLD +","HOLD -","HOLD RIGHT","HOLD OK"};
static bool equal(const char *a,const char *b) {
    while (*a && *a==*b) { ++a; ++b; } return *a==*b;
}
static bool prefix(const char *a,const char *b) { while (*b) if (*a++!=*b++) return false; return true; }
static size_t reply(char out[16],const char *s) {
    size_t n=0; while (*s && n<15u) out[n++]=*s++; out[n]=0; return n;
}
static size_t value_reply(char out[16],uint32_t i,uint32_t value) {
    size_t n=reply(out,"value L 0000\r\n"); out[6]=codes[i];
    for (uint32_t j=0,divisor=1000;j<4;++j,divisor/=10)
        out[8u+j]=(char)('0'+(value/divisor)%10u);
    return n;
}
static void show(ButtonCal *b,Screen *s) {
    const char *prompt=b->phase==CAL_HOLD || b->phase==CAL_CAPTURE ? prompts[b->index] :
        b->phase==CAL_VALUE ? "CAPTURED" : b->phase==CAL_RELEASE ? "RELEASE ALL" :
        b->phase==CAL_SAVING ? "SAVING" : "HOLD OK TO SAVE";
    screen_calibration(s,prompt,b->index<BUTTON_COUNT ? codes[b->index] : ' ',
                       b->index<BUTTON_COUNT ? b->values[b->index] : 0,
                       b->phase==CAL_VALUE,
                       b->phase==CAL_CONFIRM || b->phase==CAL_HELD ? "1 SECOND THEN RELEASE" : "RELEASE BETWEEN KEYS");
}
void button_cal_init(ButtonCal *b) {
    b->active=false; b->sampled=false; b->confirming=false; b->phase=CAL_RELEASE;
    b->result=CAL_IDLE; b->index=0; b->count=0; b->started=0; b->touched=0;
    b->phase_at=0; b->last_sample=0; b->stable_at=0;
    for (uint32_t i=0;i<BUTTON_COUNT;++i) b->values[i]=0;
}
void button_cal_cancel(ButtonCal *b,DisplayConfig *c,Screen *s) {
    if (b->active) { config_cancel(c); screen_config(s,c->index); }
    b->active=false; b->result=CAL_IDLE;
}
void button_cal_error(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t result) {
    button_cal_cancel(b,c,s); b->result=result;
}
void button_cal_expire(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t now) {
    if (!b->active) return;
    if ((uint32_t)(now-b->started)>=BUTTON_SESSION_MS || (uint32_t)(now-b->touched)>=BUTTON_IDLE_MS)
        button_cal_error(b,c,s,CAL_TIMEOUT);
    else if (b->phase==CAL_CAPTURE && (uint32_t)(now-b->phase_at)>=BUTTON_CAPTURE_MS)
        button_cal_error(b,c,s,CAL_HOLD_ERROR);
}
static bool save(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t now) {
    if (!config_save_buttons(c,b->values)) { button_cal_error(b,c,s,CAL_SAVE_ERROR); return false; }
    b->phase=CAL_SAVING; b->touched=now; show(b,s); return true;
}
void button_cal_poll(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t now) {
    button_cal_expire(b,c,s,now);
    if (b->active && b->phase==CAL_SAVING) {
        config_poll(c);
        if (!c->writing) button_cal_error(b,c,s,c->fault ? CAL_SAVE_ERROR : CAL_SAVED);
    }
}
/* Release and hold counts require contiguous 10ms observations. Host status
   polling never refreshes a firmware deadline and cannot produce a sample. */
void button_cal_sample(ButtonCal *b,DisplayConfig *c,Screen *s,uint32_t raw,uint32_t now) {
    button_cal_expire(b,c,s,now);
    if (!b->active || b->phase==CAL_SAVING) return;
    if (raw>4095) { button_cal_error(b,c,s,CAL_SAMPLE); return; }
    if (b->sampled) {
        uint32_t gap=now-b->last_sample;
        if (gap<10u) return;
        if (gap>30u) {
            if (b->phase==CAL_CAPTURE) { button_cal_error(b,c,s,CAL_SAMPLE); return; }
            b->count=0; b->confirming=false;
            if (b->phase==CAL_HELD) b->phase=CAL_CONFIRM;
        }
    }
    b->sampled=true; b->last_sample=now;
    if (b->phase==CAL_RELEASE || b->phase==CAL_VALUE) {
        if (raw>BUTTON_RELEASE_MAX) {
            b->count=0;
            if (b->index==BUTTON_COUNT) { b->phase=CAL_CONFIRM; b->confirming=false; }
            return;
        }
        if (!b->count) b->stable_at=now;
        ++b->count;
        if (b->count<11 || (uint32_t)(now-b->stable_at)<100u) return;
        if (b->index==BUTTON_COUNT && b->confirming) { (void)save(b,c,s,now); return; }
        if (b->phase==CAL_VALUE) ++b->index;
        b->count=0; b->touched=now;
        b->phase=b->index==BUTTON_COUNT ? CAL_CONFIRM : CAL_HOLD; show(b,s); return;
    }
    if (b->phase==CAL_CONFIRM || b->phase==CAL_HELD) {
        uint32_t ok=b->values[4];
        if (raw>BUTTON_RELEASE_MAX && raw+BUTTON_RADIUS>=ok && raw<=ok+BUTTON_RADIUS) {
            if (!b->count) b->stable_at=now;
            ++b->count;
            if ((uint32_t)(now-b->stable_at)>=1000u) { b->phase=CAL_HELD; b->confirming=true; }
        } else if (raw<=BUTTON_RELEASE_MAX && b->confirming) {
            /* First release sample begins a separate stable release interval. */
            b->phase=CAL_RELEASE; b->count=1; b->stable_at=now;
        } else { b->phase=CAL_CONFIRM; b->count=0; b->confirming=false; }
        return;
    }
    if (b->phase==CAL_HOLD) {
        if (raw<=BUTTON_RELEASE_MAX) return;
        b->phase=CAL_CAPTURE; b->phase_at=now; b->count=0;
    }
    if (b->phase!=CAL_CAPTURE) return;
    if (raw<=BUTTON_RELEASE_MAX) { button_cal_error(b,c,s,CAL_HOLD_ERROR); return; }
    b->samples[b->count++]=(uint16_t)raw;
    if (b->count<BUTTON_SAMPLES) return;
    for (uint32_t i=1;i<BUTTON_SAMPLES;++i) {
        uint16_t v=b->samples[i]; uint32_t j=i;
        while (j && b->samples[j-1u]>v) { b->samples[j]=b->samples[j-1u]; --j; }
        b->samples[j]=v;
    }
    if ((uint32_t)(now-b->phase_at)<300u ||
        (uint32_t)b->samples[BUTTON_SAMPLES-1u]-b->samples[0]>24u) {
        button_cal_error(b,c,s,CAL_NOISE); return;
    }
    b->values[b->index]=b->samples[BUTTON_SAMPLES/2u];
    /* Validate each partial set, but never publish a partial record. */
    uint32_t value=b->values[b->index];
    bool valid=value>=BUTTON_RELEASE_MAX+BUTTON_GAP+BUTTON_RADIUS+1u;
    for (uint32_t i=0;i<b->index;++i) {
        uint32_t previous=b->values[i];
        uint32_t distance=value>previous ? value-previous : previous-value;
        if (distance<=2u*BUTTON_RADIUS+BUTTON_GAP) valid=false;
    }
    if (!valid || (b->index+1u==BUTTON_COUNT && !config_buttons_valid(b->values))) {
        button_cal_error(b,c,s,CAL_BANDS); return;
    }
    b->phase=CAL_VALUE; b->count=0; b->touched=now; show(b,s);
}
size_t button_cal_command(ButtonCal *b,DisplayConfig *c,Screen *s,const char *command,
                          uint32_t now,bool busy,char out[16]) {
    if (equal(command,"buttons")) return reply(out,"buttons 1\r\n");
    if (!prefix(command,"buttons ")) return 0;
    const char *op=command+8;
    if (equal(op,"get")) return reply(out,c->calibrated ? "buttons saved\r\n" : "buttons none\r\n");
    if (equal(op,"status")) {
        if (b->active) {
            if (b->phase==CAL_SAVING) return reply(out,"saving\r\n");
            if (b->phase==CAL_CONFIRM)
                return reply(out,"confirm\r\n");
            if (b->phase==CAL_HELD || b->index==BUTTON_COUNT) return reply(out,"release O\r\n");
            if (b->phase==CAL_VALUE) return value_reply(out,b->index,b->values[b->index]);
            size_t n=reply(out,b->phase==CAL_RELEASE ? "release L\r\n" : "hold L\r\n");
            out[b->phase==CAL_RELEASE ? 8 : 5]=codes[b->index]; return n;
        }
        static const char *const results[]={"idle\r\n","saved\r\n","err timeout\r\n","err noise\r\n",
            "err bands\r\n","err sample\r\n","err save\r\n","err hold\r\n"};
        return reply(out,results[b->result]);
    }
    bool captured=prefix(op,"value "), stored=prefix(op,"stored ");
    if (captured || stored) {
        const char *key=op+(captured ? 6 : 7);
        if (key[0] && !key[1]) for (uint32_t i=0;i<BUTTON_COUNT;++i) if (key[0]==codes[i]) {
            if (stored && c->calibrated) return value_reply(out,i,c->buttons[i]);
            if (captured && b->active && (i<b->index || (i==b->index && b->phase==CAL_VALUE)))
                return value_reply(out,i,b->values[i]);
        }
        if (stored) return reply(out,"err config\r\n");
    }
    if (equal(op,"cancel")) { button_cal_cancel(b,c,s); return reply(out,"ok cancel\r\n"); }
    if (equal(op,"start")) {
        if (busy || b->active) return reply(out,"err busy\r\n");
        button_cal_init(b); b->active=true; b->started=now; b->touched=now;
        show(b,s); return reply(out,"ok buttons\r\n");
    }
    if (equal(op,"save") && b->active && b->index==BUTTON_COUNT && b->phase==CAL_CONFIRM)
        return reply(out,save(b,c,s,now) ? "ok saving\r\n" : "err save\r\n");
    button_cal_cancel(b,c,s); return reply(out,"err buttons\r\n");
}

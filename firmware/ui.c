#include "ui.h"
static const uint32_t steps[3]={1,4,20};
static size_t reply(char out[16], const char *s) {
    size_t n=0; while (*s) out[n++]=*s++; out[n]=0; return n;
}
static bool equal(const char *a, const char *b) {
    while (*a && *a==*b) { ++a; ++b; } return *a==*b;
}
static bool token_after(const char *s, const char *prefix, uint32_t *token) {
    while (*prefix) if (*s++!=*prefix++) return false;
    uint32_t value=0;
    for (uint32_t i=0;i<8;++i) {
        char ch=*s++; uint32_t digit;
        if (ch>='0' && ch<='9') digit=(uint32_t)(ch-'0');
        else if (ch>='a' && ch<='f') digit=10u+(uint32_t)(ch-'a');
        else return false;
        value=(value<<4)|digit;
    }
    if (*s || !value) return false;
    *token=value; return true;
}
static void reset_buttons(Ui *u) {
    u->used=0; u->next=0; u->candidate=KEY_UNKNOWN; u->candidate_at=0;
    u->stable=KEY_UNKNOWN; u->last_sample=0; u->armed=false;
}
void ui_init(Ui *u, Controller *c, OledBus bus) {
    screen_init(&u->screen,bus); u->sampler.phase=0;
    u->pending=c->qdb; u->step_index=0; u->seen=c->issued;
    u->locked=false; u->token=0; u->lease_at=0; u->sample_at_us=0;
    reset_buttons(u); u->dirty=true;
}
void ui_sync(Ui *u, Controller *c) {
    if (u->seen==c->issued) return;
    u->seen=c->issued; u->pending=c->qdb; reset_buttons(u);
    screen_stop_test(&u->screen); u->dirty=true;
}
void ui_expire(Ui *u, Controller *c, uint32_t now_ms) {
    if (u->locked && (uint32_t)(now_ms-u->lease_at)>=PROFILE_LEASE_MS) {
        u->locked=false; u->token=0; u->pending=c->qdb; reset_buttons(u); u->dirty=true;
    }
}
uint32_t button_code(uint32_t raw) {
    if (raw<=32) return KEY_RELEASED;
    if (raw>=780 && raw<=850) return KEY_RIGHT;
    if (raw>=970 && raw<=1060) return KEY_MINUS;
    if (raw>=1310 && raw<=1410) return KEY_OK;
    if (raw>=1980 && raw<=2110) return KEY_PLUS;
    if (raw>=4050 && raw<=4095) return KEY_LEFT;
    return KEY_UNKNOWN;
}
void ui_button_sample(Ui *u, Controller *c, uint32_t raw, uint32_t now_ms) {
    ui_expire(u,c,now_ms); ui_sync(u,c);
    if (u->locked || u->screen.testing) { reset_buttons(u); return; }
    if (u->used && (uint32_t)(now_ms-u->last_sample)>20u) reset_buttons(u);
    u->last_sample=now_ms;
    u->samples[u->next]=raw; u->next=(u->next+1u)%5u;
    if (u->used<5) ++u->used;
    if (u->used<5) return;
    uint32_t sorted[5];
    for (uint32_t i=0;i<5;++i) sorted[i]=u->samples[i];
    for (uint32_t i=1;i<5;++i) {
        uint32_t v=sorted[i],j=i;
        while (j && sorted[j-1u]>v) { sorted[j]=sorted[j-1u]; --j; } sorted[j]=v;
    }
    uint32_t key=button_code(sorted[2]);
    if (key!=u->candidate) { u->candidate=key; u->candidate_at=now_ms; return; }
    if ((uint32_t)(now_ms-u->candidate_at)<30u || key==KEY_UNKNOWN || key==u->stable) return;
    u->stable=key;
    if (key==KEY_RELEASED) { u->armed=true; return; }
    if (!u->armed) return;
    u->armed=false;
    uint32_t step=steps[u->step_index];
    if (key==KEY_PLUS) u->pending=u->pending+step>127u ? 127u : u->pending+step;
    else if (key==KEY_MINUS) u->pending=u->pending<step ? 0 : u->pending-step;
    else if (key==KEY_LEFT) u->step_index=(u->step_index+2u)%3u;
    else if (key==KEY_RIGHT) u->step_index=(u->step_index+1u)%3u;
    else if (key==KEY_OK) { (void)controller_set_from(c,u->pending,SOURCE_LOCAL); ui_sync(u,c); }
    u->dirty=true;
}
size_t ui_console_feed(Ui *u, Line *line, Controller *c, const AdcIo *adc,
                       Oled *probe, uint32_t now_ms, char ch, bool rx_error, char out[16]) {
    ui_expire(u,c,now_ms); ui_sync(u,c);
    if ((ch=='\r' || ch=='\n') && line->used && !line->discard && !rx_error) {
        line->data[line->used]=0; const char *s=line->data; uint32_t token;
        const char *answer=0;
        if (equal(s,"profile")) answer="profile 1\r\n";
        else if (token_after(s,"profile ",&token)) {
            if (u->locked) answer="err busy\r\n";
            else {
                u->locked=true; u->token=token; u->lease_at=now_ms;
                u->pending=c->qdb; reset_buttons(u); u->dirty=true; answer="ok profile\r\n";
            }
        } else if (token_after(s,"profile keep ",&token)) {
            if (!u->locked || u->token!=token) answer="err lock\r\n";
            else { u->lease_at=now_ms; answer="ok keep\r\n"; }
        } else if (token_after(s,"profile end ",&token)) {
            if (!u->locked || u->token!=token) answer="err lock\r\n";
            else {
                u->locked=false; u->token=0; u->pending=c->qdb; reset_buttons(u);
                u->dirty=true; answer="ok end\r\n";
            }
        } else if (equal(s,"oledtest")) {
            if (probe->active || u->screen.testing) answer="err busy\r\n";
            else { reset_buttons(u); screen_test(&u->screen,now_ms); answer="ok oledtest\r\n"; }
        }
        if (answer) { line->used=0; return reply(out,answer); }
        /* Raw diagnostics serialize with the automatic ADC sampler. */
        if (equal(s,"adc") || equal(s,"diag adc") || equal(s,"bootloader")) adc_cancel(&u->sampler,adc);
        if (equal(s,"oledprobe")) { screen_stop_test(&u->screen); screen_suspend(&u->screen); reset_buttons(u); }
    }
    size_t n=probe_console_feed(line,c,adc,probe,now_ms,ch,rx_error,out);
    ui_sync(u,c);
    if (line->bootloader) { adc_cancel(&u->sampler,adc); screen_suspend(&u->screen); }
    return n;
}
size_t ui_poll(Ui *u, Controller *c, const AdcIo *adc, Oled *probe,
               uint32_t now_ms, uint32_t now_us, char out[16], bool can_report) {
    ui_expire(u,c,now_ms); ui_sync(u,c);
    if (probe->active) { adc_cancel(&u->sampler,adc); reset_buttons(u); return 0; }
    uint32_t raw;
    int result=adc_poll(&u->sampler,adc,now_us,&raw);
    if (result>0) ui_button_sample(u,c,raw,now_ms);
    else if (result<0) reset_buttons(u);
    if (!u->sampler.phase && (uint32_t)(now_us-u->sample_at_us)>=5000u) {
        u->sample_at_us=now_us;
        if (!adc_begin(&u->sampler,adc,now_us)) reset_buttons(u);
    }
    if (u->dirty) {
        screen_show(&u->screen,c->qdb,u->pending,steps[u->step_index],c->source,u->locked);
        u->dirty=false;
    }
    screen_poll(&u->screen,now_ms);
    if (u->screen.done && can_report) { u->screen.done=false; return reply(out,"oledtest done\r\n"); }
    return 0;
}

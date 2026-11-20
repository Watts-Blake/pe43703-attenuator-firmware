#include "display_id.h"
enum { ID_IDLE, ID_SAVED, ID_ERROR, ID_TIMEOUT };
static bool equal(const char *a, const char *b) {
    while (*a && *a==*b) { ++a; ++b; } return *a==*b;
}
static bool prefix(const char *s, const char *p) {
    while (*p) if (*s++!=*p++) return false;
    return true;
}
static size_t reply(char out[16], const char *s) {
    size_t n=0; while (*s) out[n++]=*s++; out[n]=0; return n;
}
static void number(char *p, uint32_t n) { p[0]=(char)('0'+n/10u); p[1]=(char)('0'+n%10u); }
static bool index_after(const char *s, const char *p, uint32_t *index) {
    if (!prefix(s,p)) return false;
    while (*p) { ++p; ++s; }
    if (s[0]<'0' || s[0]>'9' || s[1]<'0' || s[1]>'9' || s[2]) return false;
    *index=(uint32_t)(s[0]-'0')*10u+(uint32_t)(s[1]-'0'); return *index<96;
}
static bool ready(const DisplayId *d, const Screen *s) {
    return s->identifying && s->index==d->trial && s->phase==4 && !s->dirty;
}
void display_id_init(DisplayId *d, Screen *s, ConfigIo io) {
    config_init(&d->config,io); d->active=false; d->sweep=false; d->saving=false;
    d->trial=d->config.index; d->started=0; d->touched=0; d->result=ID_IDLE;
    screen_config(s,d->config.index);
}
void display_id_cancel(DisplayId *d, Screen *s) {
    if (d->active) config_cancel(&d->config);
    if (d->active) screen_config(s,d->config.index);
    d->active=false; d->saving=false; d->result=ID_IDLE;
}
void display_id_expire(DisplayId *d, Screen *s, uint32_t now_ms) {
    if (!d->active) return;
    if ((uint32_t)(now_ms-d->started)>=DISPLAY_SESSION_MS ||
        (uint32_t)(now_ms-d->touched)>=DISPLAY_IDLE_MS) {
        display_id_cancel(d,s); d->result=ID_TIMEOUT; return;
    }
}
void display_id_poll(DisplayId *d, Screen *s, uint32_t now_ms) {
    display_id_expire(d,s,now_ms);
    if (!d->active) return;
    if (d->saving) {
        config_poll(&d->config);
        if (!d->config.writing) {
            bool fault=d->config.fault;
            display_id_cancel(d,s); d->result=fault ? ID_ERROR : ID_SAVED;
        }
    }
}
void display_id_next(DisplayId *d, Screen *s, uint32_t now_ms) {
    if (!d->active || d->saving || !ready(d,s)) return;
    d->trial=d->sweep ? (d->trial+1u)%96u : (d->trial+48u)%96u;
    d->touched=now_ms; screen_identify(s,d->trial);
}
size_t display_id_command(DisplayId *d, Screen *s, const char *command,
                          uint32_t now_ms, bool busy, char out[16]) {
    if (equal(command,"display")) return reply(out,"display 1\r\n");
    if (!prefix(command,"display ")) return 0;
    const char *op=command+8;
    if (equal(op,"get")) {
        size_t n=reply(out,"config 00 00\r\n"); number(out+7,d->config.index);
        number(out+10,config_remaining(&d->config)); return n;
    }
    if (equal(op,"status")) {
        if (d->saving) return reply(out,"saving\r\n");
        if (d->active) {
            size_t n=reply(out,"trial 00 W\r\n"); number(out+6,d->trial);
            if (ready(d,s)) out[9]='R';
            return n;
        }
        if (d->result==ID_SAVED) {
            size_t n=reply(out,"saved 00\r\n"); number(out+6,d->config.index); return n;
        }
        return reply(out,d->result==ID_ERROR ? "err save\r\n" :
            d->result==ID_TIMEOUT ? "err timeout\r\n" : "idle\r\n");
    }
    if (equal(op,"cancel") || equal(op,"finish")) {
        display_id_cancel(d,s);
        return reply(out,equal(op,"cancel") ? "ok cancel\r\n" : "ok finish\r\n");
    }
    if (equal(op,"start") || equal(op,"sweep")) {
        if (busy || d->active) return reply(out,"err busy\r\n");
        d->active=true; d->sweep=equal(op,"sweep"); d->saving=false; d->result=ID_IDLE;
        d->started=now_ms; d->touched=now_ms;
        d->trial=d->sweep ? 0 : d->config.index%48u;
        screen_identify(s,d->trial); return reply(out,"ok display\r\n");
    }
    if (d->active && !d->saving && equal(op,"next") && ready(d,s)) {
        display_id_next(d,s,now_ms); return reply(out,"ok next\r\n");
    }
    uint32_t index;
    if (d->active && !d->saving && index_after(op,"try ",&index) &&
        (d->sweep || index%48u==d->config.index%48u)) {
        d->trial=index; d->touched=now_ms; screen_identify(s,index); return reply(out,"ok try\r\n");
    }
    if (d->active && !d->saving && ready(d,s) && index_after(op,"save ",&index) && index==d->trial) {
        if (config_save(&d->config,index)) {
            d->saving=true; d->touched=now_ms; return reply(out,"ok saving\r\n");
        }
        display_id_cancel(d,s); return reply(out,"err save\r\n");
    }
    display_id_cancel(d,s); return reply(out,"err display\r\n");
}

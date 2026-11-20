#include "display_config.h"
static uint32_t crc(const uint8_t *p, uint32_t length) {
    uint32_t value=UINT32_MAX;
    for (uint32_t i=0;i<length;++i) {
        value^=p[i];
        for (uint32_t b=0;b<8;++b)
            value=(value>>1)^((0u-(value&1u))&UINT32_C(0xedb88320));
    }
    return ~value;
}
static void put(uint8_t *p, uint32_t value) {
    for (uint32_t i=0;i<4;++i) p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t get(const uint8_t *p) {
    uint32_t value=0;
    for (uint32_t i=0;i<4;++i) value|=(uint32_t)p[i]<<(8u*i);
    return value;
}
static uint16_t half(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static uint32_t low(uint32_t v) { return v>BUTTON_RADIUS ? v-BUTTON_RADIUS : 0; }
static uint32_t high(uint32_t v) { return v+BUTTON_RADIUS>4095 ? 4095 : v+BUTTON_RADIUS; }
bool config_buttons_valid(const uint16_t values[BUTTON_COUNT]) {
    for (uint32_t i=0;i<BUTTON_COUNT;++i) {
        if (values[i]>4095 || low(values[i])<=BUTTON_RELEASE_MAX+BUTTON_GAP) return false;
        for (uint32_t j=0;j<i;++j)
            if (!(low(values[i])>high(values[j])+BUTTON_GAP ||
                  low(values[j])>high(values[i])+BUTTON_GAP)) return false;
    }
    return true;
}
uint32_t config_button_code(const DisplayConfig *c, uint32_t raw) {
    static const uint32_t keys[BUTTON_COUNT]={KEY_LEFT,KEY_PLUS,KEY_MINUS,KEY_RIGHT,KEY_OK};
    if (raw<=BUTTON_RELEASE_MAX) return KEY_RELEASED;
    if (!c->calibrated || raw>4095) return KEY_UNKNOWN;
    for (uint32_t i=0;i<BUTTON_COUNT;++i)
        if (raw>=low(c->buttons[i]) && raw<=high(c->buttons[i])) return keys[i];
    return KEY_UNKNOWN;
}
static bool valid_display(const uint8_t *p) {
    return p[0]==1 && p[1]==0 && p[2]<96 && p[3]==0 &&
           p[4]==(uint8_t)(p[2]^255u) && p[5]==255 && p[6]==0 && p[7]==0 &&
           get(p+8)==crc(p,8) && get(p+12)==UINT32_C(0x44535031);
}
static bool valid_buttons(const uint8_t *p, uint16_t values[BUTTON_COUNT]) {
    if (get(p)!=2 || half(p+14)!=0 || get(p+16)!=UINT32_C(0x42434e54) ||
        get(p+20)!=crc(p,20) || get(p+24)!=~get(p+20) ||
        get(p+28)!=UINT32_C(0x42544e32)) return false;
    for (uint32_t i=0;i<BUTTON_COUNT;++i) values[i]=half(p+4u+2u*i);
    return config_buttons_valid(values);
}
void config_init(DisplayConfig *c, ConfigIo io) {
    c->io=io; c->index=DISPLAY_DEFAULT; c->slot=0; c->cursor=0; c->write_slots=0;
    c->writing=false; c->fault=false; c->calibrated=false; c->candidate=DISPLAY_DEFAULT;
    for (uint32_t i=0;i<BUTTON_COUNT;++i) c->buttons[i]=c->candidate_buttons[i]=0;
    if (!io.read) return;
    for (uint32_t slot=0;slot<CONFIG_SLOTS;++slot) {
        uint8_t record[2u*CONFIG_RECORD_BYTES]; bool empty=true;
        uint32_t length=slot+1u<CONFIG_SLOTS ? 2u*CONFIG_RECORD_BYTES : CONFIG_RECORD_BYTES;
        for (uint32_t i=0;i<length;++i) {
            record[i]=io.read(io.context,CONFIG_BASE+slot*CONFIG_RECORD_BYTES+i);
            if (i<CONFIG_RECORD_BYTES && record[i]!=255) empty=false;
        }
        /* Append beyond ALL used/partially programmed slots, never reuse holes. */
        if (!empty) c->slot=slot+1u;
        if (valid_display(record)) c->index=record[2];
        uint16_t values[BUTTON_COUNT];
        if (length==2u*CONFIG_RECORD_BYTES && valid_buttons(record,values)) {
            for (uint32_t i=0;i<BUTTON_COUNT;++i) c->buttons[i]=values[i];
            c->calibrated=true;
        }
    }
}
uint32_t config_remaining(const DisplayConfig *c) { return CONFIG_SLOTS-c->slot; }
static bool available(const DisplayConfig *c) {
    return !c->writing && !c->fault && c->io.read && c->io.program;
}
bool config_save(DisplayConfig *c, uint32_t index) {
    if (index>=96 || !available(c)) return false;
    if (index==c->index) return true;
    if (!config_remaining(c)) return false;
    put(c->record,1u|(index<<16));
    put(c->record+4,(index^65535u));
    put(c->record+8,crc(c->record,8)); put(c->record+12,UINT32_C(0x44535031));
    c->candidate=index; c->cursor=0; c->write_slots=1; c->writing=true;
    return true;
}
bool config_save_buttons(DisplayConfig *c, const uint16_t values[BUTTON_COUNT]) {
    if (!config_buttons_valid(values) || !available(c)) return false;
    bool same=c->calibrated;
    for (uint32_t i=0;i<BUTTON_COUNT;++i) if (values[i]!=c->buttons[i]) same=false;
    if (same) return true;
    if (config_remaining(c)<2) return false;
    put(c->record,2);
    for (uint32_t i=0;i<BUTTON_COUNT;++i) {
        c->record[4u+2u*i]=(uint8_t)values[i];
        c->record[5u+2u*i]=(uint8_t)(values[i]>>8);
        c->candidate_buttons[i]=values[i];
    }
    c->record[14]=0; c->record[15]=0;
    put(c->record+16,UINT32_C(0x42434e54));
    put(c->record+20,crc(c->record,20)); put(c->record+24,~get(c->record+20));
    put(c->record+28,UINT32_C(0x42544e32));
    c->cursor=0; c->write_slots=2; c->writing=true; return true;
}
void config_cancel(DisplayConfig *c) {
    if (c->writing) { c->slot+=c->write_slots; c->writing=false; }
}
void config_poll(DisplayConfig *c) {
    if (!c->writing) return;
    uint32_t a=CONFIG_BASE+c->slot*CONFIG_RECORD_BYTES+c->cursor;
    uint16_t value=half(c->record+c->cursor);
    if (c->io.read(c->io.context,a)!=255 || c->io.read(c->io.context,a+1u)!=255 ||
        !c->io.program(c->io.context,a,value) ||
        c->io.read(c->io.context,a)!=(uint8_t)value || c->io.read(c->io.context,a+1u)!=(uint8_t)(value>>8)) {
        c->fault=true; config_cancel(c); return;
    }
    c->cursor+=2;
    if (c->cursor==c->write_slots*CONFIG_RECORD_BYTES) {
        if (c->write_slots==1) c->index=c->candidate;
        else {
            for (uint32_t i=0;i<BUTTON_COUNT;++i) c->buttons[i]=c->candidate_buttons[i];
            c->calibrated=true;
        }
        c->slot+=c->write_slots; c->writing=false;
    }
}

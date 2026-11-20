#include "display_config.h"
static uint32_t crc(const uint8_t *p) {
    uint32_t value=UINT32_MAX;
    for (uint32_t i=0;i<8;++i) {
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
static bool valid(const uint8_t *p) {
    return p[0]==1 && p[1]==0 && p[2]<96 && p[3]==0 &&
           p[4]==(uint8_t)(p[2]^255u) && p[5]==255 && p[6]==0 && p[7]==0 &&
           get(p+8)==crc(p) && get(p+12)==UINT32_C(0x44535031);
}
void config_init(DisplayConfig *c, ConfigIo io) {
    c->io=io; c->index=DISPLAY_DEFAULT; c->slot=0; c->cursor=0;
    c->writing=false; c->fault=false; c->candidate=DISPLAY_DEFAULT;
    if (!io.read) return;
    for (uint32_t slot=0;slot<CONFIG_SLOTS;++slot) {
        uint8_t record[CONFIG_RECORD_BYTES]; bool empty=true;
        for (uint32_t i=0;i<CONFIG_RECORD_BYTES;++i) {
            record[i]=io.read(io.context,CONFIG_BASE+slot*CONFIG_RECORD_BYTES+i);
            if (record[i]!=255) empty=false;
        }
        /* Append beyond ALL used/partially programmed slots, never reuse holes. */
        if (!empty) c->slot=slot+1u;
        if (valid(record)) c->index=record[2];
    }
}
uint32_t config_remaining(const DisplayConfig *c) { return CONFIG_SLOTS-c->slot; }
bool config_save(DisplayConfig *c, uint32_t index) {
    if (index>=96 || c->writing || c->fault || !c->io.read || !c->io.program) return false;
    if (index==c->index) return true;
    if (c->slot>=CONFIG_SLOTS) return false;
    put(c->record,1u|(index<<16));
    put(c->record+4,(index^65535u));
    put(c->record+8,crc(c->record)); put(c->record+12,UINT32_C(0x44535031));
    c->candidate=index; c->cursor=0; c->writing=true;
    return true;
}
void config_cancel(DisplayConfig *c) {
    if (c->writing) { ++c->slot; c->writing=false; }
}
void config_poll(DisplayConfig *c) {
    if (!c->writing) return;
    uint32_t a=CONFIG_BASE+c->slot*CONFIG_RECORD_BYTES+c->cursor;
    uint16_t value=(uint16_t)((uint16_t)c->record[c->cursor]|((uint16_t)c->record[c->cursor+1u]<<8));
    if (c->io.read(c->io.context,a)!=255 || c->io.read(c->io.context,a+1u)!=255 ||
        !c->io.program(c->io.context,a,value) ||
        c->io.read(c->io.context,a)!=(uint8_t)value || c->io.read(c->io.context,a+1u)!=(uint8_t)(value>>8)) {
        c->fault=true; config_cancel(c); return;
    }
    c->cursor+=2;
    if (c->cursor==CONFIG_RECORD_BYTES) {
        c->index=c->candidate; ++c->slot; c->writing=false;
    }
}

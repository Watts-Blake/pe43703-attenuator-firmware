#include "boot_core.h"
static uint32_t word(Flash f, uint32_t a) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; ++i) value |= (uint32_t)f.read(f.context,a+i) << (8u*i);
    return value;
}
uint32_t bl_crc(Flash f, uint32_t address, uint32_t size) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= f.read(f.context,address+i);
        for (uint32_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u-(crc & 1u)) & UINT32_C(0xedb88320));
    }
    return ~crc;
}
static bool vectors(Flash f, uint32_t size) {
    uint32_t sp = word(f,APP_BASE), pc = word(f,APP_BASE+4u);
    return sp >= RAM_BASE+256u && sp <= BOOT_RAM && !(sp & 7u) && (pc & 1u) &&
           (pc & ~1u) >= APP_BASE && (pc & ~1u) < APP_BASE+size;
}
bool bl_valid(Flash f) {
    uint32_t size = word(f,META_BASE+4u), crc = word(f,META_BASE+8u);
    return word(f,META_BASE) == APP_COMMIT && size >= 236u && size <= APP_MAX && !(size & 3u) &&
           word(f,META_BASE+12u) == ~size && word(f,META_BASE+16u) == ~crc &&
           vectors(f,size) && bl_crc(f,APP_BASE,size) == crc;
}
void bl_init(Update *u, Flash f) {
    u->flash=f; u->size=0; u->crc=0; u->received=0; u->used=0;
    u->active=false; u->discard=false; u->run=false;
}
static bool eq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch-'0';
    if (ch >= 'a' && ch <= 'f') return ch-'a'+10;
    return -1;
}
static bool hex8(const char **p, uint32_t *out) {
    uint32_t value=0;
    for (uint32_t n=0; n<8; ++n) {
        int d=digit(**p);
        if (d < 0) return false;
        value=(value<<4)|(uint32_t)d; ++*p;
    }
    *out=value; return true;
}
static bool program_word(Flash f, uint32_t address, uint32_t value) {
    return f.program(f.context,address,(uint16_t)value) &&
           f.program(f.context,address+2u,(uint16_t)(value>>16));
}
static bool command(Update *u) {
    Flash f=u->flash;
    const char *p=u->line;
    if (eq(p,"run")) {
        if (u->active || !bl_valid(f)) return false;
        u->run=true; return true;
    }
    if (eq(p,"finish")) {
        if (!u->active || u->received != u->size || !vectors(f,u->size) ||
            bl_crc(f,APP_BASE,u->size) != u->crc) return false;
        u->active=false;
        /* Commit marker LAST. A partial update/metadata write cannot boot. */
        if (!program_word(f,META_BASE+4u,u->size) || !program_word(f,META_BASE+8u,u->crc) ||
            !program_word(f,META_BASE+12u,~u->size) || !program_word(f,META_BASE+16u,~u->crc) ||
            !program_word(f,META_BASE,APP_COMMIT)) return false;
        return bl_valid(f);
    }
    if (u->used >= 6u && p[0]=='b' && p[1]=='e' && p[2]=='g' && p[3]=='i' && p[4]=='n' && p[5]==' ') {
        uint32_t size,crc; p+=6;
        if (!hex8(&p,&size) || *p++ != ' ' || !hex8(&p,&crc) || *p ||
            size < 236u || size > APP_MAX || (size & 3u)) return false;
        u->active=false;
        /* Invalidate old commit before changing any application byte. */
        if (!f.erase(f.context,META_BASE)) return false;
        for (uint32_t off=0; off<size; off+=PAGE_BYTES)
            if (!f.erase(f.context,APP_BASE+off)) return false;
        u->size=size; u->crc=crc; u->received=0; u->active=true; return true;
    }
    if (u->used >= 6u && p[0]=='w' && p[1]=='r' && p[2]=='i' && p[3]=='t' && p[4]=='e' && p[5]==' ') {
        uint32_t offset; p+=6;
        if (!u->active || !hex8(&p,&offset) || *p++ != ' ' || offset != u->received) return false;
        uint8_t data[64]; uint32_t length=0;
        while (*p) {
            if (length == sizeof(data)) return false;
            int hi=digit(*p++); if (hi < 0 || !*p) return false;
            int lo=digit(*p++); if (lo < 0) return false;
            data[length++]=(uint8_t)((unsigned)hi*16u+(unsigned)lo);
        }
        if (!length || (offset & 3u) || (length & 3u) || offset > u->size || length > u->size-offset)
            return false;
        for (uint32_t n=0; n<length; n+=2) {
            uint16_t value=(uint16_t)((uint16_t)data[n] | ((uint16_t)data[n+1u]<<8));
            if (!f.program(f.context,APP_BASE+offset+n,value)) { u->active=false; return false; }
        }
        u->received+=length; return true;
    }
    return false;
}
size_t bl_feed(Update *u, char ch, bool error, char out[64]) {
    if (error) u->discard=true;
    if (ch=='\r') return 0;
    if (ch!='\n') {
        if ((unsigned char)ch<32u || (unsigned char)ch>126u || u->used==sizeof(u->line)-1u)
            u->discard=true;
        if (!u->discard) u->line[u->used++]=ch;
        return 0;
    }
    u->line[u->used]=0;
    const char *reply="err\n";
    if (!u->discard) {
        if (eq(u->line,"hello")) reply="boot 1 08002000 0000dc00 00000040\n";
        else if (command(u)) reply="ok\n";
    }
    u->used=0; u->discard=false;
    size_t n=0; while (*reply) out[n++]=*reply++;
    out[n]=0; return n;
}

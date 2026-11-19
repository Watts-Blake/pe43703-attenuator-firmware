#include "boot_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t memory[65536], app[256];
static unsigned writes, fail_at;
static uint8_t read_byte(void *c,uint32_t a) { (void)c; assert(a>=FLASH_BASE && a<FLASH_END); return memory[a-FLASH_BASE]; }
static bool erase(void *c,uint32_t a) {
    (void)c; assert(a>=APP_BASE && a<FLASH_END && !(a&1023u));
    if (++writes==fail_at) return false;
    memset(memory+(a-FLASH_BASE),255,1024); return true;
}
static bool program(void *c,uint32_t a,uint16_t v) {
    (void)c; assert(a>=APP_BASE && a<FLASH_END-1u && !(a&1u));
    if (++writes==fail_at) return false;
    unsigned i=a-FLASH_BASE; assert(memory[i]==255 && memory[i+1u]==255);
    memory[i]=(uint8_t)v; memory[i+1u]=(uint8_t)(v>>8); return true;
}
static Flash flash={0,read_byte,erase,program};
static bool send(Update *u,const char *line) {
    char out[64]={0}; size_t n=0;
    for (;*line;++line) n=bl_feed(u,*line,false,out);
    assert(n); return strcmp(out,"ok\n")==0;
}
static uint32_t crc(void) {
    memcpy(memory+8192,app,sizeof(app));
    uint32_t value=bl_crc(flash,APP_BASE,sizeof(app));
    memset(memory+8192,255,sizeof(app)); return value;
}
static void put32(unsigned off,uint32_t value) {
    for (unsigned n=0;n<4;++n) app[off+n]=(uint8_t)(value>>(8u*n));
}
static bool update(Update *u,uint32_t checksum) {
    char line[160];
    snprintf(line,sizeof(line),"begin 00000100 %08x\n",(unsigned)checksum);
    if (!send(u,line)) return false;
    assert(!bl_valid(flash));
    for (unsigned offset=0;offset<sizeof(app);offset+=64) {
        int n=snprintf(line,sizeof(line),"write %08x ",offset);
        for (unsigned j=0;j<64;++j) n+=snprintf(line+n,sizeof(line)-(size_t)n,"%02x",app[offset+j]);
        line[n++]='\n'; line[n]=0;
        if (!send(u,line)) return false;
        assert(!bl_valid(flash));
    }
    return send(u,"finish\n");
}
int main(void) {
    memset(app,0,sizeof(app)); put32(0,BOOT_RAM); put32(4,APP_BASE+237u);
    memset(memory,255,sizeof(memory)); uint32_t checksum=crc();
    /* zlib-compatible CRC checked independently by the Python bundle tests. */
    Update u; bl_init(&u,flash); writes=0; fail_at=0;
    assert(update(&u,checksum)); assert(bl_valid(flash));
    unsigned total=writes;
    assert(send(&u,"run\n") && u.run);
    uint8_t old[65536]; memcpy(old,memory,sizeof(old));
    for (unsigned failure=1;failure<=total;++failure) {
        memcpy(memory,old,sizeof(memory)); writes=0; fail_at=failure; bl_init(&u,flash);
        assert(!update(&u,checksum));
        /* Only failure before first erase can leave the OLD valid app intact. */
        assert(bl_valid(flash)==(failure==1));
        Update reboot; bl_init(&reboot,flash);
        assert(send(&reboot,"run\n")==bl_valid(flash));
        for (unsigned i=0;i<8192;++i) assert(memory[i]==255);
    }
    fail_at=0; memset(memory,255,sizeof(memory)); bl_init(&u,flash);
    const char *bad[]={"\n","b\n","begin\n","begin ffffffff 00000000\n",
        "begin 00000101 00000000\n","begin 0000dc04 00000000\n",
        "begin 00000004 00000000\n","begin 00000100 0000000g\n",
        "write ffffffff 00000000\n","finish\n","run\n"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { writes=0; assert(!send(&u,bad[i])); assert(!writes); }
    assert(send(&u,"begin 0000dc00 00000000\n"));
    const char *bad_writes[]={"write ffffffff 00000000\n", "write 00000004 00000000\n",
        "write 00000000 000000\n", "write 00000000 0000000g\n",
        "write 00000000 00000000 trailing\n"};
    for (unsigned i=0;i<sizeof(bad_writes)/sizeof(bad_writes[0]);++i) {
        writes=0; assert(!send(&u,bad_writes[i])); assert(!writes && u.received==0);
    }
    char out[64];
    writes=0;
    const char *partial="write 00000000 00000000";
    while (*partial) assert(!bl_feed(&u,*partial++,false,out));
    assert(!writes); assert(bl_feed(&u,'\n',false,out)==3 && writes==2);
    assert(!send(&u,"finish\n") && !bl_valid(flash));
    for (unsigned i=0;i<1000;++i) assert(!bl_feed(&u,'x',false,out));
    assert(bl_feed(&u,'\n',false,out)==4 && !strcmp(out,"err\n"));
    assert(bl_feed(&u,'x',true,out)==0);
    assert(bl_feed(&u,'\n',false,out)==4);
    assert(!update(&u,checksum^1u)); assert(!bl_valid(flash)); assert(!send(&u,"run\n"));
    assert(update(&u,checksum)); memory[8192+240]^=1; assert(!bl_valid(flash));
    put32(4,FLASH_BASE+1u); checksum=crc(); assert(!update(&u,checksum));
    FILE *file=fopen("build/combined.bin","rb"); assert(file);
    assert(fread(memory,1,sizeof(memory),file)==sizeof(memory)); fclose(file);
    assert(bl_valid(flash)); /* Independently verifies Python/zlib metadata. */
    memory[META_BASE-FLASH_BASE+12u]^=1; assert(!bl_valid(flash));
    puts("boot: bounds, CRC, interruption at every flash operation, recovery passed");
}

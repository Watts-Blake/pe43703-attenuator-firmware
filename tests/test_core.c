#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t time, level, rises, latches, word, si, clk, le, last_rise, last_fall, data_changed, le_rise; } Trace;
static void pins(void *ctx, uint32_t high) {
    Trace *t = ctx;
    assert(!(high & ~PIN_MASK));
    uint32_t changed = high ^ t->level;
    if (changed & t->si) {
        if (t->rises) assert(t->time - t->last_rise >= 1);
        t->data_changed = t->time;
    }
    if ((changed & t->clk) && (high & t->clk)) {
        assert(!(high & t->le));
        assert(t->time - t->data_changed >= 1);
        assert(t->time - t->last_fall >= 1);
        assert(t->rises < 16);
        if (high & t->si) t->word |= 1u << t->rises;
        ++t->rises; t->last_rise = t->time;
    }
    if ((changed & t->clk) && !(high & t->clk)) {
        assert(t->time - t->last_rise >= 1); t->last_fall = t->time;
    }
    if ((changed & t->le) && (high & t->le)) {
        assert(t->rises == 16); assert(!(high & t->clk));
        assert(t->time - t->last_rise >= 1); ++t->latches; t->le_rise = t->time;
    }
    if ((changed & t->le) && !(high & t->le)) assert(t->time - t->le_rise >= 1);
    t->level = high;
}
static void delay(void *ctx, uint32_t us) { Trace *t = ctx; assert(us >= 1); t->time += us; }
static void trace_init(Trace *t, uint32_t permutation) {
    memset(t, 0, sizeof(*t));
    t->si = 1u << permutations[permutation][0];
    t->clk = 1u << permutations[permutation][1];
    t->le = 1u << permutations[permutation][2];
    assert(t->si != t->clk && t->si != t->le && t->clk != t->le);
    assert((t->si | t->clk | t->le) == PIN_MASK);
}
static void ignore_pins(void *ctx, uint32_t high) { (void)ctx; (void)high; }
static void ignore_delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static Bus silent(void) { Bus b = {ignore_pins, ignore_delay, NULL}; return b; }
static void test_frames(void) {
    for (uint32_t p = 0; p < 6; ++p) for (uint32_t q = 0; q <= 127; ++q) {
        Controller c; controller_boot(&c, silent());
        Trace t; trace_init(&t, p); Bus bus = {pins, delay, &t}; c.bus = bus;
        c.qdb = q;
        assert(controller_permutation(&c, p));
        assert(c.permutation == p && c.qdb == q);
        assert(t.rises == 16 && t.latches == 1 && t.level == 0);
        assert(t.word == q && t.time >= 52); assert(c.issued == 2);
    }
    Trace t; trace_init(&t, 0); Bus bus = {pins, delay, &t}; Controller c;
    controller_boot(&c, bus);
    assert(t.word == 0 && t.rises == 16 && t.latches == 1);
    assert(c.qdb == 0 && c.permutation == 0 && c.issued == 1);
    assert(!controller_set(&c, 128)); assert(!controller_set(&c, UINT32_MAX));
    assert(!controller_permutation(&c, 6)); assert(c.issued == 1);
}
static void test_mapping_transitions(void) {
    for (uint32_t old = 0; old < 6; ++old) for (uint32_t next = 0; next < 6; ++next) {
        Controller c; controller_boot(&c, silent());
        assert(controller_permutation(&c, old));
        Trace t; trace_init(&t,next); Bus bus = {pins,delay,&t}; c.bus = bus;
        assert(controller_permutation(&c,next));
        assert(t.latches == 1 && t.rises == 16 && t.level == 0 && t.word == 0);
    }
}
static void test_parser(void) {
    for (uint32_t q = 0; q <= 127; ++q) {
        char s[6]; uint32_t got = 999; format_db(q,s);
        assert(parse_db(s,&got) && got == q);
    }
    const char *bad[] = {"", "-1", "+1", "32", "31.76", "0.01", "0.1", "0.", ".25", "1e1", "nan", "inf", "1 ", " 1", "1.250", "999999999999999999999999", "000", "31.99", "1\n"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) { uint32_t q; assert(!parse_db(bad[i],&q)); }
    const char *good[] = {"0", "0.0", "0.00", "00.25", "1.5", "31.75"};
    for (size_t i = 0; i < sizeof(good)/sizeof(*good); ++i) { uint32_t q; assert(parse_db(good[i],&q)); }
}
static void command(Mailbox *m, uint32_t seq, uint32_t op, uint32_t arg) {
    m->seq = 0; m->command_magic = MB_MAGIC; m->command_version = MB_VERSION;
    m->op = op; m->arg = arg; m->reserved = 0;
    m->seq_inverse = ~seq; m->check = MB_CHECK ^ seq ^ MB_MAGIC ^ MB_VERSION ^ op ^ arg;
    m->seq = seq;
}
static void recheck(Mailbox *m) {
    m->check = MB_CHECK ^ m->seq ^ m->command_magic ^ m->command_version ^ m->op ^ m->arg ^ m->reserved;
}
static void test_mailbox(void) {
    Controller c; controller_boot(&c, silent()); Mailbox m;
    memset(&m, 0xff, sizeof(m)); mailbox_init(&m); mailbox_publish(&m,&c); m.state = MB_READY;
    assert(m.magic == MB_MAGIC && m.bytes == sizeof(m) && m.version == 1 && m.issued == 1);
    assert(!mailbox_poll(&m,&c));
    command(&m,2,OP_SET,73); m.seq = 0;
    assert(!mailbox_poll(&m,&c) && c.qdb == 0); /* unpublished */
    m.seq = 1; assert(!mailbox_poll(&m,&c)); /* in progress */
    m.seq = 2; m.seq_inverse = 0; assert(!mailbox_poll(&m,&c));
    m.seq_inverse = ~2u; m.arg = 74; assert(!mailbox_poll(&m,&c)); /* torn payload */
    m.arg = 73; assert(mailbox_poll(&m,&c));
    assert(m.ack == 2 && m.status == ST_OK && m.result_qdb == 73 && !(m.generation & 1u));
    uint32_t issued = c.issued;
    assert(!mailbox_poll(&m,&c) && c.issued == issued); /* replay */
    command(&m,2,OP_SET,4); assert(!mailbox_poll(&m,&c));
    command(&m,4,OP_SET,128); assert(mailbox_poll(&m,&c) && m.status == ST_RANGE && c.qdb == 73);
    command(&m,6,OP_PERMUTATION,6); assert(mailbox_poll(&m,&c) && m.status == ST_PERMUTATION);
    command(&m,8,OP_GET,0); m.command_magic = 0; recheck(&m);
    assert(mailbox_poll(&m,&c) && m.status == ST_MAGIC);
    command(&m,10,OP_GET,0); m.command_version = 2; recheck(&m);
    assert(mailbox_poll(&m,&c) && m.status == ST_VERSION);
    command(&m,12,99,0); assert(mailbox_poll(&m,&c) && m.status == ST_OP);
    command(&m,14,OP_GET,1); assert(mailbox_poll(&m,&c) && m.status == ST_RANGE);
    command(&m,16,OP_GET,0); m.reserved = 1; recheck(&m);
    assert(mailbox_poll(&m,&c) && m.status == ST_RESERVED);
    for (uint32_t p = 0; p < 6; ++p) {
        command(&m,18+p*2,OP_PERMUTATION,p); assert(mailbox_poll(&m,&c));
        assert(m.status == ST_OK && m.result_permutation == p && m.result_qdb == 73);
    }
    uint32_t ack = m.ack, result = m.result_qdb;
    controller_set(&c,0); mailbox_publish(&m,&c);
    assert(m.ack == ack && m.result_qdb == result && m.last_qdb == 0);
    command(&m,0xfffffffeu,OP_GET,0); assert(mailbox_poll(&m,&c));
    command(&m,2,OP_SET,4); assert(!mailbox_poll(&m,&c)); /* no wrap */
}
static void test_partial_publication(void) {
    for (size_t prefix = 0; prefix <= 7; ++prefix) {
        Controller c; controller_boot(&c,silent()); Mailbox m, prepared;
        mailbox_init(&m); mailbox_init(&prepared); command(&prepared,2,OP_SET,127);
        /* Publish zero first; every possible prefix of the seven payload words
           remains inert until the aligned sequence publication word is written. */
        memcpy((char *)&m + 36, (const char *)&prepared + 36, prefix * 4);
        assert(!mailbox_poll(&m,&c) && c.issued == 1 && c.qdb == 0);
        if (prefix == 7) {
            m.seq = 2; assert(mailbox_poll(&m,&c));
            assert(m.ack == 2 && c.qdb == 127 && c.issued == 2);
        }
    }
}
static void feed(Line *l, Controller *c, const char *s, const char *expected) {
    char out[16] = {0}; size_t n = 0;
    while (*s) { size_t next = line_feed(l,c,*s++,false,out); if (next) n = next; }
    assert(n == strlen(expected)); assert(strcmp(out,expected) == 0);
}
static void test_uart(void) {
    Controller c; controller_boot(&c,silent()); Line l = {{0},0,false};
    feed(&l,&c,"get\r\n","0.00\r\n");
    feed(&l,&c,"set 31.75\n","ok 31.75\r\n");
    feed(&l,&c,"set 0.25\r","ok 0.25\r\n");
    feed(&l,&c,"get\n","0.25\r\n");
    uint32_t count = c.issued;
    feed(&l,&c,"set 32\n","err\r\n"); assert(c.issued == count);
    feed(&l,&c,"set 0.000\n","err\r\n");
    feed(&l,&c,"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n","err\r\n");
    feed(&l,&c,"set 0\n","ok 0.00\r\n");
    char out[16]; assert(!line_feed(&l,&c,0,false,out));
    feed(&l,&c,"set 1\n","err\r\n");
    assert(!line_feed(&l,&c,'s',true,out));
    feed(&l,&c,"et 5\n","err\r\n"); assert(c.qdb == 0);
    feed(&l,&c,"set 1\r\nget\r\n","1.00\r\n");
}
int main(void) {
    test_frames(); test_mapping_transitions(); test_parser(); test_mailbox(); test_partial_publication(); test_uart();
    puts("core: frame/timing, 768 mapping/value combinations, boot, parser, mailbox, UART passed"); return 0;
}

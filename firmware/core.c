#include "core.h"
const uint8_t permutations[6][3] = {
    {11,12,13}, {11,13,12}, {12,11,13},
    {12,13,11}, {13,11,12}, {13,12,11}
};
static void barrier(void) {
#ifdef __arm__
    __asm__ volatile ("dmb" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}
uint16_t pe_frame(uint32_t qdb) {
    /* Q0..Q6 = quarter dB; Q7=D7=0; Q8..15 address zero. */
    return (uint16_t)(qdb & 127u);
}
static void send_frame(Controller *c) {
    uint32_t si = 1u << permutations[c->permutation][0];
    uint32_t clk = 1u << permutations[c->permutation][1];
    uint32_t le = 1u << permutations[c->permutation][2];
    uint16_t word = pe_frame(c->qdb);
    c->bus.pins(c->bus.context, 0);
    c->bus.delay_us(c->bus.context, 1);
    for (uint32_t bit = 0; bit < 16; ++bit) {
        uint32_t data = (word & (1u << bit)) ? si : 0;
        c->bus.pins(c->bus.context, data);
        c->bus.delay_us(c->bus.context, 1);
        c->bus.pins(c->bus.context, data | clk);
        c->bus.delay_us(c->bus.context, 1);
        c->bus.pins(c->bus.context, data);
        c->bus.delay_us(c->bus.context, 1);
    }
    c->bus.pins(c->bus.context, 0);
    c->bus.delay_us(c->bus.context, 1);
    c->bus.pins(c->bus.context, le);
    c->bus.delay_us(c->bus.context, 1);
    c->bus.pins(c->bus.context, 0);
    c->bus.delay_us(c->bus.context, 1);
    ++c->issued;
}
void controller_boot(Controller *c, Bus bus) {
    c->bus = bus; c->qdb = 0; c->permutation = 0; c->issued = 0;
    send_frame(c);
}
bool controller_set(Controller *c, uint32_t qdb) {
    if (qdb > 127) return false;
    c->qdb = qdb; send_frame(c); return true;
}
bool controller_permutation(Controller *c, uint32_t permutation) {
    if (permutation >= 6) return false;
    /* Every wire is already low after a complete transaction. Remapping only
       changes software masks; no pin modes or levels change during the switch. */
    c->bus.pins(c->bus.context, 0);
    c->bus.delay_us(c->bus.context, 1);
    c->permutation = permutation; send_frame(c); return true;
}
void mailbox_init(volatile Mailbox *m) {
    volatile uint32_t *p = (volatile uint32_t *)m;
    for (size_t i = 0; i < sizeof(*m) / 4; ++i) p[i] = 0;
    m->magic = MB_MAGIC; m->version = MB_VERSION; m->bytes = MB_BYTES;
}
static void result(volatile Mailbox *m, const Controller *c, uint32_t seq, uint32_t status) {
    ++m->generation; barrier();
    m->last_qdb = c->qdb; m->permutation = c->permutation; m->issued = c->issued;
    m->status = status; m->result_qdb = c->qdb;
    m->result_permutation = c->permutation; m->result_issued = c->issued;
    m->ack = seq;
    barrier(); ++m->generation;
}
void mailbox_publish(volatile Mailbox *m, const Controller *c) {
    ++m->generation; barrier();
    m->last_qdb = c->qdb; m->permutation = c->permutation; m->issued = c->issued;
    barrier(); ++m->generation;
}
bool mailbox_poll(volatile Mailbox *m, Controller *c) {
    uint32_t seq = m->seq;
    if (!seq || (seq & 1u) || seq <= m->ack) return false;
    barrier();
    uint32_t magic = m->command_magic, version = m->command_version;
    uint32_t op = m->op, arg = m->arg, reserved = m->reserved;
    uint32_t check = m->check, inverse = m->seq_inverse;
    barrier();
    if (m->seq != seq || inverse != ~seq ||
        check != (MB_CHECK ^ seq ^ magic ^ version ^ op ^ arg ^ reserved)) return false;
    uint32_t status = ST_OK;
    if (magic != MB_MAGIC) status = ST_MAGIC;
    else if (version != MB_VERSION) status = ST_VERSION;
    else if (reserved) status = ST_RESERVED;
    else if (op == OP_GET) { if (arg) status = ST_RANGE; }
    else if (op == OP_SET) { if (!controller_set(c, arg)) status = ST_RANGE; }
    else if (op == OP_PERMUTATION) {
        if (!controller_permutation(c, arg)) status = ST_PERMUTATION;
    } else status = ST_OP;
    result(m, c, seq, status); return true;
}
bool parse_db(const char *s, uint32_t *qdb) {
    uint32_t whole = 0, digits = 0, fraction = 0;
    while (*s >= '0' && *s <= '9') {
        if (++digits > 2) return false;
        whole = whole * 10u + (uint32_t)(*s++ - '0');
    }
    if (!digits) return false;
    if (*s == '.') {
        ++s;
        if (*s < '0' || *s > '9') return false;
        fraction = (uint32_t)(*s++ - '0') * 10u;
        if (*s >= '0' && *s <= '9') fraction += (uint32_t)(*s++ - '0');
    }
    if (*s || whole > 31 || fraction % 25u) return false;
    *qdb = whole * 4u + fraction / 25u; return true;
}
void format_db(uint32_t qdb, char out[6]) {
    uint32_t whole = qdb / 4u, fraction = (qdb % 4u) * 25u;
    uint32_t n = 0;
    if (whole >= 10) out[n++] = (char)('0' + whole / 10u);
    out[n++] = (char)('0' + whole % 10u); out[n++] = '.';
    out[n++] = (char)('0' + fraction / 10u);
    out[n++] = (char)('0' + fraction % 10u); out[n] = 0;
}
static bool equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static size_t response(char out[16], const char *prefix, uint32_t qdb) {
    size_t n = 0; char value[6]; format_db(qdb, value);
    while (*prefix) out[n++] = *prefix++;
    for (const char *p = value; *p; ++p) out[n++] = *p;
    out[n++] = '\r'; out[n++] = '\n'; out[n] = 0; return n;
}
size_t line_feed(Line *line, Controller *c, char ch, bool rx_error, char out[16]) {
    if (rx_error) line->discard = true;
    if (ch == '\r' || ch == '\n') {
        if (!line->used && !line->discard) return 0;
        line->data[line->used] = 0;
        uint32_t qdb; bool valid = false; size_t n = 0;
        if (!line->discard) {
            if (equal(line->data, "bootloader")) {
                controller_set(c, 0); line->bootloader = true;
                const char reply[] = "ok bootloader\r\n";
                for (size_t i=0; i<sizeof(reply); ++i) out[i]=reply[i];
                n=sizeof(reply)-1; valid=true;
            }
            else if (equal(line->data, "get")) { n = response(out, "", c->qdb); valid = true; }
            else if (line->used > 4 && line->data[0] == 's' && line->data[1] == 'e' &&
                     line->data[2] == 't' && line->data[3] == ' ' &&
                     parse_db(line->data + 4, &qdb)) {
                controller_set(c, qdb); n = response(out, "ok ", c->qdb); valid = true;
            }
        }
        line->used = 0; line->discard = false;
        if (valid) return n;
        const char error[] = "err\r\n";
        for (size_t i = 0; i < sizeof(error); ++i) out[i] = error[i];
        return sizeof(error) - 1;
    }
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) line->discard = true;
    if (line->discard) return 0;
    if (line->used == sizeof(line->data) - 1) line->discard = true;
    else line->data[line->used++] = ch;
    return 0;
}

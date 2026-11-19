#ifndef CORE_H
#define CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MB_MAGIC UINT32_C(0x50453433)
#define MB_VERSION 1u
#define MB_BYTES 128u
#define MB_CHECK UINT32_C(0xa5c35a3c)
#define PIN_MASK (UINT32_C(7) << 11)
/* The debugger owns words 8..15; firmware owns all other words. */
typedef struct {
    uint32_t magic, version, bytes, state;
    uint32_t generation, last_qdb, permutation, issued;
    uint32_t seq, command_magic, command_version, op;
    uint32_t arg, reserved, check, seq_inverse;
    uint32_t ack, status, result_qdb, result_permutation, result_issued;
    uint32_t unused[11];
} Mailbox;
_Static_assert(sizeof(Mailbox) == MB_BYTES, "mailbox ABI");
_Static_assert(offsetof(Mailbox, seq) == 32, "command ABI");
_Static_assert(offsetof(Mailbox, ack) == 64, "reply ABI");
enum { MB_INITIALIZING, MB_READY, MB_CLOCK_FAULT, MB_CPU_FAULT };
enum { OP_GET = 1, OP_SET, OP_PERMUTATION };
enum { ST_OK, ST_MAGIC, ST_VERSION, ST_OP, ST_RANGE, ST_PERMUTATION, ST_RESERVED };
typedef struct {
    void (*pins)(void *context, uint32_t high_mask);
    void (*delay_us)(void *context, uint32_t us);
    void *context;
} Bus;
typedef struct { Bus bus; uint32_t qdb, permutation, issued; } Controller;
extern const uint8_t permutations[6][3]; /* SI, CLK, LE */
uint16_t pe_frame(uint32_t qdb);
void controller_boot(Controller *c, Bus bus);
bool controller_set(Controller *c, uint32_t qdb);
bool controller_permutation(Controller *c, uint32_t permutation);
void mailbox_init(volatile Mailbox *m);
void mailbox_publish(volatile Mailbox *m, const Controller *c);
bool mailbox_poll(volatile Mailbox *m, Controller *c);
bool parse_db(const char *text, uint32_t *qdb);
void format_db(uint32_t qdb, char out[6]);
/* Returns response length; receives one byte at a time, discarding a bad line. */
typedef struct { char data[32]; uint32_t used; bool discard, bootloader; } Line;
size_t line_feed(Line *line, Controller *c, char ch, bool rx_error, char out[16]);
#endif

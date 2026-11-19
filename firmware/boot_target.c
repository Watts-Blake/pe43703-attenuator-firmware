#include "boot_core.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define SR REG(0x4002200cu)
#define CR REG(0x40022010u)
#define UART_SR REG(0x40013800u)
#define UART_DR REG(0x40013804u)
static void park(void) { for (;;) __asm__ volatile("wfi"); }
void Default_Handler(void) { park(); }
static bool wait(volatile uint32_t *reg, uint32_t mask, uint32_t value) {
    for (uint32_t n=0; n<1000000u; ++n) if ((*reg & mask)==value) return true;
    return false;
}
static uint8_t read_byte(void *context, uint32_t address) {
    (void)context; return *(volatile uint8_t *)address;
}
static bool unlock(void) {
    if (!(REG(0x40021000u)&2u) || !wait(&SR,1u,0)) return false;
    if (CR & 128u) {
        REG(0x40022004u)=0x45670123u; REG(0x40022004u)=0xcdef89abu;
    }
    if (CR & 128u) return false;
    SR=0x34u; return true;
}
static bool complete(void) {
    if (!wait(&SR,1u,0)) park(); /* No further flash actions after ambiguous busy. */
    uint32_t status=SR;
    CR=128u; SR=0x34u;
    return (status & 0x34u)==0x20u;
}
static bool erase(void *context, uint32_t address) {
    (void)context;
    if (address<APP_BASE || address>=FLASH_END || (address & (PAGE_BYTES-1u))) return false;
    if (!unlock()) return false;
    CR=2u; REG(0x40022014u)=address; CR=0x42u;
    if (!complete()) return false;
    for (uint32_t n=0; n<PAGE_BYTES; n+=2u)
        if (*(volatile uint16_t *)(address+n)!=UINT16_MAX) return false;
    return true;
}
static bool program(void *context, uint32_t address, uint16_t value) {
    (void)context;
    if (address<APP_BASE || address>FLASH_END-2u || (address&1u) ||
        *(volatile uint16_t *)address!=UINT16_MAX) return false;
    if (!unlock()) return false;
    CR=1u; *(volatile uint16_t *)address=value;
    return complete() && *(volatile uint16_t *)address==value;
}
/* Called with no interrupts, DMA, PLL, SysTick or pending IRQs enabled. */
__attribute__((naked,noreturn)) static void enter(uint32_t sp, uint32_t pc) {
    (void)sp; (void)pc;
    __asm__ volatile("msr msp,r0\n bx r1");
}
static void jump(void) {
    REG(0x4002100cu)=1u<<14; REG(0x4002100cu)=0; /* reset USART1 */
    REG(0x40010804u)=0x44444444u; /* restore GPIOA high reset state */
    REG(0x40021018u)=0;
    REG(0xe000ed08u)=APP_BASE;
    __asm__ volatile("dsb; isb" ::: "memory");
    enter(REG(APP_BASE),REG(APP_BASE+4u));
}
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler(void) {
    uint32_t *src=&_sidata;
    for (uint32_t *p=&_sdata; p<&_edata; ++p) *p=*src++;
    for (uint32_t *p=&_sbss; p<&_ebss; ++p) *p=0;
    bool requested=REG(BOOT_RAM)==BOOT_REQUEST && REG(BOOT_RAM+4u)==~BOOT_REQUEST;
    REG(BOOT_RAM)=0; REG(BOOT_RAM+4u)=0;
    Flash flash={0,read_byte,erase,program};
    if (!requested && bl_valid(flash)) jump();
    /* Reset HSI 8 MHz, APB2 /1. BRR=69; nominal 115942 baud (+0.64%). */
    REG(0x40021018u)=(1u<<2)|(1u<<14);
    (void)REG(0x40021018u);
    REG(0x40010804u)=(REG(0x40010804u)&~0xff0u)|0x4a0u;
    REG(0x40013808u)=69u;
    REG(0x4001380cu)=(1u<<13)|(1u<<3)|(1u<<2);
    Update update; bl_init(&update,flash);
    for (;;) {
        uint32_t status=UART_SR;
        if (status & ((1u<<5)|15u)) {
            char ch=(char)UART_DR, out[64];
            size_t n=bl_feed(&update,ch,(status&15u)!=0,out);
            for (size_t i=0; i<n; ++i) {
                if (!wait(&UART_SR,1u<<7,1u<<7)) park();
                UART_DR=(uint32_t)(unsigned char)out[i];
            }
            if (update.run) {
                if (!wait(&UART_SR,1u<<6,1u<<6)) park();
                jump();
            }
        }
    }
}

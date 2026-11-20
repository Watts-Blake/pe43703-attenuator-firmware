#include "core.h"
#include "adc.h"
#include "oled.h"
#include "ui.h"
#include "layout.h"
#define REG(address) (*(volatile uint32_t *)(address))
#define RCC_CR REG(0x40021000u)
#define RCC_CFGR REG(0x40021004u)
#define RCC_APB2ENR REG(0x40021018u)
#define GPIOA_CRH REG(0x40010804u)
#define GPIOA_CRL REG(0x40010800u)
#define GPIOA_ODR REG(0x4001080cu)
#define GPIOA_BSRR REG(0x40010810u)
#define GPIOB_CRH REG(0x40010c04u)
#define GPIOB_BSRR REG(0x40010c10u)
#define FLASH_ACR REG(0x40022000u)
#define FLASH_SR REG(0x4002200cu)
#define FLASH_CR REG(0x40022010u)
#define USART_SR REG(0x40013800u)
#define USART_DR REG(0x40013804u)
#define USART_BRR REG(0x40013808u)
#define USART_CR1 REG(0x4001380cu)
#define SYST_CSR REG(0xe000e010u)
#define SYST_RVR REG(0xe000e014u)
#define SYST_CVR REG(0xe000e018u)
#define SCB_VTOR REG(0xe000ed08u)
volatile Mailbox mailbox __attribute__((section(".mailbox"), aligned(4), used));
static bool wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t wanted) {
    for (uint32_t i = 0; i < 1000000; ++i) if ((*reg & mask) == wanted) return true;
    return false;
}
static bool clock_init(void) {
    /* Entry contract: hardware reset or loader handoff: HSI selected, UART reset. */
    RCC_CR |= 1u << 16; /* HSE crystal, design assumption 8 MHz */
    if (!wait_bits(&RCC_CR, 1u << 17, 1u << 17)) return false;
    FLASH_ACR = (1u << 4) | 2u; /* prefetch + two wait states before 72 MHz */
    if ((FLASH_ACR & 7u) != 2u) return false;
    RCC_CFGR = (7u << 18) | (1u << 16) | (4u << 8) | (2u << 14);
    /* PLL x9 from HSE /1, AHB /1, APB1 /2, APB2 /1, ADC /6 */
    RCC_CR |= 1u << 24;
    if (!wait_bits(&RCC_CR, 1u << 25, 1u << 25)) return false;
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;
    return wait_bits(&RCC_CFGR, 3u << 2, 2u << 2);
}
static void pins(void *context, uint32_t high) {
    (void)context;
    GPIOB_BSRR = ((PIN_MASK & ~high) << 16) | (PIN_MASK & high);
}
static void delay_us(void *context, uint32_t us) {
    (void)context;
    uint32_t start = SYST_CVR;
    uint32_t ticks = us * 72u;
    while (((start - SYST_CVR) & 0xffffffu) < ticks) { }
}
static uint32_t adc_read(void *context, uint32_t address) {
    (void)context; return REG(address);
}
static void adc_write(void *context, uint32_t address, uint32_t value) {
    (void)context; REG(address) = value;
}
typedef struct { uint32_t mode, latch; } OledSaved;
static void oled_pins(void *context, uint32_t high) {
    (void)context;
    GPIOA_BSRR=((OLED_MASK & ~high)<<16)|(OLED_MASK & high);
}
static void oled_begin(void *context, uint32_t idle) {
    OledSaved *s=context; s->mode=GPIOA_CRL & 0xffff0000u; s->latch=GPIOA_ODR & OLED_MASK;
    oled_pins(context,idle);
    GPIOA_CRL=(GPIOA_CRL & 0xffffu)|0x22220000u; /* PA4..7 only,2MHz PP */
}
static void oled_end(void *context) {
    OledSaved *s=context;
    GPIOA_CRL=(GPIOA_CRL & 0xffffu)|s->mode;
    oled_pins(context,s->latch);
}
static uint8_t config_read(void *context, uint32_t address) {
    (void)context;
    return *(volatile uint8_t *)address;
}
/* No erase path. Run the busy wait from RAM, including its literal pool.
   HSI remains enabled by clock_init, as required for flash programming. */
__attribute__((section(".ramfunc"),noinline))
static bool config_program(void *context, uint32_t address, uint16_t value) {
    (void)context;
    if (address<CONFIG_BASE || address>FLASH_END-2u || (address&1u) ||
        !(RCC_CR&2u) || (FLASH_SR&1u) || *(volatile uint16_t *)address!=UINT16_MAX) return false;
    if (FLASH_CR&128u) {
        REG(0x40022004u)=0x45670123u; REG(0x40022004u)=0xcdef89abu;
    }
    if (FLASH_CR&128u) return false;
    FLASH_SR=0x34u; FLASH_CR=1u;
    *(volatile uint16_t *)address=value;
    for (uint32_t n=0;n<1000000u;++n) {
        if (!(FLASH_SR&1u)) {
            uint32_t status=FLASH_SR; FLASH_CR=128u; FLASH_SR=0x34u;
            return (status&0x34u)==0x20u && *(volatile uint16_t *)address==value;
        }
    }
    /* Ambiguous completion: no more programming in this boot (config.fault).
       A permanently busy single-bank flash can stall subsequent flash fetches. */
    return false;
}
static void park(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
    for (;;) __asm__ volatile ("wfi");
}
void Default_Handler(void) { mailbox.state = MB_CPU_FAULT; park(); }
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    for (uint32_t *p = &_sdata; p < &_edata; ++p) *p = *src++;
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0;
    SCB_VTOR = APP_BASE;
    __asm__ volatile ("dsb; isb" ::: "memory");
    mailbox_init(&mailbox);
    if (!clock_init()) { mailbox.state = MB_CLOCK_FAULT; park(); }
    SYST_RVR = 0xffffffu; SYST_CVR = 0; SYST_CSR = 5; /* core clock, no IRQ */
    RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 14);
    (void)RCC_APB2ENR;
    pins(0, 0); /* preload low before enabling output drivers */
    GPIOB_CRH = (GPIOB_CRH & ~0x00fff000u) | 0x00222000u; /* PB11..13 2 MHz PP */
    /* Normal display uses the observed PA6/7/5/4 reset-mode wiring. */
    Controller controller;
    Bus bus = { pins, delay_us, 0 };
    delay_us(0, 1000); /* allow peripheral power-up; does not prove power good */
    controller_boot(&controller, bus); /* command zero before accepting input */
    GPIOA_CRH = (GPIOA_CRH & ~0x00000ff0u) | 0x000004a0u; /* PA9 AF PP, PA10 input */
    USART_BRR = 625u; /* 72 MHz / 115200 = 625, DIV=39 + 1/16 */
    USART_CR1 = (1u << 13) | (1u << 3) | (1u << 2); /* 8N1, no IRQ */
    mailbox_publish(&mailbox, &controller);
    __asm__ volatile ("dmb" ::: "memory"); mailbox.state = MB_READY;
    Line line; line.used=0; line.discard=false; line.bootloader=false; line.adc=false; line.oledprobe=false;
    const AdcIo adc = { adc_read, adc_write, delay_us, 0 };
    OledSaved saved; Oled oled;
    const OledBus display={oled_begin,oled_pins,oled_end,delay_us,&saved};
    oled_init(&oled,display);
    Ui ui; ui_init(&ui,&controller,display);
    const ConfigIo configuration={config_read,config_program,0}; ui_configure(&ui,configuration);
    TxQueue tx; tx.read=0; tx.write=0; tx.used=0;
    uint32_t last_ticks=SYST_CVR, fraction=0, now_ms=0;
    for (;;) {
        uint32_t ticks=SYST_CVR;
        fraction+=(last_ticks-ticks)&0xffffffu; last_ticks=ticks;
        now_ms+=fraction/72000u; fraction%=72000u;
        uint32_t issued=controller.issued;
        mailbox_poll(&mailbox, &controller);
        if (controller.issued!=issued) oled_cancel(&oled);
        uint32_t sr = USART_SR;
        if (!line.bootloader && tx.used<=48 && (sr & ((1u << 5) | 15u))) {
            char ch = (char)USART_DR; /* SR then DR clears RXNE and receive errors */
            char out[16];
            size_t n = ui_console_feed(&ui,&line,&controller,&adc,&oled,now_ms,ch,(sr & 15u)!=0,out);
            mailbox_publish(&mailbox, &controller);
            if (!tx_push(&tx,out,n)) park();
        }
        if (!line.bootloader) {
            char out[16]; size_t n=oled_poll(&oled,now_ms,out,tx.used<=48);
            if (!tx_push(&tx,out,n)) park();
            uint32_t before_ui=controller.issued;
            n=ui_poll(&ui,&controller,&adc,&oled,now_ms,now_ms*1000u+fraction/72u,out,tx.used<=48);
            if (controller.issued!=before_ui) mailbox_publish(&mailbox,&controller);
            if (!tx_push(&tx,out,n)) park();
        }
        char outgoing;
        if ((USART_SR & (1u<<7)) && tx_pop(&tx,&outgoing)) USART_DR=(uint32_t)(unsigned char)outgoing;
        if (line.bootloader && !tx.used) {
            if (!wait_bits(&USART_SR, 1u << 6, 1u << 6)) park();
            REG(BOOT_RAM) = BOOT_REQUEST; REG(BOOT_RAM+4u) = ~BOOT_REQUEST;
            __asm__ volatile ("dsb" ::: "memory");
            REG(0xe000ed0cu) = 0x05fa0004u;
            park();
        }
    }
}

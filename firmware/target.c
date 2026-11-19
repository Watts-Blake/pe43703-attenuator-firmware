#include "core.h"
#define REG(address) (*(volatile uint32_t *)(address))
#define RCC_CR REG(0x40021000u)
#define RCC_CFGR REG(0x40021004u)
#define RCC_APB2ENR REG(0x40021018u)
#define GPIOA_CRH REG(0x40010804u)
#define GPIOB_CRH REG(0x40010c04u)
#define GPIOB_BSRR REG(0x40010c10u)
#define FLASH_ACR REG(0x40022000u)
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
    /* Entry contract: hardware reset, HSI selected, no inherited bootloader state. */
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
    SCB_VTOR = 0x08000000u;
    mailbox_init(&mailbox);
    if (!clock_init()) { mailbox.state = MB_CLOCK_FAULT; park(); }
    SYST_RVR = 0xffffffu; SYST_CVR = 0; SYST_CSR = 5; /* core clock, no IRQ */
    RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 14);
    (void)RCC_APB2ENR;
    pins(0, 0); /* preload low before enabling output drivers */
    GPIOB_CRH = (GPIOB_CRH & ~0x00fff000u) | 0x00222000u; /* PB11..13 2 MHz PP */
    /* PA4..7 remain reset floating inputs. No display or button driver. */
    Controller controller;
    Bus bus = { pins, delay_us, 0 };
    delay_us(0, 1000); /* allow peripheral power-up; does not prove power good */
    controller_boot(&controller, bus); /* command zero before accepting input */
    GPIOA_CRH = (GPIOA_CRH & ~0x00000ff0u) | 0x000004a0u; /* PA9 AF PP, PA10 input */
    USART_BRR = 625u; /* 72 MHz / 115200 = 625, DIV=39 + 1/16 */
    USART_CR1 = (1u << 13) | (1u << 3) | (1u << 2); /* 8N1, no IRQ */
    mailbox_publish(&mailbox, &controller);
    __asm__ volatile ("dmb" ::: "memory"); mailbox.state = MB_READY;
    Line line = { {0}, 0, false };
    for (;;) {
        mailbox_poll(&mailbox, &controller);
        uint32_t sr = USART_SR;
        if (sr & ((1u << 5) | 15u)) {
            char ch = (char)USART_DR; /* SR then DR clears RXNE and receive errors */
            char out[16];
            size_t n = line_feed(&line, &controller, ch, (sr & 15u) != 0, out);
            mailbox_publish(&mailbox, &controller);
            for (size_t i = 0; i < n; ++i) {
                if (!wait_bits(&USART_SR, 1u << 7, 1u << 7)) break;
                USART_DR = (uint32_t)(unsigned char)out[i];
            }
        }
    }
}

.syntax unified
.cpu cortex-m3
.thumb
.section .isr_vector,"a",%progbits
.global vectors
vectors:
.word _estack
.word Reset_Handler
.rept 5
.word Default_Handler
.endr
.rept 4
.word 0
.endr
.word Default_Handler /* SVCall */
.word Default_Handler /* DebugMonitor */
.word 0
.word Default_Handler /* PendSV */
.word Default_Handler /* SysTick */
.rept 43 /* WWDG through USBWakeUp: medium-density STM32F103 */
.word Default_Handler
.endr

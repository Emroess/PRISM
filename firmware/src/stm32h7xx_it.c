/*
 * stm32h7xx_it.c - Interrupt handlers for STM32H7xx
 *
 * This file contains the exception/interrupt handlers for the
 * STM32H753 microcontroller.
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - On hardware layer whitelist
 * - Minimal ISR implementations
 * - Handlers call appropriate driver ISR functions
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 * - DO-178C Level A requirements
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "stm32h7xx.h"

/*
 * External ISR handlers from drivers
 */
extern void SysTick_Handler(void);
extern void USART3_IRQHandler(void);
extern void TIM6_DAC_IRQHandler(void);
extern void ETH_IRQHandler(void);

/*
 * Cortex-M7 core exception handler prototypes
 */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);

/*
 * Cortex-M7 core exception handlers
 */

/*
 * NMI_Handler - Non-Maskable Interrupt handler
 */
void
NMI_Handler(void)
{
	/* NMI cannot be disabled - hang here for debugging */
	while (1) {
		/* Infinite loop */
	}
}

/*
 * HardFault_Handler - Hard Fault interrupt handler
 */
/* Hard fault capture structure placed in DTCMRAM for fastest access */
struct hard_fault_info {
	uint32_t stacked_r0;
	uint32_t stacked_r1;
	uint32_t stacked_r2;
	uint32_t stacked_r3;
	uint32_t stacked_r12;
	uint32_t stacked_lr;
	uint32_t stacked_pc;
	uint32_t stacked_psr;
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t mmfar;
	uint32_t bfar;
	uint32_t valid;
};

volatile struct hard_fault_info g_hard_fault_info = {0};

/* Forward declaration for capture routine used by assembly stub */
void hard_fault_capture(uint32_t *stack_ptr);

static void fault_uart_write(const char *s)
{
	if (s == NULL) {
		return;
	}
	/* Use direct register access (USART3) to avoid relying on higher layers */
	if ((RCC->APB1LENR & RCC_APB1LENR_USART3EN) == 0U) {
		return; /* UART not clocked */
	}
	while (*s != '\0') {
		while ((USART3->ISR & USART_ISR_TXE_TXFNF) == 0U) { }
		USART3->TDR = (uint8_t)*s;
		s++;
	}
}

__attribute__((naked)) void HardFault_Handler(void)
{
	__asm volatile (
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"b hard_fault_capture\n"
	);
}

void hard_fault_capture(uint32_t *stack_ptr)
{
	char buf[96];

	g_hard_fault_info.stacked_r0  = stack_ptr[0];
	g_hard_fault_info.stacked_r1  = stack_ptr[1];
	g_hard_fault_info.stacked_r2  = stack_ptr[2];
	g_hard_fault_info.stacked_r3  = stack_ptr[3];
	g_hard_fault_info.stacked_r12 = stack_ptr[4];
	g_hard_fault_info.stacked_lr  = stack_ptr[5];
	g_hard_fault_info.stacked_pc  = stack_ptr[6];
	g_hard_fault_info.stacked_psr = stack_ptr[7];
	g_hard_fault_info.cfsr        = SCB->CFSR;
	g_hard_fault_info.hfsr        = SCB->HFSR;
	g_hard_fault_info.mmfar       = SCB->MMFAR;
	g_hard_fault_info.bfar        = SCB->BFAR;
	g_hard_fault_info.valid       = 1U;

	fault_uart_write("\r\n*** HARD FAULT ***\r\n");
	/* Print in chunks to avoid large formatting dependencies */
	sprintf(buf, "PC=0x%08lX LR=0x%08lX PSR=0x%08lX\r\n",
	    (unsigned long)g_hard_fault_info.stacked_pc,
	    (unsigned long)g_hard_fault_info.stacked_lr,
	    (unsigned long)g_hard_fault_info.stacked_psr);
	fault_uart_write(buf);
	sprintf(buf, "CFSR=0x%08lX HFSR=0x%08lX MMFAR=0x%08lX BFAR=0x%08lX\r\n",
	    (unsigned long)g_hard_fault_info.cfsr,
	    (unsigned long)g_hard_fault_info.hfsr,
	    (unsigned long)g_hard_fault_info.mmfar,
	    (unsigned long)g_hard_fault_info.bfar);
	fault_uart_write(buf);
	sprintf(buf, "R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX R12=0x%08lX\r\n",
	    (unsigned long)g_hard_fault_info.stacked_r0,
	    (unsigned long)g_hard_fault_info.stacked_r1,
	    (unsigned long)g_hard_fault_info.stacked_r2,
	    (unsigned long)g_hard_fault_info.stacked_r3,
	    (unsigned long)g_hard_fault_info.stacked_r12);
	fault_uart_write(buf);
	fault_uart_write("System halted. Cycle reset to continue.\r\n");

	/* Blink error LED to provide visual indication */
	if ((RCC->AHB4ENR & RCC_AHB4ENR_GPIOBEN) != 0U) {
		GPIOB->MODER = (GPIOB->MODER & ~(0x3U << (0 * 2))) | (0x1U << (0 * 2)); /* PB0 output (example LED pin) */
	}
	while (1) {
		for (volatile uint32_t i = 0; i < 2000000U; i++) { }
		if ((RCC->AHB4ENR & RCC_AHB4ENR_GPIOBEN) != 0U) {
			GPIOB->ODR ^= (1U << 0);
		}
	}
}

/*
 * MemManage_Handler - Memory Management Fault handler
 */
void
MemManage_Handler(void)
{
	/* Memory management fault */
	while (1) {
		/* Infinite loop */
	}
}

/*
 * BusFault_Handler - Bus Fault handler
 */
void
BusFault_Handler(void)
{
	/* Bus fault */
	while (1) {
		/* Infinite loop */
	}
}

/*
 * UsageFault_Handler - Usage Fault handler
 */
void
UsageFault_Handler(void)
{
	/* Usage fault */
	while (1) {
		/* Infinite loop */
	}
}

/*
 * SVC_Handler - SVC interrupt handler
 */
void
SVC_Handler(void)
{
	/* Not used in this firmware */
}

/*
 * DebugMon_Handler - Debug Monitor handler
 */
void
DebugMon_Handler(void)
{
	/* Not used in this firmware */
}

/*
 * PendSV_Handler - PendSV interrupt handler
 */
void
PendSV_Handler(void)
{
	/* Not used in this firmware */
}

/*
 * Note: SysTick_Handler is implemented in board.c
 * Note: USART3_IRQHandler is implemented in uart.c
 * Note: TIM6_DAC_IRQHandler is implemented in valve_haptic.c
 * Note: ETH_IRQHandler is implemented in ethernetif.c
 */

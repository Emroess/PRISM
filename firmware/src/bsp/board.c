/*
 * board.c - Board Support Package implementation for NUCLEO-H753ZI
 *
 * This is the SINGLE AUTHORITY for all hardware configuration.
 * Per IMPROVE_FIRMWARE_PLAN.md Option A:
 * - SystemInit() does minimal FPU setup only
 * - board_init() configures ALL clocks, power, GPIO, peripherals
 *
 * CRITICAL: This file is on the hardware layer whitelist.
 * Application code must NOT include stm32h7xx.h directly.
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 * - DO-178C Level A requirements
 * - Linux kernel coding style
 */

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx.h"

#include "board.h"
#include "config/board.h"
#include "status.h"

/*
 * Some CMSIS headers omit the APB4 PWR clock bit definition.
 * Define it locally if missing so we can explicitly enable the
 * PWR interface before touching voltage-scaling registers.
 */
#ifndef RCC_APB4ENR_PWREN
#define RCC_APB4ENR_PWREN        (0x1U)
#endif

/* Some HAL headers don't expose the IOSV bit that powers VDDIO2. */
#ifndef PWR_CR2_IOSV
#define PWR_CR2_IOSV              (0x1UL << 9)
#endif

/*
 * Global variables
 */
static volatile uint32_t systick_counter_ms = 0;

/* GPIO port mapping table (logical to hardware) */
static GPIO_TypeDef* const gpio_ports[] = {
	[BOARD_GPIO_PORT_A] = GPIOA,
	[BOARD_GPIO_PORT_B] = GPIOB,
	[BOARD_GPIO_PORT_C] = GPIOC,
	[BOARD_GPIO_PORT_D] = GPIOD,
	[BOARD_GPIO_PORT_E] = GPIOE,
	[BOARD_GPIO_PORT_F] = GPIOF,
	[BOARD_GPIO_PORT_G] = GPIOG,
	[BOARD_GPIO_PORT_H] = GPIOH,
};

/*
 * Forward declarations
 */
static status_t configure_power_regulator(void);
static status_t configure_flash_latency(void);
static status_t configure_pll1(void);
static status_t configure_clock_tree(void);
static status_t configure_gpio_clocks(void);
static status_t configure_gpio_pins(void);
static status_t configure_systick(void);
static uint32_t get_hpre_divider(uint32_t hpre_bits);

/* ISR handlers */
void SysTick_Handler(void);

/*
 * board_init - Initialize all board hardware
 */
status_t
board_init(void)
{
	status_t status;

	/* Configure LEDs first for early diagnostics */
	status = configure_gpio_clocks();
	if (status != STATUS_OK) {
		return status;
	}

	/* Configure MPU for Ethernet DMA cache coherency */
	/* WHY: Must be called early to protect DMA buffers before any DMA operations */
	MPU_Config();

	status = configure_gpio_pins();
	if (status != STATUS_OK) {
		return status;
	}

	/* Turn on boot LED to indicate initialization started */
	board_led_on(BOARD_LED_BOOT);
	
	/* Add a visible delay so we know we got this far */
	for (volatile uint32_t i = 0; i < 10000000; i++) {
		/* busy wait to see LED */
	}

	/* Configure power regulator for VOS1 before raising clocks */
	status = configure_power_regulator();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}
	
	/* Configure flash latency before increasing clock speed */
	status = configure_flash_latency();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}

	/* Configure PLL1 for 400 MHz system clock */
	status = configure_pll1();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}

	/* Switch to PLL1 and configure dividers */
	status = configure_clock_tree();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}

	/* Update SystemCoreClock variable (CMSIS requirement) */
	SystemCoreClock = BOARD_SYSCLK_HZ;

	/* Configure SysTick for 1ms timebase */
	status = configure_systick();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}

	/* Validate the final clock/power configuration */
	status = board_validate();
	if (status != STATUS_OK) {
		board_panic(status, __FILE__, __LINE__);
	}

	return STATUS_OK;
}

/*
 * board_validate - Verify hardware configuration
 */
status_t
board_validate(void)
{
	uint32_t sysclk;
	uint32_t hclk;
	uint32_t hpre_bits;
	uint32_t hpre_div;

	/* Verify system clock */
	sysclk = SystemCoreClock;
	if (sysclk != BOARD_SYSCLK_HZ) {
		return STATUS_ERROR_INVALID_CONFIG;
	}

	/* Verify HCLK divider */
	hpre_bits = (RCC->D1CFGR & RCC_D1CFGR_HPRE);
	hpre_div = get_hpre_divider(hpre_bits);
	if (hpre_div == 0U) {
		return STATUS_ERROR_INVALID_CONFIG;
	}

	hclk = sysclk / hpre_div;
	if (hclk != BOARD_HCLK_HZ) {
		return STATUS_ERROR_INVALID_CONFIG;
	}

	/* Verify power regulator is in VOS1 (bits should be 11b) */
	if ((PWR->D3CR & PWR_D3CR_VOS) != (PWR_D3CR_VOS_1 | PWR_D3CR_VOS_0)) {
		return STATUS_ERROR_INVALID_CONFIG;
	}

	return STATUS_OK;
}

/*
 * configure_power_regulator - Set VOS1 for 400 MHz operation
 */
__attribute__((unused))
static status_t
configure_power_regulator(void)
{
	uint32_t timeout;

	/* Ensure the PWR interface clock is enabled */
	RCC->APB4ENR |= RCC_APB4ENR_PWREN;
	(void)RCC->APB4ENR;  /* Dummy read to guarantee write completion */

	/* Enable VDDIO2 domain so GPIOG Ethernet pins are actually driven */
	PWR->CR2 |= PWR_CR2_IOSV;

	/*
	 * Force LDO mode (NUCLEO-H753ZI supplies VDD via LDO) and make sure
	 * the regulator is ready before changing the VOS level. This mirrors
	 * the sequence recommended in RM0433.
	 */
	PWR->CR3 &= ~PWR_CR3_BYPASS;
	PWR->CR3 |= PWR_CR3_LDOEN;

	timeout = 100000U;
	while ((PWR->CSR1 & PWR_CSR1_ACTVOSRDY) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	/* 
	 * Ensure we start from a known state - system should be on HSI (64MHz)
	 * after reset, which is safe for any VOS level transition
	 */

	/* Clear VOS bits first */
	PWR->D3CR &= ~PWR_D3CR_VOS;
	
	/* Configure VOS1 (Scale 1) for maximum performance (400 MHz) */
	PWR->D3CR |= PWR_D3CR_VOS_1 | PWR_D3CR_VOS_0;  /* VOS = 11b (VOS1) */

	/* Wait for voltage scaling to complete */
	timeout = 100000U;
	while ((PWR->D3CR & PWR_D3CR_VOSRDY) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	return STATUS_OK;
}

/*
 * configure_flash_latency - Set flash wait states for 400 MHz
 */
static status_t
configure_flash_latency(void)
{
	/*
	 * Per RM0433 Table 13:
	 * 400 MHz @ VOS1 requires 4 wait states (LATENCY = 4)
	 */
	FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | 
	             (BOARD_FLASH_LATENCY << FLASH_ACR_LATENCY_Pos);

	/* Verify latency was set */
	if (((FLASH->ACR & FLASH_ACR_LATENCY) >> FLASH_ACR_LATENCY_Pos) != 
	    BOARD_FLASH_LATENCY) {
		return STATUS_ERROR_HARDWARE_FAULT;
	}

	return STATUS_OK;
}

/*
 * configure_pll1 - Configure PLL1 for system and FDCAN clocks
 */
static status_t
configure_pll1(void)
{
	uint32_t timeout;

	/*
	 * Configure HSE oscillator/bypass mode.
	 * Per STM32H7 Reference Manual section 8.7.2:
	 * "The HSEBYP bit can be written only if the HSE oscillator is disabled"
	 *
	 * Sequence:
	 * 1. Ensure HSE is disabled (clear HSEON)
	 * 2. Wait for HSERDY to clear
	 * 3. Program HSEBYP based on board configuration
	 * 4. Enable HSE
	 * 5. Wait for HSERDY
	 */

	/* Step 1: Disable HSE if it's on */
	if ((RCC->CR & RCC_CR_HSEON) != 0U) {
		RCC->CR &= ~RCC_CR_HSEON;

		/* Step 2: Wait for HSE to turn off */
		timeout = 100000U;
		while ((RCC->CR & RCC_CR_HSERDY) != 0U) {
			timeout--;
			if (timeout == 0U) {
				return STATUS_ERROR_TIMEOUT;
			}
		}
	}

	/* Step 3: Program bypass mode */
	if (BOARD_HSE_BYPASS_MODE != 0U) {
		RCC->CR |= RCC_CR_HSEBYP;
	} else {
		RCC->CR &= ~RCC_CR_HSEBYP;
	}

	/* Step 4: Enable HSE */
	RCC->CR |= RCC_CR_HSEON;

	/* Step 5: Wait for HSE ready */
	timeout = 100000U;
	while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	/* Disable PLL1 before configuration */
	RCC->CR &= ~RCC_CR_PLL1ON;
	timeout = 100000U;
	while ((RCC->CR & RCC_CR_PLL1RDY) != 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	/* Select HSE as PLL1 source */
	RCC->PLLCKSELR = (RCC->PLLCKSELR & ~RCC_PLLCKSELR_PLLSRC) | 
	                 RCC_PLLCKSELR_PLLSRC_HSE;

	/* Configure PLL1 dividers */
	RCC->PLLCKSELR = (RCC->PLLCKSELR & ~RCC_PLLCKSELR_DIVM1) | 
	                 (BOARD_PLL1_M << RCC_PLLCKSELR_DIVM1_Pos);

	RCC->PLL1DIVR = ((BOARD_PLL1_N - 1U) << RCC_PLL1DIVR_N1_Pos) |
	                ((BOARD_PLL1_P - 1U) << RCC_PLL1DIVR_P1_Pos) |
	                ((BOARD_PLL1_Q - 1U) << RCC_PLL1DIVR_Q1_Pos) |
	                ((BOARD_PLL1_R - 1U) << RCC_PLL1DIVR_R1_Pos);

	/*
	 * Configure PLL1 input frequency range.
	 * PLL input = BOARD_HSE_VALUE_HZ / BOARD_PLL1_M.
	 * With 8MHz/2 = 4 MHz → use 4-8 MHz range (RGE=10b).
	 */
	RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_PLL1RGE) | 
	               RCC_PLLCFGR_PLL1RGE_2;  /* 4-8 MHz input range */

	/*
	 * Configure PLL1 VCO range
	 * VCO = (HSE/M)*N = 4MHz*200 = 800 MHz → use wide VCO (192-836 MHz)
	 */
	RCC->PLLCFGR |= RCC_PLLCFGR_PLL1VCOSEL;  /* Wide VCO range */

	/* Configure PLL1 fractional divider to 0 (integer mode) */
	RCC->PLL1FRACR = 0U;

	/* Enable PLL1 P and Q outputs */
	RCC->PLLCFGR |= RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN;

	/* Enable PLL1 */
	RCC->CR |= RCC_CR_PLL1ON;

	/* Wait for PLL1 ready */
	timeout = 100000U;
	while ((RCC->CR & RCC_CR_PLL1RDY) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	return STATUS_OK;
}

/*
 * configure_clock_tree - Switch to PLL1 and configure dividers
 */
static status_t
configure_clock_tree(void)
{
	uint32_t timeout;

	/* Configure D1 domain (AHB/AXI) prescaler: SYSCLK/2 = 200 MHz */
	RCC->D1CFGR = (RCC->D1CFGR & ~RCC_D1CFGR_HPRE) | 
	              RCC_D1CFGR_HPRE_DIV2;

	/* Configure APB3 prescaler: HCLK/2 = 100 MHz */
	RCC->D1CFGR = (RCC->D1CFGR & ~RCC_D1CFGR_D1PPRE) | 
	              RCC_D1CFGR_D1PPRE_DIV2;

	/* Configure D2 domain APB1 and APB2 prescalers: HCLK/2 = 100 MHz */
	RCC->D2CFGR = (RCC->D2CFGR & ~RCC_D2CFGR_D2PPRE1) | 
	              RCC_D2CFGR_D2PPRE1_DIV2;
	RCC->D2CFGR = (RCC->D2CFGR & ~RCC_D2CFGR_D2PPRE2) | 
	              RCC_D2CFGR_D2PPRE2_DIV2;

	/* Configure D3 domain APB4 prescaler: HCLK/2 = 100 MHz */
	RCC->D3CFGR = (RCC->D3CFGR & ~RCC_D3CFGR_D3PPRE) | 
	              RCC_D3CFGR_D3PPRE_DIV2;

	/*
	 * CRITICAL: Configure USART28 kernel clock source
	 * 
	 * On STM32H7, many peripherals (USART, SPI, I2C, etc.) have selectable
	 * kernel clock sources via RCC_DxCCIPxR registers. The peripheral clock
	 * enable (RCC_APBxENR) only enables the bus interface clock - the
	 * functional kernel clock MUST be explicitly selected here.
	 * 
	 * For USART2/3/4/5/7/8, the kernel clock source is selected via
	 * RCC_D2CCIP2R[USART28SEL] bits:
	 *   000 = rcc_pclk1 (APB1 clock) - selected here
	 *   001 = pll2_q_ck
	 *   010 = pll3_q_ck
	 *   011 = hsi_ker_ck
	 *   100 = csi_ker_ck
	 *   101 = lse_ck
	 * 
	 * Without this configuration, USART will not function even if APB1LENR
	 * is set, because the baud rate generator has no clock source.
	 * 
	 * See RM0433 Rev 7, Section 8.7.30 "RCC Domain 2 Kernel Clock Configuration Register"
	 */
	RCC->D2CCIP2R = (RCC->D2CCIP2R & ~RCC_D2CCIP2R_USART28SEL) | 
	                (0U << RCC_D2CCIP2R_USART28SEL_Pos);  /* 000 = PCLK */

	/* Switch system clock to PLL1 */
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;

	/* Wait for switch to complete */
	timeout = 100000U;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL1) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	return STATUS_OK;
}

/*
 * configure_gpio_clocks - Enable GPIO port clocks
 */
static status_t
configure_gpio_clocks(void)
{
	/* Enable GPIO clocks for ports B and D */
	RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN;
	(void)RCC->AHB4ENR;  /* Read back to ensure write completion */

	return STATUS_OK;
}

/*
 * configure_gpio_pins - Configure GPIO pins for LEDs and peripherals
 */
static status_t
configure_gpio_pins(void)
{
	GPIO_TypeDef *gpiob;
	GPIO_TypeDef *gpiod;

	gpiob = gpio_ports[BOARD_GPIO_PORT_B];
	gpiod = gpio_ports[BOARD_GPIO_PORT_D];

	/* Configure LED pins as outputs (PB0, PB7, PB14) */
	gpiob->MODER = (gpiob->MODER & ~GPIO_MODER_MODE0) | 
	               (0x1U << GPIO_MODER_MODE0_Pos);  /* Output */
	gpiob->MODER = (gpiob->MODER & ~GPIO_MODER_MODE7) | 
	               (0x1U << GPIO_MODER_MODE7_Pos);  /* Output */
	gpiob->MODER = (gpiob->MODER & ~GPIO_MODER_MODE14) | 
	               (0x1U << GPIO_MODER_MODE14_Pos);  /* Output */

	/* Set output type to push-pull */
	gpiob->OTYPER &= ~(GPIO_OTYPER_OT0 | GPIO_OTYPER_OT7 | GPIO_OTYPER_OT14);

	/* Set speed to low (LEDs don't need high speed) */
	gpiob->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED0 | GPIO_OSPEEDR_OSPEED7 | 
	                    GPIO_OSPEEDR_OSPEED14);

	/* No pull-up/pull-down */
	gpiob->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD7 | GPIO_PUPDR_PUPD14);

	/* Configure USART3 pins (PD8=TX, PD9=RX) as alternate function */
	gpiod->MODER = (gpiod->MODER & ~GPIO_MODER_MODE8) | 
	               (0x2U << GPIO_MODER_MODE8_Pos);  /* AF */
	gpiod->MODER = (gpiod->MODER & ~GPIO_MODER_MODE9) | 
	               (0x2U << GPIO_MODER_MODE9_Pos);  /* AF */

	/* Set alternate function to AF7 (USART3) */
	gpiod->AFR[1] = (gpiod->AFR[1] & ~GPIO_AFRH_AFSEL8) | 
	                (BOARD_USART3_TX_AF << GPIO_AFRH_AFSEL8_Pos);
	gpiod->AFR[1] = (gpiod->AFR[1] & ~GPIO_AFRH_AFSEL9) | 
	                (BOARD_USART3_RX_AF << GPIO_AFRH_AFSEL9_Pos);

	/* High speed for USART */
	gpiod->OSPEEDR |= (0x3U << GPIO_OSPEEDR_OSPEED8_Pos) | 
	                  (0x3U << GPIO_OSPEEDR_OSPEED9_Pos);

	/* Push-pull */
	gpiod->OTYPER &= ~(GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9);

	/* Pull-up for RX */
	gpiod->PUPDR = (gpiod->PUPDR & ~GPIO_PUPDR_PUPD9) | 
	               (0x1U << GPIO_PUPDR_PUPD9_Pos);

	/* Configure FDCAN1 pins (PD0=RX, PD1=TX) as alternate function */
	gpiod->MODER = (gpiod->MODER & ~GPIO_MODER_MODE0) | 
	               (0x2U << GPIO_MODER_MODE0_Pos);  /* AF */
	gpiod->MODER = (gpiod->MODER & ~GPIO_MODER_MODE1) | 
	               (0x2U << GPIO_MODER_MODE1_Pos);  /* AF */

	/* Set alternate function to AF9 (FDCAN1) */
	gpiod->AFR[0] = (gpiod->AFR[0] & ~GPIO_AFRL_AFSEL0) | 
	                (BOARD_FDCAN1_RX_AF << GPIO_AFRL_AFSEL0_Pos);
	gpiod->AFR[0] = (gpiod->AFR[0] & ~GPIO_AFRL_AFSEL1) | 
	                (BOARD_FDCAN1_TX_AF << GPIO_AFRL_AFSEL1_Pos);

	/* High speed for FDCAN */
	gpiod->OSPEEDR |= (0x3U << GPIO_OSPEEDR_OSPEED0_Pos) | 
	                  (0x3U << GPIO_OSPEEDR_OSPEED1_Pos);

	/* Push-pull */
	gpiod->OTYPER &= ~(GPIO_OTYPER_OT0 | GPIO_OTYPER_OT1);

	/* No pull-up/pull-down (external termination) */
	gpiod->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1);

	return STATUS_OK;
}

/*
 * configure_systick - Configure SysTick for 1ms timebase
 */
static status_t
configure_systick(void)
{
	uint32_t reload_value;

	/*
	 * SysTick clock source is the processor clock (SystemCoreClock).
	 * With SYSCLK = 400 MHz this yields a 1 ms period when dividing by 1000.
	 */
	reload_value = (BOARD_SYSCLK_HZ / 1000U) - 1U;  /* 1 ms period */

	SysTick->LOAD = reload_value;
	SysTick->VAL = 0U;  /* Clear current value */
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |  /* Use processor clock */
	                SysTick_CTRL_TICKINT_Msk |    /* Enable interrupt */
	                SysTick_CTRL_ENABLE_Msk;      /* Enable SysTick */

	return STATUS_OK;
}

/*
 * get_hpre_divider - Decode HPRE prescaler field
 */
static uint32_t
get_hpre_divider(uint32_t hpre_bits)
{
	switch (hpre_bits) {
	case RCC_D1CFGR_HPRE_DIV1:
		return 1U;
	case RCC_D1CFGR_HPRE_DIV2:
		return 2U;
	case RCC_D1CFGR_HPRE_DIV4:
		return 4U;
	case RCC_D1CFGR_HPRE_DIV8:
		return 8U;
	case RCC_D1CFGR_HPRE_DIV16:
		return 16U;
	case RCC_D1CFGR_HPRE_DIV64:
		return 64U;
	case RCC_D1CFGR_HPRE_DIV128:
		return 128U;
	case RCC_D1CFGR_HPRE_DIV256:
		return 256U;
	case RCC_D1CFGR_HPRE_DIV512:
		return 512U;
	default:
		return 0U;
	}
}

/*
 * SysTick_Handler - SysTick interrupt handler
 */
void
SysTick_Handler(void)
{
	systick_counter_ms++;
}

/*
 * board_get_systick_ms - Get millisecond counter
 */
uint32_t
board_get_systick_ms(void)
{
	return systick_counter_ms;
}

/*
 * board_delay_ms - Blocking delay in milliseconds
 */
void
board_delay_ms(uint32_t ms)
{
	uint32_t start;
	uint32_t elapsed;

	start = systick_counter_ms;
	do {
		elapsed = systick_counter_ms - start;
	} while (elapsed < ms);
}

/*
 * board_led_on - Turn on specified LED
 */
void
board_led_on(enum board_led led)
{
	GPIO_TypeDef *port;
	uint32_t pin;

	switch (led) {
	case BOARD_LED_BOOT:
		port = gpio_ports[BOARD_LED_BOOT_PORT];
		pin = BOARD_LED_BOOT_PIN;
		break;
	case BOARD_LED_ERROR:
		port = gpio_ports[BOARD_LED_ERROR_PORT];
		pin = BOARD_LED_ERROR_PIN;
		break;
	case BOARD_LED_STATUS:
		port = gpio_ports[BOARD_LED_STATUS_PORT];
		pin = BOARD_LED_STATUS_PIN;
		break;
	default:
		return;
	}

	port->BSRR = (1U << pin);  /* Set bit (turn on LED) */
}

/*
 * board_led_off - Turn off specified LED
 */
void
board_led_off(enum board_led led)
{
	GPIO_TypeDef *port;
	uint32_t pin;

	switch (led) {
	case BOARD_LED_BOOT:
		port = gpio_ports[BOARD_LED_BOOT_PORT];
		pin = BOARD_LED_BOOT_PIN;
		break;
	case BOARD_LED_ERROR:
		port = gpio_ports[BOARD_LED_ERROR_PORT];
		pin = BOARD_LED_ERROR_PIN;
		break;
	case BOARD_LED_STATUS:
		port = gpio_ports[BOARD_LED_STATUS_PORT];
		pin = BOARD_LED_STATUS_PIN;
		break;
	default:
		return;
	}

	port->BSRR = (1U << (pin + 16U));  /* Reset bit (turn off LED) */
}

/*
 * board_led_toggle - Toggle specified LED
 */
void
board_led_toggle(enum board_led led)
{
	GPIO_TypeDef *port;
	uint32_t pin;

	switch (led) {
	case BOARD_LED_BOOT:
		port = gpio_ports[BOARD_LED_BOOT_PORT];
		pin = BOARD_LED_BOOT_PIN;
		break;
	case BOARD_LED_ERROR:
		port = gpio_ports[BOARD_LED_ERROR_PORT];
		pin = BOARD_LED_ERROR_PIN;
		break;
	case BOARD_LED_STATUS:
		port = gpio_ports[BOARD_LED_STATUS_PORT];
		pin = BOARD_LED_STATUS_PIN;
		break;
	default:
		return;
	}

	port->ODR ^= (1U << pin);  /* Toggle bit */
}

/*
 * board_get_sysclk_hz - Get system clock frequency
 */
uint32_t
board_get_sysclk_hz(void)
{
	return SystemCoreClock;
}

/*
 * board_get_unique_id - Get STM32 unique device ID
 */
void
board_get_unique_id(uint32_t id[3])
{
	id[0] = *(volatile uint32_t *)(UID_BASE);
	id[1] = *(volatile uint32_t *)(UID_BASE + 4U);
	id[2] = *(volatile uint32_t *)(UID_BASE + 8U);
}

/*
 * board_panic - Critical error handler (never returns)
 *
 * Per IMPROVE_FIRMWARE_PLAN.md section 0.5:
 * - Zero dependencies (inline register writes only)
 * - No function calls except absolute minimum
 */
void
board_panic(status_t error_code, const char *file, int line)
{
	volatile uint32_t delay;
	uint8_t blink_count;
	uint8_t i;

	(void)file;  /* Unused in this implementation */
	(void)line;  /* Unused in this implementation */

	/* Disable all interrupts immediately */
	__disable_irq();

	/* Turn off boot LED, turn on error LED (direct register access) */
	GPIOB->BSRR = (1U << (BOARD_LED_BOOT_PIN + 16U));   /* Off */
	GPIOB->BSRR = (1U << BOARD_LED_ERROR_PIN);          /* On */

	/* Blink pattern based on error code (1-15 blinks) */
	blink_count = (uint8_t)(error_code & 0x0FU);
	if (blink_count == 0U) {
		blink_count = 1U;
	}

	/* Infinite loop with blink pattern */
	while (1) {
		/* Blink error code */
		for (i = 0; i < blink_count; i++) {
			GPIOB->BSRR = (1U << BOARD_LED_ERROR_PIN);  /* On */
			for (delay = 0; delay < 1000000U; delay++) {
				/* Busy wait */
			}
			GPIOB->BSRR = (1U << (BOARD_LED_ERROR_PIN + 16U));  /* Off */
			for (delay = 0; delay < 1000000U; delay++) {
				/* Busy wait */
			}
		}
		/* Long pause before repeating */
		for (delay = 0; delay < 5000000U; delay++) {
			/* Busy wait */
		}
	}
}

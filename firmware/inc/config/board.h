/*
 * config/board.h - Hardware configuration for NUCLEO-H753ZI
 *
 * This is the SINGLE SOURCE OF TRUTH for all hardware configuration.
 * Per IMPROVE_FIRMWARE_PLAN.md section 0.2:
 * - NO hardware register types (GPIOx, RCC, etc.) allowed in this file
 * - Only logical identifiers and compile-time constants
 * - All clock frequencies defined here and validated
 *
 * Clock Configuration Ownership: OPTION A (board.c owns all hardware)
 * SystemInit() does minimal FPU setup only
 * board_init() configures ALL clocks, power, GPIO, peripherals
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 * - DO-178C Level A requirements
 */

#ifndef CONFIG_BOARD_H
#define CONFIG_BOARD_H

#include <stdint.h>

/*
 * ===========================================================================
 * Clock Configuration Ownership
 * ===========================================================================
 */
#define CLOCK_CONFIG_OWNER_BOARD        1
#define CLOCK_CONFIG_OWNER_SYSTEMINIT   2
#define CLOCK_CONFIG_OWNER              CLOCK_CONFIG_OWNER_BOARD

#if CLOCK_CONFIG_OWNER == CLOCK_CONFIG_OWNER_BOARD
#define SYSTEMINIT_MINIMAL 1
#elif CLOCK_CONFIG_OWNER == CLOCK_CONFIG_OWNER_SYSTEMINIT
#define BOARD_INIT_IS_VALIDATOR_ONLY 1
#else
#error "Must define CLOCK_CONFIG_OWNER"
#endif

/*
 * ===========================================================================
 * Crystal and External Clock Configuration
 * ===========================================================================
 * NUCLEO-H753ZI uses the ST-LINK MCO to drive HSE with an 8 MHz square wave.
 * Set BOARD_HSE_BYPASS_MODE to 1 because the HSE pin is not using a crystal.
 */
#define BOARD_HSE_VALUE_HZ      8000000U   /* 8 MHz MCO from ST-LINK */
#define BOARD_HSE_BYPASS_MODE   1U         /* Bypass mode required for MCO */
#define BOARD_LSE_VALUE_HZ      32768U     /* 32.768 kHz LSE */

/* Compile-time check that HSE matches CMSIS definition */
#if !defined(HSE_VALUE)
#define HSE_VALUE BOARD_HSE_VALUE_HZ
#elif HSE_VALUE != BOARD_HSE_VALUE_HZ
#error "HSE_VALUE mismatch - recalculate entire clock tree"
#endif

#if BOARD_HSE_BYPASS_MODE > 1U
#error "BOARD_HSE_BYPASS_MODE must be 0 (crystal) or 1 (bypass/MCO)"
#endif

/*
 * ===========================================================================
 * Target Clock Frequencies (400 MHz system clock)
 * ===========================================================================
 * Per STM32H753 datasheet and RM0433:
 * - Max SYSCLK: 480 MHz (we use 400 MHz for margin)
 * - VOS1 (Scale 1) required for 400 MHz operation
 * - Flash latency: 4 wait states @ 400 MHz, VOS1, 3.3V
 */
#define BOARD_SYSCLK_HZ         400000000U  /* 400 MHz system clock */
#define BOARD_HCLK_HZ           200000000U  /* 200 MHz AHB (SYSCLK/2) */
#define BOARD_APB1_HZ           100000000U  /* 100 MHz APB1 (HCLK/2) */
#define BOARD_APB2_HZ           100000000U  /* 100 MHz APB2 (HCLK/2) */
#define BOARD_APB3_HZ           100000000U  /* 100 MHz APB3 (HCLK/2) */
#define BOARD_APB4_HZ           100000000U  /* 100 MHz APB4 (HCLK/2) */

/* FDCAN kernel clock (from PLL1Q) */
#define BOARD_FDCAN_KERNEL_HZ   100000000U  /* 100 MHz for clean bit timing */

/* USART3 kernel clock (from APB1) */
#define BOARD_USART3_KERNEL_HZ  BOARD_APB1_HZ

/*
 * PLL Configuration
 * PLL1: Main PLL for system clock
 * Input: HSE = 8 MHz (ST-LINK MCO)
 * VCO: 8 MHz / 2 * 200 = 800 MHz (within 192-836 MHz wide range)
 * P: 800 / 2 = 400 MHz (SYSCLK)
 * Q: 800 / 8 = 100 MHz (FDCAN kernel)
 * R: 800 / 2 = 400 MHz (unused but valid)
 * 
 * PLL input = 8/2 = 4 MHz → RGE = 10b (4-8 MHz range)
 */
#define BOARD_PLL1_M            2U
#define BOARD_PLL1_N            200U
#define BOARD_PLL1_P            2U
#define BOARD_PLL1_Q            8U
#define BOARD_PLL1_R            2U

/* Flash latency for 400 MHz @ VOS1 */
#define BOARD_FLASH_LATENCY     4U

/*
 * ===========================================================================
 * GPIO Port Logical IDs (no hardware register types)
 * ===========================================================================
 */
enum board_gpio_port {
	BOARD_GPIO_PORT_A = 0,
	BOARD_GPIO_PORT_B = 1,
	BOARD_GPIO_PORT_C = 2,
	BOARD_GPIO_PORT_D = 3,
	BOARD_GPIO_PORT_E = 4,
	BOARD_GPIO_PORT_F = 5,
	BOARD_GPIO_PORT_G = 6,
	BOARD_GPIO_PORT_H = 7,
};

/*
 * ===========================================================================
 * LED Configuration (NUCLEO-H753ZI)
 * ===========================================================================
 * LED1 (Green):  PB0
 * LED2 (Yellow): PB7 (used as error indicator)
 * LED3 (Red):    PB14
 */
#define BOARD_LED_BOOT_PORT     BOARD_GPIO_PORT_B
#define BOARD_LED_BOOT_PIN      0U    /* Green LED - boot indicator */

#define BOARD_LED_ERROR_PORT    BOARD_GPIO_PORT_B
#define BOARD_LED_ERROR_PIN     7U    /* Yellow LED - error indicator */

#define BOARD_LED_STATUS_PORT   BOARD_GPIO_PORT_B
#define BOARD_LED_STATUS_PIN    14U   /* Red LED - status indicator */

/*
 * ===========================================================================
 * USART3 Configuration (ST-LINK VCP)
 * ===========================================================================
 * TX: PD8, AF7
 * RX: PD9, AF7
 * Baud rate: 115200 (standard)
 */
#define BOARD_USART3_TX_PORT    BOARD_GPIO_PORT_D
#define BOARD_USART3_TX_PIN     8U
#define BOARD_USART3_TX_AF      7U

#define BOARD_USART3_RX_PORT    BOARD_GPIO_PORT_D
#define BOARD_USART3_RX_PIN     9U
#define BOARD_USART3_RX_AF      7U

#define BOARD_USART3_BAUDRATE   115200U

/*
 * ===========================================================================
 * FDCAN1 Configuration
 * ===========================================================================
 * TX: PD1, AF9
 * RX: PD0, AF9
 * Bit rate: 1 Mbps (standard CAN)
 * Sample point: 87.5% (recommended for CAN)
 */
#define BOARD_FDCAN1_TX_PORT    BOARD_GPIO_PORT_D
#define BOARD_FDCAN1_TX_PIN     1U
#define BOARD_FDCAN1_TX_AF      9U

#define BOARD_FDCAN1_RX_PORT    BOARD_GPIO_PORT_D
#define BOARD_FDCAN1_RX_PIN     0U
#define BOARD_FDCAN1_RX_AF      9U

#define BOARD_FDCAN1_BITRATE    1000000U    /* 1 Mbps */
#define BOARD_FDCAN1_SAMPLE_POINT_PERCENT 87U

/*
 * FDCAN Message RAM Configuration
 * Per RM0433 Section 56.3.1, Table 408:
 * FDCAN1 message RAM base: 0x4000AC00
 * Size: 2560 words (10240 bytes)
 */
#define BOARD_FDCAN1_RAM_BASE       0x4000AC00U
#define BOARD_FDCAN1_RAM_SIZE_WORDS 2560U
#define BOARD_FDCAN1_RAM_SIZE_BYTES (BOARD_FDCAN1_RAM_SIZE_WORDS * 4U)

/*
 * ===========================================================================
 * Memory Region Sizes (from linker script)
 * ===========================================================================
 */
#define BOARD_FLASH_SIZE        (2048U * 1024U)  /* 2 MB */
#define BOARD_DTCMRAM_SIZE      (128U * 1024U)   /* 128 KB */
#define BOARD_SRAM1_SIZE        (512U * 1024U)   /* 512 KB AXI SRAM */
#define BOARD_SRAM2_SIZE        (288U * 1024U)   /* 288 KB SRAM1+2 */
#define BOARD_SRAM4_SIZE        (64U * 1024U)    /* 64 KB */

/* Stack and heap sizes (must match linker script) */
#define BOARD_STACK_SIZE        (4U * 1024U)     /* 4 KB */
#define BOARD_HEAP_SIZE         (4U * 1024U)     /* 4 KB */

/*
 * ===========================================================================
 * Static Assertions (compile-time validation)
 * ===========================================================================
 * Per IMPROVE_FIRMWARE_PLAN.md: catch configuration errors at compile time
 */

/* Verify clock tree math is correct */
_Static_assert(BOARD_SYSCLK_HZ == 400000000U,
	"SYSCLK must be 400 MHz - verify PLL configuration");

_Static_assert(BOARD_FDCAN_KERNEL_HZ == 100000000U,
	"FDCAN kernel must be 100 MHz - update timing calculations if changed");

_Static_assert(BOARD_HCLK_HZ == (BOARD_SYSCLK_HZ / 2U),
	"HCLK must be SYSCLK/2 (200 MHz)");

_Static_assert(BOARD_APB1_HZ == (BOARD_HCLK_HZ / 2U),
	"APB1 must be HCLK/2 (100 MHz)");

/* Verify PLL math */
_Static_assert(
	((BOARD_HSE_VALUE_HZ / BOARD_PLL1_M) * BOARD_PLL1_N) / BOARD_PLL1_P == BOARD_SYSCLK_HZ,
	"PLL1 P output does not match SYSCLK - recalculate PLL");

_Static_assert(
	((BOARD_HSE_VALUE_HZ / BOARD_PLL1_M) * BOARD_PLL1_N) / BOARD_PLL1_Q == BOARD_FDCAN_KERNEL_HZ,
	"PLL1 Q output does not match FDCAN kernel clock - recalculate PLL");

/* Verify FDCAN message RAM is valid */
_Static_assert(BOARD_FDCAN1_RAM_BASE == 0x4000AC00U,
	"FDCAN1 message RAM base per RM0433 Table 408");

_Static_assert(BOARD_FDCAN1_RAM_SIZE_WORDS == 2560U,
	"FDCAN1 message RAM size per RM0433 Section 56.3.1");

#endif /* CONFIG_BOARD_H */

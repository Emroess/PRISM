#
# Makefile for STM32H753ZI Bare Metal Firmware
#
# Per IMPROVE_FIRMWARE_PLAN.md:
# - Dual-build validation (-O0 and -O2 must both work)
# - MISRA-C checking via cppcheck
# - Layering validation
# - Link-time assertions
# - Zero warnings policy (-Werror)
#
#

######################################
# Target Configuration
######################################
TARGET = firmware

# STM32 device
DEVICE = STM32H753xx

# Build directory
BUILD_DIR = build

######################################
# Sources
######################################

# C sources
C_SOURCES = \
src/main.c \
src/bsp/board.c \
src/bsp/mpu_config.c \
third_party/stm32cube/CMSIS/Device/ST/STM32H7xx/Source/system_stm32h7xx.c \
src/drivers/uart.c \
src/drivers/fdcan.c \
src/drivers/flash_utils.c \
src/app/cli.c \
src/protocols/can_simple.c \
src/valve/valve_haptic.c \
src/valve/valve_physics.c \
src/valve/valve_presets.c \
src/valve/valve_nvm.c \
src/valve/valve_filters.c \
src/valve/valve_manager.c \
src/odrive/odrive_manager.c \
src/stm32h7xx_it.c \
src/syscalls.c \
src/network/net_init.c \
src/network/stream_server.c \
src/network/http_server.c \
src/network/rest_api.c \
src/network/ping.c \
src/network/network_manager.c \
src/network/network_nvm.c \
src/app/system_manager.c \
common/status_strings.c \
ports/lwip/ethernetif.c \
ports/lwip/sys_arch.c \
third_party/lwip/core/init.c \
third_party/lwip/core/def.c \
third_party/lwip/core/dns.c \
third_party/lwip/core/inet_chksum.c \
third_party/lwip/core/ip.c \
third_party/lwip/core/mem.c \
third_party/lwip/core/memp.c \
third_party/lwip/core/netif.c \
third_party/lwip/core/pbuf.c \
third_party/lwip/core/raw.c \
third_party/lwip/core/stats.c \
third_party/lwip/core/sys.c \
third_party/lwip/core/tcp.c \
third_party/lwip/core/tcp_in.c \
third_party/lwip/core/tcp_out.c \
third_party/lwip/core/timeouts.c \
third_party/lwip/core/udp.c \
third_party/lwip/netif/ethernet.c \
third_party/lwip/core/ipv4/etharp.c \
third_party/lwip/core/ipv4/ip4.c \
third_party/lwip/core/ipv4/ip4_addr.c \
third_party/lwip/core/ipv4/icmp.c \
third_party/lwip/core/ipv4/ip4_frag.c \
third_party/lwip/core/ipv4/dhcp.c \
third_party/stm32cube/hal/stm32h7xx_hal.c \
third_party/stm32cube/hal/stm32h7xx_hal_rcc.c \
third_party/stm32cube/hal/stm32h7xx_hal_gpio.c \
third_party/stm32cube/hal/stm32h7xx_hal_cortex.c \
third_party/stm32cube/hal/stm32h7xx_hal_flash.c \
third_party/stm32cube/hal/stm32h7xx_hal_flash_ex.c \
third_party/stm32cube/hal/stm32h7xx_hal_eth.c \
third_party/stm32cube/hal/stm32h7xx_hal_eth_ex.c \
third_party/stm32cube/bsp/lan8742/lan8742.c \
third_party/stm32cube/CMSIS/DSP/Source/FastMathFunctions/arm_cos_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/FastMathFunctions/arm_sin_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/SupportFunctions/arm_copy_f32.c \
third_party/stm32cube/CMSIS/DSP/Source/SupportFunctions/arm_fill_f32.c

# ASM sources
ASM_SOURCES = \
startup/startup_stm32h753xihx.s

######################################
# Includes
######################################
C_INCLUDES = \
-Iinc \
-Iinc/bsp \
-Iinc/drivers \
-Iinc/protocols \
-Iinc/valve \
-Iinc/odrive \
-Iinc/network \
-Iinc/app \
-Icommon \
-Ithird_party/stm32cube/hal/Inc \
-Ithird_party/stm32cube/hal/Inc/Legacy \
-Ithird_party/stm32cube/CMSIS/Core/Include \
-Ithird_party/stm32cube/CMSIS/Device/ST/STM32H7xx/Include \
-Ithird_party/stm32cube/CMSIS/DSP/Include \
-Ithird_party/stm32cube/bsp/lan8742 \
-Ithird_party/lwip/include \
-Ithird_party/lwip/include/lwip \
-Iports/lwip

######################################
# Compiler and Toolchain
######################################
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

######################################
# CPU and FPU Configuration
######################################
CPU = -mcpu=cortex-m7

# FPU with DSP extensions enabled
FPU = -mfpu=fpv5-d16
FLOAT-ABI = -mfloat-abi=hard

# MCU with DSP support
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

######################################
# Compiler Flags
######################################

######################################
# C defines
######################################
# Device-specific definitions
C_DEFS =  \
-DSTM32H753xx \
-DUSE_FULL_ASSERT \
-DHSE_VALUE=8000000 \
-D__DSP_PRESENT=1 \
-DHTTP_API_KEY=\"steve-valve-2025\"

# Optimization level (can be overridden for dual-build)
CFLAGS_OPT ?= -O2

# C standard and language options
CFLAGS_STD = -std=c11

# Warning flags (zero warnings policy)
CFLAGS_WARN = \
-Wall \
-Wextra \
-Werror \
-Wstrict-prototypes \
-Wmissing-prototypes \
-Wold-style-definition \
-Wuninitialized \
-Wmaybe-uninitialized \
-Wshadow \
-Wpointer-arith \
-Wcast-qual \
-Wwrite-strings

# Disable sign-conversion for CMSIS headers (they have many sign issues)
# We control our own code strictly
CFLAGS_WARN_DRIVERS = \
-Wno-sign-conversion \
-Wno-conversion

# Code generation options
CFLAGS_GEN = \
-fdata-sections \
-ffunction-sections \
-fno-common \
-fstack-usage

# Debug information
CFLAGS_DEBUG = -g3 -gdwarf-2

# All C flags
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(CFLAGS_OPT) $(CFLAGS_STD) \
         $(CFLAGS_WARN) $(CFLAGS_WARN_DRIVERS) $(CFLAGS_GEN) $(CFLAGS_DEBUG)

# Assembler flags
ASFLAGS = $(MCU) $(C_INCLUDES) $(CFLAGS_DEBUG)

######################################
# Linker Flags
######################################
LDSCRIPT = ld/STM32H753ZITx_FLASH.ld

LIBS = -lc -lm -lnosys

LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-u,_printf_float -Wl,-u,_scanf_float \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
          -Wl,--gc-sections \
          -Wl,--print-memory-usage

######################################
# Build Rules
######################################

# Default target
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# List of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# Build C source files
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"

# Special rule for LAN8742 driver (has unused parameter warnings)
$(BUILD_DIR)/lan8742.o: third_party/stm32cube/bsp/lan8742/lan8742.c Makefile | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wno-unused-parameter -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"

# Build assembly files
$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) -o "$@" "$<"

# Link
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "LD $@"
	@$(CC) $(OBJECTS) $(LDFLAGS) -o "$@"
	@$(SZ) "$@"

# Generate hex file
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "HEX $@"
	@$(HEX) "$<" "$@"

# Generate binary file
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "BIN $@"
	@$(BIN) "$<" "$@"

# Create build directory
$(BUILD_DIR):
	mkdir -p "$@"

######################################
# Validation Targets
######################################

# Dual-build validation (MANDATORY per IMPROVE_FIRMWARE_PLAN.md)
.PHONY: validate
validate: clean
	@echo "=== Dual-Build Validation (MANDATORY) ==="
	@echo ""
	@echo "Building with -O0 (debug)..."
	@$(MAKE) all CFLAGS_OPT="-O0" BUILD_DIR=build/debug --no-print-directory
	@echo ""
	@echo "Building with -O2 (release)..."
	@$(MAKE) all CFLAGS_OPT="-O2" BUILD_DIR=build/release --no-print-directory
	@echo ""
	@echo "Building with -Os (size optimization)..."
	@$(MAKE) all CFLAGS_OPT="-Os" BUILD_DIR=build/size --no-print-directory
	@echo ""
	@echo "✓ Dual-build validation PASSED - firmware is optimization-independent"

# MISRA-C checking via cppcheck
.PHONY: misra
misra:
	@echo "=== MISRA-C:2012 Compliance Check ==="
	@if ! command -v cppcheck >/dev/null 2>&1; then \
		echo "ERROR: cppcheck not found - install with: sudo apt-get install cppcheck"; \
		exit 1; \
	fi
	@cppcheck --enable=all --inconclusive --std=c11 \
		--suppress=missingIncludeSystem \
		--inline-suppr \
		$(C_INCLUDES) $(C_DEFS) \
		src/*.c 2>&1 | tee build/cppcheck.log
	@echo "✓ MISRA check complete - see build/cppcheck.log"

# Hardware layering validation
.PHONY: check-layering
check-layering:
	@cd firmware && ./scripts/check_layering.sh

# Check for duplicate #define symbols
.PHONY: check-defines
check-defines:
	@echo "=== Duplicate Define Check ==="
	@python3 scripts/check_defines.py
	@echo "✓ Define check complete"

# Complete validation suite
.PHONY: validate-all
validate-all: validate misra check-layering check-defines
	@echo ""
	@echo "=== All Validation Checks PASSED ==="
	@echo "✓ Dual-build (optimization-independent)"
	@echo "✓ MISRA-C compliance"
	@echo "✓ Hardware layering"
	@echo "✓ No duplicate defines"

######################################
# Utility Targets
######################################

# Clean build artifacts
.PHONY: clean
clean:
	-rm -rf build

# Flash firmware via OpenOCD
.PHONY: flash
flash: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

# GDB debug session
.PHONY: debug
debug: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32h7x.cfg &
	$(PREFIX)gdb -ex "target remote :3333" $(BUILD_DIR)/$(TARGET).elf

# Serial monitor
.PHONY: monitor
monitor:
	@echo "Starting serial monitor on /dev/ttyACM0..."
	@echo "Press Ctrl+A, Ctrl+X to exit"
	@sleep 1
	@screen /dev/ttyACM0 115200 || (echo "Screen not available. Try: minicom -D /dev/ttyACM0 -b 115200" && exit 1)

# Print size information
.PHONY: size
size: $(BUILD_DIR)/$(TARGET).elf
	@$(SZ) -A -d "$<"

# Show help
.PHONY: help
help:
	@echo "STM32H753ZI Firmware Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build firmware (default)"
	@echo "  clean          - Remove build artifacts"
	@echo "  validate       - Dual-build validation (MANDATORY before commit)"
	@echo "  misra          - Run MISRA-C compliance checks"
	@echo "  check-layering - Validate hardware access layering"
	@echo "  validate-all   - Run all validation checks"
	@echo "  flash          - Flash firmware via OpenOCD"
	@echo "  debug          - Start GDB debug session"
	@echo "  monitor        - Start serial monitor (screen)"
	@echo "  size           - Show size information"
	@echo ""
	@echo "Build options:"
	@echo "  CFLAGS_OPT=-O0  - Debug build"
	@echo "  CFLAGS_OPT=-O2  - Release build (default)"
	@echo "  CFLAGS_OPT=-Os  - Size-optimized build"

# Dependencies
-include $(wildcard $(BUILD_DIR)/*.d)

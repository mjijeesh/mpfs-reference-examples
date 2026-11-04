# ==============================================================================
# Advanced Multi-Directory PolarFire SoC Makefile
# Supports Linux and Windows (Microchip SoftConsole / MSYS2 / Native)
# Memory Targets: eNVM (XIP), LIM, LIM-APP, DDR, ENVM-LIM
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. Operating System & Host Toolchain Discovery
# ------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    HOST_OS               := Windows
    BIN_EXT               := .exe
    # Default Microchip installation paths for Windows
    SC_INSTALL_DIR        ?= C:/Microchip/SoftConsole-v2022.2-RISC-V-747
    LIBERO_DIR            ?= C:/Microchip/Libero_SoC_2025.2/Libero_SoC
    PYTHON                ?= $(SC_INSTALL_DIR)/python3/python.exe
    JAVA                  ?= $(SC_INSTALL_DIR)/eclipse/jre/bin/java.exe
    OPENOCD               ?= $(SC_INSTALL_DIR)/openocd/bin/openocd.exe
    FPGENPROG             ?= $(LIBERO_DIR)/Designer/bin64/fpgenprog.exe
    FPEXPRESS             ?= $(LIBERO_DIR)/Designer/bin/fpexpress.exe
else
    HOST_OS               := Linux
    BIN_EXT               :=
    # Default Microchip installation paths for Linux
    SC_INSTALL_DIR        ?= /opt/microchip/SoftConsole/SoftConsole-v2022
    LIBERO_DIR            ?= /opt/microchip/Libero_SoC_2025.2/Libero_SoC
    PYTHON                ?= python3
    JAVA                  ?= java
    OPENOCD               ?= $(SC_INSTALL_DIR)/openocd/bin/openocd
    FPGENPROG             ?= $(LIBERO_DIR)/Designer/bin64/fpgenprog
    FPEXPRESS             ?= $(LIBERO_DIR)/Designer/bin/FPExpress
endif

# Sanitize backslashes to forward slashes for GNU Make portability
SC_INSTALL_DIR := $(subst \,/,$(SC_INSTALL_DIR))
LIBERO_DIR     := $(subst \,/,$(LIBERO_DIR))
FPGENPROG      := $(subst \,/,$(FPGENPROG))
FPEXPRESS      := $(subst \,/,$(FPEXPRESS))

# ------------------------------------------------------------------------------
# 2. RISC-V Cross Compiler Detection
# ------------------------------------------------------------------------------
# Auto-detect toolchain prefix: Prefer system PATH (riscv-none-elf- or riscv64-unknown-elf-)
CROSS_COMPILE ?= $(shell which riscv-none-elf-gcc >/dev/null 2>&1 && echo riscv-none-elf- || \
                         (which riscv64-unknown-elf-gcc >/dev/null 2>&1 && echo riscv64-unknown-elf- || \
                          echo $(SC_INSTALL_DIR)/riscv-unknown-elf-gcc/bin/riscv64-unknown-elf-))

CC      := $(CROSS_COMPILE)gcc
AS      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size
GDB     := $(CROSS_COMPILE)gdb
READELF := $(CROSS_COMPILE)readelf

TARGET  ?= mpfs-baremetal

# Cross-platform build timestamp generator
BUILD_TIMESTAMP ?= $(shell $(PYTHON) -c "import datetime; print(datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))" 2>/dev/null || date +"%Y-%m-%d %H:%M:%S")
CFLAGS += -DBUILD_TIMESTAMP="\"$(BUILD_TIMESTAMP)\""

# ------------------------------------------------------------------------------
# 3. Dynamic Board Configuration & Hardware Hooks
# ------------------------------------------------------------------------------
BOARD     ?= mpfs-icicle-kit
BOARD_DIR := boards/$(BOARD)

AVAILABLE_BOARDS := $(notdir $(wildcard boards/*))

ifeq ($(wildcard $(BOARD_DIR)),)
    $(error Board directory '$(BOARD_DIR)' not found! Available boards: $(AVAILABLE_BOARDS))
endif

# Generic fallbacks
DIE         ?= MPFS250T
PACKAGE     ?= FCVG484
STDIO_UART  ?= 1
SERIAL_PORT ?= /dev/ttyUSB0
JOB_FILE    ?= $(BOARD_DIR)/fpga_design/$(BOARD).job

# Include target-specific definitions from board directory
include $(BOARD_DIR)/board.mk

CFLAGS += -DBOARD_NAME="\"$(BOARD)\"" \
          -DMPFS_TARGET_DIE="\"$(DIE)\"" \
          -DMPFS_TARGET_PACKAGE="\"$(PACKAGE)\""


# ------------------------------------------------------------------------------
# 5. External Tools & Flash Programmer Configuration
# ------------------------------------------------------------------------------
BOOTMODE_PROGRAMMER ?= $(SC_INSTALL_DIR)/extras/mpfs/mpfsBootmodeProgrammer.jar
CRCFBIGEN           ?= $(PYTHON) tools/crcfbigen.py
MPFS_TERMINAL       ?= $(PYTHON) tools/mpfs-terminal.py
SOC_GEN_SCRIPT      := platform/soc_config_generator/mpfs_configuration_generator.py
XML_CONFIG_DIR      ?= $(BOARD_DIR)/fpga_design/design_description
CONFIG_OUT_DIR      ?= $(BOARD_DIR)

HSS_PAYLOAD_GEN_DIR ?= tools/hss-payload-generator
ifeq ($(wildcard $(HSS_PAYLOAD_GEN_DIR)/Makefile),)
    HSS_PAYLOAD_GEN_DIR := tools
endif
HSS_PAYLOAD_GEN     := $(HSS_PAYLOAD_GEN_DIR)/hss-payload-generator$(BIN_EXT)

# Export variables for sub-tools
export SC_INSTALL_DIR FPGENPROG FPEXPRESS BOOTMODE_PROGRAMMER

# ------------------------------------------------------------------------------
# 6. Dynamic Memory Target Configuration Switcher
# ------------------------------------------------------------------------------
MEM_TARGET ?= lim

ifeq ($(MEM_TARGET),envm)
    BUILD_DIR     := build/envm
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/envm-release/linker/mpfs-envm.ld
    CONFIG_DIR    := envm-release
else ifeq ($(MEM_TARGET),ddr)
    BUILD_DIR     := build/ddr
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/ddr-release/linker/mpfs-ddr-loaded-by-boot-loader.ld
    CONFIG_DIR    := ddr-release
    CFLAGS        += -DIMAGE_LOADED_BY_BOOTLOADER=1
else ifeq ($(MEM_TARGET),lim-app)
    BUILD_DIR     := build/lim-app
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/lim-release/linker/mpfs-lim-app.ld
    CONFIG_DIR    := lim-release
    CFLAGS        += -DIMAGE_LOADED_BY_BOOTLOADER=1
else ifeq ($(MEM_TARGET),envm-lim)
    BUILD_DIR     := build/envm-lim
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/envm-scratchpad-release/linker/mpfs-envm-lma-scratchpad-vma.ld
    CONFIG_DIR    := envm-scratchpad-release
else
    BUILD_DIR     := build/lim
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/lim-debug/linker/mpfs-lim.ld
    CONFIG_DIR    := lim-debug
endif

# ------------------------------------------------------------------------------
# 7. Architecture & Compilation Flags
# ------------------------------------------------------------------------------
ARCH_FLAGS := -march=rv64gc \
              -mabi=lp64d \
              -mcmodel=medany \
              -msmall-data-limit=8 \
              -mstrict-align \
              -mno-save-restore

INCLUDE_DIRS := application \
                application/bios \
                application/bios/commands \
                application/drivers/flash \
                application/drivers/uart \
                application/utils \
                platform/drivers/mss/mss_mmuart \
                platform/drivers/mss/mss_gpio \
                platform/drivers/mss/mss_spi \
                platform/drivers/mss/mss_sys_services \
                platform/drivers/fpga_ip/CoreSPI \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR) \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/mpfs_hal_config \
                platform \
                $(BOARD_DIR)

INC_FLAGS := $(addprefix -I, $(INCLUDE_DIRS))

CFLAGS += $(ARCH_FLAGS) \
          -O0 \
          -g3 \
          -fmessage-length=0 \
          -fsigned-char \
          -ffunction-sections \
          -fdata-sections \
          -std=gnu11 \
          -Wstrict-prototypes \
          -Wbad-function-cast \
          --specs=nano.specs \
          $(INC_FLAGS)

LDFLAGS := $(ARCH_FLAGS) \
           -O0 \
           -g3 \
           -nostartfiles \
           -T $(LINKER_SCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
           -Wl,--gc-sections \
           --specs=nano.specs \
           --specs=nosys.specs

# Disable RWX segment warnings on newer toolchains (GCC 12+)
ifeq ($(findstring riscv-none-elf,$(CROSS_COMPILE)),riscv-none-elf)
    LDFLAGS += -Wl,--no-warn-rwx-segments
endif

# ------------------------------------------------------------------------------
# 8. Source Files Discovery
# ------------------------------------------------------------------------------
APP_SRCS_C    := $(wildcard application/*.c) \
                 $(wildcard application/*/*.c) \
                 $(wildcard application/*/*/*.c)

DRIVER_SRCS_C := platform/drivers/mss/mss_mmuart/mss_uart.c \
                 platform/drivers/mss/mss_gpio/mss_gpio.c \
                 platform/drivers/mss/mss_spi/mss_spi.c \
                 platform/drivers/fpga_ip/CoreSPI/core_spi.c \
                 platform/drivers/mss/mss_sys_services/mss_sys_services.c

HAL_SRCS_C    := $(wildcard platform/mpfs_hal/startup_gcc/*.c) \
                 $(wildcard platform/mpfs_hal/common/*.c) \
                 $(wildcard platform/mpfs_hal/common/nwc/*.c) \
                 $(wildcard platform/hal/*.c)

HAL_SRCS_ASM  := $(wildcard platform/hal/*.S) \
                 $(wildcard platform/mpfs_hal/startup_gcc/*.S)

SRCS_C   := $(sort $(APP_SRCS_C) $(DRIVER_SRCS_C) $(HAL_SRCS_C))
SRCS_ASM := $(sort $(HAL_SRCS_ASM))

OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS_C)) \
        $(patsubst %.S, $(BUILD_DIR)/%.o, $(SRCS_ASM))

# ------------------------------------------------------------------------------
# 9. Primary Build & Execution Rules
# ------------------------------------------------------------------------------
.PHONY: all clean print_build_info generate_headers show_size \
        lim lim-app envm envm-lim ddr \
        ddr-boot lim-boot ddr-flash lim-flash flash-payload serialboot \
        program-envm program-envm-lim flash-envm program-job \
        openocd debug gdbinit help

all: print_build_info generate_headers \
     $(BUILD_DIR)/$(TARGET).elf \
     $(BUILD_DIR)/$(TARGET).bin \
     $(BUILD_DIR)/$(TARGET).hex \
     $(BUILD_DIR)/$(TARGET).fbi \
     $(BUILD_DIR)/$(TARGET)_payload.bin \
     show_size

print_build_info:
	@echo "========================================================================"
	@echo "                   PolarFire SoC Build Context                          "
	@echo "========================================================================"
	@echo "  * HOST OS           : $(HOST_OS)"
	@echo "  * TARGET BOARD      : $(BOARD) ($(BOARD_DIR))"
	@echo "  * DIE / PACKAGE     : $(DIE) / $(PACKAGE)"
	@echo "  * MEMORY TARGET     : $(MEM_TARGET) (Linker: $(LINKER_SCRIPT))"
	@echo "  * CROSS COMPILER    : $(CC)"
	@echo "  * STDIO UART        : MSS_UART$(STDIO_UART)"
	@echo "========================================================================"

generate_headers:
	@echo " [SOC_GEN] Processing Libero XML hardware design..."
	@$(PYTHON) $(SOC_GEN_SCRIPT) $(XML_CONFIG_DIR) $(CONFIG_OUT_DIR)

$(OBJS): | generate_headers

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo " [CC]  Compiling $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo " [AS]  Assembling $<"
	@$(AS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	@echo " [LD]  Linking $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo " [OBJ] Generating Binary: $@"
	@$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	@echo " [OBJ] Generating Intel HEX: $@"
	@$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).fbi: $(BUILD_DIR)/$(TARGET).bin
	@echo " [FBI] Generating Flash Image: $@"
	@$(CRCFBIGEN) $< -o $@ --fbi --little

# ------------------------------------------------------------------------------
# 10. HSS Payload Generation Rules
# ------------------------------------------------------------------------------
$(HSS_PAYLOAD_GEN):
	@echo " [HOST] Building hss-payload-generator in $(HSS_PAYLOAD_GEN_DIR)..."
	@$(MAKE) -C $(HSS_PAYLOAD_GEN_DIR)

$(BUILD_DIR)/payload.yaml: $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	@ENTRY_POINT=$$($(READELF) -h $< | awk '/Entry point address:/ {print $$4}'); \
	echo "Generating HSS payload config ($@) at entry: $$ENTRY_POINT"; \
	echo "set-name: 'PolarFire-SoC-HSS::$(TARGET)'" > $@; \
	echo "" >> $@; \
	echo "hart-entry-points: {" >> $@; \
	echo "  u54_1: '$$ENTRY_POINT'," >> $@; \
	echo "  u54_2: '$$ENTRY_POINT'," >> $@; \
	echo "  u54_3: '$$ENTRY_POINT'," >> $@; \
	echo "  u54_4: '$$ENTRY_POINT'" >> $@; \
	echo "}" >> $@; \
	echo "" >> $@; \
	echo "payloads:" >> $@; \
	echo "  $<:" >> $@; \
	echo "    owner-hart: u54_1" >> $@; \
	echo "    secondary-hart: u54_2" >> $@; \
	echo "    secondary-hart: u54_3" >> $@; \
	echo "    secondary-hart: u54_4" >> $@; \
	echo "    priv-mode: prv_m" >> $@; \
	echo "    skip-opensbi: true" >> $@; \
	echo "    payload-name: \"$(TARGET)\"" >> $@

$(BUILD_DIR)/$(TARGET)_payload.bin: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/payload.yaml $(HSS_PAYLOAD_GEN)
	@echo " [HSS] Generating formatted boot payload: $@"
	@$(HSS_PAYLOAD_GEN) -c $(BUILD_DIR)/payload.yaml $@
	@cp $@ $(BUILD_DIR)/payload.bin

show_size: $(BUILD_DIR)/$(TARGET).elf
	@echo ""
	@echo "=== Memory Allocation Summary [$(MEM_TARGET)] ==="
	@$(SIZE) $<
	@echo "=================================================="

# ------------------------------------------------------------------------------
# 11. Convenience Memory Target Invokers
# ------------------------------------------------------------------------------
envm:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm all

lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim all

lim-app:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim-app all

envm-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm-lim all

ddr:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr all

# ------------------------------------------------------------------------------
# 12. Automated Sub-Make Boot Targets
# ------------------------------------------------------------------------------
# Guarantees that compilation and streaming pick up the exact target directory
SERIAL_PORT     ?= /dev/ttyUSB0
FLASH_BOOT_ADDR ?= 0x00020000
DDR_ADDR        ?= 0x80000000

ddr-boot:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr serialboot

lim-boot:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim serialboot

ddr-flash:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr flash-payload

lim-flash:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim flash-payload

serialboot: $(BUILD_DIR)/$(TARGET).bin
	@echo "=================================================="
	@echo " Streaming & Executing Raw Binary to RAM"
	@echo " File        : $<"
	@echo " Load Addr   : $(DDR_ADDR)"
	@echo " Serial Port : $(SERIAL_PORT)"
	@echo "=================================================="
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel $< --kernel-adr $(DDR_ADDR)

flash-payload: $(BUILD_DIR)/$(TARGET)_payload.bin
	@echo "=================================================="
	@echo " Flashing HSS Payload to SPI Flash"
	@echo " File        : $<"
	@echo " Port        : $(SERIAL_PORT)"
	@echo " Flash Addr  : $(FLASH_BOOT_ADDR)"
	@echo "=================================================="
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel $< --flash --kernel-adr $(FLASH_BOOT_ADDR)

# ------------------------------------------------------------------------------
# 13. Hardware Programming & JTAG Debugging Targets
# ------------------------------------------------------------------------------
program-envm:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm all
	"$(JAVA)" -jar $(BOOTMODE_PROGRAMMER) \
		--bootmode 1 \
		--die $(DIE) \
		--package $(PACKAGE) \
		--workdir $(CURDIR)/build/envm

program-envm-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm-lim all
	"$(JAVA)" -jar $(BOOTMODE_PROGRAMMER) \
		--bootmode 1 \
		--die $(DIE) \
		--package $(PACKAGE) \
		--workdir $(CURDIR)/build/envm-lim

program-job:
	@if [ ! -f "$(JOB_FILE)" ]; then \
		echo "[ERROR] Job file '$(JOB_FILE)' was not found!"; exit 1; \
	fi
	@mkdir -p build/fp_job
	@echo "create_job_project -job_project_location {$(CURDIR)/build/fp_job} -job_file {$(CURDIR)/$(JOB_FILE)} -overwrite 1" > build/fp_job/program_job.tcl
	@echo "run_selected_actions" >> build/fp_job/program_job.tcl
	@echo "close_project" >> build/fp_job/program_job.tcl
	$(FPEXPRESS) SCRIPT:build/fp_job/program_job.tcl

openocd:
	$(OPENOCD) -c "set DEVICE MPFS" -f board/microsemi-riscv.cfg

debug:
	$(GDB) $(BUILD_DIR)/$(TARGET).elf \
		-ex 'set $$target_riscv=1' \
		-ex 'set mem inaccessible-by-default off' \
		-ex 'target extended-remote localhost:3333' \
		-ex 'monitor reset halt' \
		-ex 'load' \
		-ex 'thread apply all set $$pc = _start' \
		-ex 'continue'

clean:
	@echo "Cleaning build directories..."
	@rm -rf build .gdbinit

help:
	@echo "========================================================================"
	@echo " PolarFire SoC Bare-Metal Build System (Linux & Windows)"
	@echo "========================================================================"
	@echo " Memory Build Targets:"
	@echo "   make ddr          - Build application for volatile DDR Memory"
	@echo "   make lim          - Build application for Lower LIM (0x08000000)"
	@echo "   make lim-app      - Build application for Upper LIM (0x08040000)"
	@echo "   make envm         - Build for direct Execute-In-Place (XIP) in eNVM"
	@echo "   make envm-lim     - Build for eNVM -> LIM scratchpad copy"
	@echo ""
	@echo " Fast Serial Boot Targets:"
	@echo "   make ddr-boot     - Cleanly compile DDR and stream raw binary via UART"
	@echo "   make lim-boot     - Cleanly compile LIM and stream raw binary via UART"
	@echo "   make ddr-flash    - Compile DDR, generate payload, and flash to SPI"
	@echo "   make lim-flash    - Compile LIM, generate payload, and flash to SPI"
	@echo "   make flash-payload- Flash existing HSS Payload to SPI Flash"
	@echo ""
	@echo " Flashing & Hardware Targets:"
	@echo "   make program-job  - Flash FPGA fabric (.job file) using FlashPro Express"
	@echo "   make program-envm - Write Boot Mode 1 eNVM client via mpfsBootmodeProgrammer"
	@echo "   make openocd      - Start OpenOCD JTAG daemon"
	@echo "   make debug        - Attach GDB to target"
	@echo "========================================================================"
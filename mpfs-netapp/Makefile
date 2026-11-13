# ==============================================================================
# PolarFire SoC Bare-Metal Network Application Makefile
# Supports Linux & Windows | Targets: lim, lim-app, ddr, envm, envm-lim
# ==============================================================================

TARGET     ?= mpfs-net-app
BOARD      ?= mpfs-disco-kit
MEM_TARGET ?= lim-app
OPT_LEVEL  ?= -O2

BOARD_DIR        := boards/$(BOARD)
AVAILABLE_BOARDS := $(notdir $(wildcard boards/*))

ifeq ($(wildcard $(BOARD_DIR)),)
    $(error Board directory '$(BOARD_DIR)' not found! Available boards: $(AVAILABLE_BOARDS))
endif

# 1. Load Host Toolchain & System Paths
include toolchain.mk

# Generic fallbacks (overridden by board.mk)
DIE            ?= MPFS095T
PACKAGE        ?= FCSG325
STDIO_UART     ?= 4
SERIAL_PORT    ?= /dev/ttyUSB0
JOB_FILE       ?= $(BOARD_DIR)/fpga_design/$(BOARD).job
XML_CONFIG_DIR ?= $(BOARD_DIR)/fpga_design/design_description
CONFIG_OUT_DIR ?= $(BOARD_DIR)

# 2. Load Board-Specific Configuration
include $(BOARD_DIR)/board.mk

# Build timestamp
BUILD_TIMESTAMP ?= $(shell $(PYTHON) -c "import datetime; print(datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))" 2>/dev/null || date +"%Y-%m-%d %H:%M:%S")

# 3. Memory Target Switcher (scales lwIP/TFTP footprint via MEM_TARGET_LIM vs DDR)
ifeq ($(MEM_TARGET),envm)
    BUILD_DIR     := build/$(BOARD)/envm
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/envm-release/linker/mpfs-envm.ld
    CONFIG_DIR    := envm-release
    TARGET_DEFS   := -DMEM_TARGET_LIM
else ifeq ($(MEM_TARGET),ddr)
    BUILD_DIR     := build/$(BOARD)/ddr
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/ddr-release/linker/mpfs-ddr-loaded-by-boot-loader.ld
    CONFIG_DIR    := ddr-release
    TARGET_DEFS   := -DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1
else ifeq ($(MEM_TARGET),lim-app)
    BUILD_DIR     := build/$(BOARD)/lim-app
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/lim-release/linker/mpfs-lim-app.ld
    CONFIG_DIR    := lim-release
    TARGET_DEFS   := -DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1
else ifeq ($(MEM_TARGET),envm-lim)
    BUILD_DIR     := build/$(BOARD)/envm-lim
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/envm-scratchpad-release/linker/mpfs-envm-lma-scratchpad-vma.ld
    CONFIG_DIR    := envm-scratchpad-release
    TARGET_DEFS   := -DMEM_TARGET_LIM
else
    BUILD_DIR     := build/$(BOARD)/lim
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/lim-debug/linker/mpfs-lim.ld
    CONFIG_DIR    := lim-debug
    TARGET_DEFS   := -DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1
endif

# 4. Architecture, Include & Linker Flags
ARCH_FLAGS := -march=rv64gc \
              -mabi=lp64d \
              -mcmodel=medany \
              -msmall-data-limit=8 \
              -mstrict-align \
              -mno-save-restore

INCLUDE_DIRS := application \
				application/inc \
                middleware/net_stack \
                middleware/lwip/src/include \
                middleware/lwip/src/include/ipv4 \
                platform \
                platform/mpfs_hal \
                platform/hal \
                $(BOARD_DIR) \
                $(BOARD_DIR)/fpga_design_config \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR) \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/mpfs_hal_config

INC_FLAGS := $(addprefix -I, $(INCLUDE_DIRS))

CFLAGS += $(ARCH_FLAGS) \
          $(OPT_LEVEL) \
          -g3 \
          -fmessage-length=0 \
          -fsigned-char \
          -ffunction-sections \
          -fdata-sections \
          -std=gnu11 \
          -Wall -Wextra \
          --specs=nano.specs \
          $(BOARD_CFLAGS) \
          $(TARGET_DEFS) \
          -DBOARD_NAME="\"$(BOARD)\"" \
          -DMPFS_TARGET_DIE="\"$(DIE)\"" \
          -DMPFS_TARGET_PACKAGE="\"$(PACKAGE)\"" \
          -DBUILD_TIMESTAMP="\"$(BUILD_TIMESTAMP)\"" \
          $(INC_FLAGS)

LDFLAGS := $(ARCH_FLAGS) \
           $(OPT_LEVEL) \
           -g3 \
           -nostartfiles \
           -T $(LINKER_SCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
           -Wl,--gc-sections \
           --specs=nano.specs \
           --specs=nosys.specs

ifeq ($(findstring riscv-none-elf,$(CROSS_COMPILE)),riscv-none-elf)
    LDFLAGS += -Wl,--no-warn-rwx-segments
endif

# 5. Source Discovery
APP_SRCS_C    := $(BOARD_DIR)/board_init.c \
                 $(wildcard application/hart*/*.c)

MW_SRCS_C     := middleware/net_stack/net_port.c \
                 middleware/net_stack/web_server.c \
                 middleware/net_stack/tftp_server_app.c \
                 $(wildcard middleware/lwip/src/core/*.c) \
                 $(wildcard middleware/lwip/src/core/ipv4/*.c) \
                 middleware/lwip/src/netif/ethernet.c \
                 middleware/lwip/src/apps/http/httpd.c \
                 middleware/lwip/src/apps/http/fs.c \
                 $(wildcard middleware/lwip/src/apps/tftp/*.c)

DRIVER_SRCS_C := platform/drivers/mss/mss_mmuart/mss_uart.c \
                 $(wildcard platform/drivers/mss/mss_ethernet_mac/*.c)

HAL_SRCS_C    := $(wildcard platform/mpfs_hal/startup_gcc/*.c) \
                 $(wildcard platform/mpfs_hal/common/*.c) \
                 $(wildcard platform/mpfs_hal/common/nwc/*.c) \
                 $(wildcard platform/hal/*.c)

HAL_SRCS_ASM  := $(wildcard platform/hal/*.S) \
                 $(wildcard platform/mpfs_hal/startup_gcc/*.S)

SRCS_C   := $(sort $(APP_SRCS_C) $(MW_SRCS_C) $(DRIVER_SRCS_C) $(HAL_SRCS_C))
SRCS_ASM := $(sort $(HAL_SRCS_ASM))

OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS_C)) \
        $(patsubst %.S, $(BUILD_DIR)/%.o, $(SRCS_ASM))

# 6. Build & Artifact Targets
.PHONY: all clean print_build_info generate_headers show_size memusage \
        lim lim-app envm envm-lim ddr \
        ddr-boot lim-boot lim-app-boot ddr-flash lim-flash flash-payload serialboot \
        program-envm program-envm-lim program-job openocd debug help

all: print_build_info generate_headers \
     $(BUILD_DIR)/$(TARGET).elf \
     $(BUILD_DIR)/$(TARGET).bin \
     $(BUILD_DIR)/$(TARGET).hex \
     $(BUILD_DIR)/$(TARGET).fbi \
     $(BUILD_DIR)/$(TARGET)_payload.bin \
     show_size

print_build_info:
	@echo "========================================================================"
	@echo "                   PolarFire SoC Network App Build                      "
	@echo "========================================================================"
	@echo "  * HOST OS           : $(HOST_OS)"
	@echo "  * TARGET BOARD      : $(BOARD) ($(BOARD_DIR))"
	@echo "  * DIE / PACKAGE     : $(DIE) / $(PACKAGE)"
	@echo "  * MEMORY TARGET     : $(MEM_TARGET) (Linker: $(LINKER_SCRIPT))"
	@echo "  * CROSS COMPILER    : $(CC)"
	@echo "  * STDIO UART        : MSS_UART$(STDIO_UART)"
	@echo "========================================================================"

generate_headers:
	@if [ -d "$(XML_CONFIG_DIR)" ] && [ -f "$(SOC_GEN_SCRIPT)" ]; then \
		echo " [SOC_GEN] Processing Libero XML hardware design..."; \
		$(PYTHON) $(SOC_GEN_SCRIPT) $(XML_CONFIG_DIR) $(CONFIG_OUT_DIR); \
	fi

$(OBJS): | generate_headers

# Keep -Wall -Wextra strict on application/, boards/, and middleware/net_stack/,
# while suppressing third-party vendor warnings in platform/ and lwip/
$(BUILD_DIR)/platform/%.o: CFLAGS += -w
$(BUILD_DIR)/middleware/lwip/%.o: CFLAGS += -w

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo " [CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo " [AS]  $<"
	@$(AS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT)
	@echo " [LD]  $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	@$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).fbi: $(BUILD_DIR)/$(TARGET).bin
	@$(CRCFBIGEN) $< -o $@ --fbi --little

# 7. HSS Payload Generation (dispatches u54_1..4 to satisfy mpfs_hal barrier)
$(HSS_PAYLOAD_GEN):
ifeq ($(HOST_OS),Windows)
	@if [ ! -f "$@" ]; then echo "[ERROR] '$(HSS_PAYLOAD_GEN)' not found!"; exit 1; fi
else
	@$(MAKE) -C $(HSS_PAYLOAD_GEN_DIR)
endif

$(BUILD_DIR)/payload.yaml: $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	@ENTRY_POINT=$$($(READELF) -h $< | awk '/Entry point address:/ {print $$4}'); \
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
	@$(PYTHON) -c "import shutil; shutil.copyfile('$@', '$(BUILD_DIR)/payload.bin')"

show_size: $(BUILD_DIR)/$(TARGET).elf
	@echo ""
	@echo "=== Memory Allocation Summary [$(MEM_TARGET)] ==="
	@$(SIZE) $<
	@echo "=================================================="

memusage: $(BUILD_DIR)/$(TARGET).elf
	@$(MEMUSAGE) $< $(LINKER_SCRIPT) $(patsubst %-,%,$(CROSS_COMPILE))

# 8. Convenience Memory Targets
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

# 9. Serial Boot & SPI Flash Targets
FLASH_BOOT_ADDR ?= 0x00020000
DDR_ADDR        ?= 0x80000000

ddr-boot:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr serialboot

lim-boot:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim serialboot

lim-app-boot:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim-app serialboot

ddr-flash:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr flash-payload

lim-flash:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim-app flash-payload

serialboot: $(BUILD_DIR)/$(TARGET)_payload.bin
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel $< --kernel-adr $(DDR_ADDR)

flash-payload: $(BUILD_DIR)/$(TARGET)_payload.bin
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel $< --flash --kernel-adr $(FLASH_BOOT_ADDR)

# 10. Hardware Programming & JTAG Debugging
program-envm:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm all
	"$(JAVA)" -jar $(BOOTMODE_PROGRAMMER) --bootmode 1 --die $(DIE) --package $(PACKAGE) --workdir $(CURDIR)/build/$(BOARD)/envm

program-envm-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm-lim all
	"$(JAVA)" -jar $(BOOTMODE_PROGRAMMER) --bootmode 1 --die $(DIE) --package $(PACKAGE) --workdir $(CURDIR)/build/$(BOARD)/envm-lim

program-job:
	@if [ ! -f "$(JOB_FILE)" ]; then echo "[ERROR] Job file '$(JOB_FILE)' not found!"; exit 1; fi
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
	@rm -rf build .gdbinit

help:
	@echo "Targets: make BOARD=mpfs-disco-kit [lim | lim-app | ddr | envm | envm-lim | lim-app-boot | ddr-boot | lim-flash | memusage | clean]"
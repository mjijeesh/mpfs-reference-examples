# ==============================================================================
# PolarFire SoC Bare-Metal Network Application Makefile
# ==============================================================================

TARGET ?= mpfs-net-app
BOARD  ?= $(if $(board),$(board),mpfs-disco-kit)

# Infer MEM_TARGET automatically from convenience goals when not set explicitly
ifeq ($(origin MEM_TARGET), undefined)
    ifneq ($(filter lim-debug debug debug-lim,$(MAKECMDGOALS)),)
        MEM_TARGET := lim-debug
    else ifneq ($(filter ddr-debug debug-ddr,$(MAKECMDGOALS)),)
        MEM_TARGET := ddr-debug
    else ifneq ($(filter ddr-release,$(MAKECMDGOALS)),)
        MEM_TARGET := ddr-release
    else ifneq ($(filter envm envm-lim program-envm-lim,$(MAKECMDGOALS)),)
        MEM_TARGET := envm-lim
    else
        MEM_TARGET := lim-release
    endif
endif

BOARD_DIR        := boards/$(BOARD)
AVAILABLE_BOARDS := $(notdir $(wildcard boards/*))
ifeq ($(wildcard $(BOARD_DIR)),)
    $(error Board '$(BOARD_DIR)' not found! Available: $(AVAILABLE_BOARDS))
endif

# 1. Toolchain & Board Configuration
include toolchain.mk

DIE             ?= MPFS095T
PACKAGE         ?= FCSG325
STDIO_UART      ?= 4
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/$(BOARD).job
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
CONFIG_OUT_DIR  ?= $(BOARD_DIR)

include $(BOARD_DIR)/board.mk

# Fallbacks if a custom board.mk omits optional hooks
BOARD_SRCS_C     ?= $(wildcard $(BOARD_DIR)/*.c)
BOARD_INC_DIRS   ?= $(BOARD_DIR)
BOARD_PHY_DRIVER ?= platform/drivers/mss/mss_ethernet_mac/null_phy.c
OPENOCD_CFG      ?= -c "set DEVICE MPFS" -f board/microsemi-riscv.cfg

BUILD_TIMESTAMP ?= $(shell $(PYTHON) -c "import datetime; print(datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))" 2>/dev/null || date +"%Y-%m-%d %H:%M:%S")

# 2. Memory Target Configuration
NEEDS_HSS_PAYLOAD := 0

ifeq ($(MEM_TARGET),lim-debug)
    BUILD_DIR     := build/$(BOARD)/lim-debug
    CONFIG_DIR    := lim-debug
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/linker/mpfs-lim.ld
    TARGET_DEFS   := -DMEM_TARGET_LIM
    OPT_LEVEL     ?= -Og
else ifeq ($(MEM_TARGET),lim-release)
    BUILD_DIR     := build/$(BOARD)/lim-release
    CONFIG_DIR    := lim-release
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/linker/mpfs-lim-app.ld
    TARGET_DEFS   := -DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1
    OPT_LEVEL     ?= -O2
    NEEDS_HSS_PAYLOAD := 1
else ifeq ($(MEM_TARGET),ddr-debug)
    BUILD_DIR     := build/$(BOARD)/ddr-debug
    CONFIG_DIR    := ddr-debug
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/linker/mpfs-ddr-loaded-by-boot-loader.ld
    TARGET_DEFS   := -DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1
    OPT_LEVEL     ?= -Og
else ifeq ($(MEM_TARGET),ddr-release)
    BUILD_DIR     := build/$(BOARD)/ddr-release
    CONFIG_DIR    := ddr-release
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/linker/mpfs-ddr-loaded-by-boot-loader.ld
    TARGET_DEFS   := -DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1
    OPT_LEVEL     ?= -O2
    NEEDS_HSS_PAYLOAD := 1
else ifeq ($(MEM_TARGET),envm-lim)
    BUILD_DIR     := build/$(BOARD)/envm-lim
    CONFIG_DIR    := envm-scratchpad-release
    LINKER_SCRIPT := $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/linker/mpfs-envm-lma-scratchpad-vma.ld
    TARGET_DEFS   := -DMEM_TARGET_LIM
    OPT_LEVEL     ?= -O2
else
    $(error Unknown MEM_TARGET='$(MEM_TARGET)'. Valid: lim-debug, lim-release, ddr-debug, ddr-release, envm-lim)
endif

# 3. Compiler & Linker Flags
#include middleware/middleware.mk

ARCH_FLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany \
              -msmall-data-limit=8 -mstrict-align -mno-save-restore

INCLUDE_DIRS := application application/inc \
                $(MIDDLEWARE_INC_DIRS) \
                platform platform/mpfs_hal platform/hal \
                $(BOARD_INC_DIRS) $(BOARD_DIR)/fpga_design_config \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR) \
                $(BOARD_DIR)/platform_config/$(CONFIG_DIR)/mpfs_hal_config

CFLAGS += $(ARCH_FLAGS) $(OPT_LEVEL) -g3 -fmessage-length=0 -fsigned-char \
          -ffunction-sections -fdata-sections -std=gnu11 -Wall -Wextra \
          --specs=nano.specs $(BOARD_CFLAGS) $(TARGET_DEFS) \
          -DBOARD_NAME="\"$(BOARD)\"" \
          -DMPFS_TARGET_DIE="\"$(DIE)\"" \
          -DMPFS_TARGET_PACKAGE="\"$(PACKAGE)\"" \
          -DBUILD_TIMESTAMP="\"$(BUILD_TIMESTAMP)\"" \
          $(addprefix -I, $(INCLUDE_DIRS))

LDFLAGS := $(ARCH_FLAGS) $(OPT_LEVEL) -g3 -nostartfiles -T $(LINKER_SCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -Wl,--gc-sections \
           --specs=nano.specs --specs=nosys.specs

ifeq ($(findstring riscv-none-elf,$(CROSS_COMPILE)),riscv-none-elf)
    LDFLAGS += -Wl,--no-warn-rwx-segments
endif

# 4. Sources & Objects
APP_SRCS_C := $(BOARD_SRCS_C) \
              $(wildcard application/hart*/*.c)

DRIVER_SRCS_C := platform/drivers/mss/mss_mmuart/mss_uart.c \
                 platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac.c \
                 $(BOARD_PHY_DRIVER)

HAL_SRCS_C := platform/mpfs_hal/startup_gcc/newlib_stubs.c \
              platform/mpfs_hal/startup_gcc/system_startup.c \
              $(wildcard platform/mpfs_hal/common/*.c) \
              $(wildcard platform/mpfs_hal/common/nwc/*.c) \
              $(wildcard platform/hal/*.c)

SRCS_C := $(sort $(APP_SRCS_C) $(MIDDLEWARE_SRCS_C) $(DRIVER_SRCS_C) $(HAL_SRCS_C))

SRCS_ASM := $(sort \
    $(wildcard platform/hal/*.S) \
    $(wildcard platform/mpfs_hal/startup_gcc/*.S))

OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS_C)) \
        $(patsubst %.S, $(BUILD_DIR)/%.o, $(SRCS_ASM))

ELF_FILE     := $(BUILD_DIR)/$(TARGET).elf
BIN_FILE     := $(BUILD_DIR)/$(TARGET).bin
HEX_FILE     := $(BUILD_DIR)/$(TARGET).hex
FBI_FILE     := $(BUILD_DIR)/$(TARGET).fbi
PAYLOAD_FILE := $(BUILD_DIR)/$(TARGET)_payload.bin

BASE_ARTIFACTS := $(ELF_FILE) $(BIN_FILE) $(HEX_FILE) $(FBI_FILE)
ALL_ARTIFACTS  := $(BASE_ARTIFACTS) $(if $(filter 1,$(NEEDS_HSS_PAYLOAD)),$(PAYLOAD_FILE))

# 5. Core Build Rules
.PHONY: all clean banner generate_headers show_size memusage help \
        lim-debug lim-release ddr-debug ddr-release envm-lim \
        program-envm-lim program-job openocd debug debug-lim debug-ddr

all: banner generate_headers $(ALL_ARTIFACTS) show_size

banner:
	@echo "========================================================================"
	@echo "  PolarFire SoC NetApp | Board: $(BOARD) | Target: $(MEM_TARGET) ($(OPT_LEVEL))"
	@echo "  Linker: $(LINKER_SCRIPT)"
	@echo "========================================================================"

generate_headers:
	@if [ -d "$(XML_CONFIG_DIR)" ] && [ -f "$(SOC_GEN_SCRIPT)" ]; then \
		$(PYTHON) $(SOC_GEN_SCRIPT) $(XML_CONFIG_DIR) $(CONFIG_OUT_DIR); \
	fi

$(OBJS): | generate_headers

# Suppress vendor warnings in platform/ and lwip/ while keeping -Wall -Wextra on app code
$(BUILD_DIR)/platform/%.o $(BUILD_DIR)/middleware/lwip/%.o: CFLAGS += -w

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo " [CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo " [AS]  $<"
	@$(AS) $(CFLAGS) -c $< -o $@

$(ELF_FILE): $(OBJS) $(LINKER_SCRIPT)
	@echo " [LD]  $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BIN_FILE): $(ELF_FILE)
	@$(OBJCOPY) -O binary $< $@

$(HEX_FILE): $(ELF_FILE)
	@$(OBJCOPY) -O ihex $< $@

$(FBI_FILE): $(BIN_FILE)
	@$(CRCFBIGEN) $< -o $@ --fbi --little

show_size: $(ELF_FILE)
	@echo ""
	@$(SIZE) $<

memusage: $(ELF_FILE)
	@$(MEMUSAGE) $< $(LINKER_SCRIPT) $(patsubst %-,%,$(CROSS_COMPILE))

# 6. HSS Payload Generation (lim-release & ddr-release targets)
$(HSS_PAYLOAD_GEN):
ifeq ($(HOST_OS),Windows)
	@if [ ! -f "$@" ]; then echo "[ERROR] '$(HSS_PAYLOAD_GEN)' not found!"; exit 1; fi
else
	@$(MAKE) -C $(HSS_PAYLOAD_GEN_DIR)
endif

$(BUILD_DIR)/payload.yaml: $(ELF_FILE)
	@mkdir -p $(dir $@)
	@ENTRY=$$($(READELF) -h $< | awk '/Entry point address:/ {print $$4}'); \
	printf "set-name: 'PolarFire-SoC-HSS::$(TARGET)'\n\nhart-entry-points: {\n  u54_1: '%s',\n  u54_2: '%s',\n  u54_3: '%s',\n  u54_4: '%s'\n}\n\npayloads:\n  %s:\n    owner-hart: u54_1\n    secondary-hart: u54_2\n    secondary-hart: u54_3\n    secondary-hart: u54_4\n    priv-mode: prv_m\n    skip-opensbi: true\n    payload-name: \"%s\"\n" \
	"$$ENTRY" "$$ENTRY" "$$ENTRY" "$$ENTRY" "$<" "$(TARGET)" > $@

$(PAYLOAD_FILE): $(ELF_FILE) $(BUILD_DIR)/payload.yaml $(HSS_PAYLOAD_GEN)
	@echo " [HSS] Generating boot payload: $@"
	@$(HSS_PAYLOAD_GEN) -c $(BUILD_DIR)/payload.yaml $@
	@cp -f $@ $(BUILD_DIR)/payload.bin

# 7. Build, Flash & JTAG Debug Workflows
lim-debug lim-release ddr-debug ddr-release envm-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=$@ all

program-envm-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=envm-lim all
	"$(JAVA)" -jar $(BOOTMODE_PROGRAMMER) --bootmode 1 --die $(DIE) --package $(PACKAGE) --workdir $(CURDIR)/build/$(BOARD)/envm-lim

program-job:
	@if [ ! -f "$(JOB_FILE)" ]; then echo "[ERROR] Job file '$(JOB_FILE)' not found!"; exit 1; fi
	@mkdir -p build/fp_job
	@printf "create_job_project -job_project_location {$(CURDIR)/build/fp_job} -job_file {$(CURDIR)/$(JOB_FILE)} -overwrite 1\nrun_selected_actions\nclose_project\n" > build/fp_job/program_job.tcl
	$(FPEXPRESS) SCRIPT:build/fp_job/program_job.tcl

openocd:
	$(OPENOCD) $(OPENOCD_CFG)

debug debug-lim:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=lim-debug gdb-lim

debug-ddr:
	@$(MAKE) BOARD=$(BOARD) MEM_TARGET=ddr-debug gdb-ddr

gdb-lim: $(ELF_FILE)
	$(GDB) $(ELF_FILE) \
		-ex 'set $$target_riscv=1' \
		-ex 'set mem inaccessible-by-default off' \
		-ex 'target extended-remote localhost:3333' \
		-ex 'monitor reset halt' \
		-ex 'load' \
		-ex 'thread apply all set $$pc = _start' \
		-ex 'continue'

gdb-ddr: $(ELF_FILE)
	$(GDB) $(ELF_FILE) \
		-ex 'set $$target_riscv=1' \
		-ex 'set mem inaccessible-by-default off' \
		-ex 'target extended-remote localhost:3333' \
		-ex 'monitor halt' \
		-ex 'load' \
		-ex 'thread apply 2-5 set $$pc = _start' \
		-ex 'continue'

clean:
	@rm -rf build .gdbinit

help:
	@echo "========================================================================"
	@echo "                 PolarFire SoC NetApp - Makefile Help                   "
	@echo "========================================================================"
	@echo "Available Boards: $(AVAILABLE_BOARDS) (select via BOARD=<name>)"
	@echo ""
	@echo "1. Build Targets (make BOARD=<board> <target>):"
	@echo "   lim-debug        : Build for execution from L2-LIM (Debug mode, -Og)."
	@echo "   lim-release      : Build for execution from L2-LIM (Release mode, -O2)."
	@echo "   ddr-debug        : Build for execution from DDR (Debug mode, -Og)."
	@echo "   ddr-release      : Build for execution from DDR (Release mode, -O2)."
	@echo "   envm-lim         : Build for eNVM boot, running in L2-LIM (Release mode)."
	@echo ""
	@echo "2. JTAG Debugging:"
	@echo "   openocd          : Start OpenOCD connection to the board (Terminal 1)."
	@echo "   debug-lim        : Build lim-debug, reset/load via GDB, and run (Terminal 2)."
	@echo "   debug-ddr        : Build ddr-debug, halt/load via GDB without resetting DDR (Terminal 2)."
	@echo ""
	@echo "3. Flashing & Deployment:"
	@echo "   program-envm-lim : Build envm-lim and program into eNVM using Bootmode Programmer."
	@echo "   program-job      : Program the FPGA using the .job file specified in board.mk."
	@echo "   * Note on HSS    : For HSS YMODEM or SPI Flash boot, build lim-release or ddr-release"
	@echo "                      and deploy the generated _payload.bin using mpfs-terminal."
	@echo "                      (See README.md for exact commands)."
	@echo ""
	@echo "4. Utilities:"
	@echo "   memusage         : Show memory usage summary for the active target."
	@echo "   clean            : Remove all generated build files."
	@echo "========================================================================"
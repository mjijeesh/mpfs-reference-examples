BOARD       ?= mpfs-disco-kit
ROOT_DIR    := ..
BOARD_DIR   := $(ROOT_DIR)/boards/$(BOARD)
BUILD_DIR   := build/ddr
TARGET      := mpfs-baremetal

# Cross Compiler Detection
CROSS_COMPILE ?= riscv-none-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

# Include board definitions
-include $(BOARD_DIR)/board.mk

ARCH_FLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany -msmall-data-limit=8 -mstrict-align -mno-save-restore

INCLUDES := -I$(ROOT_DIR)/common \
            -I$(ROOT_DIR)/application \
            -I$(ROOT_DIR)/application/bios \
            -I$(ROOT_DIR)/application/drivers/uart \
            -I$(ROOT_DIR)/platform \
            -I$(ROOT_DIR)/platform/drivers/mss/mss_mmuart \
            -I$(ROOT_DIR)/platform/drivers/mss/mss_gpio \
            -I$(ROOT_DIR)/platform/drivers/mss/mss_sys_services \
            -I$(BOARD_DIR)/platform_config/ddr-release \
            -I$(BOARD_DIR)/platform_config/ddr-release/mpfs_hal_config \
            -I$(BOARD_DIR)

CFLAGS += $(ARCH_FLAGS) -O0 -g3 -fmessage-length=0 -fsigned-char \
          -ffunction-sections -fdata-sections -std=gnu11 \
          --specs=nano.specs $(INCLUDES) \
          -DIMAGE_LOADED_BY_BOOTLOADER=1 \
          -DBOARD_NAME="\"$(BOARD)\""

LINKER_SCRIPT := $(BOARD_DIR)/platform_config/ddr-release/linker/mpfs-ddr-loaded-by-boot-loader.ld

LDFLAGS := $(ARCH_FLAGS) -O0 -g3 -nostartfiles -T $(LINKER_SCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -Wl,--gc-sections \
           --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-rwx-segments

# Source discovery from parent directory
APP_SRCS := $(wildcard $(ROOT_DIR)/application/*.c) \
            $(wildcard $(ROOT_DIR)/application/hart1/*.c) \
            $(wildcard $(ROOT_DIR)/application/bios/*.c)

DRV_SRCS := $(ROOT_DIR)/platform/drivers/mss/mss_mmuart/mss_uart.c \
            $(ROOT_DIR)/platform/drivers/mss/mss_gpio/mss_gpio.c \
            $(ROOT_DIR)/platform/drivers/mss/mss_sys_services/mss_sys_services.c

HAL_SRCS_C   := $(wildcard $(ROOT_DIR)/platform/mpfs_hal/startup_gcc/*.c) \
                $(wildcard $(ROOT_DIR)/platform/mpfs_hal/common/*.c) \
                $(wildcard $(ROOT_DIR)/platform/mpfs_hal/common/nwc/*.c) \
                $(wildcard $(ROOT_DIR)/platform/hal/*.c)

HAL_SRCS_ASM := $(wildcard $(ROOT_DIR)/platform/hal/*.S) \
                $(wildcard $(ROOT_DIR)/platform/mpfs_hal/startup_gcc/*.S)

OBJS := $(patsubst $(ROOT_DIR)/%.c, $(BUILD_DIR)/%.o, $(APP_SRCS) $(DRV_SRCS) $(HAL_SRCS_C)) \
        $(patsubst $(ROOT_DIR)/%.S, $(BUILD_DIR)/%.o, $(HAL_SRCS_ASM))

# Targets
.PHONY: all ddr clean

all: ddr

ddr: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo " [CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo " [AS]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	@echo " [LD]  $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo " [OBJ] $@"
	@$(OBJCOPY) -O binary $< $@

clean:
	@rm -rf build
# Hardware parameters
DIE             := MPFS095T
PACKAGE         := FCSG325

# Discovery Kit stdio routing (MMUART4 is the 2nd USB-UART port)
#STDIO_UART      := 4
#CFLAGS          += -DMICROCHIP_STDIO_THRU_MMUARTX=\&g_mss_uart4_lo \
                   -DMICROCHIP_STDIO_PERIPH=MSS_PERIPH_MMUART4

# Platform flags & drivers
#CFLAGS          += -DMPFS_DISCOVERY_KIT
#CFLAGS          += -DUSE_MSS_SPI0

# Host flashing port (HSS console on /dev/ttyUSB1)
#SERIAL_PORT     ?= /dev/ttyUSB1
#JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-disco-kit.job


BOARD_CFLAGS += -DBOARD_MPFS_DISCO_KIT   # Use -DBOARD_MPFS_ICICLE_KIT in boards/mpfs-icicle-kit/board.mk

ifeq ($(BUILD_CONFIG),lim-debug)
    MEM_EXEC_ADDR := 0x08000000
    LDSCRIPT      := $(BOARD_DIR)/platform_config/lim-debug/linker/mpfs-lim.ld
    PLAT_CFG_DIR  := $(BOARD_DIR)/platform_config/lim-debug
else ifeq ($(BUILD_CONFIG),lim-release)
    MEM_EXEC_ADDR := 0x08000000
    LDSCRIPT      := $(BOARD_DIR)/platform_config/lim-release/linker/mpfs-lim-app.ld
    PLAT_CFG_DIR  := $(BOARD_DIR)/platform_config/lim-release
else ifeq ($(BUILD_CONFIG),ddr-debug)
    MEM_EXEC_ADDR := 0x80000000
    LDSCRIPT      := $(BOARD_DIR)/platform_config/ddr-debug/linker/mpfs-ddr-loaded-by-boot-loader.ld
    PLAT_CFG_DIR  := $(BOARD_DIR)/platform_config/ddr-debug
else ifeq ($(BUILD_CONFIG),ddr-release)
    MEM_EXEC_ADDR := 0x80000000
    LDSCRIPT      := $(BOARD_DIR)/platform_config/ddr-release/linker/mpfs-ddr-loaded-by-boot-loader.ld
    PLAT_CFG_DIR  := $(BOARD_DIR)/platform_config/ddr-release
else
    $(error Unsupported BUILD_CONFIG '$(BUILD_CONFIG)'. Use: lim-debug | lim-release | ddr-debug | ddr-release)
endif


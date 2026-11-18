# ==============================================================================
# Board Configuration: Microchip PolarFire SoC Discovery Kit (MPFS095T)
# ==============================================================================

# Silicon & package metadata
DIE             := MPFS095T
PACKAGE         := FCSG325
STDIO_UART      := 4

# Libero design & flash job paths
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-disco-kit.job

# Board-specific sources, includes & Ethernet PHY driver
BOARD_SRCS_C     := $(BOARD_DIR)/board_init.c
BOARD_INC_DIRS   := $(BOARD_DIR)

BOARD_PHY_DRIVER := platform/drivers/mss/mss_ethernet_mac/vsc8221_phy.c \
                    platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac.c \
                    platform/drivers/mss/mss_ethernet_mac/null_phy.c


# Board & Ethernet MAC/PHY compile definitions
BOARD_CFLAGS     := -DBOARD_MPFS_DISCO_KIT \
                    -DMPFS_DISCOVERY_KIT \
                    -DTARGET_DISCOVERY_KIT \
                    -DTARGET_G5_SOC \
                    -DMSS_MAC_SIMPLE_TX_QUEUE \
                    -DGEM0_MSS_MDIO

# Enabling support for emmc driver for the board
# CoreGPIO is needed to do the mux selection of sdcard/emmc and also to control the reset pin of the emmc device
#BOARD_CFLAGS     += -DMSS_MMC_ENABLE
BOARD_SRCS_C += platform/drivers/mss/mss_mmc/mss_mmc.c \
                platform/drivers/mss/mss_mmc/mss_mmc_if.c \
                platform/drivers/mss/mss_gpio/mss_gpio.c
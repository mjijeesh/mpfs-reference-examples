# ==============================================================================
# Board Configuration: Microchip PolarFire SoC Icicle Kit (MPFS250T)
# ==============================================================================

# Silicon & package metadata (use MPFS250T_ES for early Engineering Sample kits)
DIE             := MPFS250T
PACKAGE         := FCVG484
STDIO_UART      := 1

# Libero design & flash job paths
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-icicle-kit.job

# Board-specific sources, includes & Ethernet PHY driver
BOARD_SRCS_C     := $(BOARD_DIR)/board_init.c
BOARD_INC_DIRS   := $(BOARD_DIR)

BOARD_PHY_DRIVER := platform/drivers/mss/mss_ethernet_mac/vsc8662_phy.c \
                    platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac.c \
                    platform/drivers/mss/mss_ethernet_mac/null_phy.c

# Board & Ethernet MAC/PHY compile definitions (MDIO bus wired to GEM1)
BOARD_CFLAGS     := -DBOARD_MPFS_ICICLE_KIT \
                    -DMPFS_ICICLE_KIT \
                    -DTARGET_ICICLE_KIT \
                    -DTARGET_G5_SOC \
                    -DMSS_MAC_SIMPLE_TX_QUEUE \
                    -DGEM1_MSS_MDIO
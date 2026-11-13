# Silicon & package metadata
DIE             := MPFS095T
PACKAGE         := FCSG325

# Discovery Kit UART routing (MMUART0 = HSS CLI on ttyUSB0, MMUART4 = App stdio on ttyUSB2)
STDIO_UART      := 4
SERIAL_PORT     ?= /dev/ttyUSB0

# Libero design & flash job paths
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-disco-kit.job

# Board & Ethernet MAC/PHY compile definitions (matched to working Discovery Kit reference)
BOARD_CFLAGS    += -DBOARD_MPFS_DISCO_KIT \
                   -DMPFS_DISCOVERY_KIT \
                   -DTARGET_DISCOVERY_KIT \
                   -DTARGET_G5_SOC \
                   -DMSS_MAC_SIMPLE_TX_QUEUE \
                   -DGEM0_MSS_MDIO
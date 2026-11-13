# Silicon & package metadata
DIE             := MPFS250T
PACKAGE         := FCVG484

# Icicle Kit UART routing (MMUART0 = HSS CLI, MMUART1 = App stdio)
STDIO_UART      := 1
SERIAL_PORT     ?= /dev/ttyUSB0

# Libero design & flash job paths
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-icicle-kit.job

# Board & PHY driver compile definitions
BOARD_CFLAGS    += -DBOARD_MPFS_ICICLE_KIT \
                   -DMSS_MAC_USE_PHY_VSC8662
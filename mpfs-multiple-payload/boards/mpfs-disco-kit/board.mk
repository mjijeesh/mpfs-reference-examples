# Hardware parameters
DIE             := MPFS095T
PACKAGE         := FCSG325

# Discovery Kit stdio routing (MMUART4 is the 2nd USB-UART port)
STDIO_UART      := 4
CFLAGS          += -DMICROCHIP_STDIO_THRU_MMUARTX=\&g_mss_uart4_lo \
                   -DMICROCHIP_STDIO_PERIPH=MSS_PERIPH_MMUART4

# Platform flags & drivers
CFLAGS          += -DMPFS_DISCOVERY_KIT
CFLAGS          += -DUSE_MSS_SPI0

# Host flashing port (HSS console on /dev/ttyUSB1)
SERIAL_PORT     ?= /dev/ttyUSB1
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-disco-kit.job

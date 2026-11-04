# Hardware parameters
DIE             := MPFS250T
PACKAGE         := FCVG484

# Icicle Kit stdio routing (MMUART1 is the 2nd USB-UART port)
STDIO_UART      := 1
CFLAGS          += -DMICROCHIP_STDIO_THRU_MMUARTX=\&g_mss_uart1_lo \
                   -DMICROCHIP_STDIO_PERIPH=MSS_PERIPH_MMUART1

# Platform flags & drivers
CFLAGS          += -DMPFS_ICICLE_KIT
CFLAGS          += -DUSE_CORE_SPI -DCORESPI_BASE_ADDR=0x40000000UL

# Host flashing port (HSS console on /dev/ttyUSB0)
SERIAL_PORT     ?= /dev/ttyUSB0
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-icicle-kit.job

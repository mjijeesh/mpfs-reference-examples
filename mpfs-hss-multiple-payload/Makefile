ifeq ($(OS),Windows_NT)
    HOST_OS  := Windows
    BIN_EXT  := .exe
    PYTHON   ?= python
else
    HOST_OS  := Linux
    BIN_EXT  :=
    PYTHON   ?= python3
endif

BOARD           ?= mpfs-disco-kit
HART1_DIR       ?= hart1-cli
SERIAL_PORT     ?= /dev/ttyUSB1
FLASH_ADDR      ?= 0x00020000

# Cross-compiler toolchain
CROSS_COMPILE ?= riscv-none-elf-
OBJCOPY       ?= $(CROSS_COMPILE)objcopy



# Look for hss-payload-generator with or without subdirectory
HSS_PAYLOAD_GEN_DIR ?= tools/hss-payload-generator
ifeq ($(wildcard $(HSS_PAYLOAD_GEN_DIR)/Makefile),)
    HSS_PAYLOAD_GEN ?= tools/hss-payload-generator$(BIN_EXT)
else
    HSS_PAYLOAD_GEN ?= $(HSS_PAYLOAD_GEN_DIR)/hss-payload-generator$(BIN_EXT)
endif

MPFS_TERMINAL   ?= $(PYTHON) tools/mpfs-terminal.py

.PHONY: all clean build-projects generate-payload flash

all: build-projects generate-payload

build-projects:
	@echo "--- [1/4] Building Project 1 (hart1-cli) ---"
	$(MAKE) -C $(HART1_DIR) BOARD=$(BOARD) ddr
	@echo "--- [2/4] Building Project 2 (hart2-math) ---"
	$(MAKE) -C hart2-math
	@echo "--- [3/4] Building Project 3 (hart3-memstress) ---"
	$(MAKE) -C hart3-memstress
	@echo "--- [4/4] Building Project 4 (hart4-watchdog) ---"
	$(MAKE) -C hart4-watchdog

generate-payload: multi_payload.bin

multi_payload.bin: multi_payload.yaml
	@echo "--- Generating Multi-Hart HSS Payload ---"
	$(HSS_PAYLOAD_GEN) -c multi_payload.yaml multi_payload.bin

flash: multi_payload.bin
	@echo "--- Flashing Combined Multi-App Payload to SPI Flash ---"
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel multi_payload.bin --flash --kernel-adr $(FLASH_ADDR)

# ------------------------------------------------------------------------------
# FBI Single-Binary Targets
# ------------------------------------------------------------------------------
CRCFBIGEN ?= $(PYTHON) tools/crcfbigen.py

fbi: build-projects
	@echo "--- Extracting flat binaries for Harts 2, 3, 4 ---"
	$(OBJCOPY) -O binary hart2-math/hart2-math.elf hart2-math/hart2-math.bin
	$(OBJCOPY) -O binary hart3-memstress/hart3-memstress.elf hart3-memstress/hart3-memstress.bin
	$(OBJCOPY) -O binary hart4-watchdog/hart4-watchdog.elf hart4-watchdog/hart4-watchdog.bin
	@echo "--- Stitching 4 binaries into multi_app.bin ---"
	$(PYTHON) tools/stitch_fbi.py
	@echo "--- Generating multi_app.fbi header ---"
	$(PYTHON) tools/crcfbigen.py multi_app.bin -o multi_app.fbi --fbi --little

flash-fbi: fbi
	$(MPFS_TERMINAL) $(SERIAL_PORT) --kernel multi_app.fbi --flash --kernel-adr $(FLASH_ADDR)

clean:
	$(MAKE) -C $(HART1_DIR) clean
	$(MAKE) -C hart2-math clean
	$(MAKE) -C hart3-memstress clean
	$(MAKE) -C hart4-watchdog clean
	rm -f multi_payload.bin
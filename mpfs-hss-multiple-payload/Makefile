BOARD               ?= mpfs-disco-kit
HSS_PAYLOAD_GEN     ?= tools/hss-payload-generator/hss-payload-generator
MPFS_TERMINAL       ?= python3 tools/mpfs-terminal.py
SERIAL_PORT         ?= /dev/ttyUSB1
FLASH_ADDR          ?= 0x00020000

.PHONY: all clean build-projects generate-payload flash

all: build-projects generate-payload

build-projects:
	@echo "--- [1/4] Building Project 1 (hart1-cli) ---"
	$(MAKE) -C hart1-cli BOARD=$(BOARD) ddr
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

clean:
	$(MAKE) -C hart1-cli clean
	$(MAKE) -C hart2-math clean
	$(MAKE) -C hart3-memstress clean
	$(MAKE) -C hart4-watchdog clean
	rm -f multi_payload.bin
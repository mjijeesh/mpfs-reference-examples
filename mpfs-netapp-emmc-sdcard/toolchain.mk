# ==============================================================================
# Host OS, Microchip Tool Paths & RISC-V Cross-Compiler Configuration
# ==============================================================================

ifeq ($(OS),Windows_NT)
    HOST_OS               := Windows
    BIN_EXT               := .exe
    SC_INSTALL_DIR        ?= C:/Microchip/SoftConsole-v2022.2-RISC-V-747
    LIBERO_DIR            ?= C:/Microchip/Libero_SoC_2025.2/Libero_SoC
    PYTHON                ?= $(SC_INSTALL_DIR)/python3/python.exe
    JAVA                  ?= $(SC_INSTALL_DIR)/eclipse/jre/bin/java.exe
    OPENOCD               ?= $(SC_INSTALL_DIR)/openocd/bin/openocd.exe
    FPGENPROG             ?= $(LIBERO_DIR)/Designer/bin64/fpgenprog.exe
    FPEXPRESS             ?= $(LIBERO_DIR)/Designer/bin/fpexpress.exe
else
    HOST_OS               := Linux
    BIN_EXT               :=
    SC_INSTALL_DIR        ?= /opt/microchip/SoftConsole/SoftConsole-v2022
    LIBERO_DIR            ?= /opt/microchip/Libero_SoC_2025.2/Libero_SoC
    PYTHON                ?= python3
    JAVA                  ?= java
    OPENOCD               ?= $(SC_INSTALL_DIR)/openocd/bin/openocd
    FPGENPROG             ?= $(LIBERO_DIR)/Designer/bin64/fpgenprog
    FPEXPRESS             ?= $(LIBERO_DIR)/Designer/bin/FPExpress
endif

SC_INSTALL_DIR := $(subst \,/,$(SC_INSTALL_DIR))
LIBERO_DIR     := $(subst \,/,$(LIBERO_DIR))
FPGENPROG      := $(subst \,/,$(FPGENPROG))
FPEXPRESS      := $(subst \,/,$(FPEXPRESS))

# Auto-detect RISC-V GCC prefix (system PATH first, then SoftConsole)
CROSS_COMPILE ?= $(shell which riscv-none-elf-gcc >/dev/null 2>&1 && echo riscv-none-elf- || \
                         (which riscv64-unknown-elf-gcc >/dev/null 2>&1 && echo riscv64-unknown-elf- || \
                          echo $(SC_INSTALL_DIR)/riscv-unknown-elf-gcc/bin/riscv64-unknown-elf-))

CC      := $(CROSS_COMPILE)gcc
AS      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size
GDB     := $(CROSS_COMPILE)gdb
READELF := $(CROSS_COMPILE)readelf

# Host utilities & flash programmers
BOOTMODE_PROGRAMMER ?= $(SC_INSTALL_DIR)/extras/mpfs/mpfsBootmodeProgrammer.jar
CRCFBIGEN           ?= $(PYTHON) tools/crcfbigen.py
MPFS_TERMINAL       ?= $(PYTHON) tools/mpfs-terminal.py
MEMUSAGE            ?= $(PYTHON) tools/memusage.py
SOC_GEN_SCRIPT      := platform/soc_config_generator/mpfs_configuration_generator.py

ifeq ($(HOST_OS),Windows)
    HSS_PAYLOAD_GEN_DIR := tools/hss-payload-generator/binaries
    HSS_PAYLOAD_GEN     := $(HSS_PAYLOAD_GEN_DIR)/hss-payload-generator.exe
else
    HSS_PAYLOAD_GEN_DIR ?= tools/hss-payload-generator
    ifeq ($(wildcard $(HSS_PAYLOAD_GEN_DIR)/Makefile),)
        HSS_PAYLOAD_GEN_DIR := tools
    endif
    HSS_PAYLOAD_GEN     := $(HSS_PAYLOAD_GEN_DIR)/hss-payload-generator$(BIN_EXT)
endif

export SC_INSTALL_DIR FPGENPROG FPEXPRESS BOOTMODE_PROGRAMMER
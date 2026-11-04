CROSS_COMPILE ?= riscv-none-elf-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

CFLAGS  := -march=rv64gc -mabi=lp64d -mcmodel=medany -O2 -nostdlib -ffreestanding -Wall -I../common
LDFLAGS := -T linker.ld -nostdlib

SRCS    := crt0.S main.c
TARGET  := hart4-watchdog.elf

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET)
CROSS_COMPILE ?= riscv-none-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

ARCH_FLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany
CFLAGS     := $(ARCH_FLAGS) -O2 -nostdlib -ffreestanding -Wall -I../common
LDFLAGS    := $(ARCH_FLAGS) -T linker.ld -nostdlib -Wl,--no-warn-rwx-segments

OBJS    := crt0.o main.o
TARGET  := hart2-math.elf

all: $(TARGET)

crt0.o: crt0.S
	@echo " [AS]  $<"
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c
	@echo " [CC]  $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@echo " [LD]  $@"
	$(LD) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(OBJS) $(TARGET)
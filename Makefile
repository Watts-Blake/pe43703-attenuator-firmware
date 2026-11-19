CROSS ?= arm-none-eabi-
CC_HOST ?= /usr/bin/cc
PYTHON ?= python3
CFLAGS = -std=c11 -Os -g3 -Wall -Wextra -Werror -Wconversion -ffreestanding -fno-builtin -fstack-usage -ffile-prefix-map="$(CURDIR)"=. -fdata-sections -ffunction-sections -mcpu=cortex-m3 -mthumb
.PHONY: all test inspect clean
all: build/attenuator.elf build/attenuator.bin build/attenuator.hex
build:
	mkdir -p build
build/attenuator.elf: firmware/core.c firmware/core.h firmware/target.c firmware/startup.S firmware/stm32f103c8.ld | build
	$(CROSS)gcc $(CFLAGS) -nostdlib -Wl,--gc-sections,-Map=build/attenuator.map -Tfirmware/stm32f103c8.ld firmware/startup.S firmware/target.c firmware/core.c -o $@
build/attenuator.bin: build/attenuator.elf
	$(CROSS)objcopy -O binary $< $@
build/attenuator.hex: build/attenuator.elf
	$(CROSS)objcopy -O ihex $< $@
build/test_core: tests/test_core.c firmware/core.c firmware/core.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_core.c firmware/core.c -o $@
test: all build/test_core
	./build/test_core
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
inspect: all
	$(PYTHON) tools/inspect_image.py build/attenuator.elf
	$(CROSS)size -A build/attenuator.elf
	$(CROSS)objdump -d build/attenuator.elf > build/attenuator.dis
clean:
	rm -rf build

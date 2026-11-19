CROSS ?= arm-none-eabi-
CC_HOST ?= /usr/bin/cc
PYTHON ?= python3
CFLAGS = -std=c11 -Os -g3 -Wall -Wextra -Werror -Wconversion -ffreestanding -fno-builtin -fstack-usage -ffile-prefix-map="$(CURDIR)"=. -fdata-sections -ffunction-sections -mcpu=cortex-m3 -mthumb
.PHONY: all test inspect clean
all: build/attenuator.elf build/attenuator.bin build/attenuator.hex build/bootloader.bin build/bootloader.hex build/combined.bin build/combined.hex
build:
	mkdir -p build
build/attenuator.elf: firmware/core.c firmware/core.h firmware/version.h firmware/adc.c firmware/adc.h firmware/oled.c firmware/oled.h firmware/layout.h firmware/target.c firmware/startup.S firmware/stm32f103c8.ld | build
	$(CROSS)gcc $(CFLAGS) -nostdlib -Wl,--gc-sections,-Map=build/attenuator.map -Tfirmware/stm32f103c8.ld firmware/startup.S firmware/target.c firmware/core.c firmware/adc.c firmware/oled.c -o $@
build/attenuator.bin: build/attenuator.elf
	$(CROSS)objcopy -O binary $< $@
build/attenuator.hex: build/attenuator.elf
	$(CROSS)objcopy -O ihex $< $@
build/test_core: tests/test_core.c firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_core.c firmware/core.c -o $@
build/bootloader.elf: firmware/boot_core.c firmware/boot_core.h firmware/layout.h firmware/boot_target.c firmware/startup.S firmware/bootloader.ld | build
	$(CROSS)gcc $(CFLAGS) -nostdlib -Wl,--gc-sections,-Map=build/bootloader.map -Tfirmware/bootloader.ld firmware/startup.S firmware/boot_target.c firmware/boot_core.c -o $@
build/bootloader.bin: build/bootloader.elf
	$(CROSS)objcopy -O binary $< $@
build/bootloader.hex: build/bootloader.elf
	$(CROSS)objcopy -O ihex $< $@
build/combined.bin: build/attenuator.bin build/bootloader.bin tools/pack_image.py tools/image.py
	$(PYTHON) tools/pack_image.py
build/combined.hex: build/combined.bin
	$(CROSS)objcopy -I binary -O ihex --change-addresses 0x08000000 $< $@
build/test_boot: tests/test_boot.c firmware/boot_core.c firmware/boot_core.h firmware/layout.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_boot.c firmware/boot_core.c -o $@
build/test_adc: tests/test_adc.c firmware/adc.c firmware/adc.h firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_adc.c firmware/adc.c firmware/core.c -o $@
build/test_oled: tests/test_oled.c firmware/oled.c firmware/oled.h firmware/adc.c firmware/adc.h firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_oled.c firmware/oled.c firmware/adc.c firmware/core.c -o $@
test: all build/test_core build/test_boot build/test_adc build/test_oled
	./build/test_boot
	./build/test_core
	./build/test_adc
	./build/test_oled
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
inspect: all
	$(PYTHON) tools/inspect_image.py build/attenuator.elf
	$(PYTHON) tools/inspect_image.py build/bootloader.elf --loader
	$(CROSS)size -A build/bootloader.elf
	$(CROSS)objdump -d build/bootloader.elf > build/bootloader.dis
	$(CROSS)size -A build/attenuator.elf
	$(CROSS)objdump -d build/attenuator.elf > build/attenuator.dis
clean:
	rm -rf build

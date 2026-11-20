CROSS ?= arm-none-eabi-
CC_HOST ?= /usr/bin/cc
PYTHON ?= python3
CFLAGS = -std=c11 -Os -g3 -Wall -Wextra -Werror -Wconversion -ffreestanding -fno-builtin -fstack-usage -ffile-prefix-map="$(CURDIR)"=. -fdata-sections -ffunction-sections -mcpu=cortex-m3 -mthumb
.PHONY: all test inspect clean
all: build/attenuator.elf build/attenuator.bin build/attenuator.hex build/bootloader.bin build/bootloader.hex build/combined.bin build/combined.hex
build:
	mkdir -p build
APP_SOURCES = firmware/target.c firmware/core.c firmware/adc.c firmware/oled.c firmware/screen.c firmware/ui.c firmware/display_id.c firmware/display_config.c firmware/button_cal.c
APP_HEADERS = firmware/core.h firmware/version.h firmware/adc.h firmware/oled.h firmware/screen.h firmware/ui.h firmware/display_id.h firmware/display_config.h firmware/button_cal.h firmware/layout.h
build/attenuator.elf: $(APP_SOURCES) $(APP_HEADERS) firmware/startup.S firmware/stm32f103c8.ld | build
	$(CROSS)gcc $(CFLAGS) -nostdlib -Wl,--nmagic,--gc-sections,-Map=build/attenuator.map -Tfirmware/stm32f103c8.ld firmware/startup.S $(APP_SOURCES) -o $@
build/attenuator.bin: build/attenuator.elf
	$(CROSS)objcopy -O binary $< $@
build/attenuator.hex: build/attenuator.elf
	$(CROSS)objcopy -O ihex $< $@
build/test_core: tests/test_core.c firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_core.c firmware/core.c -o $@
build/bootloader.elf: firmware/boot_core.c firmware/boot_core.h firmware/layout.h firmware/boot_target.c firmware/startup.S firmware/bootloader.ld | build
	$(CROSS)gcc $(CFLAGS) -nostdlib -Wl,--nmagic,--gc-sections,-Map=build/bootloader.map -Tfirmware/bootloader.ld firmware/startup.S firmware/boot_target.c firmware/boot_core.c -o $@
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
build/test_oled: tests/test_oled.c firmware/screen.c firmware/screen.h firmware/oled.c firmware/oled.h firmware/adc.c firmware/adc.h firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_oled.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c -o $@
build/test_ui: tests/test_ui.c firmware/ui.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c firmware/display_id.c firmware/display_config.c firmware/button_cal.c $(APP_HEADERS) | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_ui.c firmware/ui.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c firmware/display_id.c firmware/display_config.c firmware/button_cal.c -o $@
build/test_display_config: tests/test_display_config.c firmware/display_config.c firmware/boot_core.c firmware/boot_core.h firmware/display_config.h firmware/button_cal.h firmware/core.h firmware/layout.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_display_config.c firmware/display_config.c firmware/boot_core.c -o $@
test: all build/test_screen build/test_button_cal build/test_display_config build/test_core build/test_boot build/test_adc build/test_oled build/test_ui
	./build/test_display_config
	./build/test_boot
	./build/test_core
	./build/test_adc
	./build/test_oled
	./build/test_ui
	./build/test_button_cal
	./build/test_screen
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

build/test_button_cal: tests/test_button_cal.c $(APP_HEADERS) firmware/button_cal.c firmware/display_config.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_button_cal.c firmware/button_cal.c firmware/display_config.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c -o $@

build/test_screen: tests/test_screen.c firmware/screen.c firmware/screen.h firmware/oled.c firmware/oled.h firmware/adc.c firmware/adc.h firmware/core.c firmware/core.h firmware/version.h | build
	$(CC_HOST) -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Ifirmware tests/test_screen.c firmware/screen.c firmware/oled.c firmware/adc.c firmware/core.c -o $@

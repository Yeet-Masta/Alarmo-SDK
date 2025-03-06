.PHONY: all clean

all:
	arm-none-eabi-gcc -o payload.elf main.c start.S -mthumb -march=armv7e-m -mtune=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -nostartfiles -nodefaultlibs -T link.ld

clean:
	rm payload.elf

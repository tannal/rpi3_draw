CC = aarch64-none-elf-gcc
OBJCOPY = aarch64-none-elf-objcopy

all: kernel8.img

kernel8.img: boot.o main.o uart.o
	aarch64-none-elf-ld -nostdlib boot.o main.o uart.o -T linker.ld -o kernel8.elf
	$(OBJCOPY) -O binary kernel8.elf kernel8.img

%.o: %.S
	$(CC) -c $< -o $@ -g

%.o: %.c
	$(CC) -g -ffreestanding -O0 -nostdlib -c $< -o $@

clean:
	rm *.o *.elf *.img
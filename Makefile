MCU ?= atmega328p
F_CPU ?= 12000000UL
BAUD ?= 115200UL
PROGRAMMER ?= gpio
PORT ?= /dev/spidev0.0
BUILD_DIR ?= build

CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude

CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -DBAUD=$(BAUD) -Os -Wall -Wextra -std=c11

.PHONY: all sender receiver sender-build receiver-build clean

all: sender-build receiver-build

sender: $(BUILD_DIR)/sender.hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$<

receiver: $(BUILD_DIR)/receiver.hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$<

sender-build: $(BUILD_DIR)/sender.hex

receiver-build: $(BUILD_DIR)/receiver.hex

$(BUILD_DIR)/%.elf: %.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

clean:
	rm -rf $(BUILD_DIR)

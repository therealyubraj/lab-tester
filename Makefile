MCU ?= atmega328p
F_CPU ?= 12000000UL
BAUD ?= 115200UL
SEND_BPS ?= 1000UL
INTER_FRAME_MS ?= 40
RX_QUEUE_FRAMES ?= 16u
RX_QUEUE_PAYLOAD_SIZE ?= 32u
TEST_SOURCE ?= 0x58u
TEST_DESTINATION ?= 0x00u
PROGRAMMER ?= gpio
PORT ?= /dev/spidev0.0
BUILD_ROOT ?= build
BUILD_CONFIG = $(MCU)-f$(F_CPU)-uart$(BAUD)-bps$(SEND_BPS)-gap$(INTER_FRAME_MS)-rxq$(RX_QUEUE_FRAMES)-rxp$(RX_QUEUE_PAYLOAD_SIZE)-src$(TEST_SOURCE)-dst$(TEST_DESTINATION)
BUILD_DIR = $(BUILD_ROOT)/$(BUILD_CONFIG)

CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude

CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -DBAUD=$(BAUD) -DSEND_BPS=$(SEND_BPS) -DINTER_FRAME_MS=$(INTER_FRAME_MS) -DRX_QUEUE_FRAMES=$(RX_QUEUE_FRAMES) -DRX_QUEUE_PAYLOAD_SIZE=$(RX_QUEUE_PAYLOAD_SIZE) -DTEST_SOURCE=$(TEST_SOURCE) -DTEST_DESTINATION=$(TEST_DESTINATION) -Os -Wall -Wextra -std=c11

.PHONY: all sender receiver relayer sender-build receiver-build relayer-build clean

all: sender-build receiver-build relayer-build

sender: $(BUILD_DIR)/sender.hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$<

receiver: $(BUILD_DIR)/receiver.hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$<

relayer: $(BUILD_DIR)/relayer.hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$<

sender-build: $(BUILD_DIR)/sender.hex

receiver-build: $(BUILD_DIR)/receiver.hex

relayer-build: $(BUILD_DIR)/relayer.hex

$(BUILD_DIR)/%.elf: %.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

clean:
	rm -rf $(BUILD_ROOT)

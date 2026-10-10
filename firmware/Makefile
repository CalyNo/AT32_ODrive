# AT32F435CGT7 ODrive-style firmware build
#
# Toolchain: arm-none-eabi-gcc (Keil GNU toolchain or official Arm GNU toolchain)
# Build:     mingw32-make -j   (Git Bash / MSYS2 / MinGW)
#
# The AT32F435_437 SDK subset is vendored under Drivers/.

TARGET        := AT32_ODrive
BUILD_DIR     := build
CC            := arm-none-eabi-gcc
OBJCOPY       := arm-none-eabi-objcopy
SIZE          := arm-none-eabi-size

DEVICE_DEF    := -DAT32F435CGT7 -DHEXT_VALUE=8000000

CPU_FLAGS     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
C_FLAGS       := $(CPU_FLAGS) -O2 -g3 -ffunction-sections -fdata-sections \
                 -Wall -Wextra -Wno-unused-parameter $(DEVICE_DEF)
AS_FLAGS      := $(CPU_FLAGS) -x assembler-with-cpp

INCLUDES      := -IInc -IConfig \
                 -IDrivers/CMSIS/Core/Include \
                 -IDrivers/CMSIS/Device/AT32F435_437 \
                 -IDrivers/AT32F435_437_Firmware_Library/inc \
                 -IDrivers/USB_Device/inc

LDSCRIPT      := Startup/AT32F435xG_FLASH.ld

APP_SRCS      := $(sort $(wildcard Src/*.c))

DRIVER_SRCS   := \
  Drivers/CMSIS/Device/AT32F435_437/system_at32f435_437.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_crm.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_gpio.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_misc.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_pwc.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_flash.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_tmr.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_adc.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_spi.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_usart.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_can.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_dma.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_exint.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_debug.c \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_wdt.c

# USB FS device stack for the CDC virtual COM port (Src/usb_cdc.c).
USB_SRCS      := \
  Drivers/AT32F435_437_Firmware_Library/src/at32f435_437_usb.c \
  Drivers/USB_Device/src/usb_core.c \
  Drivers/USB_Device/src/usbd_core.c \
  Drivers/USB_Device/src/usbd_int.c \
  Drivers/USB_Device/src/usbd_sdr.c \
  Drivers/USB_Device/src/cdc_class.c \
  Drivers/USB_Device/src/cdc_desc.c

C_SRCS        := $(APP_SRCS) $(DRIVER_SRCS) $(USB_SRCS)
ASM_SRCS      := Startup/startup_at32f435_437.s

C_OBJS        := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ASM_OBJS      := $(patsubst %.s,$(BUILD_DIR)/%.o,$(ASM_SRCS))
OBJS          := $(C_OBJS) $(ASM_OBJS)

ELF           := $(BUILD_DIR)/$(TARGET).elf
HEX           := $(BUILD_DIR)/$(TARGET).hex
BIN           := $(BUILD_DIR)/$(TARGET).bin
MAP           := $(BUILD_DIR)/$(TARGET).map

.PHONY: all clean size

all: $(ELF) $(HEX) $(BIN) size

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(AS_FLAGS) $(INCLUDES) -c $< -o $@

# -u _printf_float: newlib-nano omits float formatting unless it is pulled in
# explicitly.  The ASCII protocol answers reads with bare numeric values, so
# "%f" must work on this build path too (the Keil path already supports it).
$(ELF): $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(CPU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(MAP) \
		-Wl,--no-warn-rwx-segments \
		--specs=nano.specs --specs=nosys.specs \
		-u _printf_float \
		$(OBJS) -o $@ -lm

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

# Keil/armcc links the whole object set (no --gc-sections equivalent by default),
# so a symbol referenced from a section that GCC would garbage-collect still has
# to resolve -- the CDC class's usb_delay_ms() hook was exactly that case.
# This target mirrors the Keil behaviour and fails on any undefined symbol.
.PHONY: check-link
check-link: $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $(ELF))
	$(CC) $(CPU_FLAGS) -T$(LDSCRIPT) -Wl,--no-warn-rwx-segments \
		--specs=nano.specs --specs=nosys.specs \
		-u _printf_float \
		$(OBJS) -o $(BUILD_DIR)/$(TARGET)_fulllink.elf -lm

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

size: $(ELF)
	$(SIZE) $(ELF)

clean:
	rm -rf $(BUILD_DIR)

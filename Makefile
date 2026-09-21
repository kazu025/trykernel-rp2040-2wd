# ===================================================================================
# Pico RTOS system v00
# original source TryKernel - Interface 2023/07 Part4Chap3
# Build for Raspberry Pi Pico / RP2040 / Cortex-M0+
# ===================================================================================
TARGET	:= tkv
BUILD   := build

PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
SIZE    := $(PREFIX)size

CPUFLAGS := -mcpu=cortex-m0plus -mthumb

INCLUDES := \
			-Iinclude \
			-Iuser \
			-Idrivers

CFLAGS  := \
			$(CPUFLAGS) -O0 -g3 -Wall -Wextra \
			-ffreestanding -fno-builtin -ffunction-sections \
			-fdata-sections -MMD -MP $(INCLUDES)

ASFLAGS  := \
			$(CPUFLAGS) -O0 -g3 \
			-x assembler-with-cpp \
			-MMD \
			-MP \
			$(INCLUDES)

LDSCRIPT := linker/pico_memmap.ld

LDFLAGS := \
			$(CPUFLAGS) \
			-T$(LDSCRIPT) \
			-nostdlib \
			-nostartfiles \
			-Wl,--gc-sections \
			-Wl,-Map=$(BUILD)/$(TARGET).map

C_SRCS  := \
	boot/boot2.c \
	boot/reset_hdr.c \
	boot/vector_tbl.c \
	kernel/context.c \
	kernel/cyclic.c \
	kernel/eventflag.c \
	kernel/inittsk.c \
	kernel/message_queue.c \
	kernel/mutex.c \
	kernel/scheduler.c \
	kernel/systimer.c \
	kernel/semaphore.c \
	kernel/task_mange.c \
	kernel/task_queue.c \
	kernel/task_sync.c \
	drivers/gpio.c \
	drivers/i2c.c \
	drivers/pwm.c \
	drivers/spi.c \
	drivers/uart.c \
	user/task_led.c	\
	user/task_lcdtemp.c \
	user/task_mpuirq.c \
	user/task_msgtest.c \
	user/task_mutexpi.c \
	user/task_motionled.c \
	user/task_flashlog.c \
	user/task_inapower.c \
	user/task_uartrx.c \
	user/usermain.c \
	user/motor.c \
	user/command.c \
	user/task_uartlog.c \
	user/console.c	\
	user/uart_sync.c \
	user/task_uarttx.c \
	user/uart_tx.c	\
	user/mini_printf.c \
	drivers/gpio.c \
	drivers/adt7410.c \
	drivers/ina226.c \
	drivers/mpu6050.c \
	drivers/w25qxx.c \
	drivers/grove_lcd.c \
	drivers/i2c.c \
	drivers/uart.c
#	kernel/syslib.c \

S_SRCS := \
	kernel/dispatch.S

OBJS := \
	$(addprefix $(BUILD)/, $(C_SRCS:.c=.o)) \
	$(addprefix $(BUILD)/, $(S_SRCS:.S=.o))

DEPS := $(OBJS:.o=.d)

ELF := $(BUILD)/$(TARGET).elf
BIN := $(BUILD)/$(TARGET).bin
HEX := $(BUILD)/$(TARGET).hex
LST := $(BUILD)/$(TARGET).lst

.PHONY: all clean size disasm flash flash-halt halt

all: $(ELF) $(BIN) $(HEX) size

$(ELF): $(OBJS)
	$(CC) $(LDFLAGS) $^ -lgcc -o $@
	$(OBJDUMP) -h -S $@ > $(LST)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

size: $(ELF)
	$(SIZE) $<

disasm: $(ELF)
	$(OBJDUMP) -d -S $< | less

flash: $(ELF)
	openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
		-c "adapter speed 1000" \
		-c "program $(ELF) verify reset exit"

flash-halt: $(ELF)
	openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
		-c "adapter speed 1000" \
		-c "program $(ELF) verify" \
		-c "reset halt" \
		-c "halt" \
		-c "exit"

halt:
	openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
		-c "adapter speed 1000" \
		-c "init" \
		-c "halt" \
		-c "exit"

clean:
	rm -rf $(BUILD)

-include $(DEPS)

# Host tests run kernel logic with interrupt/context switching mocked.
HOSTCC ?= cc
.PHONY: test-mutex
test-mutex:
	@mkdir -p $(BUILD)/tests
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Itests/host -Iinclude \
		tests/host/mutex_test.c kernel/mutex.c kernel/task_queue.c \
		kernel/systimer.c kernel/cyclic.c kernel/task_mange.c -o $(BUILD)/tests/mutex_test
	$(BUILD)/tests/mutex_test

.PHONY: test-cyclic
test-cyclic:
	@mkdir -p $(BUILD)/tests
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Itests/host -Iinclude \
		tests/host/cyclic_test.c kernel/cyclic.c kernel/systimer.c \
		kernel/task_queue.c -o $(BUILD)/tests/cyclic_test
	$(BUILD)/tests/cyclic_test

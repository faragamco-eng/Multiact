# BLDC-310 firmware - STM32G431CBT6
#   make cmsis   fetch CMSIS headers/startup (once)
#   make         build build/bldc310.elf/.bin/.hex
#   make test    six-step simulator + Modbus tests + FOC test suite
#   make test-foc-random   FOC: 40 random motors identified from scratch
#   make sweep   start-up robustness: 5 scenarios x 12 initial rotor angles
#   make plots   run the simulator with CSV output and plot (needs matplotlib)
#   make flash   program with st-flash (optional)

PREFIX  ?= arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size
BUILD    = build
CMSIS    = cmsis

MCU      = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS   = $(MCU) -O2 -g3 -std=c11 -Wall -Wextra -Wshadow -Wdouble-promotion \
           -fsingle-precision-constant -ffunction-sections -fdata-sections \
           -DSTM32G431xx -Iinc -Icore -Ibsp -I$(CMSIS)/include
LDFLAGS  = $(MCU) -Tlinker/stm32g431cb.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/bldc310.map -lm

SRCS = app/main.c core/sixstep.c bsp/bsp_stm32g431.c $(CMSIS)/src/system_stm32g4xx.c
ASRC = $(CMSIS)/src/startup_stm32g431xx.s
OBJS = $(SRCS:%.c=$(BUILD)/%.o) $(ASRC:%.s=$(BUILD)/%.o)

all: $(BUILD)/bldc310.elf

$(CMSIS)/include/stm32g431xx.h:
	@echo "CMSIS missing - run 'make cmsis'"; exit 1

cmsis:
	./tools/fetch_cmsis.sh

$(BUILD)/%.o: %.c inc/app_config.h core/sixstep.h | $(CMSIS)/include/stm32g431xx.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -c $< -o $@

$(BUILD)/bldc310.elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(OBJCOPY) -O binary $@ $(BUILD)/bldc310.bin
	$(OBJCOPY) -O ihex $@ $(BUILD)/bldc310.hex
	$(SIZE) $@

# ---------------- host simulator ----------------
SIMCC ?= gcc
$(BUILD)/motor_sim: sim/motor_sim.c core/sixstep.c core/sixstep.h inc/app_config.h
	@mkdir -p $(BUILD)
	$(SIMCC) -O2 -Wall -Wextra -Iinc -Icore sim/motor_sim.c core/sixstep.c -lm -o $@

test: $(BUILD)/motor_sim test-comm test-foc
	@fail=0; for s in default heavy reverse inertia lowbus; do \
	  ./$(BUILD)/motor_sim $$s || fail=1; done; exit $$fail

$(BUILD)/test_modbus: comm/test_modbus.c comm/modbus_slave.c comm/cooler_regs.c comm/modbus_slave.h comm/cooler_regs.h
	@mkdir -p $(BUILD)
	$(SIMCC) -O2 -Wall -Wextra -Wconversion -Icomm comm/test_modbus.c comm/modbus_slave.c comm/cooler_regs.c -o $@

test-comm: $(BUILD)/test_modbus
	./$(BUILD)/test_modbus

# ---------------- sensorless FOC (cooler motors) ----------------
$(BUILD)/foc_sim: foc/foc_sim.c foc/foc.c foc/foc.h
	@mkdir -p $(BUILD)
	$(SIMCC) -O2 -Wall -Wextra -Ifoc foc/foc_sim.c foc/foc.c -lm -o $@

test-foc: $(BUILD)/foc_sim
	./$(BUILD)/foc_sim

test-foc-random: $(BUILD)/foc_sim
	./$(BUILD)/foc_sim random 40 777

plots-foc: $(BUILD)/foc_sim
	./$(BUILD)/foc_sim csv
	python3 foc/plot_foc.py $(BUILD)/foc_id_run_A.csv $(BUILD)/foc_locked.csv ../docs/img

sweep: $(BUILD)/motor_sim
	python3 sim/sweep.py

plots: $(BUILD)/motor_sim
	./$(BUILD)/motor_sim default $(BUILD)/sim
	python3 sim/plot.py $(BUILD)/sim_default.csv $(BUILD)/sim_default_hires.csv ../docs/img

flash: $(BUILD)/bldc310.elf
	st-flash --reset write $(BUILD)/bldc310.bin 0x08000000

clean:
	rm -rf $(BUILD)

.PHONY: all cmsis test test-comm test-foc test-foc-random plots-foc sweep plots flash clean

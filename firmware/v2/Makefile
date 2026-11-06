# Cooler drive v2 firmware (board hardware/v2)
#   make test       host tests of the application layer (gcc)
#   make            ARM build: build/drive_v2.elf (application) + build/boot_v2.elf
#   make pack       build/drive_v2_update.bin with size/CRC filled (for the panel)
#   make phy        STM32C011 panel-bus PHY bridge (build/phy.elf)
CMSIS   ?= ../cmsis
PREFIX  ?= arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size
B        = build
COMMON   = -O2 -g3 -std=c11 -Wall -Wextra -Wshadow -Wdouble-promotion -fsingle-precision-constant \
           -ffunction-sections -fdata-sections
M4       = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
INC      = -Iapp -I../comm -I../foc -Ibsp -I$(CMSIS)/include
APP_SRC  = main_v2.c app/drive.c app/mains.c app/params.c app/fwu.c app/crc32.c app/motorlib.c \
           bsp/bsp_v2.c ../foc/foc.c ../comm/modbus_slave.c ../comm/cooler_regs.c $(CMSIS)/src/system_stm32g4xx.c
BOOT_SRC = boot/boot_main.c app/fwu.c app/crc32.c $(CMSIS)/src/system_stm32g4xx.c
APP_OBJ  = $(APP_SRC:%.c=$(B)/app/%.o) $(B)/app/startup.o
BOOT_OBJ = $(BOOT_SRC:%.c=$(B)/boot/%.o) $(B)/boot/startup.o
LDCOMMON = $(M4) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -lm

all: $(B)/drive_v2.elf $(B)/boot_v2.elf $(B)/phy.elf

$(B)/app/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(M4) $(COMMON) -DSTM32G431xx $(INC) -c $< -o $@
$(B)/boot/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(M4) $(COMMON) -Os -DSTM32G431xx -DBOOTLOADER $(INC) -c $< -o $@
$(B)/app/startup.o $(B)/boot/startup.o: $(CMSIS)/src/startup_stm32g431xx.s
	@mkdir -p $(dir $@)
	$(CC) $(M4) -c $< -o $@

$(B)/drive_v2.elf: $(APP_OBJ) linker/app.ld
	$(CC) $(APP_OBJ) $(LDCOMMON) -Tlinker/app.ld -Wl,-Map=$(B)/drive_v2.map -o $@
	$(OBJCOPY) -O binary $@ $(B)/drive_v2.bin
	$(SIZE) $@
$(B)/boot_v2.elf: $(BOOT_OBJ) linker/boot.ld
	$(CC) $(BOOT_OBJ) $(LDCOMMON) -Tlinker/boot.ld -Wl,-Map=$(B)/boot_v2.map -o $@
	$(OBJCOPY) -O binary $@ $(B)/boot_v2.bin
	$(SIZE) $@

# ---------------- PHY (STM32C011, Cortex-M0+) ----------------
M0       = -mcpu=cortex-m0plus -mthumb
PHY_SRC  = phy/phy_main.c phy/phy_core.c $(CMSIS)/src/system_stm32c0xx.c
PHY_OBJ  = $(PHY_SRC:%.c=$(B)/phy/%.o) $(B)/phy/startup.o
$(B)/phy/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(M0) $(COMMON) -Os -DSTM32C011xx -Iphy -I$(CMSIS)/include -c $< -o $@
$(B)/phy/startup.o: $(CMSIS)/src/startup_stm32c011xx.s
	@mkdir -p $(dir $@)
	$(CC) $(M0) -c $< -o $@
$(B)/phy.elf: $(PHY_OBJ) linker/phy.ld
	$(CC) $(PHY_OBJ) $(M0) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Tlinker/phy.ld -Wl,-Map=$(B)/phy.map -o $@
	$(OBJCOPY) -O binary $@ $(B)/phy.bin
	$(SIZE) $@
phy: $(B)/phy.elf

pack: $(B)/drive_v2.elf
	python3 tools/fw_pack.py $(B)/drive_v2.bin $(B)/drive_v2_update.bin

# ---------------- host tests ----------------
HOSTCC ?= gcc
HFLAGS  = -O1 -g -std=c11 -Wall -Wextra -Wshadow -D_DEFAULT_SOURCE -Iapp -I../comm
$(B)/test_drive: test/test_drive.c app/*.c app/*.h ../comm/*.c ../comm/*.h
	@mkdir -p $(B)
	$(HOSTCC) $(HFLAGS) test/test_drive.c app/drive.c app/mains.c app/params.c app/fwu.c app/crc32.c \
	  app/motorlib.c ../comm/modbus_slave.c ../comm/cooler_regs.c -lm -o $@
$(B)/test_phy: test/test_phy.c phy/phy_core.c phy/phy_core.h ../comm/*.c ../comm/*.h
	@mkdir -p $(B)
	$(HOSTCC) $(HFLAGS) -Iphy test/test_phy.c phy/phy_core.c ../comm/modbus_slave.c ../comm/cooler_regs.c -lm -o $@
test: $(B)/test_drive $(B)/test_phy
	./$(B)/test_drive
	./$(B)/test_phy

clean:
	rm -rf $(B)
.PHONY: all pack test clean phy

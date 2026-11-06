# Cooler panel firmware
#   make test   portable core against the simulated drive (real drive-side Modbus + update code)
#   ESP32 port: see esp32/README.md (ESP-IDF 5.x, idf.py build)
B      = build
HOSTCC ?= gcc
CFLAGS = -O1 -g -std=c11 -Wall -Wextra -Wshadow -Icore -I../comm -I../v2/app
CORE   = core/panel.c core/mb_master.c core/drive_update.c core/json_api.c core/seg7.c
DRIVE  = ../comm/modbus_slave.c ../comm/cooler_regs.c ../v2/app/fwu.c ../v2/app/crc32.c
$(B)/test_panel: test/test_panel.c $(CORE) core/*.h $(DRIVE)
	@mkdir -p $(B)
	$(HOSTCC) $(CFLAGS) test/test_panel.c $(CORE) $(DRIVE) -o $@
test: $(B)/test_panel
	./$(B)/test_panel
clean:
	rm -rf $(B)
.PHONY: test clean

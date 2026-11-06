#!/bin/sh
# Fetch the CMSIS core + STM32G4 and STM32C0 device headers/startup (Apache-2.0) into firmware/cmsis.
set -e
cd "$(dirname "$0")/.."
DST=cmsis
TMP=$(mktemp -d)
git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis_device_g4.git "$TMP/dev"
git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis-device-c0.git "$TMP/c0"   # v2 PHY MCU
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/ARM-software/CMSIS_5.git "$TMP/core"
(cd "$TMP/core" && git sparse-checkout set CMSIS/Core/Include)
mkdir -p $DST/include $DST/src
cp "$TMP"/core/CMSIS/Core/Include/*.h $DST/include/
cp "$TMP"/dev/Include/stm32g4xx.h "$TMP"/dev/Include/stm32g431xx.h "$TMP"/dev/Include/system_stm32g4xx.h $DST/include/
cp "$TMP"/dev/Source/Templates/gcc/startup_stm32g431xx.s $DST/src/
cp "$TMP"/dev/Source/Templates/system_stm32g4xx.c $DST/src/
cp "$TMP"/c0/Include/stm32c0xx.h "$TMP"/c0/Include/stm32c011xx.h "$TMP"/c0/Include/system_stm32c0xx.h $DST/include/
cp "$TMP"/c0/Source/Templates/gcc/startup_stm32c011xx.s "$TMP"/c0/Source/Templates/system_stm32c0xx.c $DST/src/
rm -rf "$TMP"
echo "CMSIS ready in firmware/$DST"

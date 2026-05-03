#!/usr/bin/env bash
# Enter the STM32F411 ROM DFU bootloader without pressing BOOT0/NRST.
#
# Requires the ST-Link SWD connection. After this succeeds, USB should enumerate
# as 0483:df11 and `pio run -e dfu --target upload` can flash over DFU.
set -euo pipefail

OPENOCD="${HOME}/.platformio/packages/tool-openocd/bin/openocd"
SCRIPTS="${HOME}/.platformio/packages/tool-openocd/openocd/scripts"

if [[ ! -x "${OPENOCD}" ]]; then
  echo "OpenOCD not found at ${OPENOCD}" >&2
  exit 1
fi

"${OPENOCD}" \
  -s "${SCRIPTS}" \
  -f interface/stlink.cfg \
  -f target/stm32f4x.cfg \
  -c "adapter speed 480" \
  -c "init" \
  -c "reset halt" \
  -c "mww 0xE000ED08 0x1FFF0000" \
  -c "reg msp 0x20002E58" \
  -c "reg pc 0x1FFF4253" \
  -c "resume" \
  -c "shutdown"

echo "Requested STM32 ROM DFU bootloader. Check with: lsusb | grep -i df11"

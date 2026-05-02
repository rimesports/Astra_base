#!/usr/bin/env bash
# flash.sh - flash STM32F411 Black Pill via ST-Link (Linux equivalent of stlink_flash.bat)
# Usage: flash.sh <firmware.elf>
# Requires NRST wired: ST-Link RST -> Black Pill left header pin 16
set -euo pipefail

OPENOCD="$HOME/.platformio/packages/tool-openocd/bin/openocd"
SCRIPTS="$HOME/.platformio/packages/tool-openocd/openocd/scripts"
CFG="$(dirname "$(realpath "$0")")/../blackpill_stlink_upload.cfg"

"$OPENOCD" -s "$SCRIPTS" -c "set FIRMWARE {$1}" -f "$CFG"

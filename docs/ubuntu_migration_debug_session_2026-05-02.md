# Ubuntu Migration Debug Session - 2026-05-02

## Scope

This session validated the Astra Base STM32 development flow on Ubuntu after the
hardware wiring and ST-Link flashing path were confirmed good. The focus was the
Ubuntu host environment:

- USB CDC enumeration and serial access
- ST-Link visibility and reset-under-control flashing
- ROM DFU bootloader recovery and flashing
- repeatable command-line workflows for future bring-up

## Environment

- Host: Ubuntu 22.04-class system, kernel `6.8.0-110-generic`
- User: `geng`
- Project: `~/Astra_base`
- Board: WeAct Black Pill STM32F411
- ST-Link: `0483:3748`, firmware `V2J39S7`
- App USB CDC: `0483:5740`, product `Astra Base Controller`
- ROM DFU: `0483:df11`, product `STM32 BOOTLOADER`

## CDC Findings

Initial CDC checks showed no `/dev/ttyACM*`, but later USB inspection confirmed
the firmware enumerated correctly:

```bash
lsusb | grep -i '0483'
ls -l /dev/ttyACM0 /dev/serial/by-id/*
pio device list
```

Validated state:

- `/dev/ttyACM0` appears for the firmware CDC interface
- stable by-id link appears as
  `usb-Astra_Robotics_Astra_Base_Controller_3776368F3235-if00`
- device permissions are usable by the local user
- `pio device list` reports `Astra Base Controller`
- CDC echo smoke test responds to `{"T":143}`

Smoke test:

```bash
python3 - <<'PY'
import serial, time
with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as ser:
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(b'{"T":143}\n')
    ser.flush()
    print(ser.readline().decode(errors='replace').strip())
PY
```

Expected:

```json
{"T":143}
```

## CDC Host Fixes

The user was already in `dialout` and `plugdev`, so group membership was not the
blocker. The main host-side improvement was preventing ModemManager from probing
the STM32 CDC ACM device.

Updated `tools/49-stlink.rules` to include:

```udev
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", ENV{ID_MM_DEVICE_IGNORE}="1", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", ENV{ID_MM_DEVICE_IGNORE}="1", GROUP="dialout", MODE="0666"
```

Installed and reloaded:

```bash
sudo cp tools/49-stlink.rules /etc/udev/rules.d/49-stlink.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Validation:

```bash
udevadm info -q property -n /dev/ttyACM0 | grep ID_MM_DEVICE_IGNORE
```

Expected:

```text
ID_MM_DEVICE_IGNORE=1
```

## PlatformIO PATH Finding

PlatformIO was installed under:

```text
~/.platformio/penv/bin/pio
```

The current shell initially could not find `pio`. The immediate fix is:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

The persistent fix was adding the PlatformIO virtualenv path to shell startup.

## ST-Link Findings

ST-Link and target probing were healthy:

```bash
st-info --probe
STM32_Programmer_CLI --list
```

Validated target:

- probe: ST-Link V2, `V2J39S7`
- target voltage: about `3.23 V`
- MCU: STM32F411, device id `0x0431`
- flash: 512 KiB
- SRAM: 128 KiB

The custom OpenOCD upload path worked, but Ubuntu/libusb was more reliable with
the adapter speed reduced to 480 kHz in `blackpill_stlink_upload.cfg`.

## ST-Link Flash Validation

Build:

```bash
pio run
```

Flash:

```bash
pio run --target upload
```

Result:

- build succeeded
- ST-Link upload succeeded
- app returned as CDC after reset/re-enumeration
- post-flash echo `{"T":143}` succeeded

One observed behavior: after some ST-Link uploads the app did not immediately
re-enumerate until an explicit reset was issued. This appears to be upload/reset
sequencing rather than a CDC host problem.

## ROM DFU Bootloader Validation

Physical ROM bootloader mode was validated:

```text
Hold BOOT0
Tap/release NRST
Release BOOT0
```

Expected USB:

```bash
lsusb | grep -i df11
dfu-util -l
```

Expected interface:

```text
0483:df11 STMicroelectronics STM Device in DFU Mode
@Internal Flash /0x08000000/04*016Kg,01*064Kg,03*128Kg
```

DFU flash:

```bash
pio run -e dfu --target upload
```

Result:

- erase completed
- download completed
- leave request submitted
- app returned as `/dev/ttyACM0`
- CDC echo test passed

## ST-Link-Assisted DFU Entry

To avoid the BOOT0/NRST button sequence while ST-Link is connected, a helper was
added:

```bash
tools/enter_dfu_via_stlink.sh
```

It uses OpenOCD to halt the target, set `VTOR` to STM32 system memory, load the
ROM bootloader MSP/PC values, and resume execution in the ROM bootloader.

Usage:

```bash
cd ~/Astra_base
tools/enter_dfu_via_stlink.sh
lsusb | grep -i df11
pio run -e dfu --target upload
```

Validated result:

- board entered ROM DFU as `0483:df11`
- `dfu-util -l` listed internal flash
- `pio run -e dfu --target upload` flashed successfully
- app returned as `/dev/ttyACM0`
- CDC echo passed

## Firmware CDC Bootloader Command Experiment

A pure CDC command was investigated: send a JSON command to reboot the firmware
into the STM32 ROM bootloader without using ST-Link.

Findings:

- a runtime branch to system memory can reach the ROM bootloader
- the independent watchdog and FreeRTOS shutdown state make this path fragile
- one attempted handoff reached ROM code but did not reliably enumerate USB DFU
- another reset-flag handoff returned to the app instead of staying in DFU

Resolution:

- the risky firmware command path was not kept
- the validated ST-Link-assisted DFU entry script was kept instead
- future work can revisit a CDC bootloader command by first designing a clean
  watchdog strategy or adding an application bootloader

## Protocol Cleanup

The command protocol documentation was clarified away from “Waveshare-compatible”
wording toward the actual newline-terminated JSON T-code protocol.

Firmware now defines and handles:

```json
{"T":0}
```

as an immediate stop command. The ROS 2 Jetson bridge can use:

- `T:13` for `/cmd_vel`
- `T:0` for shutdown stop

## Final Validated Commands

Normal build:

```bash
pio run
```

ST-Link flash:

```bash
pio run --target upload
```

Enter ROM DFU without BOOT0/NRST, using ST-Link:

```bash
tools/enter_dfu_via_stlink.sh
```

Flash through ROM DFU:

```bash
pio run -e dfu --target upload
```

CDC smoke test:

```bash
python3 - <<'PY'
import serial, time
with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as ser:
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(b'{"T":143}\n')
    ser.flush()
    print(ser.readline().decode(errors='replace').strip())
PY
```

## Resolution Summary

- Ubuntu CDC path is working
- ST-Link path is working
- ROM DFU path is working
- ST-Link-assisted ROM DFU entry is working
- udev permissions and ModemManager behavior are handled
- PlatformIO CLI path issue is documented
- risky firmware CDC-to-DFU command was investigated and intentionally not kept

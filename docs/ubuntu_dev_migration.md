# Ubuntu Development Migration Checklist
## STM32 + Jetson unified workspace on Ubuntu

Target: single Ubuntu VS Code window covering both STM32 (PlatformIO) and Jetson projects,
with full autonomous agent operation — no sudo passwords during normal development.

---

## 1. Base packages + PlatformIO

```bash
# Install base packages
sudo apt update
sudo apt install python3-pip python3-venv python3-serial git curl dfu-util -y

# Install PlatformIO CLI
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
python3 /tmp/get-platformio.py

# Add pio to PATH (add to ~/.bashrc permanently)
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify
pio --version
```

---

## 2. udev Rules (one-time — enables passwordless USB access)

```bash
sudo tee /etc/udev/rules.d/49-stlink.rules << 'EOF'
# ST-Link V2/V2-1/V3 (include common IDs seen in practice)
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"
# STM32 DFU Bootloader (Black Pill BOOT0 mode)
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="df11", MODE="0666", GROUP="plugdev"
# USB CDC serial (Black Pill firmware — becomes /dev/ttyACM0)
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", ENV{ID_MM_DEVICE_IGNORE}="1", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", ENV{ID_MM_DEVICE_IGNORE}="1", GROUP="dialout", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger

# Add user to required groups (log out and back in after this)
sudo usermod -aG plugdev,dialout $USER
```

After re-login, `pio run -t upload`, `dfu-util`, and serial monitor all work without sudo.

The `ID_MM_DEVICE_IGNORE=1` flag keeps ModemManager from probing the firmware
CDC interface as a modem candidate.

---

## 3. Clone the STM32 repo

```bash
mkdir -p ~/Robotics
cd ~/Robotics
git clone https://github.com/rimesports/Astra_base.git STM32
cd STM32

# Let PlatformIO download the toolchain on first build
pio run
```

---

## 4. Verify flash (ST-Link SWD)

Connect the Black Pill via ST-Link, then:

```bash
pio run -t upload
```

Expected: build succeeds, ST-Link flashes, board resets with `reset run`, and the serial port appears as `/dev/ttyACM0`.

If upload fails with `init mode failed` or `STLINK_JTAG_GET_IDCODE_ERROR`, verify
the reset line before chasing firmware:

```bash
st-flash --connect-under-reset --freq=100K read /tmp/astra_probe.bin 0x08000000 4
```

`NRST is not connected` means ST-Link RST is not reaching the Black Pill NRST pin.
The custom OpenOCD upload path depends on that wire so it can connect while the
USB firmware is active.

**Important:** the current repo `upload_command` in [platformio.ini](../platformio.ini) is Windows-specific:

```ini
upload_command = C:\Users\yegen\Robotics\STM32\stlink_flash.bat $SOURCE
```

For Ubuntu, use one of these:

1. Remove `upload_command` entirely and let PlatformIO use its default ST-Link/OpenOCD path.
2. Replace it with a Linux shell wrapper that preserves the validated post-flash
   `resume` behavior and avoids re-sampling BOOT0 after flashing.

Recommended Linux wrapper:

```bash
cat > tools/flash.sh << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

pio run --target upload "$@"

OPENOCD="$HOME/.platformio/packages/tool-openocd/bin/openocd"
if [[ -x "$OPENOCD" ]]; then
  "$OPENOCD" -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init" -c "reset run" -c "shutdown"
fi
EOF

chmod +x tools/flash.sh
```

Then in `platformio.ini` use:

```ini
upload_command = ${PROJECT_DIR}/tools/flash.sh $SOURCE
```

This matches the currently validated Windows workflow more closely than a plain default upload.

---

## 5. Verify DFU flash

Enter DFU mode: hold BOOT0 → unplug/replug USB → release BOOT0.

```bash
dfu-util -l          # should list Device ID 0483:df11
pio run -e dfu -t upload
```

If ST-Link is connected and you want to avoid the BOOT0/NRST button sequence,
use the helper script:

```bash
tools/enter_dfu_via_stlink.sh
lsusb | grep -i df11
pio run -e dfu --target upload
```

This jumps into the STM32 ROM bootloader via SWD, then flashes the same image
through USB DFU.

---

## 6. Verify serial monitor

```bash
# Find the port
ls /dev/ttyACM*

# Open serial console
python3 tools/serial_console.py --port /dev/ttyACM0

# Quick test
python3 tools/serial_console.py --port /dev/ttyACM0
```

Then send:

```text
echo
diag
```

Using the Python console is preferred over raw `echo > /dev/ttyACM0` because it handles line endings and reads replies directly.

---

## 7. VS Code multi-root workspace

Install VS Code and PlatformIO extension:

```bash
# VS Code
sudo snap install code --classic

# Or via apt:
wget -qO- https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > /tmp/packages.microsoft.gpg
sudo install -D /tmp/packages.microsoft.gpg /etc/apt/keyrings/packages.microsoft.gpg
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/packages.microsoft.gpg] https://packages.microsoft.com/repos/code stable main" \
  | sudo tee /etc/apt/sources.list.d/vscode.list
sudo apt update && sudo apt install code -y
```

Install extensions:
```
code --install-extension platformio.platformio-ide
code --install-extension ms-python.python
```

Create the unified workspace file (adjust Jetson path):

```bash
cat > ~/Robotics/robotics.code-workspace << 'EOF'
{
  "folders": [
    { "name": "STM32", "path": "./STM32" },
    { "name": "Jetson", "path": "/path/to/jetson/project" }
  ],
  "settings": {
    "terminal.integrated.defaultProfile.linux": "bash"
  }
}
EOF

code ~/Robotics/robotics.code-workspace
```

---

## 8. Update serial port references

On Linux the Black Pill CDC port is `/dev/ttyACM0` (not `COM6`).

Files to update after migration:
- [platformio.ini](../platformio.ini): `monitor_port = /dev/ttyACM0`

`tools/serial_console.py` already accepts `--port`, so it does not require a code change if you pass the device path explicitly.

Confirm the port name first — if another ACM device is present it may be `ttyACM1`:
```bash
ls /dev/ttyACM*
# or watch what appears when you plug in:
dmesg | tail -5
```

---

## 9. VS Code notes

Ubuntu + VS Code should make this repo easier to work with, but it will not by itself reduce firmware bugs. The main wins are:

- better USB / serial visibility with `dmesg`, `lsusb`, and `/dev/ttyACM*`
- easier scripting around `openocd`, `dfu-util`, and PlatformIO
- smoother automation and CI alignment

The firmware still needs the same validation for watchdog, CDC, shared state, and fault handling.

---

## 10. Sudo-free agent operation summary

After the udev rules and group membership above, these all run without password:

| Operation | Command |
|-----------|---------|
| Build | `pio run` |
| Flash via ST-Link | `pio run -t upload` |
| Flash via DFU | `pio run -e dfu -t upload` |
| Serial monitor | `python3 tools/serial_console.py` |
| Jetson sync | `jetson-sync` (already works) |

Only `apt install` and `systemctl` still need sudo — scope those in `/etc/sudoers.d/` if needed for full automation.

---

## 11. Port assignment stability (optional but recommended)

If `/dev/ttyACM0` shifts when other USB devices are present, pin it with a symlink rule:

```bash
sudo tee /etc/udev/rules.d/99-blackpill.rules << 'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", \
  SYMLINK+="ttyBlackPill", MODE="0666", GROUP="dialout"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then use `/dev/ttyBlackPill` everywhere instead of `ttyACM0`.

---

## 12. Recommended migration outcome

If the move is done, the repo should end up with:

- Linux-native `upload_command` or no custom `upload_command`
- `monitor_port = /dev/ttyBlackPill` or `/dev/ttyACM0`
- verified `pio run`, `pio run -t upload`, `pio run -e dfu -t upload`
- verified `python3 tools/serial_console.py --port /dev/ttyBlackPill`
- no routine dependence on `sudo` during normal development

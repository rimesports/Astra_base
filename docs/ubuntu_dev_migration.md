# Ubuntu Development Migration Checklist
## STM32 + Jetson unified workspace on Ubuntu

Target: single Ubuntu VS Code window covering both STM32 (PlatformIO) and Jetson projects,
with full autonomous agent operation — no sudo passwords during normal development.

---

## 1. PlatformIO

```bash
# Install Python pip if needed
sudo apt install python3-pip python3-venv -y

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
# ST-Link V2/V3
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"
# STM32 DFU Bootloader (Black Pill BOOT0 mode)
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="df11", MODE="0666", GROUP="plugdev"
# USB CDC serial (Black Pill firmware — becomes /dev/ttyACM0)
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", GROUP="dialout", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger

# Add user to required groups (log out and back in after this)
sudo usermod -aG plugdev,dialout $USER
```

After re-login, `pio run -t upload`, `dfu-util`, and serial monitor all work without sudo.

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

Expected: build succeeds, ST-Link flashes, board resets. Serial port appears as `/dev/ttyACM0`.

**Note:** `upload_command` in [platformio.ini](../platformio.ini) currently points to `stlink_flash.bat` — update it for Linux:

```ini
; platformio.ini — replace the upload_command line with:
upload_command = openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "reset_config srst_only srst_nogate connect_assert_srst" \
  -c "program {$SOURCE} verify reset exit 0x08000000"
```

Or remove `upload_command` entirely to let PlatformIO use its default OpenOCD invocation.

---

## 5. Verify DFU flash (no ST-Link)

Enter DFU mode: hold BOOT0 → unplug/replug USB → release BOOT0.

```bash
dfu-util -l          # should list Device ID 0483:df11
pio run -e dfu -t upload
```

---

## 6. Verify serial monitor

```bash
# Find the port
ls /dev/ttyACM*

# Open serial console
python3 tools/serial_console.py --port /dev/ttyACM0

# Quick test (no hardware needed)
echo '{"T":143}' > /dev/ttyACM0    # echo command — board should reply
```

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
- [tools/serial_console.py](../tools/serial_console.py): `PORT = "/dev/ttyACM0"`

Confirm the port name first — if another ACM device is present it may be `ttyACM1`:
```bash
ls /dev/ttyACM*
# or watch what appears when you plug in:
dmesg | tail -5
```

---

## 9. Sudo-free agent operation summary

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

## 10. Port assignment stability (optional but recommended)

If `/dev/ttyACM0` shifts when other USB devices are present, pin it with a symlink rule:

```bash
sudo tee /etc/udev/rules.d/99-blackpill.rules << 'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", \
  SYMLINK+="ttyBlackPill", MODE="0666", GROUP="dialout"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then use `/dev/ttyBlackPill` everywhere instead of `ttyACM0`.

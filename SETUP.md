# Astra Base — Engineer Setup Guide

Complete setup from a fresh Windows or Ubuntu machine to building, flashing, and communicating with the STM32 controller.

---

## Hardware Required

| Item | Part | Notes |
|------|------|-------|
| STM32 controller | WeAct STM32F411CEU6 Black Pill | UFQFPN48, 100 MHz, 512 KB Flash, 128 KB SRAM |
| Programmer | ST-Link V2 (external dongle) | Required — Black Pill has no onboard debugger |
| IMU | MPU-6050 GY-521 breakout | I2C address 0x68 (AD0 = GND) |
| Current sensor | INA219 breakout | I2C address 0x40, 0.1 Ω shunt |
| Motor driver | Cytron MDD10A | Dual channel, 10A, 7–30V |
| Motors | DC motors with quadrature encoders | 28 PPR × 19.2 gear ratio |
| Battery | 2S LiPo (7.4V nominal) | Powers motor driver only |
| USB cable | USB-C | Black Pill → PC / Jetson |
| SWD cable | 5-wire dupont | ST-Link → Black Pill SWD pads |

---

## 1. Install Development Tools

### Windows

**1a. VS Code**
Download and install from https://code.visualstudio.com

**1b. PlatformIO IDE extension**
In VS Code → Extensions → search `PlatformIO IDE` → Install
This installs the full PlatformIO toolchain including ARM GCC, OpenOCD, and ST-Link support automatically.

**1c. Git**
Download from https://git-scm.com/download/win — use all defaults during install.

Verify after install:
```bash
git --version
# expected: git version 2.x.x
```

**1d. GitHub CLI (`gh`)**

GitHub CLI lets you create repos, open PRs, and authenticate — all from the terminal.

```bash
winget install --id GitHub.cli --silent --accept-source-agreements --accept-package-agreements
```

Verify:
```bash
gh --version
# expected: gh version 2.x.x
```

**1e. Python 3 (for post-build scripts and fw_update.py)**
Download from https://www.python.org/downloads — check "Add to PATH" during install.

**1f. Install Python dependencies**
```bash
pip install pyserial crcmod
```

### Ubuntu / Jetson

```bash
# Git + Python
sudo apt update && sudo apt install -y git python3 python3-pip

# GitHub CLI
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] \
  https://cli.github.com/packages stable main" \
  | sudo tee /etc/apt/sources.list.d/github-cli.list
sudo apt update && sudo apt install -y gh

# PlatformIO
pip3 install platformio

# USB rules for ST-Link (so you don't need sudo to flash)
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger

# Add your user to the dialout group (for serial port access)
sudo usermod -aG dialout $USER
# Log out and back in for this to take effect

# Python dependencies
pip3 install pyserial crcmod
```

---

## 2. GitHub Account and Authentication

### 2a. Create a GitHub account
If you don't have one: https://github.com/signup
Use your work email. Ask the repo owner (rimesports) to add you as a collaborator:
**Settings → Collaborators → Add people** → enter your GitHub username.

### 2b. Authenticate GitHub CLI

```bash
gh auth login
```

Select:
- **GitHub.com**
- **HTTPS** (recommended) or SSH
- **Login with a web browser** → follow the one-time code prompt

Verify authentication:
```bash
gh auth status
# expected: Logged in to github.com account <your-username>
```

### 2c. Configure Git identity

```bash
git config --global user.name "Your Name"
git config --global user.email "your@email.com"
```

Verify:
```bash
git config --global --list
```

### 2d. (Optional) SSH key setup — faster, no password prompts

```bash
# Generate key (accept defaults, add a passphrase)
ssh-keygen -t ed25519 -C "your@email.com"

# Add to ssh-agent
# Windows:
Get-Service ssh-agent | Set-Service -StartupType Automatic
Start-Service ssh-agent
ssh-add "$HOME\.ssh\id_ed25519"

# Linux/macOS:
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# Copy public key to GitHub
gh ssh-key add ~/.ssh/id_ed25519.pub --title "My Dev Machine"
```

Then re-authenticate with SSH:
```bash
gh auth login --git-protocol ssh
```

**References:**
- GitHub account signup: https://github.com/signup
- GitHub CLI docs: https://cli.github.com/manual
- SSH key guide: https://docs.github.com/en/authentication/connecting-to-github-with-ssh
- Git setup guide: https://docs.github.com/en/get-started/getting-started-with-git/setting-your-username-in-git

---

## 3. Clone the Repository

```bash
git clone --recurse-submodules https://github.com/rimesports/Astra_base.git
cd Astra_base
```

The `--recurse-submodules` flag is required — FreeRTOS kernel is a git submodule in `lib/FreeRTOS/Source`.

If you already cloned without it:
```bash
git submodule update --init --recursive
```

---

## 4. Open in VS Code with PlatformIO

```bash
code .
```

PlatformIO will automatically detect `platformio.ini` and download any missing platform packages on first open (ARM GCC toolchain, STM32Cube framework, OpenOCD). This takes 2–5 minutes on first run.

---

## 5. Wiring — ST-Link to Black Pill

The Black Pill has no onboard debugger. Connect an external ST-Link V2 to the 4-pad SWD header on the Black Pill board edge:

| ST-Link V2 Pin | Signal | Black Pill Pad |
|---|---|---|
| Pin 1 (SWDIO) | SWDIO | SWDIO |
| Pin 5 (SWCLK) | SWCLK | SWCLK |
| Pin 3 (GND) | GND | GND |
| Pin 9 (3.3V) | VDD ref | 3V3 |
| Pin 15 (NRST) | NRST | NRST (optional) |

**Power:** The Black Pill can be powered from the ST-Link 3.3V during programming, or independently via USB-C. Do not power it from both simultaneously at different voltages.

---

## 6. Peripheral Wiring

### I2C Bus (shared — MPU-6050 and INA219)

| Signal | Black Pill Pin | Notes |
|--------|---------------|-------|
| SCL | PB8 | Shared by both devices |
| SDA | PB9 | Shared by both devices |
| Pull-ups | 4.7 kΩ to 3.3V | INA219 board usually has these onboard |

### MPU-6050 IMU

| GY-521 Pin | Black Pill |
|-----------|-----------|
| VCC | 3.3V |
| GND | GND |
| SCL | PB8 |
| SDA | PB9 |
| AD0 | GND (sets I2C address = 0x68) |

### INA219 Current Sensor

| INA219 Pin | Connection |
|-----------|------------|
| VCC | 3.3V |
| GND | GND |
| SCL | PB8 |
| SDA | PB9 |
| VIN+ | Battery positive |
| VIN- | MDD10A B+ (motor driver power input) |

### MDD10A Motor Driver — Control Connector

| MDD10A Pin | Signal | Black Pill Pin |
|-----------|--------|---------------|
| Pin 1 | GND | GND (shared with battery negative) |
| Pin 2 | PWM2 | PA1 (TIM2_CH2) |
| Pin 3 | DIR2 | PB1 |
| Pin 4 | PWM1 | PA0 (TIM2_CH1) |
| Pin 5 | DIR1 | PB0 |

**Critical:** Connect MDD10A battery negative (B−) to Black Pill GND. Without this shared ground, STM32 signals have no return path and motors will not respond.

### Encoders

| Signal | Left Motor | Right Motor |
|--------|-----------|------------|
| Channel A | PB3 | PB6 |
| Channel B | PB4 | PB5 |
| VCC | 3.3V | 3.3V |
| GND | GND | GND |

### Jetson Communication

Connect Black Pill USB-C → Jetson USB port.
The STM32 enumerates as a USB CDC device (`/dev/ttyACM0` on Linux).

---

## 7. Build the Firmware

### VS Code / PlatformIO GUI
Click the **checkmark (Build)** button in the PlatformIO toolbar at the bottom.

### Command Line
```bash
# Windows
%USERPROFILE%\.platformio\penv\Scripts\pio run

# Linux / macOS
~/.platformio/penv/bin/pio run
```

Expected output:
```
RAM:   [==  ]  17.2% (used 22540 bytes from 131072 bytes)
Flash: [    ]   7.7% (used 39624 bytes from 524288 bytes)
[SUCCESS]
```

---

## 8. Flash the Firmware

Connect ST-Link V2 to PC via USB, ensure it enumerates (LED should be solid or blinking).

### VS Code / PlatformIO GUI
Click the **arrow (Upload)** button in the PlatformIO toolbar.

### Command Line
```bash
# Windows
%USERPROFILE%\.platformio\penv\Scripts\pio run --target upload

# Linux / macOS
~/.platformio/penv/bin/pio run --target upload
```

Expected output:
```
** Programming Started **
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

---

## 9. Open Serial Monitor

```bash
# Windows
%USERPROFILE%\.platformio\penv\Scripts\pio device monitor

# Linux / macOS
~/.platformio/penv/bin/pio device monitor
```

Or use PuTTY / screen / minicom at **115200 baud** on the relevant port (`COM5` Windows, `/dev/ttyACM0` Linux).

---

## 10. Verify the Board with T-codes

Send these JSON commands in the serial monitor to confirm each subsystem:

```json
{"T":143}
```
→ Echoes the line back. Confirms UART/USB is working.

```json
{"T":200}
```
→ System diagnostic. Check `i2c_imu`, `i2c_ina`, `tim2`, `temp` fields.

```json
{"T":127}
```
→ I2C scan. Expect `mpu6050_68.ack: true` and `ina219_40.ack: true`.

```json
{"T":126}
```
→ IMU snapshot. Expect valid roll/pitch/temp values.

```json
{"T":1,"L":0.2,"R":0.2}
```
→ Drive both motors forward at 20% speed. Motors should spin.

```json
{"T":1,"L":0,"R":0}
```
→ Stop.

```json
{"T":130,"cmd":1}
```
→ Enable continuous T:1001 telemetry (RPM, IMU, battery every 200 ms).

---

## 11. Firmware Update from Jetson (OTA)

Once the bootloader is implemented, firmware updates are sent from the Jetson without ST-Link:

```bash
python3 scripts/fw_update.py /dev/ttyACM0 .pio/build/blackpill_f411ce/firmware.bin
```

See [docs/fw_update_design.md](docs/fw_update_design.md) for the full protocol and rollback design.

---

## Project Structure

```
Astra_base/
├── src/                    # Application firmware (C/C++)
│   ├── main.cpp            # FreeRTOS task setup, peripheral init
│   ├── astra_config.h      # All pin definitions, T-codes, task priorities
│   ├── json_cmd.cpp/.h     # JSON T-code command dispatcher
│   ├── motor_ctrl.cpp/.h   # PWM motor output
│   ├── encoder.cpp/.h      # Quadrature encoder (EXTI-based)
│   ├── imu.cpp/.h          # MPU-6050 driver + complementary filter
│   ├── ina219.cpp/.h       # INA219 current/voltage sensor
│   ├── i2c_bus.cpp/.h      # Shared I2C1 bus driver
│   ├── serial_cmd.cpp/.h   # UART/USB line reader
│   ├── shared_state.cpp/.h # g_state — inter-task shared data
│   ├── pid.cpp/.h          # PID controller (for future speed control)
│   └── FreeRTOSConfig.h    # FreeRTOS tuning (heap, priorities, tick)
├── lib/
│   └── FreeRTOS/Source/    # FreeRTOS kernel (git submodule)
├── docs/
│   ├── bringup_left_motor.md       # Motor + encoder bring-up log
│   ├── bringup_imu_ina219.md       # IMU + current sensor bring-up log
│   ├── chipset_migration_l476_to_f411.md  # Board change decision log
│   └── fw_update_design.md         # OTA firmware update architecture
├── platformio.ini          # Build configuration (board, framework, flags)
├── freertos_flags.py       # PlatformIO extra_script — FPU link flags
├── SETUP.md                # This file — engineer onboarding
├── BRINGUP.md              # Hardware bring-up procedure
├── WIRING.md               # Wiring reference
└── CONTEXT.md              # Project context and architecture notes
```

---

## 12. Git Workflow for Contributing

### Day-to-day commands

```bash
# Check what you've changed
git status
git diff

# Stage and commit your work
git add src/motor_ctrl.cpp src/motor_ctrl.h
git commit -m "Fix PWM scaling for right motor channel"

# Pull latest changes from team before pushing
git pull --rebase origin main

# Push your commits
git push origin main
```

### Branch workflow (recommended for features/fixes)

```bash
# Create a branch for your work
git checkout -b feature/pid-speed-control

# ... make changes, commit ...

# Push branch and open a PR
git push origin feature/pid-speed-control
gh pr create --title "Add PID speed control loop" --body "Closes #12"

# After PR is merged, clean up
git checkout main
git pull
git branch -d feature/pid-speed-control
```

### Keeping the FreeRTOS submodule up to date

```bash
# Update submodule to latest pinned commit
git submodule update --remote lib/FreeRTOS/Source

# Commit the submodule pointer update
git add lib/FreeRTOS/Source
git commit -m "Update FreeRTOS submodule to latest"
```

### Useful `gh` commands

```bash
gh repo view --web                  # Open repo in browser
gh pr list                          # List open pull requests
gh pr checkout 12                   # Check out PR #12 locally
gh issue list                       # List open issues
gh issue create                     # Create a new issue
gh release list                     # List releases
```

---

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| `init mode failed (unable to connect to target)` | ST-Link can't reach MCU | Check USB connection, target voltage should be ~3.3V in OpenOCD output |
| `Target voltage: 1.7V` | 3.3V rail pulled down | Disconnect all peripherals and test bare board |
| Motors don't spin | Missing shared GND | Connect MDD10A B− to Black Pill GND |
| Right motor spins uncontrolled | PWM2/DIR2 not wired | Wire PA1→MDD10A pin 2, PB1→MDD10A pin 3 |
| `i2c_imu: false` in T:200 | IMU not on bus | Check SDA/SCL wires, AD0=GND, VCC=3.3V |
| `batt_ok: false` in T:200 | INA219 VIN− not on motor rail | Wire INA219 VIN− to MDD10A B+ |
| Build fails: FPU ABI mismatch | Linker flags missing | Ensure `freertos_flags.py` is in root and listed in `platformio.ini` `extra_scripts` |
| Submodule empty after clone | Missing `--recurse-submodules` | Run `git submodule update --init --recursive` |

---

## T-Code Reference

| T-code | Command | Description |
|--------|---------|-------------|
| T:1 | `{"T":1,"L":-0.5..0.5,"R":-0.5..0.5}` | Normalized speed control |
| T:11 | `{"T":11,"L":-255..255,"R":-255..255}` | Direct PWM control |
| T:13 | `{"T":13,"linear":m/s,"angular":rad/s}` | ROS differential drive |
| T:126 | `{"T":126}` | IMU snapshot → T:1002 response |
| T:127 | `{"T":127}` | I2C device scan (MPU-6050 + INA219) |
| T:128 | `{"T":128}` | Full I2C bus sweep 0x01–0x7F |
| T:130 | `{"T":130,"cmd":1}` | Enable/disable continuous telemetry |
| T:131 | `{"T":131,"cmd":1,"interval":100}` | Set telemetry interval (ms) |
| T:143 | `{"T":143}` | Echo — connectivity check |
| T:150 | `{"T":150}` | Motor current sweep (INA219 at each PWM step) |
| T:160 | `{"T":160}` | IMU calibration (500 samples, robot must be flat) |
| T:200 | `{"T":200}` | System self-diagnostic |
| T:1001 | (periodic) | Chassis telemetry: RPM, IMU, battery |
| T:1002 | (response) | IMU snapshot: roll/pitch/yaw/accel/gyro |

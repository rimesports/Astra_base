# Astra Base — Robot Bring-Up Plan

## Philosophy

Bring-up is a layered process. Each phase is a **gate** — you must fully pass it before
adding more hardware or complexity. Skipping phases makes failures harder to diagnose
because you cannot isolate which layer broke. A phase failure is always cheaper to debug
early than after the full system is assembled.

```
Stage 1: STM32 standalone  (laptop USB power only, no Jetson, no motor battery)
  Phase 0 → Pre-power checklist
  Phase 1 → Flash + FreeRTOS boot
  Phase 2 → GPIO and PWM signal verification (no motors)
  Phase 3 → Motor driver commissioning (one motor at a time)
  Phase 4 → Encoder verification
  Phase 5 → I2C bus — BNO055 IMU
  Phase 6 → I2C bus — INA219 battery monitor
  Phase 7 → Full standalone integration test

Stage 2: Jetson integration
  Phase 8  → USB-VCP handshake
  Phase 9  → Full command / telemetry loop
  Phase 10 → Dual battery commissioning
  Phase 11 → First full drive test
```

---

## Tools Required

| Tool | Use | Required? |
|------|-----|-----------|
| Digital multimeter | Voltage, continuity, diode check | **Essential** |
| USB-C cable | Black Pill → PC / Jetson (data cable, not charge-only) | **Essential** |
| Serial terminal — PuTTY / Tera Term / `pio device monitor` | Send T-codes, read responses | **Essential** |
| Logic analyzer (Saleae, cheap clone) | Verify PWM freq/duty, I2C bus | Strongly recommended |
| Oscilloscope | PWM waveform, back-EMF check | Optional but useful |
| Bench power supply (0–12V, 3A) | Safer than LiPo for first motor tests | Recommended |
| Fully charged 2S LiPo (7.4V nom.) | Motor battery | Required for Phase 3+ |
| LiPo balance charger | Charge before each session | **Essential** |
| LiPo safe bag | Charge and store the LiPo inside | **Essential** |
| Non-conductive stand / box | Elevate robot with wheels free | Required for Phase 3–4 |

---

## Safety Rules — Read Before Powering Anything

1. **LiPo first aid**: never charge an unbalanced, swollen, or punctured LiPo.
   Store and charge inside a LiPo safe bag on a non-flammable surface.
2. **Never connect motor battery while motors are running** — back-EMF spikes during
   hot-plug can destroy the MDD10A H-bridge. Always stop motors first.
3. **Never discharge below 3.3V per cell (6.6V total)** — permanent cell damage.
   Land at 7.0V. Never below 6.6V.
4. **Wheels off ground for all motor tests until Phase 11**. An unintended motor
   command on a rolling robot is a safety hazard.
5. **Always have a stop command ready** at the terminal before sending a drive command.
   Copy `{"T":1,"L":0,"R":0}` to your clipboard before every test run.
6. **3.3V for encoder VCC only**. The STM32 I/O absolute maximum is 3.6V.
   5V on a GPIO pin destroys the pin permanently.
7. **Shared GND is mandatory**. STM32, MDD10A, and motor battery negative must all
   connect to the same GND node. Floating grounds cause phantom motor starts and
   corrupt UART communication.

---

## Stage 1 — STM32 Standalone

Power source: **laptop USB only** until Phase 3.

---

### Phase 0 — Pre-Power Checklist

Do every item before connecting USB for the first time.

#### 0-A — Firmware

- [ ] `~/.platformio/penv/Scripts/pio.exe run` completes with `[SUCCESS]`
  - RAM: ~17% of 131 072 bytes
  - Flash: ~8% of 524 288 bytes

#### 0-B — Board Inspection (Black Pill)

- [ ] No bent or bridged pins on headers
- [ ] Blue LED on Black Pill lights up when USB-C is plugged in
- [ ] No visible solder bridges, burned components, or corrosion
- [ ] Confirm the MCU is an STM32**F411**CEU6 (printed on chip top)

#### 0-C — Wiring Continuity (Before Connecting to MDD10A)

With the Black Pill **unpowered**, use the multimeter continuity beeper:

| Check | Expected |
|-------|----------|
| PA0 to GND | No continuity (open) |
| PA1 to GND | No continuity (open) |
| PB0 to GND | No continuity (open) |
| PB1 to GND | No continuity (open) |
| PB3 to GND | No continuity (open) |
| PB8 to PB9 | No continuity (open) — I2C lines should not be shorted |
| 3V3 to GND | No continuity — if beeps, power rail short, do NOT power up |
| 5V to GND | No continuity — if beeps, power rail short, do NOT power up |

#### 0-D — LiPo Pre-Check

- [ ] Measure LiPo voltage with multimeter — both cells balanced (±50 mV of each other)
- [ ] Total voltage between 7.4V and 8.4V
- [ ] No swelling, punctures, or damaged wires
- [ ] XT30/XT60 connector polarity matches MDD10A VIN connector (+ to red, − to black)

---

### Phase 1 — Flash + FreeRTOS Boot Verification

**Goal**: firmware flashes successfully, FreeRTOS scheduler starts, UART works.
**Power**: laptop USB only.

#### 1-A — Flash

```bash
~/.platformio/penv/Scripts/pio.exe run --target upload
```

Expected output (last few lines):
```
** Programming Started **
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

If it fails:
- `init mode failed`: NRST not wired to ST-Link, or USB ISR blocking SWD — see WIRING.md section 5
- `Error: libusb_open() failed with LIBUSB_ERROR_ACCESS`: on Linux, add udev rule:
  ```bash
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0483", MODE="0666"' | sudo tee /etc/udev/rules.d/49-stlink.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

#### 1-B — Serial Terminal Setup

Open the serial terminal **before** resetting the board:

| Setting | Value |
|---------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None (off) |
| Line ending sent | LF (`\n`) |
| Port | Windows: `COM6` shown in Device Manager → Ports → USB Serial Device |
| Port | Linux: `/dev/ttyACM0` |

Via PlatformIO monitor:
```bash
~/.platformio/penv/Scripts/pio.exe device monitor --baud 115200
```

#### 1-C — Boot Verification

Press the **NRST** button on the Black Pill (or unplug/replug USB-C).

Within 500 ms you should see T:1001 telemetry arriving every 200 ms:
```json
{"T":1001,"L":0.0,"R":0.0,"r":0.0,"p":0.0,"y":0.0,"temp":0.0,"v":0.00,"i":0.00}
```

Sensors read 0.0 — that is expected; they are not connected yet.

#### 1-D — Two-Way Communication Test

Send the echo command (type in terminal, press Enter):
```json
{"T":143,"cmd":"boot_ok"}
```
Expected immediate response:
```json
{"T":143,"cmd":"boot_ok"}
```

#### 1-E — Silence Telemetry

For the rest of bring-up, stop continuous telemetry to keep the terminal readable:
```json
{"T":130,"cmd":0}
```
No response is expected. Telemetry lines should stop.

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| No serial output after reset | Wrong COM port, wrong baud | Check Device Manager / `ls /dev/ttyACM*` |
| Serial garbage characters | Baud rate mismatch | Set exactly 115200, no auto-baud |
| Echo command gives no response | `\r\n` vs `\n` line ending | Set terminal to send LF only, not CRLF |
| Board resets in a loop (<1 sec) | Stack overflow or malloc failure — FreeRTOS hook fired | Attach VS Code debugger; check `pcTaskName` in `vApplicationStackOverflowHook` |
| Upload fails — "Target voltage: 0.0V" | USB cable is charge-only | Replace cable with a data cable |

**Phase 1 pass**: T:1001 appears, echo works, telemetry stops on command.

---

### Phase 2 — GPIO and PWM Signal Verification

**Goal**: confirm that STM32 DIR and PWM outputs reach the MDD10A header at correct
voltage and frequency. **No motor battery. No motors connected to MDD10A.**

#### 2-A — Wiring

Connect the 5-wire control cable from the Black Pill to the MDD10A header:

| MDD10A Header | STM32 Signal | Black Pill Pin |
|--------------|-------------|---------------|
| Pin 1 — GND | GND | GND |
| Pin 2 — PWM2 | PA1 (TIM2_CH2) | PA1 |
| Pin 3 — DIR2 | PB1 | PB1 |
| Pin 4 — PWM1 | PA0 (TIM2_CH1) | PA0 |
| Pin 5 — DIR1 | PB0 | PB0 |

Double-check pin 1 is GND (use multimeter continuity from MDD10A pin 1 to Nucleo GND
before plugging in).

#### 2-B — Idle State Measurement

With the robot stopped (telemetry off, no commands sent), measure at the MDD10A header
pins with multimeter, GND reference at Nucleo GND:

| Pin | Signal | Expected idle voltage |
|-----|--------|----------------------|
| MDD10A pin 2 | PWM2 | 0V (PWM low when duty = 0) |
| MDD10A pin 3 | DIR2 | 0V |
| MDD10A pin 4 | PWM1 | 0V |
| MDD10A pin 5 | DIR1 | 0V |

#### 2-C — Direction Signal Test

Send a forward command:
```json
{"T":11,"L":50,"R":50}
```

Re-measure at the MDD10A header:

| Pin | Signal | Expected |
|-----|--------|---------|
| MDD10A pin 3 | DIR2 | ~3.3V (RIGHT motor forward) |
| MDD10A pin 5 | DIR1 | ~3.3V (LEFT motor forward) |

Send a reverse command:
```json
{"T":11,"L":-50,"R":-50}
```

| Pin | Signal | Expected |
|-----|--------|---------|
| MDD10A pin 3 | DIR2 | ~0V (direction flipped) |
| MDD10A pin 5 | DIR1 | ~0V |

Stop motors:
```json
{"T":11,"L":0,"R":0}
```

#### 2-D — PWM Frequency and Duty Cycle

With a logic analyzer or frequency-capable multimeter on PWM1 (MDD10A pin 4):

Send:
```json
{"T":11,"L":50,"R":0}
```

| Measurement | Expected | Calculation |
|------------|---------|------------|
| Frequency | **10 000 Hz** | TIM2 @ 10 MHz / period 1000 |
| Duty cycle | **~19.6%** | (50/255) × 100% |
| High voltage | ~3.3V | STM32 GPIO logic high |
| Low voltage | 0V | |

Try different speeds and verify duty scales linearly:

| Command L | Expected duty |
|-----------|--------------|
| 25 | ~9.8% |
| 50 | ~19.6% |
| 100 | ~39.2% |
| 200 | ~78.4% |
| 255 | ~100% |

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| DIR pins always 0V | PB0/PB1 not wired to MDD10A, or GPIO not initialized | Check wiring and `MX_GPIO_Init()` |
| DIR doesn't toggle on command | Wiring wrong pin | Re-verify against WIRING.md table |
| PWM reads DC voltage (no frequency) | PA0/PA1 not configured as TIM2 AF1 | Check `GPIO_InitStruct.Alternate = GPIO_AF1_TIM2` in `MX_GPIO_Init` |
| PWM frequency incorrect | TIM2 prescaler wrong | Verify prescaler=9, period=1000, SYSCLK=96MHz |
| Multimeter reads ~1.1V DC on PWM | DMM averaging a 10 kHz waveform at ~33% duty — this is normal | Use oscilloscope or logic analyzer for true reading |

**Phase 2 pass**: DIR toggles correctly, PWM at 10 kHz with correct duty cycle at multiple speeds.

---

### Phase 3 — Motor Driver Commissioning

**Goal**: MDD10A drives each motor in both directions.
**Prerequisite**: Phase 2 fully passed.
**Important**: wheels off the ground on a stand for this entire phase.

#### 3-A — Motor Battery Connection

1. Fully charge LiPo. Measure: should be 8.2–8.4V.
2. Connect LiPo to MDD10A VIN+ / VIN− **with motors disconnected from M1A/M1B and M2A/M2B**.
3. Measure MDD10A VIN with multimeter: should read LiPo voltage (8.0–8.4V).
4. Verify MDD10A power LED illuminates (if present on your board).

#### 3-B — Left Motor (M1)

1. Connect left motor to **M1A and M1B only**. Leave M2A/M2B unconnected.
2. Send a low-speed forward command:
   ```json
   {"T":11,"L":30,"R":0}
   ```
3. Left wheel should spin. Note which direction is "forward" for the robot.
4. Stop immediately:
   ```json
   {"T":11,"L":0,"R":0}
   ```
5. Send reverse:
   ```json
   {"T":11,"L":-30,"R":0}
   ```
6. Wheel should spin the **opposite** direction.
7. Stop: `{"T":11,"L":0,"R":0}`
8. Ramp up to higher speeds to check for instability:
   ```json
   {"T":11,"L":80,"R":0}
   ```
   Motor should spin smoothly. No buzzing, vibration, or oscillation.
9. Stop.

**If forward is the wrong direction**: swap M1A ↔ M1B at the MDD10A screw terminals.
Do not change firmware. Hardware fix only.

#### 3-C — Left Motor Current Draw

With INA219 not yet installed, use the bench power supply's current display (if available)
or the multimeter in series with the positive battery lead. Expected no-load current at 30%:

| Speed command | Expected no-load current |
|--------------|------------------------|
| 30 | 200–500 mA |
| 80 | 400–900 mA |
| 255 (full) | 600–1200 mA |

Stall current (wheel held) at 30%: up to 3–4A briefly — this is normal but do not hold stall.
If current exceeds 8A at any point with no load: check for motor winding short.

#### 3-D — Right Motor (M2)

1. Disconnect left motor from M1. Connect right motor to **M2A / M2B**.
2. Repeat steps in 3-B using commands:
   ```json
   {"T":11,"L":0,"R":30}   → forward
   {"T":11,"L":0,"R":-30}  → reverse
   {"T":11,"L":0,"R":0}    → stop
   ```
3. Fix direction by swapping M2A ↔ M2B if needed.

#### 3-E — Both Motors Simultaneously

1. Connect both motors (M1A/M1B for left, M2A/M2B for right).
2. Test matched forward:
   ```json
   {"T":11,"L":30,"R":30}
   ```
   Both wheels should spin at similar speed in the "forward" direction.
3. Test matched reverse: `{"T":11,"L":-30,"R":-30}`
4. Test spin-in-place: `{"T":11,"L":30,"R":-30}` — wheels spin opposite directions.
5. Stop.

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| Motor doesn't spin | No VIN to MDD10A, or motor terminals loose | Check VIN voltage, tighten screw terminals |
| Motor spins one direction only | DIR wiring to wrong STM32 pin | Re-check WIRING.md |
| Motor hums but doesn't spin | PWM duty too low, or motor binding | Try `{"T":11,"L":100,"R":0}` to increase duty |
| Motor oscillates / stutters at low speed | GND not shared between STM32 and battery | Add GND wire from Black Pill GND to battery negative |
| Both motors always spin regardless of command | DIR floating high due to missing GND | Verify MDD10A pin 1 is connected to Black Pill GND |
| MDD10A gets hot quickly | Motor stall or overcurrent | Check motor shaft is not binding; add current limiting |

**Phase 3 pass**: both motors spin in both directions, stop on command, no abnormal heat or current.

---

### Phase 4 — Encoder Verification

**Goal**: encoder counts increment correctly, RPM sign matches motor direction.
**Wheels still off ground.**

#### 4-A — Left Encoder Wiring

| Encoder wire | Connects to | Black Pill Pin |
|-------------|-------------|---------------|
| VCC (red) | **3.3V** | 3V3 |
| GND (black) | GND | GND |
| Channel A (green) | PB3 | PB3 |
| Channel B (blue) | PB4 | PB4 |

> **Critical**: use 3.3V, not 5V. Measure before connecting to confirm the pin reads 3.3V.

#### 4-B — Left Encoder Static Test

With motor stopped and wheels off ground:

1. Enable telemetry at 1000 ms:
   ```json
   {"T":131,"cmd":1,"interval":1000}
   ```
2. Manually rotate the left wheel slowly by hand (one full rotation).
3. The `"L"` field in T:1001 is left RPM — it will momentarily show a non-zero value.
4. This confirms the encoder is being read without needing to run the motor.

#### 4-C — Left Encoder Dynamic Test

1. Set telemetry to 500 ms:
   ```json
   {"T":131,"cmd":1,"interval":500}
   ```
2. Spin left motor at low speed:
   ```json
   {"T":11,"L":25,"R":0}
   ```
3. Expected T:1001 output (left RPM positive, right near zero):
   ```json
   {"T":1001,"L":38.4,"R":0.1,...}
   ```
4. Reverse the motor:
   ```json
   {"T":11,"L":-25,"R":0}
   ```
5. Expected: `"L"` value negative (e.g., `-37.9`).
6. Stop: `{"T":11,"L":0,"R":0}`

**If RPM sign is correct but magnitude seems too high or low**:
- Expected RPM at command 25 (≈10% duty): roughly 20–50 RPM at output shaft depending on load.
  At no-load with 19.2:1 gearbox, the output shaft should be 20–80 RPM.
- The formula: `ENCODER_COUNTS_PER_REV = 28 × 19.2 × 4 = 2150`

**If RPM sign is inverted (positive command → negative RPM)**:
Swap encoder A and B wires at the connector for that motor. Do not change firmware.

#### 4-D — Right Encoder

Connect right encoder (VCC → 3.3V, GND, A → PB6, B → PB5) and repeat 4-B and 4-C
using right motor commands: `{"T":11,"L":0,"R":25}` and `{"T":11,"L":0,"R":-25}`.

> Note: Right encoder A/B are mapped to PB6/PB5 respectively (swapped from physical label) to match physical motor direction.

The `"R"` field in T:1001 is right RPM.

#### 4-E — Both Encoders Together

1. Spin both motors at matched speed:
   ```json
   {"T":11,"L":30,"R":30}
   ```
2. Both `"L"` and `"R"` in T:1001 should be positive and within ~20% of each other.
   Identical command → motors may differ slightly due to manufacturing tolerance.
3. Stop. Verify both RPM values return to 0 within 1–2 seconds.

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| RPM always 0 even when motor spins | Encoder VCC not connected, or A/B wires swapped entirely | Check VCC=3.3V, check A/B connections |
| RPM wildly noisy / jumping | Encoder VCC on 5V rail — GPIO input above 3.6V | Switch to 3.3V immediately |
| RPM correct direction but wrong magnitude | `ENCODER_COUNTS_PER_REV` constant wrong for your motor | Verify motor gear ratio; 19.2:1 is default Yellow Jacket |
| One encoder works, other always 0 | Wrong PB pin wired | Re-verify PB5/PB6 for right encoder |
| EXTI fires but RPM bounces at rest | EMI from motor cable coupling into encoder cable | Add ferrite bead on encoder cable, or route encoder wires away from motor power wires |

**Phase 4 pass**: both encoders show correct RPM sign and reasonable magnitude when spinning.

---

### Phase 5 — I2C Bus: MPU-6050 IMU

**Goal**: MPU-6050 detected, returns valid roll/pitch and temperature.

#### 5-A — Wiring

| GY-521 Pin | Connects to | Black Pill Pin |
|-----------|-------------|---------------|
| VCC | 3.3V | 3V3 |
| GND | GND | GND |
| SCL | PB8 | PB8 |
| SDA | PB9 | PB9 |
| AD0 | GND | GND — sets I2C address to 0x68 |
| INT | Leave unconnected | — |

> GY-521 breakout includes 4.7 kΩ pull-ups on SCL/SDA.
> Do not add external pull-ups — double pull-ups reduce noise margin.

#### 5-B — Power-On Timing

The MPU-6050 boots quickly (< 10 ms) but firmware calls `HAL_Delay(100)` after reset
and `HAL_Delay(10)` after wake — sufficient for reliable init. Connect the sensor
while the STM32 is already running; wait 0.5 s before querying.

#### 5-C — Chip ID Verification

First scan the I2C bus to confirm the sensor is present:
```json
{"T":127}
```
Expected: `"mpu6050_68":{"ack":true,"id":"0x68"}`

Then query IMU data:
```json
{"T":126}
```

Expected response (T:1002):
```json
{"T":1002,"r":0.0,"p":-1.5,"y":0.0,"temp":25.3,"ax":0.01,"ay":-0.02,"az":9.81,"gx":0.1,"gy":0.0,"gz":0.0,"ok":true}
```

Key fields to check:
- `"ok":true` — WHO_AM_I returned 0x68 and sensor initialized
- `"temp"` — should read roughly ambient temperature (20–30°C typical)
- `"az"` — near 9.81 m/s² when board is flat (1g on Z axis)
- `"r"` and `"p"` — small values (within ±5°) when sensor is flat

If `"ok":false` or no T:1002 arrives: see failure table below.

#### 5-D — Motion Check

With the sensor powered and responding:

1. Tilt the board forward (nose down): `"p"` (pitch) should go positive.
2. Tilt left: `"r"` (roll) should go negative.
3. Rotate in place: `"y"` (yaw) will drift — this is expected (MPU-6050 has no magnetometer).

#### 5-E — IMU Calibration

The MPU-6050 uses a complementary filter. Run calibration while the robot is flat and still:
```json
{"T":160}
```
This collects 500 samples (~1 s) and zeroes gyro offsets. Gyro drift on yaw is normal.
Roll and pitch are accurate immediately after calibration.

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| `"ok":false` | Chip not found at 0x68 — I2C not reaching sensor | Check SCL/SDA wiring, 3.3V present, AD0→GND |
| `"mpu6050_68":{"ack":false}` in T:127 | Sensor not on bus | Check SCL/SDA, verify 3.3V at VCC pin |
| No T:1002 response | I2C bus hung | Reset STM32; check for SDA/SCL short |
| `"temp":0.0` | Temp register read failed | I2C timeout; check pull-ups not doubled |
| `"r","p"` stuck at 0.0 | imu_update() failing silently | Check T:127 first; ensure WHO_AM_I = 0x68 |
| Values noisy at rest | EMI from motor wires coupling into I2C | Route I2C wires away from motor power wires |

**Phase 5 pass**: T:1002 returns `"ok":true`, temperature plausible, angles change when sensor is moved.

---

### Phase 6 — I2C Bus: INA219 Battery Monitor

**Goal**: INA219 detected, reports correct bus voltage and non-zero current when motor runs.

#### 6-A — INA219 Shunt Wiring

The INA219 measures current by reading voltage across a shunt resistor placed **in series**
with the positive battery lead:

```
LiPo (+) ──► INA219 VIN+ ──► INA219 VIN− ──► MDD10A VIN+
LiPo (−) ──────────────────────────────────► MDD10A VIN− (common GND)
```

Most INA219 breakout boards include a 0.1Ω shunt resistor, calibrated for up to 3.2A
full-scale at the default gain, or up to 8A at the lowest gain. The firmware uses the
INA219 default register settings (PGA gain ÷8, 32V bus range).

| INA219 Pin | Connects to |
|-----------|-------------|
| VCC | 3.3V (Black Pill 3V3) |
| GND | GND |
| SCL | PB8 — shared with MPU-6050 |
| SDA | PB9 — shared with MPU-6050 |
| A0 | GND → I2C address 0x40 |
| A1 | GND → I2C address 0x40 |
| VIN+ | Battery positive (before MDD10A) |
| VIN− | MDD10A VIN+ (after shunt, toward load) |

Both MPU-6050 and INA219 share the same SCL/SDA lines — this is correct I2C multi-device
topology. They have different addresses (0x68 and 0x40) so there is no conflict.

#### 6-B — Voltage Reading

Enable telemetry:
```json
{"T":131,"cmd":1,"interval":500}
```

With motor battery connected (motors stopped), the `"v"` field in T:1001 shows bus voltage:

```json
{"T":1001,"L":0.0,"R":0.0,"r":0.0,...,"temp":25.0,"v":8.24,"i":12.3}
```

Expected `"v"` values:
| LiPo state | Expected voltage |
|-----------|-----------------|
| Fully charged | 8.2–8.4V |
| Nominal | 7.4V |
| Low | 7.0–7.2V |
| Critical (stop!) | <6.6V |

The `"i"` field is current in mA. At rest (motors stopped), expect 20–100 mA
(MDD10A quiescent + INA219 own current).

#### 6-C — Current Reading Under Load

Spin both motors at moderate speed:
```json
{"T":11,"L":60,"R":60}
```

Expected `"i"` increase to **300–1500 mA** depending on load. On the bench with
no load on the wheels, expect 200–600 mA. With the robot weight on the wheels,
expect 500–2000 mA during driving.

Stop: `{"T":11,"L":0,"R":0}`

Current should drop back to idle levels within 1–2 seconds.

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| `"v":0.00` always | INA219 not found at 0x40 | Check A0/A1 both at GND, SCL/SDA wired |
| `"v"` reads 3.3V instead of LiPo voltage | VIN+ wired to 3.3V instead of battery | Re-check VIN+ connection |
| `"i"` always 0.0 | Shunt not in series with load current | Re-check VIN+ → VIN− → MDD10A path |
| `"i"` reads negative | VIN+ and VIN− swapped | Swap the two current sense wires |
| BNO055 stops working after INA219 added | I2C address conflict or double pull-up degrading signal | Verify INA219 address is 0x40, check pull-up count |

**Phase 6 pass**: `"v"` reads correct LiPo voltage, `"i"` increases when motors spin.

---

### Phase 7 — Full Standalone Integration Test

**Goal**: all subsystems operating simultaneously under FreeRTOS, robot drives correctly,
failsafe works.

All hardware connected:
- Both motors → MDD10A M1/M2
- Both encoders → PB3/PB4 (left), PB6/PB5 (right)
- MPU-6050 → I2C1 (PB8/PB9)
- INA219 → I2C1 (same bus)
- Motor battery → MDD10A VIN
- Black Pill → laptop USB-C (CDC COM6)
- Robot on stand, wheels off ground

#### 7-A — Full Telemetry Check

Enable telemetry at 200 ms:
```json
{"T":131,"cmd":1,"interval":200}
```

Confirm **all fields are live** in T:1001:

```json
{"T":1001,"L":0.0,"R":0.0,"r":-1.2,"p":0.5,"y":234.1,"temp":24.8,"v":8.21,"i":45.2}
```

| Field | Should show |
|-------|-------------|
| `"L"`, `"R"` | 0.0 at rest, non-zero when motors spin |
| `"r"`, `"p"` | Angles changing when board tilted (yaw drifts — normal, no magnetometer) |
| `"temp"` | Ambient temperature (~20–30°C) |
| `"v"` | LiPo voltage (7.0–8.4V) |
| `"i"` | Low quiescent current at rest |

#### 7-B — Drive Sequence Test

With telemetry running, execute each command and confirm the expected telemetry response:

```json
{"T":1,"L":0.3,"R":0.3}
```
→ Both `"L"` and `"R"` RPM positive, `"i"` increases.

```json
{"T":1,"L":-0.3,"R":-0.3}
```
→ Both RPM negative, current increases.

```json
{"T":1,"L":0.3,"R":-0.3}
```
→ Left RPM positive, right RPM negative (spin left).

```json
{"T":1,"L":0,"R":0}
```
→ RPM decays to 0 within 1–2 seconds.

#### 7-C — Heartbeat Failsafe Test

This is a **mandatory safety test** before any untethered driving.

1. Send a drive command:
   ```json
   {"T":1,"L":0.3,"R":0.3}
   ```
2. Do **not** send any further commands.
3. Count 3 seconds.
4. Both motors must stop automatically. Verify RPM drops to 0 in T:1001.

The heartbeat timeout (`HEARTBEAT_TIMEOUT_MS = 3000`) in `control_task` detects
the silence and clears targets. If this test fails, **do not proceed to Jetson
integration**. The failsafe is the primary safety mechanism for a lost UART connection.

#### 7-D — IMU Motion Test

While motors are running (T:1 forward), physically tilt the robot on the stand.
Confirm that `"p"` (pitch) and `"r"` (roll) change in T:1001 while motors continue
running — this confirms FreeRTOS is context-switching between tasks without starving
the I2C reads in `telemetry_task`.

**Phase 7 pass**: all telemetry fields live, drive sequence correct, heartbeat stops motors within 3 s.

---

## Stage 2 — Jetson Integration

---

### Phase 8 — USB-CDC Handshake

**Goal**: Jetson enumerates the Black Pill as `/dev/ttyACM0` and can exchange messages.

#### 8-A — Hardware Connection

Move the Black Pill USB-C cable from the **laptop to the Jetson USB port**.
The Jetson must be booted and logged in before plugging in.

#### 8-B — Device Enumeration

On the Jetson terminal:
```bash
# Verify the device node appears
ls /dev/ttyACM*
```
Expected: `/dev/ttyACM0`

If missing:
```bash
# Check USB enumeration
lsusb | grep -i stm
# Expected line: Bus 001 Device 003: ID 0483:5740 STMicroelectronics Virtual COM Port

# Check kernel driver loaded
dmesg | tail -20 | grep -i "ttyACM\|cdc_acm"
# Expected: cdc_acm 1-1:1.2: ttyACM0: USB ACM device
```

If `cdc_acm` module is not loaded:
```bash
sudo modprobe cdc_acm
```

#### 8-C — Permissions

```bash
# Add user to dialout group (one-time setup)
sudo usermod -aG dialout $USER
# Log out and back in, then verify:
groups | grep dialout
```

Without this, every serial command needs `sudo`.

#### 8-D — Terminal Test on Jetson

```bash
# Method 1: screen
sudo screen /dev/ttyACM0 115200

# Method 2: Python miniterm
python3 -m serial.tools.miniterm /dev/ttyACM0 115200

# Method 3: PlatformIO monitor (if PIO installed on Jetson)
pio device monitor --port /dev/ttyACM0 --baud 115200
```

T:1001 telemetry should appear immediately. Send echo test:
```
{"T":143,"cmd":"jetson_ping"}
```
Expected echo response:
```json
{"T":143,"cmd":"jetson_ping"}
```

#### 8-E — Python Serial Test Script

Save and run this on the Jetson to verify the full handshake programmatically:

```python
#!/usr/bin/env python3
# test_serial.py — run on Jetson to verify STM32 comms
import serial
import time
import json

PORT  = "/dev/ttyACM0"
BAUD  = 115200

with serial.Serial(PORT, BAUD, timeout=2.0) as ser:
    time.sleep(0.5)          # let STM32 finish boot
    ser.reset_input_buffer()

    # --- Echo test ---
    cmd = json.dumps({"T": 143, "cmd": "jetson_hello"}) + "\n"
    ser.write(cmd.encode())
    resp = ser.readline().decode().strip()
    print(f"Echo test: {resp}")
    assert '"cmd":"jetson_hello"' in resp, "ECHO FAILED"

    # --- Enable telemetry ---
    ser.write(b'{"T":131,"cmd":1,"interval":500}\n')
    time.sleep(0.1)

    # --- Read 3 telemetry packets ---
    for i in range(3):
        line = ser.readline().decode().strip()
        pkt  = json.loads(line)
        print(f"T:1001 #{i+1}: v={pkt['v']:.2f}V  temp={pkt['temp']:.1f}C  L={pkt['L']:.1f}rpm")

    # --- Stop telemetry ---
    ser.write(b'{"T":130,"cmd":0}\n')

print("All tests passed.")
```

Run:
```bash
python3 test_serial.py
```

Expected output:
```
Echo test: {"T":143,"cmd":"jetson_hello"}
T:1001 #1: v=8.23V  temp=25.2C  L=0.0rpm
T:1001 #2: v=8.23V  temp=25.2C  L=0.0rpm
T:1001 #3: v=8.22V  temp=25.2C  L=0.0rpm
All tests passed.
```

#### Failure Modes

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| `/dev/ttyACM0` missing | `cdc_acm` driver not loaded | `sudo modprobe cdc_acm` |
| `Permission denied` | Not in `dialout` group | `sudo usermod -aG dialout $USER`, re-login |
| No T:1001 data | STM32 not powered (USB cable unplugged) | Check Blue LED on Black Pill, press NRST |
| `serial.SerialException` in Python | Port in use by another process | `sudo fuser /dev/ttyACM0` to find and kill it |
| Garbled data | Line ending mismatch | Ensure sending `\n` not `\r\n` |

**Phase 8 pass**: Python test script prints "All tests passed."

---

### Phase 9 — Full Command / Telemetry Loop from Jetson

**Goal**: the existing Jetson robot software stack controls the STM32 exactly as it
controlled the ESP32 — same protocol, only the port name changes.

#### 9-A — Port Configuration

In the Jetson robot software (ugv_rpi or equivalent):
```python
# Old (ESP32):
SERIAL_PORT = "/dev/ttyUSB0"

# New (STM32):
SERIAL_PORT = "/dev/ttyACM0"
```

Baud rate, protocol, and T-code commands are **identical**. No other changes needed.

#### 9-B — Functional Tests from Jetson Software

| Test | Command sent | Expected result |
|------|-------------|----------------|
| Move forward | `{"T":1,"L":0.3,"R":0.3}` | Both motors spin forward |
| Move backward | `{"T":1,"L":-0.3,"R":-0.3}` | Both motors spin backward |
| Turn left | `{"T":1,"L":-0.1,"R":0.3}` | Robot arcs left |
| Turn right | `{"T":1,"L":0.3,"R":-0.1}` | Robot arcs right |
| Stop | `{"T":1,"L":0,"R":0}` | Motors stop immediately |
| IMU query | `{"T":126}` | T:1002 response with angles |
| Telemetry rate | `{"T":131,"cmd":1,"interval":100}` | T:1001 at 100 ms intervals |
| Heartbeat test | Stop sending for >3 s | Motors stop automatically |

#### 9-C — Latency Check

With telemetry at 100 ms, measure round-trip time from the Jetson:
```python
import time, serial, json

with serial.Serial("/dev/ttyACM0", 115200, timeout=1.0) as ser:
    ser.write(b'{"T":143,"cmd":"latency_test"}\n')
    t0 = time.time()
    resp = ser.readline()
    t1 = time.time()
    print(f"Round-trip: {(t1-t0)*1000:.1f} ms")
```

Expected: **5–15 ms** round-trip (USB VCP adds ~1–2 ms, STM32 processes in <1 ms,
serial_task has up to 10 ms timeout). If >50 ms, check for USB throttling or Jetson
USB power-saving mode (`sudo systemctl mask usb-power-saving` if applicable).

**Phase 9 pass**: all functional tests pass, latency <20 ms.

---

### Phase 10 — Dual Battery Commissioning

**Goal**: both battery systems operate correctly together, startup/shutdown sequences
understood, voltage monitoring active.

#### 10-A — Power Architecture

```
┌─────────────────────────────────────────────────────┐
│  Battery A — Motor LiPo (7.4V 2S)                  │
│                                                      │
│  LiPo+ ──► INA219 VIN+ ──► INA219 VIN− ──► MDD10A VIN+  │
│  LiPo− ──────────────────────────────────► MDD10A VIN−  │
│  LiPo− ──────────────────────────────────► Nucleo GND   │
│                (common GND tie)                      │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│  Battery B — Jetson power bank / 19V supply         │
│                                                      │
│  Jetson PSU ──► Jetson barrel / USB-C               │
│  Jetson USB-A ──► Black Pill USB-C (5V @ ≤500mA)   │
│  Black Pill 3.3V reg ──► MPU-6050, INA219 logic     │
└─────────────────────────────────────────────────────┘
```

The two battery negatives share a common GND node at the Nucleo.
This is required — without it, DIR/PWM signal levels are undefined.

#### 10-B — Voltage Thresholds

Configure in firmware (future task) or monitor from Jetson software:

| Condition | Motor battery voltage | Action |
|-----------|----------------------|--------|
| Full charge | 8.2–8.4V | Normal operation |
| Nominal | 7.4V | Normal operation |
| Low battery warning | < 7.0V | Alert, plan to stop soon |
| Critical — stop robot | < 6.6V | Stop motors, disallow driving |
| Hard cutoff | < 6.4V | MDD10A may brownout; cell damage risk |

These thresholds come from the `"v"` field in T:1001 telemetry.

#### 10-C — Startup Sequence

Follow this order every session:

```
Step 1  Verify motor LiPo voltage ≥ 7.0V with multimeter before connecting.
Step 2  Connect motor LiPo to MDD10A VIN.
        → MDD10A powers on; motors are stopped (PWM = 0).
Step 3  Boot Jetson from Battery B.
        → Wait for Jetson to fully boot to desktop/terminal.
Step 4  Connect Black Pill USB-C cable to Jetson USB port.
        → STM32 powers on; FreeRTOS starts; /dev/ttyACM0 appears.
Step 5  Confirm /dev/ttyACM0 is present: ls /dev/ttyACM*
Step 6  Confirm T:1001 telemetry is flowing (run test_serial.py or open terminal).
Step 7  Start robot software on Jetson.
```

#### 10-D — Shutdown Sequence

```
Step 1  Send stop command from Jetson software: {"T":1,"L":0,"R":0}
Step 2  Verify RPM = 0 in telemetry — motors fully stopped.
Step 3  Stop robot software on Jetson.
Step 4  Disconnect Black Pill USB-C from Jetson.
        → STM32 powers down; MDD10A is now signal-less (DIR/PWM float).
Step 5  Immediately after: disconnect motor LiPo.
        → Important: disconnect within a few seconds of STM32 powerdown,
          before floating inputs cause spurious motor activation on MDD10A.
Step 6  Store LiPo at storage voltage (3.8–3.85V/cell = 7.6–7.7V) if not
        using again within 24 hours. Use the balance charger's storage mode.
```

**Phase 10 pass**: both batteries connected, telemetry shows correct voltage, startup/shutdown sequences practiced at least twice.

---

### Phase 11 — First Full Drive Test

**Prerequisites**: Phases 0–10 all passed. Robot on the floor, open space, clear of obstacles.

#### 11-A — Environment

- Minimum 2m × 2m clear floor space
- No stairs, drops, or obstacles within 1m of starting position
- Emergency stop ready: keep `{"T":1,"L":0,"R":0}` in clipboard, or hand on keyboard
  for Jetson software E-stop button
- Second person optional but recommended for first drive

#### 11-B — Slow Walk Forward

```json
{"T":1,"L":0.1,"R":0.1}
```
- Robot moves forward slowly (~10 cm/s estimated).
- Observe: does it track straight, or curve? Small curve is expected due to motor tolerance.
- Run for 2 seconds, then stop.

#### 11-C — Direction Confirmation

| Command | Expected physical behavior |
|---------|--------------------------|
| `{"T":1,"L":0.2,"R":0.2}` | Forward, roughly straight |
| `{"T":1,"L":-0.2,"R":-0.2}` | Reverse |
| `{"T":1,"L":0.3,"R":-0.3}` | Spin clockwise (from above) |
| `{"T":1,"L":-0.3,"R":0.3}` | Spin counter-clockwise |

#### 11-D — Battery Monitor During Drive

Watch `"v"` during a 60-second driving session. A healthy 2S LiPo should not drop
below 7.8V under normal driving load. If it drops rapidly below 7.5V under light load,
the LiPo may be old or damaged.

#### 11-E — Heartbeat Final Verification on Floor

1. Drive the robot forward: `{"T":1,"L":0.3,"R":0.3}`
2. Physically disconnect the USB cable from the Jetson.
3. Robot must stop within 3 seconds and remain stopped.
4. Reconnect USB, confirm `/dev/ttyACM0` reappears.

This simulates a real cable failure scenario. If the robot does not stop — do not
use it untethered until the heartbeat is debugged.

**Phase 11 pass**: robot drives in all four directions, stops on command, heartbeat failsafe verified on floor.

---

## Quick Command Reference

```json
── Connectivity ────────────────────────────────────────────────
{"T":143,"cmd":"ping"}                 Echo test

── Telemetry control ───────────────────────────────────────────
{"T":130,"cmd":1}                      Enable telemetry (default interval)
{"T":130,"cmd":0}                      Disable telemetry
{"T":131,"cmd":1,"interval":200}       Enable at 200 ms
{"T":131,"cmd":1,"interval":100}       Enable at 100 ms

── Motor control (T:1 — normalized ±1.0) ───────────────────────
{"T":1,"L":0.3,"R":0.3}               Forward ~30%
{"T":1,"L":-0.3,"R":-0.3}             Reverse ~30%
{"T":1,"L":0.3,"R":-0.3}              Spin left
{"T":1,"L":-0.3,"R":0.3}              Spin right
{"T":1,"L":0,"R":0}                   STOP

── Motor control (T:11 — direct PWM ±255) ──────────────────────
{"T":11,"L":50,"R":50}                Both motors ~20% forward
{"T":11,"L":50,"R":0}                 Left motor only
{"T":11,"L":0,"R":0}                  STOP

── Sensor queries ───────────────────────────────────────────────
{"T":126}                              IMU snapshot (T:1002 response)

── ROS velocity (T:13) ──────────────────────────────────────────
{"T":13,"linear":0.3,"angular":0.0}   0.3 m/s forward, straight
{"T":13,"linear":0.0,"angular":1.0}   Rotate 1 rad/s
```

---

## Bring-Up Sign-Off Checklist

```
Stage 1 — STM32 Standalone
  [ ] Phase 0  Pre-power checks: board inspected, continuity checked, LiPo OK
  [ ] Phase 1  Firmware flashes, T:1001 appears, echo works, telemetry stops
  [ ] Phase 2  DIR toggles correctly, PWM at 10 kHz with correct duty cycle
  [ ] Phase 3  Both motors spin, direction correct, current in expected range
  [ ] Phase 4  Both encoders return correct RPM and sign
  [ ] Phase 5  MPU-6050 returns "ok":true, angles change when tilted
  [ ] Phase 6  INA219 reads correct LiPo voltage, current rises with motor load
  [ ] Phase 7  Full telemetry all-fields live, drive sequence OK, heartbeat stops motors

Stage 2 — Jetson Integration
  [ ] Phase 8  /dev/ttyACM0 appears, Python test_serial.py passes
  [ ] Phase 9  Jetson software drives robot, telemetry parsed, heartbeat tested
  [ ] Phase 10 Dual battery startup/shutdown practiced, LiPo thresholds known
  [ ] Phase 11 First floor drive: all directions, stops on command, cable-pull failsafe
```

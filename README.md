# Astra Base — STM32 Robot Controller

STM32F411CEU6 firmware for the Astra differential-drive robot base. Runs FreeRTOS with a JSON T-code command interface compatible with the Waveshare/ugv_rpi ecosystem, allowing the Jetson to control motors, read IMU and battery data, and receive periodic telemetry over USB CDC.

## Hardware

| Component | Part |
|-----------|------|
| MCU | WeAct STM32F411CEU6 Black Pill (100 MHz, 512 KB Flash, 128 KB SRAM) |
| Motor driver | Cytron MDD10A (dual channel, 10A) |
| IMU | MPU-6050 GY-521 (I2C, complementary filter for roll/pitch) |
| Current sensor | INA219 (I2C, battery voltage + motor current) |
| Encoders | Quadrature, 28 PPR × 19.2:1 gear ratio |
| Host | Jetson (connected via USB-C → `/dev/ttyACM0`) |
| Programmer | ST-Link V2 (external, SWD) |

## Quick Start

```bash
git clone --recurse-submodules https://github.com/rimesports-sj/Astra_base.git
cd Astra_base
~/.platformio/penv/bin/pio run --target upload
```

See [SETUP.md](SETUP.md) for full tool installation, wiring, and verification steps.

## Architecture

Three FreeRTOS tasks:

| Task | Priority | Period | Role |
|------|----------|--------|------|
| `control_task` | 9 (highest) | 10 ms | Encoder update → heartbeat failsafe → motor PWM output |
| `serial_task` | 6 | Event-driven | UART/USB line reader → JSON T-code dispatcher |
| `telemetry_task` | 2 (lowest) | Configurable | Periodic T:1001 chassis feedback to Jetson |

Command interface uses JSON T-codes matching the Waveshare Wave Rover / ugv_rpi protocol — the Jetson `base_ctrl.py` requires no modification.

## Key Commands

```json
{"T":1,"L":0.5,"R":0.5}      // Drive forward 50%
{"T":126}                     // IMU snapshot
{"T":130,"cmd":1}             // Enable 200ms telemetry stream
{"T":200}                     // System self-diagnostic
{"T":160}                     // IMU calibration (robot flat and still)
```

## Pin Map (STM32F411CEU6)

| Pin | Function |
|-----|----------|
| PA0 | TIM2_CH1 — PWM Motor L |
| PA1 | TIM2_CH2 — PWM Motor R |
| PA8–PA10 | TIM1_CH1–3 — PWM Motors 3–5 (future) |
| PA11/PA12 | USB_DM/DP — Jetson USB CDC |
| PB0/PB1 | DIR Motor L/R |
| PB3–PB6 | Encoder L-A, L-B, R-B, R-A |
| PB8/PB9 | I2C1 SCL/SDA — IMU + INA219 |

## Documentation

| Doc | Contents |
|-----|----------|
| [SETUP.md](SETUP.md) | Tool installation, wiring, build, flash, verify |
| [BRINGUP.md](BRINGUP.md) | Layered hardware bring-up procedure |
| [WIRING.md](WIRING.md) | Full wiring reference |
| [docs/bringup_imu_ina219.md](docs/bringup_imu_ina219.md) | IMU + current sensor bring-up log |
| [docs/bringup_left_motor.md](docs/bringup_left_motor.md) | Motor + encoder bring-up log |
| [docs/chipset_migration_l476_to_f411.md](docs/chipset_migration_l476_to_f411.md) | Board migration decision (L476RG → F411CEU6) |
| [docs/fw_update_design.md](docs/fw_update_design.md) | OTA firmware update architecture |

## Roadmap

- [x] Port firmware to STM32F411CEU6 (Black Pill V3.1)
- [x] USB CDC transport over OTG FS (PA11/PA12)
- [x] ST-Link SWD flashing without DFU button dance (VC_CORERESET trick)
- [ ] Wire remaining peripherals (right motor, MPU-6050, INA219)
- [ ] Verify right encoder (PB5/PB6)
- [ ] PID speed control loop
- [ ] Custom bootloader + OTA firmware update from Jetson
- [ ] Motors 3–5 (TIM1 channels)
- [ ] ROS2 node integration

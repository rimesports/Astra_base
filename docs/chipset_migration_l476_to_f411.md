# Chipset Migration: STM32L476RG → STM32F411CEU6
**Date:** 2026-04-20
**Reason:** STM32L476RG Nucleo board hardware failure — 3.3V rail collapsed to 1.73V, preventing ST-Link connection and flashing.

---

## Root Cause of L476RG Failure

OpenOCD reported `Target voltage: 1.734122` (expected 3.28–3.30V).
The onboard 3.3V LDO was pulled down by an external peripheral with a wiring fault.
ST-Link side of the Nucleo remained functional (enumerated correctly in Device Manager).

---

## Boards Evaluated

| Board | Flash | SRAM | Core | Verdict |
|-------|-------|------|------|---------|
| STM32G431CBU6 | 128 KB | 32 KB | M4F | RAM too tight (32 KB < 22.5 KB used) |
| STM32F407ZGT6 | 1 MB | 192 KB | M4F | Good, but large board, slower arrival |
| STM32F103C8T6 (Blue Pill) | 64 KB | 20 KB | **M3** | Rejected — no FPU, RAM too small |
| STM32F401CCU6 | 256 KB | 64 KB | M4F | Good, same pinout as F411 |
| **STM32F411CEU6 (Black Pill)** | **512 KB** | **128 KB** | **M4F** | **Selected** |

---

## Selected Board: STM32F411CEU6 (WeAct Black Pill)

**Ordered:** 2026-04-20
**Source:** Amazon — WeAct STM32F411CEU6 Black Pill

### Specs vs Old Board

| | STM32L476RG | STM32F411CEU6 |
|---|---|---|
| Core | Cortex-M4F | Cortex-M4F |
| Clock | 80 MHz | 100 MHz |
| Flash | 1 MB | 512 KB |
| SRAM | 96 KB | 128 KB |
| Package | LQFP64 | UFQFPN48 |
| Onboard ST-Link | Yes (Nucleo) | No |
| USB | No | USB Full Speed OTG (USB-C) |
| I2C peripheral | v2 (Timing reg) | v1 (ClockSpeed reg) |

### Programmer
No onboard ST-Link. Use the broken Nucleo's ST-Link half as external programmer:
- Remove CN2 jumpers on Nucleo to disconnect the dead L476 target
- Wire Nucleo CN2 → Black Pill SWD pads:

| Nucleo CN2 Pin | Signal | Black Pill |
|---|---|---|
| Pin 1 | VDD_TARGET | 3.3V |
| Pin 2 | SWCLK | SWCLK (PA14) |
| Pin 3 | GND | GND |
| Pin 4 | SWDIO | SWDIO (PA13) |
| Pin 5 | NRST | NRST |

---

## IO Capacity: F411CEU6 vs Project Requirements

### Available GPIO (UFQFPN48 package)
- PA0–PA15: 16 pins (PA13/PA14 reserved for SWD)
- PB0–PB10, PB12–PB15: 15 pins (PB11 not in 48-pin package)
- PC13–PC15: 3 pins
- PH0–PH1: 2 pins (free when using HSI clock)
- **Total usable: ~32 GPIO**

### Full Pin Allocation — 5 Motors + Encoders + USB + I2C

| Pin | Function | Signal |
|-----|----------|--------|
| PA0 | TIM2_CH1 | PWM Motor 1 (left) |
| PA1 | TIM2_CH2 | PWM Motor 2 (right) |
| PA2 | USART2_TX | UART TX (backup / debug) |
| PA3 | USART2_RX | UART RX (backup / debug) |
| PA8 | TIM1_CH1 | PWM Motor 3 |
| PA9 | TIM1_CH2 | PWM Motor 4 |
| PA10 | TIM1_CH3 | PWM Motor 5 |
| PA11 | USB_DM | USB CDC to Jetson |
| PA12 | USB_DP | USB CDC to Jetson |
| PA15 | GPIO OUT | DIR Motor 5 |
| PB0 | GPIO OUT | DIR Motor 1 (remapped from PC0) |
| PB1 | GPIO OUT | DIR Motor 2 (remapped from PC1) |
| PB3 | EXTI3 | Encoder 1A |
| PB4 | EXTI4 | Encoder 1B |
| PB5 | EXTI5 | Encoder 2B |
| PB6 | EXTI6 | Encoder 2A |
| PB7 | EXTI7 | Encoder 3A |
| PB8 | I2C1_SCL | IMU + INA219 (shared bus) |
| PB9 | I2C1_SDA | IMU + INA219 (shared bus) |
| PB10 | EXTI10 | Encoder 3B |
| PB12 | EXTI12 | Encoder 4A |
| PB13 | EXTI13 | Encoder 4B |
| PB14 | EXTI14 | Encoder 5A |
| PB15 | EXTI15 | Encoder 5B |
| PC13 | GPIO OUT | DIR Motor 3 |
| PH0 | GPIO OUT | DIR Motor 4 |

**Total used: 26 pins — ~6 pins spare for future use**

### Communication: USB CDC preferred over UART
- USB-C → Jetson USB: appears as `/dev/ttyACM0`, same JSON protocol
- Single cable replaces UART wires + USB-UART adapter
- Can power F411 from Jetson's USB port
- UART pins (PA2/PA3) retained as debug/fallback
- Verify board has 1.5 kΩ pull-up on PA12 (USB DP) — WeAct version does

---

## Firmware Port Checklist

| File | Change Required |
|------|----------------|
| `platformio.ini` | `board = blackpill_f411ce`, `-DSTM32F411xE`, remove `-DSTM32L476xx` |
| All source files | `stm32l4xx_hal.h` → `stm32f4xx_hal.h` |
| `main.cpp` | Rewrite `SystemClock_Config()` for F411 (HSI → PLL → 100 MHz) |
| `main.cpp` | Remove `HAL_PWREx_EnableVddIO2()` — L4 specific, not on F4 |
| `main.cpp` | Add `MX_TIM1_Init()` for motors 3–5 |
| `main.cpp` | Add `MX_USB_CDC_Init()` for Jetson connection |
| `i2c_bus.cpp` | `Init.Timing = 0x10909CEC` → `Init.ClockSpeed = 100000` + `Init.DutyCycle = I2C_DUTYCYCLE_2` |
| `astra_config.h` | Remap `MOTOR_LEFT_DIR` PC0 → PB0, `MOTOR_RIGHT_DIR` PC1 → PB1 |
| `astra_config.h` | Add Motor 3–5 pin definitions (TIM1 channels, DIR pins) |
| `FreeRTOSConfig.h` | `configCPU_CLOCK_HZ 80000000UL` → `100000000UL` |
| `FreeRTOSConfig.h` | `configTOTAL_HEAP_SIZE` can stay at 20 KB (128 KB SRAM, no pressure) |
| `FreeRTOSConfig.h` | Port path `portable/GCC/ARM_CM4F` — unchanged |

### I2C Key Difference
STM32L4/G4 use I2C peripheral v2 (single 32-bit Timing register).
STM32F4 uses I2C peripheral v1 (separate ClockSpeed + DutyCycle fields).

```cpp
// L476 (old) — v2
hi2c1.Init.Timing = 0x10909CEC;

// F411 (new) — v1
hi2c1.Init.ClockSpeed = 100000;
hi2c1.Init.DutyCycle  = I2C_DUTYCYCLE_2;
```

---

## Current Firmware Status (as of migration date)

| Feature | Status |
|---------|--------|
| MPU-6050 IMU (T:126, T:160) | Working on L476, pending port |
| INA219 current sensor (T:150) | I2C working, VIN wiring pending |
| Left motor + encoder | Working on L476, pending port |
| Right motor (M2A) | Floating input issue — not yet wired to STM32 |
| USB CDC to Jetson | New on F411 — replaces UART |
| T:200 system diagnostic | Implemented, pending flash |
| Motors 3–5 | Planned, not yet implemented |

---

## Next Steps After Board Arrives
1. Wire Nucleo ST-Link → Black Pill SWD (CN2 jumpers off)
2. Update `platformio.ini` board target
3. Port HAL headers and SystemClock_Config
4. Port i2c_bus.cpp (v1 I2C init)
5. Update astra_config.h pin definitions
6. Implement USB CDC (TinyUSB or STM32 USB library)
7. Flash T:200 diagnostic and verify all peripherals
8. Wire right motor PWM2/DIR2 (PA1/PB1 → MDD10A)
9. Complete INA219 VIN wiring for motor current sweep

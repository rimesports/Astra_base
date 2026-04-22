# Black Pill F411 Wiring — Connection Reference
**Board:** WeAct Studio STM32F411CEU6 Black Pill V3.1
**Date:** 2026-04-22

---

## Full Pin Map

```
                    ┌─────────────────────────────┐
                    │  GND  SWDIO  SWSCK  3V3      │ ← SWD programmer pads (top)
                    │  [●]   [●]    [●]   [●]      │
                    │          ┌─────────┐          │
   3V3 ────────────│          │         │          │──────────── VB
   G   ────────────│          │F411CEU6 │          │──────────── C13
   5V  ────────────│          │         │          │──────────── C14
   B9  ────────────│          │         │          │──────────── C15
   B8  ────────────│          └─────────┘          │──────────── R  (NRST)
   B7  ────────────│                               │──────────── A0
   B6  ────────────│      [KEY]    [BOOT0]         │──────────── A1
   B5  ────────────│      [USB-C]                  │──────────── A2
   B4  ────────────│                               │──────────── A3
   B3  ────────────│                               │──────────── A4
   A15 ────────────│                               │──────────── A5
   A12 ────────────│                               │──────────── A6
   A11 ────────────│                               │──────────── A7
   A10 ────────────│                               │──────────── B0
   A9  ────────────│                               │──────────── B1
   A8  ────────────│                               │──────────── B2
   B15 ────────────│                               │──────────── B10
   B14 ────────────│                               │──────────── 3V3
   B13 ────────────│                               │──────────── G
   B12 ────────────│                               │──────────── 5V
                    └─────────────────────────────┘
```

---

## Pin Assignment Summary

| Black Pill Pin | Location | Assigned To | Direction | Notes |
|----------------|----------|-------------|-----------|-------|
| **3V3** | Left top | Power rail | OUT | 3.3V supply — max ~300mA from onboard LDO |
| **G (GND)** | Left top | Ground | — | Shared ground for all peripherals |
| **5V** | Left top | — | IN/OUT | USB VBUS (5V when USB-C connected) |
| **B9** | Left 4 | I2C1_SDA | I/O OD | IMU + INA219 data |
| **B8** | Left 5 | I2C1_SCL | I/O OD | IMU + INA219 clock |
| **B7** | Left 6 | *free* | — | |
| **B6** | Left 7 | Encoder Right A (EXTI6) | IN PU | Yellow Jacket right motor Ch.A |
| **B5** | Left 8 | Encoder Right B (EXTI5) | IN PU | Yellow Jacket right motor Ch.B |
| **B4** | Left 9 | Encoder Left B (EXTI4) | IN PU | Yellow Jacket left motor Ch.B |
| **B3** | Left 10 | Encoder Left A (EXTI3) | IN PU | Yellow Jacket left motor Ch.A |
| **A15** | Left 11 | *free* | — | Was JTAG, usable as GPIO |
| **A12** | Left 12 | USB_DP | I/O | USB CDC to Jetson (D+) |
| **A11** | Left 13 | USB_DM | I/O | USB CDC to Jetson (D−) |
| **A10** | Left 14 | *free* | — | |
| **A9** | Left 15 | *free* (TIM1_CH2) | — | Motor 4 PWM — future |
| **A8** | Left 16 | *free* (TIM1_CH1) | — | Motor 3 PWM — future |
| **B15** | Left 17 | *free* | — | |
| **B14** | Left 18 | *free* | — | |
| **B13** | Left 19 | *free* | — | |
| **B12** | Left 20 | *free* | — | |
| **VB** | Right 1 | *tie to 3V3* | — | VBAT — connect to 3V3 if no RTC battery |
| **C13** | Right 2 | *free* | — | Low-current GPIO only (RTC domain) |
| **C14** | Right 3 | *free* | — | OSC32 — avoid if LSE crystal fitted |
| **C15** | Right 4 | *free* | — | OSC32 — avoid if LSE crystal fitted |
| **R (NRST)** | Right 5 | Reset | IN | ST-Link NRST (optional) |
| **A0** | Right 6 | TIM2_CH1 — PWM Motor L | OUT AF | Left motor speed |
| **A1** | Right 7 | TIM2_CH2 — PWM Motor R | OUT AF | Right motor speed |
| **A2** | Right 8 | USART2_TX | OUT AF | Debug UART TX (backup) |
| **A3** | Right 9 | USART2_RX | IN AF | Debug UART RX (backup) |
| **A4** | Right 10 | *free* | — | |
| **A5** | Right 11 | *free* | — | SPI1_SCK if SPI needed |
| **A6** | Right 12 | *free* | — | SPI1_MISO if SPI needed |
| **A7** | Right 13 | *free* | — | SPI1_MOSI if SPI needed |
| **B0** | Right 14 | DIR Motor L | OUT PP | Left motor direction |
| **B1** | Right 15 | DIR Motor R | OUT PP | Right motor direction |
| **B2** | Right 16 | BOOT1 | — | Pull low (leave unconnected = OK) |
| **B10** | Right 17 | *free* | — | I2C2_SCL or TIM2_CH3 option |
| **3V3** | Right 18 | Power rail | OUT | Second 3V3 access point |
| **G** | Right 19 | Ground | — | Second GND access point |
| **5V** | Right 20 | — | IN/OUT | Second 5V access point |
| **SWDIO** | SWD pad | PA13 — Debug | I/O | ST-Link SWDIO |
| **SWSCK** | SWD pad | PA14 — Debug | IN | ST-Link SWCLK |

---

## Connection Tables by Subsystem

### 1. ST-Link V2 Programmer → Black Pill SWD Pads

| ST-Link V2 Pin | Signal | Black Pill | Notes |
|---------------|--------|------------|-------|
| Pin 1 | SWDIO | SWDIO pad (top) | Data |
| Pin 5 | SWCLK | SWSCK pad (top) | Clock |
| Pin 3 | GND | GND pad (top) | |
| Pin 9 | 3.3V | 3V3 pad (top) | Voltage reference only — 50mA max from ST-Link |
| Pin 15 | NRST | R pin (right rail) | Optional — improves reliability |

> Power the Black Pill via USB-C independently. The ST-Link 3.3V pin is only a voltage reference, not a power supply.

---

### 2. MPU-6050 IMU (GY-521 breakout) → Black Pill

| GY-521 Pin | Black Pill Pin | Wire Color (suggest) | Notes |
|-----------|---------------|---------------------|-------|
| VCC | 3V3 (left top) | Red | 3.3V only — GY-521 has onboard regulator, 3.3V input is fine |
| GND | G (left top) | Black | |
| SCL | **B8** (left 5) | Yellow | I2C1_SCL |
| SDA | **B9** (left 4) | Green | I2C1_SDA |
| AD0 | G (GND) | Black | Sets I2C address = 0x68 |
| XDA | — | — | Leave unconnected |
| XCL | — | — | Leave unconnected |
| INT | — | — | Leave unconnected (polling mode) |

---

### 3. INA219 Current Sensor → Black Pill + Power Rail

| INA219 Pin | Connection | Wire Color (suggest) | Notes |
|-----------|------------|---------------------|-------|
| VCC | 3V3 (right 18) | Red | Sensor power |
| GND | G (right 19) | Black | Shared ground |
| SCL | **B8** (left 5) | Yellow | Shared I2C bus with IMU |
| SDA | **B9** (left 4) | Green | Shared I2C bus with IMU |
| VIN+ | Battery (+) terminal | Red/thick | In-line with power path |
| VIN− | MDD10A B+ | Red/thick | Shunt between battery and motor driver |

> **Power path:** Battery(+) → INA219 VIN+ → [shunt resistor] → INA219 VIN− → MDD10A B+
> Current flows through the shunt, INA219 measures the voltage drop across it.

---

### 4. Cytron MDD10A Motor Driver → Black Pill

**Control connector (5-pin):**

| MDD10A Pin | Signal | Black Pill Pin | Notes |
|-----------|--------|---------------|-------|
| Pin 1 | GND | G (right 19) | **Critical** — shared ground, must be connected |
| Pin 2 | PWM2 (Right) | **A1** (right 7) | TIM2_CH2, 10 kHz PWM |
| Pin 3 | DIR2 (Right) | **B1** (right 15) | GPIO output, LOW=forward |
| Pin 4 | PWM1 (Left) | **A0** (right 6) | TIM2_CH1, 10 kHz PWM |
| Pin 5 | DIR1 (Left) | **B0** (right 14) | GPIO output, LOW=forward |

**Power connections:**

| MDD10A Terminal | Connection | Notes |
|----------------|------------|-------|
| B+ | INA219 VIN− | Battery positive through current sensor |
| B− | Battery (−) AND Black Pill G | Both battery return AND shared logic ground |
| M1A / M1B | Left motor terminals | Polarity sets default forward direction |
| M2A / M2B | Right motor terminals | Polarity sets default forward direction |

---

### 5. Yellow Jacket Encoders → Black Pill

Yellow Jacket (goBILDA) motor encoders use a JST-PH 4-pin connector:

| Encoder Wire | Color (typical) | Black Pill Pin | Notes |
|-------------|----------------|---------------|-------|
| **Left Motor** | | | |
| GND | Black | G | |
| VCC | Red | 3V3 | Power encoder at 3.3V — output will be 3.3V logic |
| Channel A | White/Blue | **B3** (left 10) | EXTI3 interrupt |
| Channel B | Yellow/Green | **B4** (left 9) | EXTI4 interrupt |
| **Right Motor** | | | |
| GND | Black | G | |
| VCC | Red | 3V3 | |
| Channel A | White/Blue | **B6** (left 7) | EXTI6 interrupt |
| Channel B | Yellow/Green | **B5** (left 8) | EXTI5 interrupt |

> **Power at 3.3V not 5V.** Yellow Jacket encoders work at 3.3V. Powering at 5V produces 5V output signals — some STM32F411 pins tolerate 5V (FT pins) but B3/B4/B5/B6 are FT, so 5V is safe if needed.
>
> **Direction:** If the robot reports negative encoder counts when driving forward, swap Ch.A ↔ Ch.B for that motor (or set the `A/B swapped` note in firmware as currently done for right motor).

---

### 6. Jetson → Black Pill (USB CDC)

| Connection | Notes |
|------------|-------|
| Black Pill USB-C → Jetson USB-A/C port | Single cable — power + data |
| Appears as `/dev/ttyACM0` on Jetson | `ls /dev/ttyACM*` to confirm |
| PA11 (USB_DM) / PA12 (USB_DP) | Used internally — no wiring needed |

> No separate UART wires needed. USB-C cable replaces the old UART + USB-UART adapter.

---

## Complete Wire List

Sorted by Black Pill pin for bench wiring:

| Black Pill Pin | Goes To | Signal |
|----------------|---------|--------|
| 3V3 (left) | GY-521 VCC | IMU power |
| 3V3 (right) | INA219 VCC | Current sensor power |
| 3V3 (right) | Left encoder VCC | Encoder power |
| 3V3 (right) | Right encoder VCC | Encoder power |
| G (left) | GY-521 GND | IMU ground |
| G (left) | GY-521 AD0 | IMU address select (pull LOW) |
| G (right) | INA219 GND | Current sensor ground |
| G (right) | MDD10A Pin 1 | Motor driver logic ground |
| G (right) | Left encoder GND | Encoder ground |
| G (right) | Right encoder GND | Encoder ground |
| G (right) | MDD10A B− | Battery return + shared ground |
| **A0** | MDD10A Pin 4 (PWM1) | Left motor speed |
| **A1** | MDD10A Pin 2 (PWM2) | Right motor speed |
| **B0** | MDD10A Pin 5 (DIR1) | Left motor direction |
| **B1** | MDD10A Pin 3 (DIR2) | Right motor direction |
| **B3** | Left encoder Ch.A | Encoder left A |
| **B4** | Left encoder Ch.B | Encoder left B |
| **B5** | Right encoder Ch.B | Encoder right B |
| **B6** | Right encoder Ch.A | Encoder right A |
| **B8** | GY-521 SCL + INA219 SCL | I2C clock (shared) |
| **B9** | GY-521 SDA + INA219 SDA | I2C data (shared) |
| **SWDIO pad** | ST-Link Pin 1 (SWDIO) | Programmer data |
| **SWSCK pad** | ST-Link Pin 5 (SWCLK) | Programmer clock |
| **GND pad** | ST-Link Pin 3 (GND) | Programmer ground |
| **3V3 pad** | ST-Link Pin 9 (3.3V) | Programmer voltage ref |
| **USB-C** | Jetson USB port | Communication + power |
| **INA219 VIN+** | Battery (+) | Current measurement in |
| **INA219 VIN−** | MDD10A B+ | Current measurement out |

---

## I2C Pull-up Resistors

The INA219 breakout board typically includes 10 kΩ pull-ups on SDA and SCL to 3.3V.
**Verify with a multimeter:** SDA to 3.3V and SCL to 3.3V should each read ~10 kΩ when board is unpowered.
If your INA219 board does NOT have pull-ups, add 4.7 kΩ resistors from SDA→3.3V and SCL→3.3V.

---

## Unused Pins Available for Expansion

| Pin | Alternate Function | Possible Use |
|-----|--------------------|-------------|
| A4 | SPI1_NSS / DAC1 | Chip select |
| A5 | SPI1_SCK | SPI display / IMU |
| A6 | SPI1_MISO | SPI |
| A7 | SPI1_MOSI | SPI |
| A8 | TIM1_CH1 | Motor 3 PWM |
| A9 | TIM1_CH2 | Motor 4 PWM |
| A10 | TIM1_CH3 | Motor 5 PWM |
| B7 | I2C1_SDA (alt) / TIM4_CH2 | Second I2C bus |
| B10 | I2C2_SCL / TIM2_CH3 | Second I2C or Motor 3 encoder |
| B12–B15 | SPI2 / TIM1 | SPI2 peripherals |
| C13 | GPIO (low-current) | Status LED |

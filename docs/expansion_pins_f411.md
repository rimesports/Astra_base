# Black Pill F411 — Expansion Pin Reference
**Board:** WeAct STM32F411CEU6 Black Pill V3.1

---

## Currently Used Pins (do not touch)

| Pin | Function |
|-----|----------|
| A0 | TIM2_CH1 — PWM Motor 1 (left) |
| A1 | TIM2_CH2 — PWM Motor 2 (right) |
| A2 | USART2_TX — debug UART |
| A3 | USART2_RX — debug UART |
| A11 | USB_DM — Jetson USB CDC |
| A12 | USB_DP — Jetson USB CDC |
| A13 | SWDIO — ST-Link (reserved) |
| A14 | SWCLK — ST-Link (reserved) |
| B0 | DIR Motor 1 (left) |
| B1 | DIR Motor 2 (right) |
| B3 | Encoder 1A — EXTI3 |
| B4 | Encoder 1B — EXTI4 |
| B5 | Encoder 2B — EXTI5 |
| B6 | Encoder 2A — EXTI6 |
| B8 | I2C1_SCL — IMU + INA219 |
| B9 | I2C1_SDA — IMU + INA219 |

---

## Free Pins — Full Expansion Table

| Pin | Board Location | Timer Channel | EXTI Line | ADC | Best Use |
|-----|---------------|--------------|-----------|-----|----------|
| **A4** | Right 10 | TIM2_CH4 / TIM3_CH2 | ~~EXTI4~~ blocked by B4 | ADC_IN4 | DIR pin / ADC input |
| **A5** | Right 11 | TIM2_CH1_ETR | ~~EXTI5~~ blocked by B5 | ADC_IN5 | DIR pin / ADC input |
| **A6** | Right 12 | **TIM3_CH1** | ~~EXTI6~~ blocked by B6 | ADC_IN6 | **Servo** / DIR pin |
| **A7** | Right 13 | **TIM3_CH2** | EXTI7 (if B7 unused) | ADC_IN7 | **Servo** / DIR pin |
| **A8** | Left 16 | **TIM1_CH1** | EXTI8 | — | **Motor 3 PWM** |
| **A9** | Left 15 | **TIM1_CH2** | EXTI9 | — | **Motor 4 PWM** |
| **A10** | Left 14 | **TIM1_CH3** | EXTI10 (if B10 unused) | — | **Motor 5 PWM** |
| **A15** | Left 11 | TIM2_CH1 | EXTI15 (if B15 unused) | — | DIR / servo / general IO |
| **B2** | Right 16 | — | EXTI2 | — | ⚠ BOOT1 — avoid |
| **B7** | Left 6 | TIM4_CH2 / **TIM11_CH1** | **EXTI7** | — | **Encoder 3A** / servo |
| **B10** | Right 17 | TIM2_CH3 | **EXTI10** | — | **Encoder 3B** / I2C2_SCL |
| **B12** | Left 20 | TIM1_BKIN | **EXTI12** | — | **Encoder 4A** / DIR / SPI2_NSS |
| **B13** | Left 19 | TIM1_CH1N | **EXTI13** | — | **Encoder 4B** / DIR / SPI2_SCK |
| **B14** | Left 18 | **TIM12_CH1** | **EXTI14** | — | **Encoder 5A** / servo |
| **B15** | Left 17 | **TIM12_CH2** | **EXTI15** | — | **Encoder 5B** / servo |
| **C13** | Right 2 | — | ~~EXTI13~~ blocked by B13 | — | Status LED ⚠ 3mA max |
| **C14** | Right 3 | — | ~~EXTI14~~ blocked by B14 | — | ⚠ LSE crystal — avoid |
| **C15** | Right 4 | — | ~~EXTI15~~ blocked by B15 | — | ⚠ LSE crystal — avoid |

> **EXTI conflict rule:** Each EXTI line 0–15 can only map to ONE port at a time.
> Lines 3–6 are already taken by B3–B6 (encoders). Lines blocked for the Px_n column mean the same-numbered pin on another port is already in use.

---

## Recommended Allocation for 5 Motors + 5 Encoders + Servos

### Motors 3–5 PWM (TIM1)

| Motor | PWM Pin | Timer | DIR Pin | Board Location |
|-------|---------|-------|---------|---------------|
| Motor 3 | **A8** | TIM1_CH1 | **A4** | A8=Left16, A4=Right10 |
| Motor 4 | **A9** | TIM1_CH2 | **A5** | A9=Left15, A5=Right11 |
| Motor 5 | **A10** | TIM1_CH3 | **A6** | A10=Left14, A6=Right12 |

### Encoders 3–5 (quadrature EXTI)

| Encoder | Ch.A Pin | EXTI | Ch.B Pin | EXTI |
|---------|---------|------|---------|------|
| Encoder 3 | **B7** | EXTI7 | **B10** | EXTI10 |
| Encoder 4 | **B12** | EXTI12 | **B13** | EXTI13 |
| Encoder 5 | **B14** | EXTI14 | **B15** | EXTI15 |

### Servos (50 Hz PWM — independent of motor timers)

| Servo | Pin | Timer | Notes |
|-------|-----|-------|-------|
| Servo 1 | **A7** | TIM3_CH2 | Free if encoders use B7/B10/B12–B15 |
| Servo 2 | **A15** | TIM2_CH1 | Shares TIM2 with motor PWM — use TIM2_CH1 remap |
| Servo 3 | **B14** | TIM12_CH1 | Only if encoder 5 not needed |
| Servo 4 | **B15** | TIM12_CH2 | Only if encoder 5 not needed |

> TIM3 (A6/A7) and TIM12 (B14/B15) are completely independent timers — they can run at 50 Hz for servos without affecting TIM1 (motors 3–5) or TIM2 (motors 1–2).

### General IO / Status

| Pin | Use |
|-----|-----|
| **C13** | Status LED (3mA max — use a transistor if driving more load) |
| **A4, A5** | ADC analog input (battery cell voltage, pot, sensor) if not used for DIR |

---

## Scenario Planning

### Scenario A — Current (2 motors, no expansion)
Free pins: A4 A5 A6 A7 A8 A9 A10 A15 B7 B10 B12 B13 B14 B15 C13
**= 15 pins free**

### Scenario B — 5 motors + 5 encoders (full drive system)
Uses: A8 A9 A10 (PWM) + A4 A5 A6 (DIR) + B7 B10 B12 B13 B14 B15 (encoders)
Free remaining: **A7, A15, C13**
- A7 → 1 servo
- A15 → 1 servo or general IO
- C13 → status LED

### Scenario C — 2 motors + 2 encoders + 4 servos (arm robot)
Uses: existing 2 motors + encoders
Servos: A6 (TIM3_CH1), A7 (TIM3_CH2), B14 (TIM12_CH1), B15 (TIM12_CH2)
Free remaining: A4 A5 A8 A9 A10 A15 B7 B10 B12 B13 C13
**= 11 pins free for more expansion**

---

## Bus Expansion Options

### SPI2 — for displays, external sensors, SD card
All 4 SPI2 pins are free **only if encoders 4 & 5 are not needed**:

| Signal | Pin | Conflict |
|--------|-----|---------|
| SPI2_NSS | B12 | Used by Encoder 4A |
| SPI2_SCK | B13 | Used by Encoder 4B |
| SPI2_MISO | B14 | Used by Encoder 5A |
| SPI2_MOSI | B15 | Used by Encoder 5B |

> SPI2 and encoders 4–5 are mutually exclusive on this chip's 48-pin package. Choose one.

### USART1 — second serial port
| Signal | Pin | Conflict |
|--------|-----|---------|
| USART1_TX | A9 | Used by Motor 4 PWM |
| USART1_RX | A10 | Used by Motor 5 PWM |

> USART1 and motors 4–5 are mutually exclusive. If you need a second UART, route motor 4/5 to alternate timer channels and free A9/A10.

### I2C2 — second I2C bus
| Signal | Pin | Note |
|--------|-----|------|
| I2C2_SCL | B10 | Free (used by Encoder 3B if encoder 3 fitted) |
| I2C2_SDA | B3 | **Blocked** — used by Encoder 1A |

> I2C2 SDA has no free pin on UFQFPN48. The only I2C2_SDA pin in this package (PB3) is already used for Encoder 1A. **Stick to one shared I2C1 bus (B8/B9)** and add all sensors to it by address.

---

## Visual Summary

```
Left rail               Right rail
─────────────           ─────────────
3V3  [power]            VB   [tie to 3V3]
G    [power]            C13  [LED only]
5V   [power]            C14  [avoid]
B9   [I2C SDA] ●        C15  [avoid]
B8   [I2C SCL] ●        R    [NRST]
B7   [Enc3A]   ▲        A0   [PWM1]    ●
B6   [Enc2A]   ●        A1   [PWM2]    ●
B5   [Enc2B]   ●        A2   [UART TX] ●
B4   [Enc1B]   ●        A3   [UART RX] ●
B3   [Enc1A]   ●        A4   [DIR3]    ▲
A15  [Servo2]  ▲        A5   [DIR4]    ▲
A12  [USB DP]  ●        A6   [DIR5/Sv] ▲
A11  [USB DM]  ●        A7   [Servo1]  ▲
A10  [PWM5]    ▲        B0   [DIR1]    ●
A9   [PWM4]    ▲        B1   [DIR2]    ●
A8   [PWM3]    ▲        B2   [avoid]
B15  [Enc5B]   ▲        B10  [Enc3B]   ▲
B14  [Enc5A]   ▲        3V3  [power]
B13  [Enc4B]   ▲        G    [power]
B12  [Enc4A]   ▲        5V   [power]

● = currently wired    ▲ = available for expansion
```

# Wiring Reference — STM32 (Nucleo L476RG) ↔ Cytron MDD10A ↔ Yellow Jacket Motor + Encoder

> All pin assignments verified against:
> - STM32L476RG datasheet Table 16/17 (AF mapping) and UM1724 (Nucleo solder bridges)
> - Cytron MDD10A official user manual (connector pinout, logic levels, PWM spec)

---

## Quick Reference: All Pins at a Glance

| Signal | STM32 Pin | Nucleo Connector | Direction | Notes |
|--------|-----------|-----------------|-----------|-------|
| Left Motor PWM | **PA0** | CN8 A0 | OUT → MDD10A PWM1 | TIM2_CH1 AF1, 10 kHz |
| Left Motor DIR | **PC0** | CN8 A5 | OUT → MDD10A DIR1 | GPIO push-pull |
| Right Motor PWM | **PA1** | CN8 A1 | OUT → MDD10A PWM2 | TIM2_CH2 AF1, 10 kHz |
| Right Motor DIR | **PC1** | CN8 A4 | OUT → MDD10A DIR2 | GPIO push-pull |
| Left Encoder A | **PB3** | CN9 D3 | IN ← Motor Left | EXTI3, both edges, pull-up |
| Left Encoder B | **PB4** | CN9 D5 | IN ← Motor Left | EXTI4, both edges, pull-up |
| Right Encoder A | **PB5** | CN9 D4 | IN ← Motor Right | EXTI9_5, both edges, pull-up |
| Right Encoder B | **PB6** | CN9 D10 | IN ← Motor Right | EXTI9_5, both edges, pull-up |
| I2C SCL | **PB8** | CN9 D15 | I2C → BNO055/INA219 | I2C1_SCL AF4, open-drain |
| I2C SDA | **PB9** | CN9 D14 | I2C ↔ BNO055/INA219 | I2C1_SDA AF4, open-drain |
| UART TX | **PA2** | CN10 pin 35 | OUT → Jetson RX | USART2_TX AF7 |
| UART RX | **PA3** | CN10 pin 37 | IN ← Jetson TX | USART2_RX AF7 |

---

## 1. STM32 Nucleo L476RG → Cytron MDD10A

### MDD10A Control Header — Verified Pin Order

**Confirmed from Cytron MDD10A official user manual.** The header reads left-to-right (or top-to-bottom on the board):

```
MDD10A control header (5-pin, verified)
┌─────────────────────────────┐
│  GND  PWM2  DIR2  PWM1  DIR1 │
│   1     2     3     4     5  │
└─────────────────────────────┘
```

> **Note**: Some boards include a 6th VCC pin at the end — leave it unconnected. The MDD10A generates its own logic supply from VIN.

| MDD10A Pin | Signal | Connects To | STM32 |
|-----------|--------|-------------|-------|
| **1** | GND | Nucleo GND (CN8 GND) | — |
| **2** | PWM2 | Nucleo **CN8 A1** | PA1 — Right motor speed |
| **3** | DIR2 | Nucleo **CN8 A4** | PC1 — Right motor direction |
| **4** | PWM1 | Nucleo **CN8 A0** | PA0 — Left motor speed |
| **5** | DIR1 | Nucleo **CN8 A5** | PC0 — Left motor direction |

> **Logic compatibility**: MDD10A logic HIGH threshold is **3.0 V minimum** (from datasheet). STM32 outputs 3.3 V — no level shifter needed.

### MDD10A Control Truth Table (from official manual)

| PWM | DIR | Motor output |
|-----|-----|-------------|
| LOW | X | Both outputs LOW — motor stops |
| HIGH | LOW | Output A HIGH, Output B LOW → forward |
| HIGH | HIGH | Output A LOW, Output B HIGH → reverse |

Both signals are **active-HIGH**. In firmware: positive speed → DIR=HIGH (reverse per truth table), but actual wheel direction depends on how M1A/M1B are connected to the motor terminals. If the wheel spins backward, swap the two motor wires — do not change firmware.

### PWM Parameters (from firmware + verified against MDD10A spec)

| Parameter | Value | MDD10A Spec |
|-----------|-------|-------------|
| Timer | TIM2, Prescaler 7 | — |
| TIM2 clock | 80 MHz / 8 = 10 MHz | — |
| Period (ARR) | 1000 counts | — |
| **PWM frequency** | **10 kHz** | Max **20 kHz** ✓ |
| Duty range | 0–1000 | — |
| Speed → duty mapping | ±100 → 0–1000 | — |

### MDD10A Motor Output Terminals

| MDD10A Terminal | Connects To |
|----------------|-------------|
| **M1A / M1B** | Left Yellow Jacket motor wires |
| **M2A / M2B** | Right Yellow Jacket motor wires |
| **VIN+ / VIN−** | Battery (7.4 V 2S LiPo or 6 V–30 V DC) |

---

## 2. Yellow Jacket Motor Encoder Wiring

### Encoder Connector (6-wire cable from motor)

goBILDA Yellow Jacket motors have a combined motor + encoder cable:

```
6-wire connector (colors may vary by batch)
───────────────────────────────────────────
  RED    Motor power +   → MDD10A M1A/M2A
  BLACK  Motor power −   → MDD10A M1B/M2B
  RED    Encoder VCC     → STM32 3.3V  ← use 3.3V NOT 5V
  BLACK  Encoder GND     → STM32 GND
  GREEN  Encoder Chan A  → STM32 GPIO (EXTI)
  BLUE   Encoder Chan B  → STM32 GPIO (EXTI)
```

### Encoder to STM32 Pin Mapping

| Wire | Left Motor | Right Motor |
|------|-----------|------------|
| Encoder VCC | **3.3V** (CN8 3V3) | **3.3V** (CN8 3V3) |
| Encoder GND | **GND** (CN8 GND) | **GND** (CN8 GND) |
| Channel A | **PB3** → CN9 D3 | **PB5** → CN9 D4 |
| Channel B | **PB4** → CN9 D5 | **PB6** → CN9 D10 |

Internal pull-ups enabled in firmware (`GPIO_PULLUP`) — no external resistors needed.

### Encoder Parameters

| Parameter | Value |
|-----------|-------|
| PPR (motor shaft, before gearbox) | 28 pulses/rev |
| Gear ratio | 19.2 : 1 |
| Quadrature multiplier | 4× (both edges, both channels) |
| **Counts per wheel revolution** | 28 × 19.2 × 4 = **2,150 counts/rev** |
| Decoding | Software EXTI interrupts |

---

## 3. UART — STM32 to Jetson

### Solder Bridge Status (verified from UM1724)

On the Nucleo L476RG, **SB13 and SB14 are CLOSED by default** — this routes PA2/PA3 to the ST-Link Virtual COM Port (VCP), not to the Arduino D0/D1 pins.

**Two options for connecting to Jetson:**

### Option A: USB VCP (recommended — no hardware modification)

The Nucleo's USB cable (CN1, ST-Link side) presents as a serial port on the Jetson:

```
Nucleo CN1 USB ──── USB cable ────► Jetson USB port → /dev/ttyACM0, 115200 8N1
```

No wiring needed. Works out of the box. Use this for development and deployment unless a pure UART connection is required.

### Option B: Hardware UART via Morpho connector CN10

Use the right-side Morpho connector. **Do not** connect the USB cable simultaneously (TX collision).

| Signal | STM32 Pin | CN10 Pin | → Jetson |
|--------|-----------|----------|---------|
| TX | PA2 | **Pin 35** | UART RX pin |
| RX | PA3 | **Pin 37** | UART TX pin |
| GND | — | **Pin 20** (any GND) | GND |

> Jetson hardware UART is 3.3V — STM32 3.3V output connects directly. Do not connect to 5V UART.

> To also expose PA2/PA3 on the Arduino D1/D0 pins, cut SB13/SB14 and close SB62/SB63 (solder bridge modification — not recommended unless needed).

---

## 4. I2C Bus — BNO055 IMU + INA219 Battery Monitor

Both sensors share I2C1. Verified: PB8 = I2C1_SCL (AF4), PB9 = I2C1_SDA (AF4) from STM32L476RG datasheet Table 16.

| Signal | STM32 Pin | Nucleo Pin | Connect To |
|--------|-----------|------------|------------|
| SCL | PB8 | CN9 **D15** | SCL on both BNO055 and INA219 |
| SDA | PB9 | CN9 **D14** | SDA on both BNO055 and INA219 |
| 3.3V | — | CN8 **3V3** | VCC on both sensors |
| GND | — | CN8 **GND** | GND on both sensors |

| Sensor | I2C Address | Address Config |
|--------|------------|----------------|
| BNO055 | 0x28 | ADR pin → GND |
| INA219 | 0x40 | A0, A1 both → GND (default) |

> Pull-up resistors: 4.7 kΩ to 3.3V on SCL and SDA. Most breakout boards include these — verify before adding external resistors to avoid double pull-ups.

---

## 5. Nucleo L476RG Connector Layout

```
                    Nucleo L476RG (top view)
                   ┌─────────────────────────┐
                   │    [USB ST-Link CN1]     │
                   │                         │
        CN9        │                         │  CN8
  (Arduino Left)   │  ┌─────────────────┐    │  (Arduino Right)
   D15/SCL PB8 ────┤  │                 │    ├──── A0  PA0  ──► MDD10A PWM1
   D14/SDA PB9 ────┤  │   STM32L476RG   │    ├──── A1  PA1  ──► MDD10A PWM2
   D13     PA5 ────┤  │                 │    ├──── A2  PA4
   D12     PA6 ────┤  │                 │    ├──── A3  PB0
   D11     PA7 ────┤  │                 │    ├──── A4  PC1  ──► MDD10A DIR2
   D10     PB6 ────┤◄── Right Enc B     │    ├──── A5  PC0  ──► MDD10A DIR1
   D9      PC7 ────┤  │                 │    ├──── 3V3 ──── Encoder VCC
   D8      PA9 ────┤  │                 │    ├──── 5V
   D7      PA8 ────┤  └─────────────────┘    ├──── GND ──── MDD10A GND
   D6      PB10────┤                         │
   D5      PB4 ────┤◄── Left Enc B           │
   D4      PB5 ────┤◄── Right Enc A          │
   D3      PB3 ────┤◄── Left Enc A           │
   D2      PA10────┤                         │
   D1/TX   PA2 ────┤  (routed to ST-Link     │
   D0/RX   PA3 ────┤   VCP by default)       │
                   │                         │
        CN10 (right Morpho)                  │
   pin 35  PA2 ────┤◄──► Jetson UART TX/RX   │
   pin 37  PA3 ────┤     (Option B)          │
                   └─────────────────────────┘
```

---

## 6. Complete Wiring Diagram

```
  ┌────────────────────────────────────────────────────────┐
  │               Cytron MDD10A                            │
  │                                                        │
  │  VIN+  ◄──── Battery +                                │
  │  VIN−  ◄──── Battery −                                │
  │                                                        │
  │  pin 1 GND   ◄──── STM32 GND                          │
  │  pin 2 PWM2  ◄──── STM32 PA1  (A1)  Right speed       │
  │  pin 3 DIR2  ◄──── STM32 PC1  (A4)  Right direction   │
  │  pin 4 PWM1  ◄──── STM32 PA0  (A0)  Left speed        │
  │  pin 5 DIR1  ◄──── STM32 PC0  (A5)  Left direction    │
  │                                                        │
  │  M1A ──────────────────────────────────► Left Motor + │
  │  M1B ──────────────────────────────────► Left Motor − │
  │  M2A ──────────────────────────────────► Right Motor +│
  │  M2B ──────────────────────────────────► Right Motor −│
  └────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Left Yellow Jacket Motor                │
  │  Motor+  ◄── MDD10A M1A                  │
  │  Motor−  ◄── MDD10A M1B                  │
  │  Enc VCC ◄── STM32 3.3V (CN8)            │
  │  Enc GND ◄── STM32 GND                   │
  │  Enc A   ──► STM32 PB3  (D3)            │
  │  Enc B   ──► STM32 PB4  (D5)            │
  └──────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Right Yellow Jacket Motor               │
  │  Motor+  ◄── MDD10A M2A                  │
  │  Motor−  ◄── MDD10A M2B                  │
  │  Enc VCC ◄── STM32 3.3V (CN8)            │
  │  Enc GND ◄── STM32 GND                   │
  │  Enc A   ──► STM32 PB5  (D4)            │
  │  Enc B   ──► STM32 PB6  (D10)           │
  └──────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Jetson                                  │
  │  Option A: USB ◄── Nucleo CN1 USB cable  │
  │            /dev/ttyACM0, 115200 8N1      │
  │                                          │
  │  Option B: UART RX ◄── PA2 (CN10 p35)   │
  │            UART TX ──► PA3 (CN10 p37)   │
  │            GND     ◄── STM32 GND         │
  └──────────────────────────────────────────┘
```

---

## 7. Common Mistakes & Gotchas

| Mistake | Consequence | Prevention |
|---------|------------|------------|
| Powering encoder from 5V | STM32 GPIO damage (absolute max 3.6V on most pins) | Always use **3.3V** for encoder VCC |
| Wiring MDD10A header in wrong order | Motors driven by wrong signals | Pin 1 = GND (the end closest to the edge) — verify with multimeter |
| Forgetting common GND | Random direction glitches, noise-induced motor starts | STM32 GND, MDD10A GND, and battery GND must all share a common wire |
| Using USB + CN10 UART simultaneously | TX collision on PA2 | Use one or the other — not both |
| Motor wired backward relative to encoder | PID goes unstable (positive command → negative feedback) | Swap M1A/M1B physically; do not patch in firmware |
| Double pull-ups on I2C | I2C becomes unreliable at 100 kHz | Check if breakout board already has 4.7 kΩ pull-ups before adding more |
| Using PB3 as JTAG pin | Left encoder A stops working | SWD mode (default in platformio.ini) leaves PB3 free — do not switch to JTAG |

# Wiring Reference — WeAct Black Pill STM32F411CEU6

> All pin assignments verified against:
> - STM32F411CEU6 datasheet Table 9 (AF mapping)
> - WeAct Black Pill V3.1 schematic / pinout diagram
> - Cytron MDD10A official user manual

---

## Quick Reference: All Pins at a Glance

| Signal | STM32 Pin | Direction | Notes |
|--------|-----------|-----------|-------|
| Left Motor PWM | **PA0** | OUT → MDD10A PWM1 | TIM2_CH1 AF1, ~10 kHz |
| Left Motor DIR | **PB0** | OUT → MDD10A DIR1 | GPIO push-pull |
| Right Motor PWM | **PA1** | OUT → MDD10A PWM2 | TIM2_CH2 AF1, ~10 kHz |
| Right Motor DIR | **PB1** | OUT → MDD10A DIR2 | GPIO push-pull |
| Left Encoder A | **PB3** | IN ← Motor Left | EXTI3, both edges, pull-up |
| Left Encoder B | **PB4** | IN ← Motor Left | EXTI4, both edges, pull-up |
| Right Encoder A | **PB6** | IN ← Motor Right | EXTI9_5, both edges, pull-up |
| Right Encoder B | **PB5** | IN ← Motor Right | EXTI9_5, both edges, pull-up |
| I2C SCL | **PB8** | I2C → MPU-6050/INA219 | I2C1_SCL AF4, open-drain |
| I2C SDA | **PB9** | I2C ↔ MPU-6050/INA219 | I2C1_SDA AF4, open-drain |
| USB D− / D+ | **PA11 / PA12** | USB ↔ Jetson | OTG_FS, no driver needed |
| SWD SWDIO | **PA13** | ST-Link | Do not use as GPIO |
| SWD SWDCLK | **PA14** | ST-Link | Do not use as GPIO |
| NRST | Left header pin 16 | ST-Link RST | Required for reliable ST-Link flash |

---

## 1. Black Pill → Cytron MDD10A

### MDD10A Control Header — Verified Pin Order

```
MDD10A control header (5-pin, verified from Cytron official manual)
┌──────────────────────────────┐
│  GND  PWM2  DIR2  PWM1  DIR1 │
│   1     2     3     4     5  │
└──────────────────────────────┘
```

> **Note**: Some boards include a 6th VCC pin — leave it unconnected. The MDD10A generates its own logic supply from VIN.

| MDD10A Pin | Signal | Black Pill Pin | Notes |
|-----------|--------|---------------|-------|
| **1** | GND | GND | Common ground — required |
| **2** | PWM2 | **PA1** (TIM2_CH2) | Right motor speed |
| **3** | DIR2 | **PB1** | Right motor direction |
| **4** | PWM1 | **PA0** (TIM2_CH1) | Left motor speed |
| **5** | DIR1 | **PB0** | Left motor direction |

> **Logic compatibility**: MDD10A logic HIGH threshold is 3.0 V minimum. Black Pill outputs 3.3 V — no level shifter needed.

### MDD10A Control Truth Table

| PWM | DIR | Motor output |
|-----|-----|-------------|
| LOW | X | Both outputs LOW — motor stops |
| HIGH | LOW | Output A HIGH, Output B LOW → forward |
| HIGH | HIGH | Output A LOW, Output B HIGH → reverse |

### PWM Parameters

| Parameter | Value | MDD10A Spec |
|-----------|-------|-------------|
| Timer | TIM2, CH1 (left) / CH2 (right) | — |
| TIM2 clock | 96 MHz SYSCLK, APB1=48 MHz → TIM2 timer = 96 MHz | — |
| Prescaler | 9 (÷10 → 9.6 MHz timer clock) | — |
| Period (ARR) | 1000 counts | — |
| **PWM frequency** | **~9.6 kHz** | Max 20 kHz ✓ |
| Speed → duty mapping | ±100 → 0–1000 | — |

### MDD10A Motor Output Terminals

| MDD10A Terminal | Connects To |
|----------------|-------------|
| **M1A / M1B** | Left motor wires |
| **M2A / M2B** | Right motor wires |
| **VIN+ / VIN−** | Battery (7.4 V 2S LiPo, 6–30 V range) |

---

## 2. Yellow Jacket Motor Encoder Wiring

### Encoder Connector (6-wire cable from motor)

```
6-wire connector
────────────────────────────────────────
  RED    Motor power +   → MDD10A M1A/M2A
  BLACK  Motor power −   → MDD10A M1B/M2B
  RED    Encoder VCC     → Black Pill 3.3V  ← use 3.3V NOT 5V
  BLACK  Encoder GND     → Black Pill GND
  GREEN  Encoder Chan A  → Black Pill GPIO (EXTI)
  BLUE   Encoder Chan B  → Black Pill GPIO (EXTI)
```

### Encoder to Black Pill Pin Mapping

| Wire | Left Motor | Right Motor |
|------|-----------|------------|
| Encoder VCC | **3.3V** | **3.3V** |
| Encoder GND | **GND** | **GND** |
| Channel A | **PB3** | **PB6** |
| Channel B | **PB4** | **PB5** |

> Note: Right encoder A/B are swapped relative to physical labelling so that positive PWM → positive RPM reading. Do not rewire — this is handled in firmware.

Internal pull-ups enabled (`GPIO_PULLUP`) — no external resistors needed.

### Encoder Parameters

| Parameter | Value |
|-----------|-------|
| PPR (motor shaft, before gearbox) | 28 pulses/rev |
| Gear ratio | 19.2 : 1 |
| Quadrature multiplier | 4× (both edges, both channels) |
| **Counts per wheel revolution** | 28 × 19.2 × 4 = **2,150 counts/rev** |
| Decoding | Software EXTI interrupts |

---

## 3. MPU-6050 IMU (I2C1)

| GY-521 Pin | Black Pill Pin | Notes |
|-----------|---------------|-------|
| VCC | **3.3V** | |
| GND | **GND** | |
| SCL | **PB8** | I2C1_SCL AF4 |
| SDA | **PB9** | I2C1_SDA AF4 |
| AD0 | **GND** | Sets I2C address = 0x68 |
| INT | Leave unconnected | Not used in firmware |

> **Pull-ups**: GY-521 breakout includes 4.7 kΩ pull-ups on SCL/SDA. Do not add external pull-ups — double pull-ups work but reduce noise margin.

---

## 4. INA219 Current Sensor (I2C1)

Shares the I2C1 bus with the MPU-6050. Different addresses — no conflict.

| INA219 Pin | Connection | Notes |
|-----------|------------|-------|
| VCC | **3.3V** | |
| GND | **GND** | |
| SCL | **PB8** — shared with MPU-6050 | |
| SDA | **PB9** — shared with MPU-6050 | |
| A0 | GND | I2C address = 0x40 |
| A1 | GND | I2C address = 0x40 |
| VIN+ | Battery positive (before MDD10A) | |
| VIN− | MDD10A VIN+ (after shunt, toward load) | |

**Current sense path:**
```
LiPo (+) ──► INA219 VIN+ ──► INA219 VIN− ──► MDD10A VIN+
LiPo (−) ──────────────────────────────────► MDD10A VIN− (common GND)
```

---

## 5. ST-Link SWD + NRST

| ST-Link V2 Pin | Signal | Black Pill |
|---------------|--------|-----------|
| Pin 7 (SWDIO) | SWDIO | PA13 (SWD pad) |
| Pin 9 (SWCLK) | SWCLK | PA14 (SWD pad) |
| Pin 3 (GND) | GND | GND |
| Pin 1 (3.3V) | VDD ref | 3V3 |
| Pin 15 (NRST) | RST | **Left header pin 16 (NRST)** ← required |

**NRST location on Black Pill left header** (counting from USB-C end):
```
Pin 1  → 5V
Pin 2  → GND
Pin 3  → 3V3
Pin 4  → PB10
Pin 5  → PB2
Pin 6  → PB1   ← DIR Right motor
Pin 7  → PB0   ← DIR Left motor
Pin 8  → PA7
Pin 9  → PA6
Pin 10 → PA5
Pin 11 → PA4
Pin 12 → PA3
Pin 13 → PA2
Pin 14 → PA1   ← PWM Right motor
Pin 15 → PA0   ← PWM Left motor
Pin 16 → NRST  ← ST-Link RST wire here
Pin 17 → PC15
Pin 18 → PC14
Pin 19 → PC13
Pin 20 → VBAT
```

> NRST is required for reliable ST-Link flashing. Without it, the USB ISR can block the SWD connection. See [docs/session_2026-04-22.md](docs/session_2026-04-22.md) for details.

---

## 6. Jetson Communication

Connect Black Pill USB-C → Jetson USB port (any USB-A or USB-C port).

The STM32 enumerates as a USB CDC device:
- **Windows**: `COM6` (USB Serial Device — no driver install needed, uses built-in `usbser.sys`)
- **Linux/Jetson**: `/dev/ttyACM0` (`cdc_acm` driver, built-in on L4T)

---

## 7. Complete Wiring Diagram

```
  ┌──────────────────────────────────────────────────────────┐
  │               Cytron MDD10A                              │
  │                                                          │
  │  VIN+  ◄──── Battery + (through INA219 shunt)           │
  │  VIN−  ◄──── Battery −                                  │
  │                                                          │
  │  pin 1 GND   ◄──── Black Pill GND  (common ground)      │
  │  pin 2 PWM2  ◄──── Black Pill PA1  Right speed          │
  │  pin 3 DIR2  ◄──── Black Pill PB1  Right direction      │
  │  pin 4 PWM1  ◄──── Black Pill PA0  Left speed           │
  │  pin 5 DIR1  ◄──── Black Pill PB0  Left direction       │
  │                                                          │
  │  M1A ────────────────────────────────► Left Motor +     │
  │  M1B ────────────────────────────────► Left Motor −     │
  │  M2A ────────────────────────────────► Right Motor +    │
  │  M2B ────────────────────────────────► Right Motor −    │
  └──────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Left Yellow Jacket Motor                │
  │  Motor+  ◄── MDD10A M1A                 │
  │  Motor−  ◄── MDD10A M1B                 │
  │  Enc VCC ◄── Black Pill 3.3V            │
  │  Enc GND ◄── Black Pill GND             │
  │  Enc A   ──► Black Pill PB3             │
  │  Enc B   ──► Black Pill PB4             │
  └──────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Right Yellow Jacket Motor               │
  │  Motor+  ◄── MDD10A M2A                 │
  │  Motor−  ◄── MDD10A M2B                 │
  │  Enc VCC ◄── Black Pill 3.3V            │
  │  Enc GND ◄── Black Pill GND             │
  │  Enc A   ──► Black Pill PB6             │
  │  Enc B   ──► Black Pill PB5             │
  └──────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  I2C1 Bus (shared)                       │
  │  SCL ──── Black Pill PB8                │
  │  SDA ──── Black Pill PB9                │
  │                                          │
  │  MPU-6050 GY-521                         │
  │    VCC ◄── 3.3V  GND ◄── GND            │
  │    AD0 ◄── GND  (addr 0x68)             │
  │                                          │
  │  INA219                                  │
  │    VCC ◄── 3.3V  GND ◄── GND            │
  │    A0/A1 ◄── GND  (addr 0x40)           │
  │    VIN+ ◄── Battery+                    │
  │    VIN− ──► MDD10A VIN+                 │
  └──────────────────────────────────────────┘

  ┌──────────────────────────────────────────┐
  │  Jetson                                  │
  │  USB-A/C ◄── Black Pill USB-C cable     │
  │  /dev/ttyACM0, 115200 8N1               │
  └──────────────────────────────────────────┘
```

---

## 8. Common Mistakes & Gotchas

| Mistake | Consequence | Prevention |
|---------|------------|------------|
| Powering encoder from 5V | STM32 GPIO damage (absolute max 3.6V) | Always use **3.3V** for encoder VCC |
| Wiring MDD10A header in wrong order | Motors driven by wrong signals | Pin 1 = GND (verify with multimeter before plugging in) |
| Forgetting common GND | Random direction glitches, noise-induced motor starts | Black Pill GND, MDD10A GND, and battery GND must share a common wire |
| Not wiring NRST to ST-Link | ST-Link flashing fails: "init mode failed" | Wire ST-Link pin 15 → Black Pill left header pin 16 |
| Motor wired backward relative to encoder | PID goes unstable (positive command → negative feedback) | Swap M1A/M1B physically; do not patch in firmware |
| Double pull-ups on I2C | I2C unreliable | GY-521 already has 4.7 kΩ pull-ups — do not add more |
| Using PA13/PA14 as GPIO | Breaks SWD debugger connection | These are reserved for SWD |
| Using PB3 as JTAG pin | Left encoder A stops working | SWD mode (default) leaves PB3 free — do not switch to JTAG |

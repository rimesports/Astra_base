# Left Motor & Encoder Bring-Up Log
**Date:** 2026-04-18  
**Board:** STM32 Nucleo-L476RG  
**Hardware:** MDD10A motor driver, Yellow Jacket gear motor, built-in quadrature encoder

---

## Wiring Summary

### MDD10A Motor Driver
| STM32 Pin | Nucleo Header | MDD10A Pin |
|-----------|---------------|------------|
| PA0 (TIM2_CH1) | A0 | PWM1 |
| PC0 | A5 | DIR1 |
| GND | CN6 pin 6 | GND (B-) |

- Battery B+ → MDD10A B+
- Battery B- → MDD10A B- (also connect Nucleo GND here)
- Motor terminals → MDD10A M1A / M1B

### Yellow Jacket Encoder
| Wire | Signal | STM32 Pin | Nucleo Header |
|------|--------|-----------|---------------|
| Red | VCC | 5V | CN10 pin 8 |
| Black | GND | GND | CN6 pin 6 |
| White | Channel A | PB3 | D3 |
| Blue | Channel B | PB4 | D5 |

> Encoder powered at **5V** (not 3.3V) for reliable output drive.

---

## Issues Found & Resolved

### Issue 1 — Telemetry flood on boot
**Symptom:** T:1001 packets spamming the serial console immediately on boot.  
**Cause:** `continuous_fb = true` was the default in `shared_state_init()`.  
**Fix:** Changed default to `false` in [shared_state.cpp](../src/shared_state.cpp).  
**Result:** Boot is now silent. Enable telemetry manually with `{"T":130,"cmd":1}`.

---

### Issue 2 — Motor not responding to commands (PA0 = 0V)
**Symptom:** `{"T":11,"L":100,"R":0}` sent, ACK received, DIR pin (PC0) = 3.3V, but PA0 = 0V and motor not moving.  
**Root cause:** The **3-second heartbeat timeout** was firing before the multimeter probe reached the pin. After timeout, `motor_ctrl_set_speed(0,0)` sets CCR1=0 (PA0=0V) but leaves DIR HIGH (PC0=3.3V), making it look like the command was active.  
**Diagnosis steps:**
1. Added CCR1 readback to T:11 ACK — confirmed `ccr1:390` (correct duty).
2. Extended `HEARTBEAT_TIMEOUT_MS` to 30000 for testing.
3. PA0 then measured ~1.29V (39% × 3.3V = 1.287V ✓).

**Fix:** Restored `HEARTBEAT_TIMEOUT_MS` to 3000 after confirming correct behavior.

---

### Issue 3 — Motor not moving despite correct PWM signal
**Symptom:** PA0 = 1.29V, PC0 = 3.3V, but motor still not moving.  
**Root cause:** **Missing shared GND** between STM32 and MDD10A. DIR1 on MDD10A measured 1.7V instead of 3.3V — voltage drooping due to no return path.  
**Fix:** Added a GND wire from Nucleo GND directly to MDD10A B- terminal alongside battery negative.  
**Result:** DIR1 jumped to 3.3V, motor started spinning.

---

### Issue 4 — Encoder reading ±1.4 RPM (alternating sign)
**Symptom:** L field in T:1001 alternated between +1.4 and -1.4 RPM with motor spinning. Swapping White/Blue wires made no difference.  
**Diagnosis:**
1. Added per-channel EXTI counters (`dbg_exti_left_a`, `dbg_exti_left_b`).
2. Queried with `{"T":200}` while motor running → `la:1510, lb:1510`.
3. **Both channels firing equally** — hardware confirmed working.

**Root cause:** Bug in `update_quadrature()`. The original code read the current A/B state on every interrupt and counted based on state alone. With **both** channels generating interrupts, counts cancelled out every full cycle:

```
Forward cycle:
  A rises → read A=1,B=0 → count++   ✓
  B rises → read A=1,B=1 → count--   ✗ (should be ++)
  A falls → read A=0,B=1 → count++   ✓
  B falls → read A=0,B=0 → count--   ✗ (should be ++)
Net per cycle: +1 -1 +1 -1 = 0
```

**Fix:** Replaced with a proper 4-edge quadrature decoder using a state transition lookup table with previous-state tracking:

```cpp
static const int8_t quad_table[16] = {
   0, -1, +1,  0,   // prev=00
  +1,  0,  0, -1,   // prev=01
  -1,  0,  0, +1,   // prev=10
   0, +1, -1,  0,   // prev=11
};
```

Each encoder (left/right) tracks its own `prev_state`. Index = `(prev << 2) | cur`.  
**Result:** L stabilized at ~27.9 RPM at 39% duty. Consistent and correct.

---

## Final Verified State

| Test | Result |
|------|--------|
| Motor spins forward (`{"T":11,"L":100,"R":0}`) | ✓ |
| Motor spins reverse (`{"T":11,"L":-100,"R":0}`) | ✓ |
| Encoder reads positive RPM forward | ✓ ~27.9 RPM at 39% duty |
| Heartbeat stops motor after 3s of no command | ✓ |
| Telemetry off by default, enable with `start` | ✓ |

---

## Key Lessons

1. **Heartbeat makes multimeter testing hard** — extend `HEARTBEAT_TIMEOUT_MS` temporarily when probing, restore before deploy.
2. **Shared GND is mandatory** — STM32 and MDD10A must share a common ground or signal levels float.
3. **Yellow Jacket encoder needs 5V** — 3.3V may not reliably drive both output channels.
4. **Standard quadrature decode is wrong for dual-channel EXTI** — must track previous state; simple current-state logic cancels to zero with both edges enabled.

---

## Next Steps
- Wire right motor and encoder (same pinout, same process)
- Wire INA219 current sensor (I2C, address 0x40)
- Wire BNO055 IMU (I2C, address 0x28)

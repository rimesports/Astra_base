# PID Tuning Guide - Astra Base (goBILDA Yellow Jacket + MDD10A)

## Hardware context

| Item | Value |
|---|---|
| Motor | goBILDA 5203 Yellow Jacket, 19.2:1, 28 PPR |
| Max RPM (no-load, 12 V) | 312 RPM |
| Max RPM (no-load, 7.4 V nominal) | ~193 RPM -> `MOTOR_MAX_RPM 200.0f` |
| Encoder counts/rev (output shaft) | 28 x 19.2 x 4 = 2150 counts |
| Control loop rate | 50 Hz (20 ms fixed period) |
| PID output range | +/-100 (maps to full motor driver range) |

---

## What changed from the original PID

The original implementation was a textbook PID applied directly to a step setpoint.
Four targeted improvements were made, each solving a specific problem:

### 1. Feedforward (Kf)

**Original behaviour:**  
The integrator carried the entire steady-state load. At a constant setpoint the integrator
would wind up to whatever value kept the motors at the right speed. This meant slow response
after a command change and significant overshoot when the setpoint changed sign.

**What changed:**
```text
output = Kf x setpoint + Kp x error + Ki x integrator + Kd x derivative
```

`Kf x setpoint` provides an open-loop estimate of the output the motor needs at that RPM.
The PID terms then only handle the residual: friction, load disturbances, and model mismatch.
The integrator stays much smaller under steady conditions.

**Effect:** Faster response to setpoint changes, less overshoot, less integral burden.

---

### 2. Setpoint slew rate limiter

**Original behaviour:**  
A step command such as `{"T":1,"L":0.5,"R":0.5}` jumped the RPM setpoint from 0 to 100 RPM
in one cycle. The PID saw a large error immediately and commanded full output.

**What changed:**  
A `profiled_l / profiled_r` variable tracks the actual setpoint sent to the PID. Each cycle
it steps toward the commanded target by at most `ACCEL_LIMIT_RPM_PER_S x dt`:

```text
max_delta = ACCEL_LIMIT_RPM_PER_S x dt
profiled += clamp(target - profiled, +/-max_delta)
```

At 50 Hz with `ACCEL_LIMIT_RPM_PER_S = 400`:
- max delta per cycle = 8 RPM/cycle
- 0 -> 200 RPM ramp takes 25 cycles = 0.5 s

The profiled variables are reset alongside `pid_reset()` on heartbeat timeout and when
switching to direct-PWM mode.

**Effect:** Less wheel slip on hard starts, smoother acceleration, reduced setpoint shock.

---

### 3. Derivative on measurement (not error)

**Original behaviour:**
```text
derivative = (error - prev_error) / dt
```

When a new setpoint arrived, `error` jumped in one cycle and produced derivative kick.

**What changed:**
```text
derivative = -(measurement - prev_measurement) / dt
```

The motor cannot accelerate instantaneously, so `d(measurement)/dt` is smoother than
`d(error)/dt` during a step command.

**Effect:** Derivative can be enabled later without command-step kick. `Kd` is still left at
0 for now because encoder RPM is noisy at 50 Hz.

---

### 4. Saturation-aware anti-windup

**Original behaviour:**  
The controller used only integrator clamping. That limited the integral term to a fixed
numeric range, but it still allowed the integrator to keep accumulating while the motor
command was already pinned at `+100` or `-100`.

In practice that means:
- hard accelerations can build a large stored integral while the motor is already maxed out
- after the wheel finally catches up, the extra integral keeps pushing and causes overshoot
- direction reversals and high-friction starts can feel sticky because the integrator has to
  unwind before the output settles

**What changed:**  
Before integrating, the controller computes the provisional output:

```text
unsat_output = Kf x setpoint + Kp x error + Kd x derivative + Ki x integrator
```

Then:
- if output is saturated high and error still wants to push higher, do not integrate
- if output is saturated low and error still wants to push lower, do not integrate
- otherwise, integrate normally

The integrator is still clamped as a hard numeric safety bound, but it no longer keeps
charging against a saturated actuator.

**Effect:** Less overshoot after hard starts, cleaner recovery from saturation, and better
behaviour when slew-limited commands or load changes pin the motor output at the rails.

---

## Tuning procedure

### Prerequisites

- Right motor and both encoders wired and verified
- MPU-6050 responding
- Robot on the bench with wheels free to spin, or on the floor with space to drive straight

Enable continuous telemetry to watch RPM in real time:
```json
{"T":131,"cmd":1,"interval":20}
```

The `T:1001` response includes `L` and `R` RPM fields. Use `20-50 ms` during tuning if
possible; `100 ms` is convenient for casual viewing but can hide transient overshoot or
oscillation in a 50 Hz control loop.

---

### Step 1 - Set Kf (feedforward), Ki = Kd = 0

In `src/astra_config.h`:
```c
#define PID_KP  0.00f
#define PID_KI  0.00f
#define PID_KD  0.00f
#define PID_KF  0.50f
```

Flash and send:
```json
{"T":1,"L":0.5,"R":0.5}
```

Target RPM is about 100 RPM. Adjust:
- if actual RPM < 100: raise `Kf`
- if actual RPM > 100: lower `Kf`

Typical range: `0.30 - 0.70`

---

### Step 2 - Set Kp

Keep `Ki = Kd = 0`. Raise `Kp` from 0:
```c
#define PID_KP  0.10f
```

Watch for:
- too low: slow correction, residual error
- too high: RPM oscillation around setpoint

Target: settle within about +/-5 RPM with no sustained oscillation.

Typical starting range: `0.20 - 0.50`

---

### Step 3 - Set Ki

With `Kf` and `Kp` set, re-enable `Ki`:
```c
#define PID_KI  0.50f
```

Raise until:
- steady-state error drops near zero
- no slow oscillation develops

If oscillation appears, halve `Ki`.

Typical range: `0.50 - 1.50`

---

### Step 4 - Check Kd

With `Kd = 0`, derivative is inactive. Keep it that way unless:
- you observe oscillation that `Kp` reduction alone cannot fix
- you add a low-pass filter on RPM first

If you do add `Kd`, start at `0.005 - 0.02`.

---

### Step 5 - Check yaw correction

Command a straight run:
```json
{"T":1,"L":0.3,"R":0.3}
```

If the robot drifts left or right despite equal targets:
- send `{"T":126}` while driving and check `gz` in the T:1002 response; a non-zero gz while
  stationary means the IMU needs calibration (`{"T":160}` with robot flat and still)
- if the robot corrects the wrong way: negate `YAW_CORRECTION_GAIN`
- if drift remains: raise `YAW_CORRECTION_GAIN`
- if it weaves: lower `YAW_CORRECTION_GAIN`

---

### Step 6 - Tune acceleration (slew rate)

Default `ACCEL_LIMIT_RPM_PER_S = 400` ramps 0 -> 200 RPM in 0.5 s.

| Feel | Value |
|---|---|
| Aggressive | 600 - 800 RPM/s |
| Default | 400 RPM/s |
| Gentle | 150 - 250 RPM/s |

Higher values stress the integrator more during ramp-up, so re-check overshoot if you change it.

---

## Quick reference - config constants

All tunable values live in `src/astra_config.h`:

```c
#define MOTOR_MAX_RPM           200.0f

#define PID_KP                  0.30f
#define PID_KI                  0.80f
#define PID_KD                  0.00f
#define PID_KF                  0.50f

#define ACCEL_LIMIT_RPM_PER_S   400.0f

#define YAW_CORRECTION_GAIN     0.40f
#define YAW_STRAIGHT_THRESHOLD  15
```

Notes:
- Left and right motors currently share the same PID gains and feedforward value. If the
  drivetrain proves asymmetric, start by splitting `Kf` per side before splitting all PID gains.
- The controller now uses saturation-aware anti-windup plus a hard integrator clamp.
- Leave `PID_KD = 0` unless RPM is low-pass filtered first; derivative-on-measurement removes
  setpoint kick but does not remove encoder noise.

---

## Diagnostic commands

| Command | Purpose |
|---|---|
| `{"T":200}` | System health: I2C ACK, IMU data, battery voltage, TIM2 status |
| `{"T":127}` | I2C spot-check: MPU-6050 WHO_AM_I + INA219 config register |
| `{"T":126}` | Request IMU snapshot — returns T:1002 with roll/pitch/yaw/gz/ax/ay/az |
| `{"T":160}` | IMU calibration (robot must be flat and stationary, takes ~1 s) |
| `{"T":131,"cmd":1,"interval":20}` | Enable 50 Hz telemetry (T:1001) for tuning |
| `{"T":132,"cmd":1}` | Enable PID debug fields in T:1001 (see table below) |
| `{"T":132,"cmd":0}` | Disable PID debug fields |
| `{"T":130,"cmd":0}` | Disable telemetry entirely |
| `{"T":11,"L":100,"R":100}` | Direct PWM 39% - bypasses PID, verifies motor wiring |
| `{"T":1,"L":0.0,"R":0.0}` | Stop (velocity mode) |

---

## T:132 PID debug fields

When `{"T":132,"cmd":1}` is active, the periodic T:1001 response gains extra fields:

| Field | Meaning |
|---|---|
| `pl` / `pr` | Slew-limited RPM setpoint (profiled) sent to PID — not the raw command |
| `ol` / `or` | PID output before yaw correction (±100). Actual motor command = this ± yaw correction |
| `il` / `ir` | Integrator value (±100 range). Near-zero means feedforward is carrying the load |
| `shl` / `shr` | 1 if output was saturated at the high rail this cycle |
| `sll` / `slr` | 1 if output was saturated at the low rail this cycle |
| `ifl` / `ifr` | 1 if integrator was frozen this cycle (saturation + error pushing further in) |

Typical healthy readings during a steady-speed run:
- `pl`/`pr` close to the commanded setpoint RPM
- `il`/`ir` small (near zero) if `Kf` is tuned correctly
- `shl`/`sll`/`ifl` all 0 at cruise; 1 briefly on hard acceleration starts
- `ol`/`or` close to `pl`/`pr` × `Kf`

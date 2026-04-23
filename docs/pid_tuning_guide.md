# PID Tuning Guide — Astra Base (goBILDA Yellow Jacket + MDD10A)

## Hardware context

| Item | Value |
|---|---|
| Motor | goBILDA 5203 Yellow Jacket, 19.2:1, 28 PPR |
| Max RPM (no-load, 12 V) | 312 RPM |
| Max RPM (no-load, 7.4 V nominal) | ~193 RPM → `MOTOR_MAX_RPM 200.0f` |
| Encoder counts/rev (output shaft) | 28 × 19.2 × 4 = 2150 counts |
| Control loop rate | 50 Hz (20 ms fixed period) |
| PID output range | ±100 (maps to full motor driver range) |

---

## What changed from the original PID

The original implementation was a textbook PID applied directly to a step setpoint.
Three targeted improvements were made, each solving a specific problem:

### 1. Feedforward (Kf)

**Original behaviour:**
The integrator carried the entire steady-state load. At a constant setpoint the integrator would wind up to whatever value kept the motors at the right speed. This meant slow response after a command change (integrator had to charge up from a different operating point) and significant overshoot when the setpoint changed sign.

**What changed:**
```
output = Kf × setpoint + Kp × error + Ki × integrator + Kd × derivative
```

`Kf × setpoint` provides an open-loop estimate of the output the motor needs at that RPM. The PID terms then only handle the residual — friction, load disturbances, and model mismatch. The integrator stays small and near zero under steady conditions.

**Effect:** Faster response to setpoint changes, less overshoot, integrator rarely saturates.

---

### 2. Setpoint slew rate limiter

**Original behaviour:**
A step command (e.g. `{"T":1,"L":0.5,"R":0.5}` after being stopped) jumped the RPM setpoint from 0 to 100 RPM in one cycle. The PID saw a large error immediately and commanded full output. On a real robot this causes wheel slip, sudden yaw disturbances, and integrator kick when the motors cannot follow the step.

**What changed:**
A `profiled_l / profiled_r` variable tracks the actual setpoint sent to the PID. Each cycle it steps toward the commanded target by at most `ACCEL_LIMIT_RPM_PER_S × dt`:

```
max_delta = ACCEL_LIMIT_RPM_PER_S × dt          // RPM per cycle
profiled += clamp(target - profiled, ±max_delta)
```

At 50 Hz with `ACCEL_LIMIT_RPM_PER_S = 400`:
- max delta per cycle = 400 × 0.02 = **8 RPM/cycle**
- 0 → 200 RPM ramp takes **25 cycles = 0.5 seconds**

The profiled variables are reset to 0 alongside `pid_reset()` on heartbeat timeout and when switching to direct-PWM mode, so there is no lag surprise when re-entering velocity control.

**Effect:** No wheel slip on hard starts, smoother acceleration, yaw correction stays effective during ramp-up, reduced integrator kick on direction reversal.

---

### 3. Derivative on measurement (not error)

**Original behaviour:**
```
derivative = (error - prev_error) / dt
```
When a new setpoint arrives, `error` jumps by the full setpoint delta in one cycle. This spike in `Δerror` multiplied by `Kd` produces a large output spike — the "derivative kick." With `Kd = 0` this was harmless, but adding any derivative gain would make commands jittery.

**What changed:**
```
derivative = -(measurement - prev_measurement) / dt
```

The motor cannot accelerate instantaneously, so `Δmeasurement` is always smooth regardless of how large the setpoint step is. The sign is negated because `d(error)/dt = d(setpoint)/dt − d(measurement)/dt`; when the setpoint is held constant the two forms are equivalent, but during a setpoint step the measurement-based form produces zero kick.

**Effect:** Derivative term can now be non-zero without causing jitter on commands. `Kd` is still left at 0 for now because encoder noise at 50 Hz amplifies through the derivative — but the formulation is correct if a filtered derivative is added later.

---

## Tuning procedure

### Prerequisites

- Right motor and both encoders wired and verified (T:127 I2C ACK, encoder counts increment on wheel spin)
- MPU-6050 responding (T:127 `mpu6050_68.ack: true`)
- Robot on the bench with wheels free to spin, or on the floor with space to drive straight

Enable continuous telemetry to watch RPM in real time:
```json
{"T":131,"cmd":1,"interval":100}
```

The T:1001 response includes `L` and `R` fields (RPM). Pipe to a terminal or log to a file.

---

### Step 1 — Set Kf (feedforward), Ki = Kd = 0

In `src/astra_config.h`:
```c
#define PID_KP  0.00f
#define PID_KI  0.00f
#define PID_KD  0.00f
#define PID_KF  0.50f   // starting point
```

Flash and send a constant half-speed command:
```json
{"T":1,"L":0.5,"R":0.5}
```

Observe actual RPM in T:1001. Target RPM = `0.5 × 200 × 200/100 = 100 RPM` (since L=0.5 maps to target_left=100, setpoint=100 RPM after slew ramp).

- If actual RPM < 100: **raise Kf**
- If actual RPM > 100: **lower Kf**

Iterate until actual RPM ≈ setpoint with no feedback correction at all. A good Kf means the motor is close to target open-loop. You will still see some steady-state error — the integrator fixes that in the next step.

Typical range: **0.30 – 0.70** depending on battery voltage and load.

---

### Step 2 — Set Kp (proportional)

Keep `Ki = Kd = 0`. Raise `Kp` from 0:
```c
#define PID_KP  0.10f   // start low
```

Command a constant speed and watch the response:
- Too low: slow to correct when load changes; residual error persists
- Too high: RPM oscillates around setpoint (look for ±10 RPM or more ripple in telemetry)

Target: RPM settles to within ±5 RPM of setpoint with no sustained oscillation. Back off Kp by ~20% from the first sign of oscillation.

Typical starting range: **0.20 – 0.50**

---

### Step 3 — Set Ki (integral)

With Kf and Kp set, re-enable Ki:
```c
#define PID_KI  0.50f   // start low
```

The integrator eliminates the remaining steady-state error. Raise Ki until:
- Steady-state error drops to ~0
- No slow oscillation develops (integrator wind-up symptom: RPM overshoots, undershoots, repeats every few seconds)

If oscillation appears: halve Ki. If the integrator saturates on direction change, it is winding up — lower Ki or the slew rate so the integrator has less to correct.

Typical range: **0.50 – 1.50**

---

### Step 4 — Check Kd (leave at 0 initially)

With `Kd = 0`, the derivative term is inactive. This is correct for now because:
- Encoder counts at 50 Hz produce noisy RPM estimates (differentiated position signal)
- Derivative-on-measurement avoids kick but not noise amplification

Only add Kd if:
- You observe sustained oscillation that Kp reduction alone cannot fix
- You add a low-pass filter on the RPM measurement first

If you do add Kd: start at **0.005 – 0.02** and watch for high-frequency chatter in motor output.

---

### Step 5 — Check yaw correction

Command a straight run with both motors at the same speed:
```json
{"T":1,"L":0.3,"R":0.3}
```

If the robot drifts left or right despite equal targets:
- Check T:1001 `r` (roll) and `gz` fields — if gz is consistently non-zero the yaw correction is active
- If the robot corrects the wrong way: negate `YAW_CORRECTION_GAIN` in `astra_config.h`
- If drift remains after correction: raise `YAW_CORRECTION_GAIN` (default 0.40)
- If correction overcorrects and causes weaving: lower it

---

### Step 6 — Tune acceleration (slew rate)

The default `ACCEL_LIMIT_RPM_PER_S = 400` ramps 0 → 200 RPM in 0.5 s. Adjust to taste:

| Feel | Value |
|---|---|
| Aggressive (competition, open floor) | 600 – 800 RPM/s |
| Default (indoor, good traction) | 400 RPM/s |
| Gentle (slippery surface, heavy payload) | 150 – 250 RPM/s |

Higher values also stress the integrator more at the start of each command — if you raise the slew rate, verify the integrator does not wind up badly during the ramp.

---

## Quick reference — config constants

All tunable values live in `src/astra_config.h`:

```c
#define MOTOR_MAX_RPM           200.0f   // no-load RPM at operating voltage

#define PID_KP                  0.30f    // step 2
#define PID_KI                  0.80f    // step 3
#define PID_KD                  0.00f    // step 4 (leave 0 until needed)
#define PID_KF                  0.50f    // step 1 — tune first

#define ACCEL_LIMIT_RPM_PER_S   400.0f   // step 6 — slew rate

#define YAW_CORRECTION_GAIN     0.40f    // step 5
#define YAW_STRAIGHT_THRESHOLD  15       // |target_L - target_R| below which yaw correction is active
```

---

## Diagnostic commands

| Command | Purpose |
|---|---|
| `{"T":200}` | System health: I2C ACK, IMU data, battery voltage, TIM2 status |
| `{"T":127}` | I2C spot-check: MPU-6050 WHO_AM_I + INA219 config register |
| `{"T":126}` | IMU snapshot: roll/pitch/yaw/gz — verify gz reads near 0 when stationary |
| `{"T":131,"cmd":1,"interval":100}` | Enable 10 Hz telemetry (T:1001 with L/R RPM) |
| `{"T":130,"cmd":0}` | Disable telemetry |
| `{"T":11,"L":100,"R":100}` | Direct PWM 39% — bypasses PID, useful to verify motors spin |
| `{"T":1,"L":0.0,"R":0.0}` | Stop (velocity mode) |

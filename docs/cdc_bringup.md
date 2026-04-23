# Session 2026-04-22 â€” USB CDC + ST-Link Bring-Up

## Summary

Two major systems brought up in this session:

1. **USB CDC virtual COM port** â€” STM32F411 now enumerates as `COM6` on Windows
2. **ST-Link SWD flashing** â€” `pio run --target upload` now works without DFU button dance

Both required multi-step debugging against non-obvious firmware and toolchain issues.

---

## USB CDC Debugging

### Root Causes (in order found)

#### 1. `USBD_MAX_NUM_INTERFACES` define order (warning only)
`usbd_conf.h` defined `USBD_MAX_NUM_INTERFACES 2U` **after** `#include "usbd_def.h"`.
`usbd_def.h` uses `#ifndef` guards, so our value was silently ignored â€” it stayed at 1U.

**Fix:** Moved all `USBD_*` config macros **above** the `#include "usbd_def.h"` line.
CDC requires 2 interfaces (comm + data); the wrong value caused enumeration to abort.

#### 2. Missing EP2 TX FIFO (`CDC_CMD_EP`)
`HAL_PCDEx_SetTxFiFo` was only called for EP0 and EP1.
CDC uses a third endpoint (`EP2 = 0x82`) for control notifications â€” no FIFO = `HAL_PCD_EP_Open` silently fails.

**Fix:** Added `HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x10)`.

#### 3. FIFO overflow â€” OTG FS RAM exceeded
STM32F411 OTG FS has **320 words** (1.25 KB) of FIFO RAM total.
Previous allocation: RX=128 + EP0 TX=64 + EP1 TX=128 + EP2 TX=16 = **336 words â†’ overflow**.
Overflowed FIFO corrupts OTG core register writes â†’ USB D+ never activates â†’ device invisible to host.

**Fix:** Reduced EP0 TX from 64â†’16 words (EP0 max packet = 64 B = 16 words exactly), EP1 TX 128â†’64:

| FIFO | Words | Bytes | Notes |
|------|-------|-------|-------|
| RX (shared) | 128 (0x80) | 512 | All OUT endpoints + SETUP |
| EP0 TX | 16 (0x10) | 64 | Exact EP0 max packet size |
| EP1 TX | 64 (0x40) | 256 | CDC bulk data IN |
| EP2 TX | 16 (0x10) | 64 | CDC command/notification IN |
| **Total** | **224 (0xE0)** | **896** | â‰¤ 320 limit âœ“ |

#### 4. Missing `hpcd_USB_OTG_FS.pData = pdev` (the crash bug)
The HAL PCD callbacks (`HAL_PCD_SetupStageCallback`, `HAL_PCD_ResetCallback`, etc.) all do:
```c
USBD_LL_SetupStage(hpcd->pData, ...);
```
`hpcd->pData` was never set â†’ every USB packet from the host â†’ NULL dereference â†’ HardFault.

Without this, the firmware HardFaulted on the very first USB SETUP packet. With `hpcd->pData` still NULL, the CPU froze in the HardFault handler but USB D+ stayed high â€” Windows saw "Unknown Device". Once fixed, callbacks worked but the FIFO overflow (bug #3) then prevented D+ from activating.

**Fix:** In `USBD_LL_Init`, added:
```c
pdev->pData = &hpcd_USB_OTG_FS;   // USBD_LL_* â†’ HAL_PCD_*
hpcd_USB_OTG_FS.pData = pdev;     // HAL_PCD_*Callback â†’ USBD_LL_*  â† was missing
```

### Result
Board enumerates as `USB Serial Device (COM6)` on Windows 10/11 â€” no driver install needed (built-in `usbser.sys`). Verified with `T:200` sysdiag command:

```json
{"T":200,"tick":1546997,"tasks":4,"i2c_imu":false,"imu_id":"0x00","imu_id_ok":false,
 "imu_data":false,"temp":0.0,"temp_ok":true,"i2c_ina":false,"batt_v":0.00,
 "batt_ok":false,"dir1":1,"dir2":1,"tim2":true}
```
- FreeRTOS running (`tick` advancing), 4 tasks alive, PWM/GPIO OK
- IMU/INA not connected yet (expected)

---

## ST-Link SWD Flashing

### Problem
`pio run --target upload` failed with `init mode failed (unable to connect to the target)`.

**Root cause:** USB ISR (priority 6) fires constantly when USB-C is connected. It blocks the SWD clock-bit-banging window that OpenOCD needs to establish a connection.

### Fix: Wire NRST + VC_CORERESET trick

**Hardware:** Connect ST-Link `RST` pin â†’ Black Pill **left header pin 16 (NRST)**.
Black Pill left header pinout (from USB-C end): 5V, GND, 3V3, PB10, PB2, PB1, PB0, PA7, PA6, PA5, PA4, PA3, PA2, PA1, PA0, **NRST**, PC15, PC14, PC13, VBAT.

**Firmware:** HLA (High-Level Adapter) SWD mode cannot do `reset halt` atomically. Workaround:

1. `reset_config srst_only srst_nogate connect_assert_srst` â€” keeps chip in RESET during SWD connect
2. `halt` â€” catches CPU even if it's mid-ISR or in HardFault from previous session
3. Write `DEMCR = 0x01000001` â€” sets `VC_CORERESET` (halt on first instruction after reset)
4. Write `AIRCR = 0x05FA0004` â€” triggers `SYSRESETREQ` (clean software reset, debug stays up)
5. `halt` â€” CPU halts at reset vector, before any firmware or USB code runs
6. `flash write_image erase` â€” safe to program flash now

**Files changed:**
- `blackpill_stlink_upload.cfg` â€” OpenOCD script implementing the above sequence
- `stlink_flash.bat` â€” thin batch wrapper so PlatformIO can call the script
- `platformio.ini` â€” `upload_command = stlink_flash.bat $SOURCE`

**Result:** `pio run --target upload` flashes in ~13 s, no button press needed.

### Post-flash CDC regression â€” BOOT0 sampling bug

After the first successful ST-Link flash, CDC stopped working. Device Manager showed both `USB Serial Device (COM6)` with Unknown status AND `STM32 Bootloader` â€” indicating the board booted into the system bootloader instead of user firmware.

**Root cause:** The flash script ended with `reset run`, which pulses SRST (hardware reset). The STM32 samples BOOT0 on the rising edge of NRST. If BOOT0 is floating at that moment it reads high â†’ chip boots into system bootloader, not user flash.

**Fix:** Replace `reset run` with `resume`. After flashing, the CPU is already halted at the reset vector (VC_CORERESET caught it there). Simply resuming is sufficient â€” no hardware reset needed, no BOOT0 sampling.

```tcl
# Before (broken):
mww 0xE000EDFC 0x01000000
reset run

# After (fixed):
mww 0xE000EDFC 0x01000000
resume
```

**Result:** COM6 enumerates correctly after every ST-Link flash. No button press needed.

---

## Files Modified This Session

| File | Change |
|------|--------|
| `src/usbd_conf.h` | Moved `USBD_*` defines before `#include "usbd_def.h"` |
| `src/usbd_conf.c` | Added `hpcd->pData = pdev`; fixed FIFO sizes; added EP2 TX FIFO |
| `platformio.ini` | Added `upload_command` pointing to `stlink_flash.bat` |
| `blackpill_stlink_upload.cfg` | New â€” OpenOCD flash script with VC_CORERESET sequence |
| `stlink_flash.bat` | New â€” Windows batch wrapper for PlatformIO upload_command |

---

## Current Hardware State

| Peripheral | Pin(s) | Status |
|------------|--------|--------|
| USB CDC (COM6) | PA11/PA12 | âœ… Working |
| ST-Link SWD | PA13/PA14/NRST | âœ… Working (NRST wired) |
| Left motor PWM/DIR | PA0 / PB0 | âœ… Working |
| Right motor PWM/DIR | PA1 / PB1 | âŒ Not wired to MDD10A yet |
| Left encoder | PB3/PB4 | âœ… Working |
| Right encoder | PB5/PB6 | â¬œ Not yet verified |
| MPU-6050 IMU | PB8/PB9 (I2C1) | â¬œ Not wired yet |
| INA219 current | PB8/PB9 (I2C1) | â¬œ VIN+/VIN- not wired yet |

---

## Open Issues / Next Steps

- [ ] Wire MPU-6050: SDAâ†’PB9, SCLâ†’PB8, VCCâ†’3V3, GNDâ†’GND
- [ ] Wire INA219: same I2C bus; connect VIN+/VIN- across motor rail
- [ ] Wire right motor: MDD10A M2Aâ†’PA1 (PWM2), M2Bâ†’PB1 (DIR2)
- [ ] Verify right encoder (PB5/PB6)
- [ ] Test `T:1` speed control end-to-end with both motors
- [ ] Test `T:126` IMU query after MPU-6050 is wired
- [ ] SWD debug (GDB breakpoints) not yet tested â€” may need further OpenOCD config

---

## Serial Interface Quick Reference

**Port:** COM6, 115200 baud (no parity, 8 data bits, 1 stop)

| Command | Example | Response |
|---------|---------|----------|
| System diag | `{"T":200}` | health JSON with tick, tasks, I2C, motor state |
| Motor speed | `{"T":1,"L":0.5,"R":0.5}` | (none) |
| Direct PWM | `{"T":11,"L":128,"R":128}` | (none) |
| Enable telemetry | `{"T":130,"cmd":1}` | starts T:1001 stream |
| IMU query | `{"T":126}` | T:1002 IMU snapshot |

# Session 2026-04-23 - USB CDC COM6 Recovery

## Summary

This session focused on one issue: the STM32F411 firmware flashed successfully over ST-Link, but the USB CDC serial port did not appear on Windows as `COM6`.

The board's ROM DFU bootloader already worked over the same USB connector, which ruled out the basic USB hardware path and shifted attention to the application firmware, USB middleware glue, and runtime startup sequence.

By the end of the session, the firmware was rebuilt, reflashed, and verified to enumerate correctly on Windows as:

- `USB Serial Device (COM6)`
- `USB\\VID_0483&PID_5740\\3776368F3235`

---

## Symptoms

- ST-Link flashing worked.
- STM32 system bootloader / DFU mode worked over USB.
- Application firmware did not initially show `COM6`.
- At different points in the investigation, Windows showed either nothing useful, or the device failed to enumerate cleanly.

This combination strongly suggested a firmware-side USB CDC issue rather than a broken connector, cable, or missing USB data lines.

---

## Confirmed Root Causes

### 1. USB low-level state was not fully deterministic

The USB PCD handle was being used with too many implicit/default values left in memory. That made the bring-up path fragile and contributed to invalid USB runtime state.

Fixes applied:

- zeroed `hpcd_USB_OTG_FS` before configuration
- explicitly set the important FS device init fields
- checked HAL return values during USB init instead of assuming success

Files:

- [src/usbd_conf.c](/abs/path not available)

### 2. CDC class runtime storage should not depend on the bare-metal heap

The USB middleware was using the generic heap for CDC class state. During investigation, this remained a plausible source of corruption in a small embedded runtime where heap behavior is harder to reason about.

Fix applied:

- switched USB class allocation to a fixed static buffer using `USBD_static_malloc()` / `USBD_static_free()`

Files:

- [src/usbd_conf.h](/abs/path not available)
- [src/usbd_conf.c](/abs/path not available)

### 3. The CDC failure was not just "USB never starts"

Once the stack was instrumented properly, the firmware showed:

- `g_boot_stage = 6`
- `g_usb_stage = 5`
- `g_usb_status = 0`

That proved:

- the application reached USB init
- `USBD_Init`, class registration, interface registration, and `USBD_Start` all completed successfully

The problem was therefore deeper than "serial_init never runs".

### 4. The real debugging blocker was distinguishing apparent crashes from live USB interrupt activity

Early on, the firmware appeared to be dying in `USBD_CtlPrepareRx`, and there were captures that looked like bad `pdev` pointers. After symbol refreshes and live breakpoint tracing, that interpretation turned out to be incomplete.

What was confirmed later:

- the device could be stopped live in `USBD_CtlPrepareRx`
- `r0` really was the correct `hUsbDeviceFS` pointer at function entry
- the stores inside `USBD_CtlPrepareRx` completed successfully
- the USB core registers showed active USB device-mode traffic

That changed the diagnosis from "USB stack instantly explodes" to "USB is alive and the host is talking to it; verify enumeration at the OS level directly".

### 5. The application path had to preserve USB-first startup

During isolation, `main.cpp` was reduced to a minimal USB-only idle loop to remove RTOS/task interactions from the critical path. That helped verify the CDC stack itself.

After CDC enumeration was confirmed, the normal application startup was restored, but with USB brought up first and a short delay before the rest of the runtime initialization.

Fix applied:

- initialize USB CDC before the rest of the peripheral stack
- allow a short stabilization window before motor/I2C/task startup
- restore the normal task-based application path after CDC recovery

Files:

- [src/main.cpp](/abs/path not available)

---

## Debug Strategy

The investigation used a layered approach, moving from assumptions to proof:

### 1. Reduce the system to the smallest USB-only case

The normal FreeRTOS application was temporarily stripped back so the firmware only:

- initialized HAL and clocks
- initialized GPIO / TIM
- initialized shared state
- started USB CDC
- stayed in an idle loop

This isolated CDC enumeration from motor control, I2C devices, telemetry, and task scheduling.

### 2. Add explicit runtime markers

Marker globals were added so the running firmware could report how far it got:

- boot-stage markers in `main.cpp`
- USB-stage and USB-status markers in `serial_cmd.cpp`
- hardfault capture registers in `stm32f4xx_it.cpp`

This let the investigation distinguish between:

- not reaching USB init
- USB init failing
- USB init succeeding but enumeration still failing
- actual hardfaults

### 3. Inspect the MCU live over ST-Link/OpenOCD

Instead of guessing from symptoms, the session repeatedly:

- rebuilt and flashed test firmware
- halted the MCU after reset or after delays
- read RAM markers
- dumped USB state objects
- read RCC / OTG core registers
- placed breakpoints in USB middleware and HAL functions
- single-stepped through `USBD_CtlPrepareRx` and `HAL_PCD_EP_Receive`

This was the turning point: it proved the firmware was often alive in the USB stack, not simply crashing before enumeration.

### 4. Validate enumeration from Windows by VID/PID, not just friendly names

Friendly names in Device Manager were not enough during the middle of the investigation. The final confirmation came from querying Windows directly for:

- `VID_0483&PID_5740`
- `USB Serial Device (COM6)`

That confirmed the device had actually enumerated as the CDC serial port.

---

## Resolution

The working configuration now includes:

- deterministic USB low-level initialization
- fixed static CDC class allocation
- USB CDC startup before the rest of the application
- restored normal firmware startup after CDC validation

Final verified result on Windows:

- `USB Serial Device (COM6)`

---

## Files Changed

- [src/usbd_conf.c](/abs/path not available)
- [src/usbd_conf.h](/abs/path not available)
- [src/serial_cmd.cpp](/abs/path not available)
- [src/stm32f4xx_it.cpp](/abs/path not available)
- [src/main.cpp](/abs/path not available)
- [src/usbd_desc.c](/abs/path not available)

---

## Key Takeaways

- A working STM32 ROM DFU path does not guarantee the application USB CDC stack is correct.
- For STM32 USB debugging, explicit runtime markers and live breakpoint tracing are much more reliable than inferring from Device Manager alone.
- If USB middleware says init succeeded but Windows still does not show the port, inspect OS enumeration by VID/PID and inspect OTG core activity directly.
- Reducing the app to a USB-only idle loop is a very effective way to separate CDC issues from RTOS/peripheral startup noise.

---

## Follow-Up: COM6 Opened But Did Not Respond

After COM6 enumeration was recovered, a second USB CDC issue appeared: Windows showed `USB Serial Device (COM6)`, but the port either hung during open or failed to respond to T-commands from `tools/serial_console.py`.

### Symptoms

- `python tools/serial_console.py --port COM6` hung inside pyserial / Win32 `SetCommState`
- in other attempts, COM6 disappeared briefly after flashing because the MCU had been left halted by the upload flow
- when the port did open, the immediate question was whether the failure was still USB CDC, or the JSON command path behind it

### Root Causes

#### 1. CDC control requests were not implemented correctly

The firmware accepted every CDC control request in `CDC_Control_FS()` but did not actually return line-coding data for `CDC_GET_LINE_CODING`.

That matters on Windows because opening a CDC serial port typically triggers control exchanges such as:

- `SET_LINE_CODING`
- `GET_LINE_CODING`
- `SET_CONTROL_LINE_STATE`

With the callback effectively acting as a no-op, the COM-port open sequence became unreliable.

**Fix applied:**

- added a persistent `USBD_CDC_LineCodingTypeDef`
- implemented proper handling for:
  - `CDC_SET_LINE_CODING`
  - `CDC_GET_LINE_CODING`
  - `CDC_SET_CONTROL_LINE_STATE`
  - `CDC_SEND_BREAK`

File:

- [src/usbd_cdc_if.c](/abs/path not available)

#### 2. The flash/upload flow could leave the MCU halted after programming

After ST-Link upload, OpenOCD left the core halted. In that state Windows could still have stale COM-port state while the firmware itself was not actually running, making USB look dead or inconsistent until a manual reset/run.

**Fix applied:**

- updated the flash helper to issue a post-flash `reset run`

File:

- [tools/flash.ps1](/abs/path not available)

### Debug Strategy

This follow-up used a narrower test loop than the original enumeration work:

1. Patch the CDC control path first, because host-side open failures pointed to class-request handling.
2. Rebuild and flash immediately after each change.
3. Test port-open behavior with a minimal pyserial script before using the interactive console.
4. Query Windows device presence directly to separate "enumerated but unavailable" from "device not present".
5. Halt the MCU over ST-Link and inspect runtime markers again to confirm the firmware was alive after boot.
6. Once port open succeeded, send direct T-commands over pyserial to prove the application command path also worked.

### Final Verification

Verified working after rebuild and flash:

- COM6 opens successfully from pyserial
- `{"T":143}` returns `{"T":143}`
- `{"T":200}` returns live diagnostic JSON
- the updated flash workflow brings the MCU back up automatically after upload

### Final State

The USB CDC stack is now working at both levels:

- enumeration: `USB Serial Device (COM6)`
- runtime serial exchange: T-command request / response working over COM6

---

## Final Addendum: Cable Mix-Up, Rollback Test, and Robust CDC Validation

Late in the session, one major source of confusion turned out not to be firmware at all: the wrong USB cable had been used during part of the retest sequence.

That explained why several later checks showed only stale Windows device entries with no live `COM6`, even though the firmware itself was still running correctly under ST-Link observation.

### What Was Confirmed

- the temporary loss of `COM6` after the CDC hardening work was not sufficient evidence that the new code was broken
- reflashing the reverted/simple CDC version did **not** restore `COM6` while the wrong cable was still in use
- once the correct USB cable was connected again, `COM6` came back and live testing could resume normally

This was an important control check because it ruled out the possibility that the GitHub-inspired CDC changes alone had caused the regression.

### Rollback Result

A rollback to the simpler pre-research CDC transport was tested:

- reverted the larger ring / queue / diagnostics additions
- kept the essential earlier CDC fixes, especially proper line-coding handling
- rebuilt and flashed successfully

Result:

- rollback alone did not restore `COM6` during the bad-cable test window
- therefore the apparent regression was not caused solely by the newer CDC transport code

### Re-Enable and Re-Test of the GitHub-Inspired CDC Version

After the cable issue was corrected, the stronger CDC transport was restored and tested again.

The re-enabled CDC version included:

- larger RX ring buffer
- queued TX path in the CDC layer
- `CDC_SET_CONTROL_LINE_STATE` / port-open tracking
- USB TX/RX queue and drop counters exposed through `T:200`

### Validation Performed

The updated firmware was rebuilt, flashed, and then validated on live `COM6` with:

1. basic request / response checks
2. repeated port open / close cycles
3. sustained telemetry streaming

Verified results:

- `COM6` enumerated and opened successfully
- `{"T":143}` returned `{"T":143}`
- `{"T":200}` returned valid diagnostics including:
  - `usb_cfg:true`
  - `usb_open:true`
  - `usb_tx_drop:0`
  - `usb_rx_drop:0`
- 10 repeated open / close cycles all succeeded
- 50 Hz telemetry for 2 seconds produced 100 packets
- post-telemetry diagnostics still showed:
  - `usb_tx_q:0`
  - `usb_rx_q:0`
  - `usb_tx_drop:0`
  - `usb_rx_drop:0`

### Remaining Rough Edge

The CDC runtime itself now appears stable under the tests performed.

One remaining workflow issue still showed up:

- after flashing over ST-Link, the board did not always return to `COM6` automatically until a clean `reset run` was issued

So the current conclusion is:

- the GitHub-inspired CDC runtime changes are working and improve observability
- live CDC traffic is stable in the tested scenarios
- the remaining rough edge is the post-flash recovery path, not the steady-state CDC transport

---

## Production Readiness Review of the 7 Gaps

From a production firmware standpoint, the 7 implemented gaps are reasonable and directionally correct. They are not "extra features" so much as the kinds of reliability, diagnosability, and field-serviceability work that usually separates a lab prototype from a deployable embedded controller.

### Overall Assessment

- Gap 1, hardware watchdog, is standard production hygiene on an autonomous or semi-autonomous robot.
- Gap 2, explicit fault state tracking, is a good foundation for safe degraded behavior.
- Gap 3, persistent hardfault breadcrumbs, is high-value for root-causing field resets.
- Gap 4, flash-backed configuration, is a normal requirement once tuning moves out of compile time.
- Gap 5, telemetry CRC and sequence counters, is a reasonable integrity/observability upgrade for host-controlled systems.
- Gap 6, heartbeat fault notification, is appropriate because the host should be told when the robot entered failsafe.
- Gap 7, runtime PID get/set, is practical for tuning, service, and controlled field updates.

### Why These Are Reasonable

- They address common production risks directly:
  - silent lockups
  - unrecoverable mystery resets
  - loss of tuned parameters
  - undetected telemetry gaps
  - poor host visibility into safety-state transitions
- They improve both reliability and debuggability without changing the external robot-control model drastically.
- They match normal embedded production priorities: fail safe, preserve evidence, expose health, and support controlled runtime calibration.

### Main Production Caveats

These features are good, but they still require hardening discipline:

- watchdog enable/refresh behavior must be validated around boot, debug, and RTOS scheduling
- runtime parameter writes need bounds checking and clearly defined apply/persist semantics
- flash persistence should remain versioned and corruption-safe
- shared-state updates between tasks should stay race-aware as complexity grows
- protocol evolution should eventually include explicit host/device version compatibility

### Bottom Line

The 7 gaps are a reasonable production roadmap and worth keeping. The work remaining is mostly hardening and validation, not rethinking whether these capabilities belong in the firmware.

---

## Remaining Production Software Gaps

The 7 reliability gaps were a strong step forward, but they do not by themselves make the firmware "production complete". The following software-engineering gaps still remain for the current code structure.

### 1. Firmware and Protocol Versioning

The running firmware should expose:

- firmware version
- protocol version
- board ID / hardware variant
- config schema version
- build identity such as git hash or build timestamp

This is separate from source-control hygiene. It matters because the Jetson and STM32 need an explicit compatibility contract.

### 2. Release / Change Control

Source control is absolutely one of the production gaps, but it should be framed more broadly as release discipline:

- tagged firmware releases
- traceable change history
- rollback points
- documented release procedure
- stable build artifact naming

Git solves part of this, but the production need is broader than "just use version control".

### 3. Automated Test Coverage

The project still lacks a meaningful automated test layer. High-value additions would be:

- JSON command parser tests
- config-store checksum/version tests
- CRC test vectors
- watchdog and fault-path smoke tests
- host-device protocol compatibility tests

Without this, regressions are still caught mostly by manual bring-up and bench validation.

### 4. Shared-State Concurrency Discipline

The firmware currently relies heavily on `g_state` shared across tasks. That is workable at this size, but it is a production risk as the system grows.

Areas to harden later:

- clarify field ownership by task
- identify fields that need atomicity or critical sections
- reduce cross-task write/write coupling
- consider message-passing for larger future features

### 5. Input Validation and Safety Bounds

Several host commands still accept runtime values with minimal enforcement. Production firmware should reject:

- out-of-range PID gains
- invalid intervals
- malformed or incomplete command payloads
- values that could drive unsafe behavior

This should include explicit negative acknowledgements, not silent ignore paths.

### 6. Config Lifecycle Hardening

`config_store` already has magic/version/checksum, which is a solid start. Remaining gaps include:

- migration rules across schema changes
- wear strategy
- fallback behavior for corrupt data
- separation of runtime-only vs persisted fields
- optional dual-copy / last-known-good approach

### 7. Fault Policy and Recovery Rules

The firmware now captures faults, but production behavior should also define:

- which faults are latched
- which faults auto-clear
- what commands are allowed in fault state
- how repeated boot faults are handled
- when the system should remain in degraded mode vs normal mode

### 8. Host-Device Contract Stability

The Jetson-facing JSON protocol is now important enough that it should be treated as a maintained interface.

Production gaps here include:

- stable command/telemetry schema
- explicit deprecation rules
- protocol compatibility testing
- documented required vs optional fields

### 9. Operational Observability

`T:200` is already useful, but production observability would benefit from a more deliberate model:

- reset cause
- reboot counters
- config version
- last fault
- last command time
- last error/event codes

This reduces the need for ad hoc debug instrumentation during field issues.

### 10. Security and Update Governance

Not every project needs strong security immediately, but production firmware should eventually answer:

- who is allowed to change gains
- who is allowed to clear faults
- who is allowed to push firmware updates
- how update authenticity is verified

For now this can remain a planned gap, but it should be explicitly tracked.

### Suggested Priority

If these are tackled later, a practical order would be:

1. firmware/protocol versioning
2. automated tests
3. input validation and config hardening
4. shared-state concurrency cleanup
5. fault policy formalization
6. release/change-control workflow
7. expanded observability and update governance

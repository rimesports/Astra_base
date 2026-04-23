# STM32 Robotics Project — Context & Workstream Notes

## Project Overview

Differential-drive robot base firmware for the **WeAct Black Pill STM32F411CEU6**.
Migrated from Nucleo L476RG (see [docs/chipset_migration_l476_to_f411.md](docs/chipset_migration_l476_to_f411.md) for the decision log).
The board communicates with a Jetson host via **USB CDC** (virtual COM port) using the
Waveshare JSON (T-code) protocol — **no changes are needed on the Jetson side**.

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | STM32F411CEU6 — Cortex-M4F, 96 MHz, 128 KB SRAM, 512 KB Flash, hardware FPU (VFPv4) |
| Board | WeAct Black Pill V3.1 (no onboard debugger) |
| Programmer | External ST-Link V2 via SWD (SWDIO=PA13, SWDCLK=PA14, RST=NRST) |
| Motor driver | Cytron MDD10A — DIR + PWM interface |
| IMU | MPU-6050 GY-521 (I2C1, addr 0x68) — complementary filter for roll/pitch/yaw |
| Battery monitor | INA219 (I2C1, addr 0x40) — voltage and current |
| Encoders | Quadrature, 28 PPR × 19.2:1 gear ratio, decoded via GPIO EXTI interrupts |
| Host link | USB CDC (PA11/PA12 OTG_FS) @ 115200 baud → Windows COM6 / Linux /dev/ttyACM0 |

---

## Build Toolchain

- **PlatformIO** with `framework = stm32cube` (HAL-based, not Arduino)
- **ARM GCC** via PlatformIO toolchain-gccarmnoneeabi
- **FreeRTOS Kernel 11.x** — cloned into `lib/FreeRTOS/Source/` as a git submodule
- **OpenOCD** for flashing/debugging via ST-Link

### Key build files

| File | Purpose |
|------|---------|
| `platformio.ini` | Build config — board `blackpill_f411ce`, framework, build_flags, upload_command |
| `freertos_flags.py` | SCons extra_script — injects FPU flags into LINKFLAGS (ststm32 platform does not forward build_flags to the linker step) |
| `stlink_flash.bat` | Windows batch wrapper — called by PlatformIO upload_command |
| `blackpill_stlink_upload.cfg` | OpenOCD script — VC_CORERESET trick for reliable HLA SWD flash |
| `lib/FreeRTOS/library.json` | PlatformIO library manifest — controls which FreeRTOS sources compile |

---

## FreeRTOS Integration

### Port

ARM Cortex-M4F port with hardware FPU: `lib/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c`
Heap allocator: `heap_4` (coalescing, deterministic allocation from a single static array)

### FPU ABI — the key gotcha

The ststm32 platform compiles HAL objects with hard-float ABI (`-mfloat-abi=hard`).
FreeRTOS `port.c` refuses to compile unless the project also uses hard-float.
The linker fails if any translation unit's ABI disagrees.

**Three-part fix** (all three are required):

1. **`build_flags`** in `platformio.ini` — adds `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`
   so all `src/` files are compiled with hard-float ABI.

2. **`freertos_flags.py`** — PlatformIO extra_script (pre) that appends the same flags
   to `LINKFLAGS`. The ststm32 platform constructs `LINKFLAGS` from board spec fields,
   not from `build_flags`, so the linker would otherwise get no FPU flags.

3. **`lib/FreeRTOS/library.json` `build.flags`** — injects FPU flags into the library's
   own SCons sub-environment so `port.c` itself is compiled with hard-float.

### ISR wiring (`src/FreeRTOSConfig.h`)

```c
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
```

These `#define`s make `port.c` emit symbols with the CMSIS vector table names directly.
**Do not** define `SVC_Handler` or `PendSV_Handler` anywhere else — port.c owns them.

### SysTick sharing (`src/stm32f4xx_it.cpp`)

```cpp
extern "C" void xPortSysTickHandler(void);

extern "C" void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}
```

HAL needs `HAL_IncTick()` for its internal millisecond counter.
FreeRTOS needs `xPortSysTickHandler()` to drive the tick.
Both run from the same SysTick ISR, guarded so FreeRTOS is only called after `vTaskStartScheduler()`.

### NVIC priority configuration

```c
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15   // 0xF0 — kernel ISRs
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5    // 0x50 — max priority that can use FreeRTOS API
```

ISRs that call FreeRTOS API (e.g. `xQueueSendFromISR`) must have numeric priority ≥ 5.
ISRs with priority < 5 (more urgent) must NOT call any FreeRTOS API.

---

## Task Architecture

| Task | Priority | Stack | Period | Role |
|------|----------|-------|--------|------|
| `control_task` | 9 (highest) | 256 words | 20 ms fixed (`vTaskDelayUntil`) | Read encoders, heartbeat failsafe, apply motor PWM |
| `serial_task` | 6 | 512 words | event-driven (ring buffer poll) | Parse incoming T-code commands, update shared state |
| `telemetry_task` | 2 (lowest) | 512 words | `g_state.fb_interval_ms` (default 200 ms) | Send T:1001 / T:1002 feedback packets to Jetson |

`vTaskStartScheduler()` is called at the end of `main()`. It never returns.

### Safety hooks

```cpp
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *) {
    motor_ctrl_set_speed(0, 0);   // stop motors immediately
    __disable_irq();
    while (1) {}
}

extern "C" void vApplicationMallocFailedHook(void) {
    motor_ctrl_set_speed(0, 0);
    __disable_irq();
    while (1) {}
}
```

---

## Waveshare JSON (T-code) Protocol

All messages are newline-terminated JSON. Same protocol as the ESP32 version — Jetson sees no difference.

### Commands (Jetson → STM32)

| T code | Meaning | Key fields |
|--------|---------|------------|
| `T:1` | Speed control (normalized) | `L` ∈ -1.0..+1.0, `R` ∈ -1.0..+1.0 → scaled ×200, clamped ±100 |
| `T:11` | Direct PWM | `L`, `R` ∈ -255..+255 → scaled to ±100 internally |
| `T:13` | ROS-style velocity | `X` (linear m/s), `Z` (angular rad/s) → converted to L/R |
| `T:126` | IMU snapshot query | Responds immediately with T:1002 |
| `T:127` | I2C device scan | Checks MPU-6050 (0x68) and INA219 (0x40) |
| `T:128` | Full I2C bus sweep | Scans 0x01–0x7F, reports all ACKing addresses |
| `T:130` | Telemetry on/off | `cmd`: 1=on, 0=off |
| `T:131` | Set telemetry interval | `interval` in milliseconds |
| `T:143` | Serial echo | Echoes line back as-is |
| `T:150` | Motor current sweep | Ramps both motors 0→25→50→75→100, reads INA219 at each step |
| `T:160` | IMU calibration | 500 samples, robot must be flat and stationary |
| `T:200` | System self-diagnostic | Tick, task count, I2C ACK, IMU data, battery, GPIO, TIM2 |

### Feedback (STM32 → Jetson)

| T code | Meaning | Fields |
|--------|---------|--------|
| `T:1001` | Base info (periodic) | `L`/`R` speed (RPM), `r`/`p`/`y` attitude, `temp` °C, `v` battery V, `i` current mA |
| `T:1002` | IMU data | `r`/`p`/`y` Euler, `ax`/`ay`/`az` m/s², `gx`/`gy`/`gz` deg/s |

---

## USB CDC Communication

The Black Pill uses the OTG FS peripheral (PA11 = D−, PA12 = D+) as a USB CDC ACM device.
VID = 0x0483, PID = 0x5740 (ST Virtual COM Port) — Windows recognises it with the built-in `usbser.sys` driver.

Key implementation files:
- `src/usbd_conf.h/.c` — HAL PCD configuration, FIFO sizing, two-way `pData` pointer linkage
- `src/usbd_desc.h/.c` — USB device descriptors (VID/PID, manufacturer/product strings)
- `src/usbd_cdc_if.c` — CDC receive callback → ring buffer bridge; `CDC_Transmit_FS`
- `src/serial_cmd.cpp` — ring buffer consumer, line assembly, `serial_send_line`

FIFO allocation (total OTG FS RAM = 320 words):

| FIFO | Words | Bytes | Notes |
|------|-------|-------|-------|
| RX (shared) | 128 | 512 | All OUT endpoints + SETUP |
| EP0 TX | 16 | 64 | Exact EP0 max packet size |
| EP1 TX | 64 | 256 | CDC bulk data IN |
| EP2 TX | 16 | 64 | CDC command/notification IN |
| **Total** | **224** | **896** | ≤ 320 limit ✓ |

---

## SystemClock_Config (STM32F411 @ 96 MHz)

Source: HSI 16 MHz → PLL → SYSCLK 96 MHz + USB 48 MHz

```
PLL: M=8, N=192, P=4, Q=8
  Input  = HSI / M   = 16 / 8   = 2 MHz
  VCO    = Input × N = 2 × 192  = 384 MHz
  SYSCLK = VCO / P   = 384 / 4  = 96 MHz
  USB    = VCO / Q   = 384 / 8  = 48 MHz (exact — required for USB)
```

Flash latency: 3 WS (required for SYSCLK > 90 MHz @ 3.3 V per RM0383 Table 6).

APB1 max = 50 MHz → divide HCLK by 2 → APB1 = 48 MHz.
TIM2 clock = 2 × APB1 = 96 MHz (hardware doubles when APB prescaler ≠ 1).

---

## Source File Map

```
src/
├── FreeRTOSConfig.h       FreeRTOS configuration (clock, heap, priorities, ISR mappings)
├── astra_config.h         All project constants (pins, T-codes, task priorities/stacks, CLAMP)
├── shared_state.h/.cpp    Global robot state struct (speed targets, encoder, IMU, battery, flags)
├── main.h/.cpp            HAL init, SystemClock_Config, peripheral init, task creation, scheduler start
├── stm32f4xx_it.cpp       ISR definitions (SysTick, OTG_FS, EXTI encoder interrupts)
├── usbd_conf.h/.c         USB HAL PCD config — FIFO sizing, endpoint setup, pData linkage
├── usbd_desc.h/.c         USB device descriptors (VID/PID, strings)
├── usbd_cdc_if.c/.h       CDC receive callback → ring buffer; CDC_Transmit_FS
├── motor_ctrl.h/.cpp      PWM + direction GPIO → Cytron MDD10A
├── encoder.h/.cpp         Quadrature encoder tick counting (EXTI-based)
├── pid.h/.cpp             Simple PI velocity controller
├── i2c_bus.h/.cpp         I2C1 init + read/write helpers (owns hi2c1 handle)
├── imu.h/.cpp             MPU-6050 driver + complementary filter (roll/pitch/yaw)
├── ina219.h/.cpp          INA219 battery voltage + current monitor
├── json_cmd.h/.cpp        T-code command parser and feedback packet builder
└── serial_cmd.h/.cpp      USB CDC line reader and sender (ring buffer consumer)
```

---

## Debugging Setup (`.vscode/launch.json`)

Uses **Cortex-Debug** extension with OpenOCD + ST-Link:

```json
{
  "name": "STM32 Debug (ST-Link)",
  "type": "cortex-debug",
  "servertype": "openocd",
  "configFiles": ["interface/stlink.cfg", "target/stm32f4x.cfg"],
  "executable": "${workspaceFolder}/.pio/build/blackpill_f411ce/firmware.elf",
  "svdFile": "${env:USERPROFILE}/.platformio/platforms/ststm32/misc/svd/STM32F411xx.svd",
  "device": "STM32F411CE",
  "rtos": "FreeRTOS"
}
```

SVD file enables peripheral register inspection in the debugger sidebar.
`"rtos": "FreeRTOS"` enables task-aware debugging (view all task stacks + states).

---

## Flash / Upload

```bash
# Build
~/.platformio/penv/Scripts/pio run

# Flash via ST-Link (NRST must be wired — left header pin 16)
~/.platformio/penv/Scripts/pio run --target upload

# DFU fallback (no ST-Link needed)
# Hold BOOT0, tap NRST, release BOOT0, then:
~/.platformio/penv/Scripts/pio run -e dfu --target upload

# Monitor serial (USB CDC)
~/.platformio/penv/Scripts/pio device monitor --port COM6 --baud 115200
```

No button dance needed for ST-Link flashing. The `blackpill_stlink_upload.cfg` script
uses `connect_assert_srst` + VC_CORERESET + SYSRESETREQ to halt the chip reliably
before flashing, then `resume` (not `reset run`) to avoid re-sampling BOOT0.

---

## Known Issues / Pending

- Right motor M2A/M2B: PA1/PB1 not yet connected to MDD10A
- MPU-6050: module not wired to I2C1 (PB8/PB9)
- INA219: VIN+/VIN− not connected to motor power rail
- Right encoder (PB5/PB6): not yet verified
- `parse_json_bool()` in `json_cmd.cpp` is unused — can be removed or used for future T-code extensions

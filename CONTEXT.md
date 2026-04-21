# STM32 Robotics Project — Context & Workstream Notes

## Project Overview

Tennis ball pickup robot firmware for the **Nucleo L476RG** (STM32L476RG).
This is the STM32 port of the same robot that was previously prototyped on ESP32.
The STM32 replaces the ESP32 for its hardware timers and deterministic real-time behavior.
WiFi is removed — the board communicates with a Jetson host via UART using the same
Waveshare JSON (T-code) protocol, so **no changes are needed on the Jetson side**.

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | STM32L476RG — Cortex-M4F, 80 MHz, 128 KB SRAM, 1 MB Flash, hardware FPU (VFPv4) |
| Board | Nucleo L476RG (on-board ST-Link/V2-1) |
| Motor driver | Cytron MDD10A — DIR + PWM interface |
| IMU | BNO055 (I2C1) — 9-DOF sensor fusion, Euler angles, linear accel, gyro, temperature |
| Battery monitor | INA219 (I2C1) — voltage and current |
| Encoders | Quadrature, decoded via GPIO EXTI interrupts |
| Host link | USART2 (PA2 TX / PA3 RX) @ 115200 baud to Jetson |

---

## Build Toolchain

- **PlatformIO** with `framework = stm32cube` (HAL-based, not Arduino)
- **ARM GCC 7.2.1** (`toolchain-gccarmnoneeabi`)
- **FreeRTOS Kernel 11.1.0** — cloned into `lib/FreeRTOS/Source/`
  ```
  git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git lib/FreeRTOS/Source
  ```
- **OpenOCD** for flashing/debugging via ST-Link
- **VS Code extensions**: PlatformIO IDE, Cortex-Debug, C/C++ (IntelliSense), MemoryView, PeripheralViewer, RTOS Views

### Key build files

| File | Purpose |
|------|---------|
| `platformio.ini` | Main build config — board, framework, build_flags, extra_scripts |
| `freertos_flags.py` | SCons extra_script — injects FPU flags into LINKFLAGS (ststm32 platform does not forward build_flags to the linker step) |
| `lib/FreeRTOS/library.json` | PlatformIO library manifest — controls which FreeRTOS sources compile and injects FPU flags into the library sub-environment |

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

### SysTick sharing (`src/stm32l4xx_it.cpp`)

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
| `control_task` | 9 (highest) | 256 words | 10 ms fixed (`vTaskDelayUntil`) | Read encoders, run PID, apply motor PWM |
| `serial_task` | 6 | 512 words | event-driven (`HAL_UART_Receive` 10 ms timeout) | Parse incoming T-code commands, update shared state |
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

All messages are newline-terminated JSON. Same protocol as the ESP32 version.

### Commands (Jetson → STM32)

| T code | Meaning | Key fields |
|--------|---------|------------|
| `T:1` | Speed control (normalized) | `L` ∈ -1.0..+1.0, `R` ∈ -1.0..+1.0 → scaled ×200, clamped ±100 |
| `T:11` | Direct PWM | `L`, `R` ∈ -255..+255 → scaled to ±100 internally |
| `T:13` | ROS-style velocity | `X` (linear m/s), `Z` (angular rad/s) → converted to L/R |
| `T:126` | IMU snapshot query | Responds immediately with T:1002 |
| `T:130` | Telemetry on/off | `cmd`: 1=on, 0=off |
| `T:131` | Set telemetry interval | `T` in milliseconds |
| `T:143` | Serial echo | Echoes `cmd` field back as `{"T":143,"cmd":"..."}` |

### Feedback (STM32 → Jetson)

| T code | Meaning | Fields |
|--------|---------|--------|
| `T:1001` | Base info (periodic) | `L`/`R` speed, `lv`/`rv` encoder velocity, `v` battery V, `c` current mA, `t` temp °C, `T` uptime ms |
| `T:1002` | IMU data | `r`/`p`/`y` Euler, `ax`/`ay`/`az` linear accel, `gx`/`gy`/`gz` gyro |

---

## Jetson ↔ STM32 UART Connection

### Decision: Use ST-Link VCP over USB (same approach as ESP32)

The Nucleo L476RG has an **ST-Link chip** on-board that acts as a USB VCP bridge — the
same role that the CH340/CP210x plays on the ESP32 dev board. The path is:

```
STM32 USART2 (PA2/PA3) → ST-Link MCU on Nucleo → USB cable → Jetson USB → /dev/ttyACM0
```

This is functionally identical to what worked on ESP32:

| | ESP32 → Jetson | STM32 → Jetson |
|--|---------------|----------------|
| Bridge chip | CH340 / CP210x | ST-Link VCP |
| Jetson device | `/dev/ttyUSB0` | `/dev/ttyACM0` |
| Kernel driver | `ch341` / `cp210x` | `cdc_acm` (built-in on L4T) |
| Firmware UART | UART0 | USART2 |
| Baud rate | 115200 | 115200 |
| Protocol | Waveshare JSON + `\n` | identical |

**Only change needed on the Jetson side**: update the port from `/dev/ttyUSB0` to `/dev/ttyACM0`.

### Why native GPIO UART was abandoned on ESP32 (and should be avoided here too)

The alternative "natural UART" path would be STM32 PA2/PA3 → wires → Jetson GPIO UART pins
(`/dev/ttyTHS1` etc.). This was tried on the ESP32 and failed. Common causes:

- Jetson `/dev/ttyTHS*` devices require explicit pin-mux activation — not enabled by default
- `/dev/ttyS0` is the OS console — wiring to it corrupts the terminal
- No electrical isolation — ground loops and noise spikes corrupt the UART peripheral
- Sensitive to baud-rate clock drift between two independent oscillators over longer wires
- Some Jetson UART variants run at 1.8V — connecting 3.3V signals can damage the pin

The USB VCP path eliminates all of these: USB handles framing, clock recovery, and error
detection independently of the MCU's oscillator.

### Nucleo USB cable serves three roles

The single USB connector (CN1) carries:

| Role | Active when |
|------|------------|
| STM32 power supply | always |
| ST-Link flash/debug (SWD) | laptop / VS Code connected |
| VCP serial to Jetson | Jetson connected |

**Only one host at a time.** During robot operation the cable goes to the Jetson — laptop
debugging is unavailable. If simultaneous debug + Jetson comms is ever needed, add an external
USB-to-UART adapter (CP2102 / FTDI) on CN10 pins PA2/PA3 for the Jetson, and keep the
ST-Link USB for the laptop.

---

## SystemClock_Config (STM32L476 @ 80 MHz)

Source: HSI 16 MHz → PLL → SYSCLK 80 MHz

```
PLL: M=1, N=20, R=4  →  (16 MHz / 1) × 20 / 4 = 80 MHz
```

**Critical note**: STM32L476 PLL struct does NOT have `PLLRGE`, `PLLVCOSEL`, `PLLFRACN`.
Those are STM32H7/G4-only fields. Using them causes compile errors. Use numeric `1` for `PLLM`,
not the `RCC_PLLM_DIV1` macro (L4-specific name).

---

## Source File Map

```
src/
├── FreeRTOSConfig.h       FreeRTOS configuration (clock, heap, priorities, ISR mappings)
├── astra_config.h         All project constants (pins, T-codes, task priorities/stacks, CLAMP)
├── shared_state.h/.cpp    Global robot state struct (speed targets, encoder, IMU, battery, flags)
├── main.h/.cpp            HAL init, SystemClock_Config, peripheral init, task creation, scheduler start
├── stm32l4xx_it.cpp       ISR definitions (SysTick, EXTI encoder interrupts)
├── motor_ctrl.h/.cpp      PWM + direction GPIO → Cytron MDD10A
├── encoder.h/.cpp         Quadrature encoder tick counting (EXTI-based)
├── pid.h/.cpp             Simple PI velocity controller
├── i2c_bus.h/.cpp         I2C1 init + read/write helpers (owns hi2c1 handle)
├── imu.h/.cpp             BNO055 driver (Euler, linear accel, gyro, temp)
├── ina219.h/.cpp          INA219 battery monitor
├── json_cmd.h/.cpp        T-code command parser and feedback packet builder
└── serial_cmd.h/.cpp      UART receive loop (called from serial_task)
```

---

## Debugging Setup (`.vscode/launch.json`)

Uses **Cortex-Debug** extension with OpenOCD + ST-Link:

```json
{
  "name": "STM32 Debug (ST-Link)",
  "type": "cortex-debug",
  "servertype": "openocd",
  "configFiles": ["interface/stlink.cfg", "target/stm32l4x.cfg"],
  "executable": "${workspaceFolder}/.pio/build/nucleo_l476rg/firmware.elf",
  "svdFile": "${env:USERPROFILE}/.platformio/platforms/ststm32/misc/svd/STM32L476.svd",
  "rtos": "FreeRTOS"
}
```

SVD file enables peripheral register inspection in the debugger sidebar.
`"rtos": "FreeRTOS"` enables task-aware debugging (view all task stacks + states).

---

## Flash / Upload

```bash
# Build
~/.platformio/penv/Scripts/pio.exe run

# Flash via ST-Link
~/.platformio/penv/Scripts/pio.exe run --target upload

# Monitor serial output
~/.platformio/penv/Scripts/pio.exe device monitor --baud 115200
```

**Prerequisite**: STM32CubeProgrammer must be installed for ST-Link USB drivers on Windows.
Download from st.com → STM32CubeProgrammer. OpenOCD (used by PlatformIO) depends on these drivers.

---

## Build Status

Last successful build:

```
RAM:   22.8%  (22,368 / 98,304 bytes)
Flash:  3.0%  (31,352 / 1,048,576 bytes)
```

Known harmless warning: `parse_json_bool` in `json_cmd.cpp` defined but not yet used.

---

## Known Issues / Pending

- **STM32CubeProgrammer not installed** — must be done manually by user from st.com.
  Required to flash hardware. Build and simulation work without it.
- `parse_json_bool()` in `json_cmd.cpp` is unused — can be removed or used for future T-code extensions.

# Firmware Update Design — STM32F411CEU6
**Date:** 2026-04-21
**Transport:** Jetson → STM32 over USB CDC (`/dev/ttyACM0`)

---

## Flash Layout

STM32F411CEU6 has 512 KB flash in non-uniform sectors:

```
Address      Sector   Size    Content
0x08000000   0        16 KB   Bootloader
0x08004000   1        16 KB   Bootloader (continued)
0x08008000   2        16 KB   Boot metadata (flags, CRC, version, boot count)
0x0800C000   3        16 KB   App ACTIVE
0x08010000   4        64 KB   App ACTIVE (continued)  — 80 KB total active slot
0x08020000   5       128 KB   App STAGING
0x08040000   6       128 KB   App STAGING (continued)
0x08060000   7       128 KB   App STAGING (continued) — 384 KB total staging slot
```

- **Bootloader**: 32 KB — no FreeRTOS, minimal HAL, USB CDC receive loop
- **Metadata**: 16 KB — persists boot flags across resets (smallest erasable unit)
- **App ACTIVE**: 80 KB — currently running firmware (40 KB used, 40 KB headroom)
- **App STAGING**: 384 KB — receives new image from Jetson before promotion

---

## Image Descriptor

Every firmware binary embeds a descriptor at a fixed offset from the application base
(ACTIVE_BASE + 0x200, after the 512-byte Cortex-M4 vector table).

```c
// src/fw_descriptor.h
#define APP_DESCRIPTOR_MAGIC  0xA57EC0DE

typedef struct __attribute__((packed)) {
    uint32_t magic;         // APP_DESCRIPTOR_MAGIC — identifies valid image
    uint32_t version;       // major<<24 | minor<<16 | patch
    uint32_t image_size;    // total image size in bytes
    uint32_t crc32;         // CRC32 of entire image (this field = 0 during computation)
    uint32_t timestamp;     // Unix build timestamp
    uint8_t  git_hash[8];   // first 8 bytes of short SHA
    uint8_t  reserved[8];   // pad to 36 bytes
} AppDescriptor_t;

// Placed via linker at ACTIVE_BASE + 0x200
extern const AppDescriptor_t app_descriptor;
```

The Jetson-side update script reads the descriptor from the `.bin` file to extract
version and declared CRC32 before initiating transfer.

---

## Boot Metadata (Sector 2)

Written by bootloader and application. Survives power cycles.

```c
// src/boot_meta.h
#define BOOT_META_MAGIC     0xB007F1A6
#define BOOT_META_BASE      0x08008000
#define BOOT_MAX_ATTEMPTS   3

typedef struct __attribute__((packed)) {
    uint32_t magic;               // BOOT_META_MAGIC — detects uninitialised sector
    uint8_t  staging_valid;       // 1 = staging passed CRC, ready to promote
    uint8_t  boot_count;          // incremented each boot; app resets to 0 via fw_confirm()
    uint8_t  rollback_triggered;  // set when boot_count >= BOOT_MAX_ATTEMPTS
    uint8_t  pad;
    uint32_t active_version;      // version word of currently running image
    uint32_t staging_version;     // version word of staged image
    uint32_t active_crc32;        // declared CRC of active image (from descriptor)
    uint32_t staging_crc32;       // declared CRC of staged image (from descriptor)
    uint8_t  reserved[8];
    uint32_t meta_crc32;          // CRC32 of this struct (this field = 0 during computation)
} BootMeta_t;                     // 40 bytes
```

---

## Bootloader State Machine

```
Power-on / Reset
       │
       ▼
  Read BootMeta from 0x08008000
       │
       ├─ meta_crc32 bad or magic wrong?
       │       └─→ Boot ACTIVE unconditionally (factory default)
       │
       ├─ staging_valid == 1 AND boot_count < BOOT_MAX_ATTEMPTS?
       │       │
       │       ▼
       │   Compute CRC32 of STAGING image (hardware CRC unit)
       │   Compare vs meta.staging_crc32
       │       │
       │       ├─ PASS → erase ACTIVE sectors (3–4)
       │       │          copy STAGING → ACTIVE word-by-word
       │       │          re-verify ACTIVE CRC after copy
       │       │          update meta: staging_valid=0, boot_count=0, swap versions
       │       │          write meta → reboot
       │       │
       │       └─ FAIL → meta.staging_valid = 0, write meta
       │                  fall through to normal boot
       │
       ├─ boot_count >= BOOT_MAX_ATTEMPTS?
       │       └─→ meta.rollback_triggered = 1
       │            meta.staging_valid = 0
       │            meta.boot_count = 0
       │            write meta → boot ACTIVE (last known state)
       │
       └─ Normal boot:
              meta.boot_count++
              write meta
              start IWDG (5-second timeout)
              verify ACTIVE image CRC vs AppDescriptor.crc32
              if bad → increment boot_count, reboot (triggers rollback path)
              jump to ACTIVE application
```

---

## Three-Layer Verification

### Layer 1 — Per-chunk transfer integrity
- Jetson appends CRC16-CCITT to every 256-byte chunk
- STM32 verifies before writing to flash
- Failed chunk: STM32 sends NAK, Jetson retransmits same chunk (max 3 retries)

### Layer 2 — Full image integrity (staging)
- Jetson declares `crc32` and `size` in the start packet
- STM32 accumulates CRC32 using STM32 hardware CRC unit during receive
- After last chunk: compare accumulated vs declared
- Must match before `staging_valid` is set to 1
- Mismatch → staging erased, update aborted

### Layer 3 — Post-promotion boot integrity
- Bootloader re-computes CRC32 of ACTIVE flash after copy
- Compares against `AppDescriptor.crc32` embedded in image
- Mismatch → flash was corrupted during erase/copy → boot_count incremented → eventual rollback

---

## Transfer Protocol

### Phase 1 — Announce
```
Jetson → {"T":300,"size":41984,"crc32":2891156482,"ver":"1.2.0","chunks":164}
STM32  → {"T":300,"ack":true,"chunk_size":256}
          (STM32 erases staging sectors 5–7 in background, ~1.5 s)
STM32  → {"T":300,"ready":true}   sent when erase complete
```

### Phase 2 — Binary chunks
```
Packet format (265 bytes):
  [0xFE]        1 B   start byte
  [seq]         2 B   big-endian sequence number (0-based)
  [len]         2 B   payload length (256 or less for last chunk)
  [data]      256 B   firmware bytes
  [crc16]       2 B   CRC16-CCITT of header + data

ACK  (4 bytes): [0xAC][seq:2B][0x01]
NAK  (4 bytes): [0xAC][seq:2B][0x00]   → Jetson retransmits this chunk
```

### Phase 3 — Verify
```
Jetson → {"T":302}
STM32  → {"T":302,"ok":true,"crc32":2891156482}
          or {"T":302,"ok":false,"got":3491012874}  → abort, Jetson retries from Phase 1
```

### Phase 4 — Commit
```
Jetson → {"T":303}
STM32  → {"T":303,"ack":true,"reboot_ms":500}
          writes staging_valid=1 to metadata
          reboots after 500 ms → bootloader promotes staging → active → new firmware
```

### Phase 5 — Confirmation (new firmware, within 5 s of boot)
```
New app → {"T":304,"ver":"1.2.0","boot_count":1}
Jetson  → logs upgrade success
```
If T:304 never arrives within timeout → IWDG fires → boot_count increments → rollback on 3rd attempt.

### Abort / Revert
```
Jetson → {"T":305}   abort in-progress update (any phase)
Jetson → {"T":306}   revert to staging image (if staging_valid still 1 from previous update)
```

---

## Rollback Safety

| Mechanism | How |
|-----------|-----|
| Boot counter | Bootloader increments on every boot; app resets via `fw_confirm()`; limit = 3 |
| IWDG watchdog | Started by bootloader with 5 s timeout; app refreshes in `control_task` (10 ms); crash → reset → boot_count++ |
| CRC re-check on boot | Bootloader verifies ACTIVE CRC before jump; corruption → rollback path |
| Dual copy retained | STAGING not erased until new image arrives; Jetson can trigger T:306 revert |
| Meta self-check | `meta_crc32` validates metadata struct; corruption → unconditional ACTIVE boot |

---

## CRC32 Implementation

STM32F4 has a **hardware CRC32 unit** (polynomial 0x04C11DB7, same as Ethernet/ZIP):

```c
// Reset and compute CRC32 of a buffer
uint32_t hw_crc32(const uint8_t *data, uint32_t len) {
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    CRC->CR = CRC_CR_RESET;
    for (uint32_t i = 0; i < len / 4; i++) {
        uint32_t word;
        memcpy(&word, data + i * 4, 4);
        CRC->DR = word;
    }
    // Handle remaining bytes (< 4)
    uint32_t rem = len % 4;
    if (rem) {
        uint32_t word = 0;
        memcpy(&word, data + len - rem, rem);
        CRC->DR = word;
    }
    return CRC->DR;
}
```

**Note:** Python's `binascii.crc32()` uses a different bit-reflection convention.
Use `crcmod` on the Jetson side with `poly=0x104C11DB7, initCrc=0xFFFFFFFF, rev=False, xorOut=0`.
Or validate against STM32 output during bringup with a known test vector.

---

## Jetson-Side Script Outline

```python
# fw_update.py
import serial, struct, binascii, time

CHUNK_SIZE = 256
T_START    = 300
T_VERIFY   = 302
T_COMMIT   = 303
T_CONFIRM  = 304

def update(port, bin_path):
    image = open(bin_path, 'rb').read()
    crc32 = compute_crc32(image)           # must match STM32 hw_crc32 convention
    chunks = math.ceil(len(image) / CHUNK_SIZE)

    ser = serial.Serial(port, timeout=5)

    # Phase 1 — announce
    send_json(ser, {"T": T_START, "size": len(image),
                    "crc32": crc32, "chunks": chunks})
    wait_for(ser, lambda r: r.get("ready"))

    # Phase 2 — chunks
    for seq in range(chunks):
        chunk = image[seq*CHUNK_SIZE : (seq+1)*CHUNK_SIZE]
        send_chunk(ser, seq, chunk)        # retries on NAK, max 3 attempts

    # Phase 3 — verify
    send_json(ser, {"T": T_VERIFY})
    resp = wait_json(ser)
    assert resp["ok"], f"CRC mismatch: got {resp['got']:#010x}"

    # Phase 4 — commit
    send_json(ser, {"T": T_COMMIT})
    wait_for(ser, lambda r: r.get("ack"))

    # Phase 5 — wait for confirmation from new firmware
    resp = wait_json(ser, timeout=15)
    assert resp["T"] == T_CONFIRM, "New firmware did not confirm boot"
    print(f"Update successful — running v{format_ver(resp['ver'])}")
```

---

## Linker Script Changes (App)

Add descriptor section at fixed offset from app base:

```ld
/* In app linker script — after vector table */
.fw_descriptor 0x0800C200 :
{
    KEEP(*(.fw_descriptor))
} >FLASH
```

```c
/* In firmware */
const AppDescriptor_t app_descriptor __attribute__((section(".fw_descriptor"))) = {
    .magic      = APP_DESCRIPTOR_MAGIC,
    .version    = (1 << 24) | (2 << 16) | 0,
    .image_size = 0,        /* filled by post-build script */
    .crc32      = 0,        /* filled by post-build script */
    .timestamp  = BUILD_TIMESTAMP,
    .git_hash   = GIT_HASH,
};
```

A post-build Python script fills in `image_size` and `crc32` by patching the `.bin` file.

---

## Implementation Order

1. **`fw_descriptor.h`** — struct definition, linker placement
2. **`boot_meta.h/.cpp`** — read/write metadata with self-CRC, `fw_confirm()` API
3. **`flash_drv.cpp`** — erase staging sectors, write chunks, readback verify
4. **`hw_crc32.cpp`** — hardware CRC unit wrapper
5. **Bootloader project** — separate PlatformIO env, sectors 0–1, no RTOS
6. **App changes** — call `fw_confirm()` after successful init, handle T:300–306
7. **Post-build script** — patch descriptor `image_size` + `crc32` into `.bin`
8. **`fw_update.py`** (Jetson) — full update client with retry logic

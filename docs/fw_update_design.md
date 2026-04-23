# Firmware Update Design - STM32F411CEU6
**Date:** 2026-04-21  
**Transport:** Jetson -> STM32 over USB CDC (`/dev/ttyACM0`)  
**Status:** Design spec for v1 implementation, not yet implemented in this repo

---

## Goal

Allow the Jetson to update the STM32 application firmware in the field without
ST-Link access, while preserving a safe recovery path when transfer, power, or
boot failures occur.

This design uses:
- a **custom flash bootloader** stored in STM32 flash
- the STM32 **ROM bootloader** only as an emergency recovery path
- a **staging image** written by the running application
- a **promotion step** performed by the custom bootloader at reset
- **boot confirmation** from the new application after a healthy startup

This is a **v1 field-update design**, not a full secure OTA system.

---

## Non-Goals For V1

The following are intentionally out of scope for the first implementation:
- delta patches
- image compression
- resumable interrupted transfers
- encrypted firmware payloads
- signed firmware validation in the STM32 bootloader
- true equal-slot A/B booting
- rollback to arbitrary historical versions

V1 supports:
- full-image transfer only
- CRC-protected transfer and image verification
- promotion from STAGING to ACTIVE
- boot-attempt rollback protection

---

## Terminology

- **ROM bootloader**: ST factory bootloader in chip ROM, used for DFU/recovery
- **Custom bootloader**: project-owned first-stage bootloader in flash sectors 0-1
- **ACTIVE image**: currently bootable application image
- **STAGING image**: newly downloaded candidate image
- **Promotion**: copying a verified STAGING image into ACTIVE
- **Healthy boot**: new application booted far enough to prove it is usable
- **Confirmation**: application marks itself accepted after a healthy boot

---

## High-Level Flow

Normal update sequence:

1. Jetson queries the current STM32 version.
2. If update is needed, Jetson announces a new image to the running STM32 app.
3. The STM32 app erases STAGING and receives the new image in chunks.
4. The STM32 app verifies the full staged image CRC and stores update metadata.
5. The STM32 app reboots.
6. The custom bootloader verifies STAGING and promotes it to ACTIVE.
7. The bootloader boots the new ACTIVE image in a pending-confirm state.
8. The new app reaches a healthy state and calls `fw_confirm()`.
9. The app reports success to Jetson.

Failure cases:
- transfer failure -> update session aborted, current ACTIVE image remains unchanged
- invalid staged CRC -> STAGING rejected, ACTIVE unchanged
- crash/hang before confirmation -> boot counter increments until rollback path triggers
- power loss during STAGING write -> ACTIVE unchanged
- power loss during promotion -> no automatic guarantee unless explicitly handled; see the
  "Power-Loss Policy" section below

---

## Flash Layout

STM32F411CEU6 has 512 KB flash in non-uniform sectors:

```text
Address      Sector   Size    Content
0x08000000   0        16 KB   Custom bootloader
0x08004000   1        16 KB   Custom bootloader (continued)
0x08008000   2        16 KB   Boot metadata
0x0800C000   3        16 KB   App ACTIVE
0x08010000   4        64 KB   App ACTIVE (continued)  -> 80 KB total ACTIVE slot
0x08020000   5       128 KB   App STAGING
0x08040000   6       128 KB   App STAGING (continued)
0x08060000   7       128 KB   App STAGING (continued) -> 384 KB total STAGING slot
```

### Implications

- The app can no longer be linked at `0x08000000`; it must be linked at `0x0800C000`.
- The ACTIVE slot is only **80 KB**, so the production application must stay below that limit.
- STAGING is intentionally larger than ACTIVE to make transfer simpler, but promotion still
  copies into the smaller ACTIVE slot.

### App Size Rule

Before commit, STM32 must reject any image where:
- `image_size > ACTIVE_SLOT_SIZE`
- descriptor is missing or invalid
- image CRC does not match descriptor CRC

---

## Power-Loss Policy

This design is **not** a true equal-slot A/B system. Promotion is performed by:

1. verifying STAGING
2. erasing ACTIVE
3. copying STAGING -> ACTIVE
4. verifying ACTIVE

### Important consequence

If power is lost **during STAGING download**, the old ACTIVE image remains intact.

If power is lost **during promotion after ACTIVE erase**, the device may no longer have a
valid bootable application image. In v1, this condition is considered a **manual recovery**
case unless the promotion-state metadata allows safe detection and operator recovery.

### Required metadata state

To make this explicit, metadata must track:
- `promotion_in_progress`

If the bootloader sees `promotion_in_progress = 1` on the next boot, it must:
- avoid assuming ACTIVE is valid
- avoid attempting to boot a partially copied image
- enter a defined recovery behavior

### Recommended v1 recovery behavior

One of these must be chosen and documented in implementation:

Option A: conservative recovery
- mark device unrecoverable by app
- stay in bootloader fail loop
- require ST-Link or ROM DFU recovery

Option B: advanced recovery
- preserve enough metadata to retry promotion from intact STAGING

For v1, **Option A is acceptable** if documented clearly.

---

## Image Descriptor

Every application binary embeds a descriptor at a fixed offset from the application base
(ACTIVE_BASE + `0x200`, after the vector table area).

```c
#define APP_DESCRIPTOR_MAGIC  0xA57EC0DE

typedef struct __attribute__((packed)) {
    uint32_t magic;          // APP_DESCRIPTOR_MAGIC
    uint32_t version;        // major<<24 | minor<<16 | patch
    uint32_t image_size;     // total image size in bytes
    uint32_t crc32;          // CRC32 of the full image, with this field zeroed during calc
    uint32_t timestamp;      // Unix build timestamp
    uint32_t board_id;       // hardware compatibility ID
    uint32_t image_type;     // application image type, e.g. 1 = main app
    uint8_t  git_hash[8];    // first 8 bytes of short SHA
    uint8_t  reserved[8];
} AppDescriptor_t;
```

### Descriptor requirements

- `magic` must match
- `image_size` must be non-zero and <= ACTIVE slot size
- `board_id` must match this product/hardware revision
- `image_type` must match the main application type expected by bootloader
- `crc32` must match full-image CRC using the STM32 CRC convention defined below

### Version policy

V1 should define one of these behaviors:
- allow upgrade only
- allow same-version reinstall
- allow downgrade

Recommended v1 policy:
- allow same-version reinstall
- reject downgrades by default unless a debug/recovery override is explicitly enabled

---

## Boot Metadata

Boot metadata lives in sector 2 and is shared between bootloader and application.

```c
#define BOOT_META_MAGIC     0xB007F1A6
#define BOOT_META_BASE      0x08008000
#define BOOT_MAX_ATTEMPTS   3

typedef struct __attribute__((packed)) {
    uint32_t magic;

    uint8_t  staging_received;      // full image written to STAGING
    uint8_t  staging_verified;      // full staged image CRC passed
    uint8_t  promotion_in_progress; // ACTIVE erase/copy underway
    uint8_t  boot_pending_confirm;  // booting new ACTIVE image, waiting for fw_confirm()

    uint8_t  boot_count;            // incremented on each pending-confirm boot
    uint8_t  rollback_triggered;    // latched when attempts exceeded
    uint8_t  reserved0[2];

    uint32_t active_version;
    uint32_t staging_version;
    uint32_t active_crc32;
    uint32_t staging_crc32;
    uint32_t board_id;

    uint8_t  reserved1[8];
    uint32_t meta_crc32;            // CRC32 of the struct with this field zeroed
} BootMeta_t;
```

### Why richer state is needed

Using only `staging_valid` is ambiguous. The bootloader must be able to distinguish:
- image download completed
- image verified
- promotion started but not finished
- new image booted but not yet accepted

These states make failure recovery deterministic.

### Metadata write strategy

V1 minimum:
- erase sector 2
- write one complete metadata struct
- verify readback

Recommended improvement:
- use two metadata records with sequence counters inside sector 2 so a partial write
  does not destroy the only valid metadata copy

If only a single metadata record is used in v1, that limitation should be documented.

---

## Bootloader State Machine

```text
Power-on / Reset
      |
      v
Read BootMeta
      |
      +-- invalid magic or bad meta CRC?
      |      -> use factory-default state
      |
      +-- promotion_in_progress == 1?
      |      -> enter explicit recovery behavior
      |
      +-- staging_verified == 1?
      |      -> verify STAGING image
      |      -> if pass:
      |            set promotion_in_progress = 1
      |            write metadata
      |            erase ACTIVE
      |            copy STAGING -> ACTIVE
      |            verify ACTIVE CRC
      |            if pass:
      |                clear staging flags
      |                clear promotion_in_progress
      |                set boot_pending_confirm = 1
      |                set boot_count = 0
      |                copy version/CRC ACTIVE <- STAGING
      |                write metadata
      |            else:
      |                recovery behavior
      |
      +-- boot_pending_confirm == 1?
      |      -> boot_count++
      |      -> if boot_count >= BOOT_MAX_ATTEMPTS:
      |             rollback_triggered = 1
      |             boot_pending_confirm = 0
      |             write metadata
      |             recovery behavior
      |
      +-- verify ACTIVE descriptor + CRC
      |      -> if invalid: recovery behavior
      |
      +-- jump to ACTIVE app
```

### Healthy boot confirmation

The application must call `fw_confirm()` only after all of these are true:
- vector table and scheduler are running normally
- USB CDC is initialized and able to communicate
- command handling is active
- motor outputs are in a safe initialized state
- failsafe timing is running

Do **not** confirm at the first line of `main()`.

---

## Three-Layer Verification

### Layer 1 - Per-chunk transfer integrity
- Jetson appends CRC16-CCITT to every chunk
- STM32 verifies before writing to flash
- failed chunk -> NAK
- Jetson retransmits the same chunk
- max retries per chunk: **3**

### Layer 2 - Full staged image integrity
- Jetson declares full-image `size`, `crc32`, `version`, and `board_id` in start packet
- STM32 accumulates CRC32 while receiving or after full write
- after the last chunk, STM32 compares computed CRC32 to declared CRC32
- only then may it set `staging_verified = 1`

### Layer 3 - Post-promotion boot integrity
- bootloader re-validates the ACTIVE descriptor and image CRC before jumping
- if confirmation never arrives, boot attempts are limited by `BOOT_MAX_ATTEMPTS`

---

## CRC32 Convention

STM32F4 hardware CRC uses polynomial `0x04C11DB7` in a non-reflected form.

Implementation target:
- polynomial: `0x04C11DB7`
- init value: `0xFFFFFFFF`
- reflected input/output: **false**
- xorOut: `0x00000000`

### Required test vector

Both Jetson and STM32 implementations must be validated against at least one shared
known-answer test vector before field use.

Example requirement:
- create `crc_test.bin` containing a fixed byte pattern
- record expected STM32 CRC32 in the repo
- Jetson updater must compute the exact same value before release

Do not rely on Python `binascii.crc32()` unless wrapped to match this convention.

---

## Transfer Protocol

### Session policy

V1 does **not** support resuming interrupted sessions.

If the link drops or session times out:
- discard in-progress session state
- restart from `T:300`

### Phase 1 - Announce

Jetson announces an update session:

```json
{"T":300,"session":17,"size":41984,"crc32":2891156482,"ver":16908288,"board":1,"chunks":164}
```

STM32 replies:

```json
{"T":300,"ack":true,"session":17,"chunk_size":256}
```

After STAGING erase completes:

```json
{"T":300,"ready":true,"session":17}
```

### Required start checks

STM32 must reject `T:300` if:
- another update session is active
- size exceeds ACTIVE slot
- `board` is incompatible
- descriptor policy would reject the incoming version

### Phase 2 - Binary chunks

Packet format:

```text
[0xFE]        1 B   start byte
[session]     2 B   session ID, big-endian
[seq]         2 B   chunk sequence number, big-endian
[len]         2 B   payload length (<= 256)
[data]      N B   payload
[crc16]       2 B   CRC16-CCITT of header + payload
```

ACK:

```text
[0xAC][session:2B][seq:2B][0x01]
```

NAK:

```text
[0xAC][session:2B][seq:2B][0x00]
```

### Timeout policy

- per-chunk response timeout: **500 ms**
- max per-chunk retries: **3**
- total idle session timeout on STM32: **5 s**

On session timeout:
- STM32 discards active receive state
- STAGING remains invalid
- Jetson must restart from `T:300`

### Phase 3 - Verify

Jetson sends:

```json
{"T":302,"session":17}
```

STM32 replies:

```json
{"T":302,"session":17,"ok":true,"crc32":2891156482}
```

or

```json
{"T":302,"session":17,"ok":false,"got":3491012874}
```

If verify fails:
- clear staging flags
- update aborted

### Phase 4 - Commit

Jetson sends:

```json
{"T":303,"session":17}
```

STM32 replies:

```json
{"T":303,"session":17,"ack":true,"reboot_ms":500}
```

Then the application:
- stores `staging_received = 1`
- stores `staging_verified = 1`
- stores staged version and CRC
- reboots

### Phase 5 - Confirmation From New App

After successful promotion and healthy boot, the new app sends:

```json
{"T":304,"ver":16908288,"boot_count":1,"confirmed":true}
```

Jetson treats missing `T:304` as update failure and logs it.

---

## Abort / Revert Policy

### V1

Recommended v1 behavior:
- support `T:305` abort only for an active transfer session
- do **not** implement `T:306` revert yet

Reason:
- revert adds state complexity and recovery edge cases before the base updater is proven

### `T:305` abort semantics

If the app receives:

```json
{"T":305,"session":17}
```

it should:
- cancel the active transfer state machine
- leave `staging_verified = 0`
- not schedule promotion
- keep ACTIVE unchanged

---

## Bootloader Implementation Constraints

The custom bootloader must remain minimal:
- no FreeRTOS
- no USB CDC stack
- no sensor initialization
- no motor control logic
- no dynamic allocation

It should contain only:
- metadata read/write
- flash erase/write/copy
- CRC verification
- watchdog / boot-attempt handling
- vector-table jump logic

---

## Jump-To-App Requirements

When jumping to the ACTIVE application:

1. validate the application's initial MSP
2. validate the reset vector is inside application flash
3. set MSP from the application's vector table
4. set `SCB->VTOR = ACTIVE_BASE`
5. branch to the application's reset handler

The rebased application must provide a valid vector table at `ACTIVE_BASE`.

---

## Jetson-Side Updater Responsibilities

The Jetson updater script must:
- query current STM32 version before update
- validate board compatibility before sending
- use the exact STM32 CRC32 convention
- retry chunks on NAK up to the protocol limit
- treat session timeout as fatal and restart from `T:300`
- wait for `T:304` after reboot
- log exact failure phase

Recommended outputs:
- current version
- target version
- session ID
- chunk retry count
- verify result
- confirmation result

---

## Security Notes

V1 provides **integrity**, not authenticity.

CRC protects against:
- line noise
- corruption
- incomplete writes

CRC does **not** protect against:
- wrong-but-valid image
- malicious image
- unauthorized update trigger

Recommended later improvements:
- signed manifest on Jetson
- signed image verified by bootloader
- update authorization token or policy gating

---

## Recommended V1 File Set

Application-side:
- `src/fw_descriptor.h`
- `src/fw_descriptor.cpp`
- `src/boot_meta.h`
- `src/boot_meta.cpp`
- `src/flash_drv.h`
- `src/flash_drv.cpp`
- `src/hw_crc32.h`
- `src/hw_crc32.cpp`
- `src/fw_update.h`
- `src/fw_update.cpp`
- `src/fw_confirm.h`
- `src/fw_confirm.cpp`

Bootloader-side:
- `bootloader/src/main.cpp`
- `bootloader/src/boot_jump.cpp`
- shared or duplicated `boot_meta`, `flash_drv`, `hw_crc32`

Build / tooling:
- bootloader PlatformIO environment
- app linker script rebased to `0x0800C000`
- post-build script to patch descriptor size + CRC
- `tools/fw_update.py`

---

## Implementation Order

1. Define flash map and linker scripts
2. Rebase app to ACTIVE slot
3. Add image descriptor
4. Add CRC wrapper
5. Add flash driver
6. Add metadata read/write and validation
7. Build minimal bootloader that can jump to app
8. Add promotion logic
9. Add app confirmation logic
10. Add app-side update session and transfer handling
11. Add Jetson updater script
12. Add failure testing for power loss, bad CRC, and unconfirmed boot

---

## Open Decisions Before Implementation

These should be finalized before coding starts:

1. **Promotion recovery policy**
   If power fails after ACTIVE erase, is manual recovery acceptable for v1?

2. **Version policy**
   Are downgrades allowed in manufacturing/debug builds only, or never?

3. **Board compatibility**
   What exact `board_id` values should be used now and for future hardware revisions?

4. **Metadata format**
   Is a single metadata record acceptable for v1, or should dual records be implemented now?

5. **Abort command**
   Should `T:305` ship in v1, or should v1 rely only on timeout and restart?

---

## Summary

This design matches the intended STM32 OTA architecture for this project:
- the **running app** receives and stages the update
- the **custom bootloader** verifies and promotes it
- the **new app** confirms healthy boot
- the **Jetson** orchestrates the update and records success/failure

The main tradeoff of this v1 design is that it uses **STAGING -> ACTIVE copy**
instead of a true equal-slot A/B scheme. That keeps the flash map simple but
requires clear handling of promotion-time power loss.

# P3 Fixes — Manual Test & Verification Plan

Run the applicable checklist after each fix iteration.
**Build:** `make build && make flash` before each section.
**Monitor:** `make monitor` — look for log output tagged `app_main`, `app_driver`.

---

## Fix 1 — `make monitor` log rotation

**File:** `Makefile`

### What changed
`monitor` target archives existing `screenlog.0` to `screenlog.YYYYMMDDHHMMSS` before opening a new session. `.gitignore` now covers `screenlog.*`.

### Tests

**1a — First session**
- Run `make monitor`. Connect to device.
- Expected: `screenlog.0` is created.
- Quit screen (`Ctrl-A K`).

**1b — Second session**
- Run `make monitor` again.
- Expected: Previous `screenlog.0` renamed to `screenlog.YYYYMMDDHHMMSS` (check with `ls screenlog.*`).
- New `screenlog.0` created fresh.

**1c — Git status**
- Run `git status`.
- Expected: No `screenlog.*` files appear as untracked.

---

## Fix 2 — `init_normal_mode()` refactor

**File:** `main/app_main.cpp`

### What changed
Extracted `create_switch_endpoints()` and `init_display_post_matter()` as separate static functions. `init_normal_mode()` is now ~25 lines.

### Tests

**2a — Normal boot (commissioned)**
- Boot device in **NORMAL mode** (EXT/GPIO5 floating or HIGH).
- Expected in monitor: "Created N Generic Switch endpoints", "Already commissioned — showing button selector" (if previously commissioned).
- Expected on display: button name / room shown for selected slot.

**2b — Normal boot (not yet commissioned)**
- Erase flash (`make erase`), flash, boot.
- Expected: QR code rendered on e-ink display.
- Expected in monitor: "Matter QR payload: MT:..." log line.

**2c — Endpoint creation errors surface**
- No change in behavior — all `ABORT_APP_ON_FAILURE` macros remain in place.
- If a slot endpoint fails, device still panics with the same error log as before.

---

## Fix 3 — `generate-pairing` Python `while` loop

**File:** `Makefile`

### What changed
`exec('while p in invalid: p=...')` replaced with a plain `while p in invalid: p=...` statement in the Python one-liner.

### Tests

**3a — Basic execution**
- Run `make generate-pairing`.
- Expected: prompted with "Proceed with these changes? [y/N]".
- Type `n` to abort.
- Expected: "Aborted." and exit code 1 (no files modified).

**3b — Generates valid passcode**
- Run `make generate-pairing` and type `y`.
- Expected: `CHIPPairingConfig.h` updated with new discriminator and passcode.
- Verify passcode is not in the invalid set: `{0, 11111111, 22222222, 33333333, 44444444, 55555555, 66666666, 77777777, 88888888, 99999999, 12345678, 87654321}`.

---

## Fix 4 — Remove `app_driver_handle_t` typedef

**File:** `main/app_priv.h`

### What changed
Removed unused `typedef void *app_driver_handle_t;` leftover from esp-matter template.

### Tests

**4a — Build succeeds**
- `make build` completes without errors.
- No references to `app_driver_handle_t` in any source file (verify: `grep -r app_driver_handle_t main/`).

---

## Fix 5 — Remove `LED_BLINK_SLOW_MS`

**File:** `main/app_priv.h`

### What changed
Removed `#define LED_BLINK_SLOW_MS 1000` (defined but never used).

### Tests

**5a — Build succeeds**
- `make build` completes without errors.
- No references to `LED_BLINK_SLOW_MS` remain (verify: `grep -r LED_BLINK_SLOW_MS main/`).

---

## Fix 6 — Remove `led_set()` wrapper

**File:** `main/app_driver.cpp`

### What changed
`static void led_set(bool on)` wrapper removed; all internal call sites now call `app_driver_led_set()` directly.

### Tests

**6a — LED still works in NORMAL mode**
- Boot in NORMAL mode, unconfigured (no WiFi).
- Expected: LED blinks at 2 Hz (fast blink — commissioning mode).
- After commissioning, LED should stop blinking (solid off).

**6b — LED still works in CONFIG mode**
- Boot in CONFIG mode (hold EXT/GPIO5).
- Expected: LED solid ON immediately after boot.

**6c — Factory reset countdown LED sequence**
- In CONFIG mode, hold EXT button for 5+ seconds.
- Expected: LED blinks arming sequence as before (rapid blinks during countdown).

---

## Fix 7 — Remove duplicate `s_endpoint_ids` static array

**File:** `main/app_main.cpp`

### What changed
`static uint16_t s_endpoint_ids[MAX_BUTTONS]` removed from file scope; now a local variable in `init_normal_mode()` (passed to `create_switch_endpoints()`).

### Tests

**7a — Endpoints created correctly**
- Boot in NORMAL mode.
- Expected in monitor: each enabled slot logs "Slot N '...' / '...' → endpoint N".
- Button navigation (UP/DOWN) and MID press still work as expected.

---

## Fix 8 — Standardize constant naming

**Files:** `main/app_main.cpp`, `main/app_driver.cpp`

### What changed
- `k_timeout_seconds` → `kTimeoutSeconds`
- `NVS_NS` → `kNvsNs`
- `NVS_SEL_KEY` → `kNvsSelKey`

### Tests

**8a — Build succeeds**
- `make build` completes without errors.
- No references to old names remain (verify: `grep -r "k_timeout_seconds\|NVS_NS\|NVS_SEL_KEY" main/`).

**8b — NVS selection key still works**
- Boot in NORMAL mode with previously saved button selection.
- Navigate with UP/DOWN, then reboot.
- Expected: device restores the previously selected button on boot (kNvsSelKey used for read/write).

---

## Fix 9 — Bump `label_val[35]` → `label_val[36]`

**File:** `main/app_main.cpp`

### What changed
Buffer size increased from 35 to 36 bytes (one byte margin beyond max-length fields).

### Tests

**9a — Max-length label still fits**
- Configure a slot with: l1a=`"AAAAAAAA"` (8), l1b=`"BBBBBBBB"` (8), room=`"CCCCCCCCCCCCCCCC"` (16).
- Flash and boot in NORMAL mode.
- Expected: no truncation in monitor logs; Apple Home shows the full label.
- Expected label format: `"AAAAAAAA BBBBBBBB CCCCCCCCCCCCCCCC"` (35 chars + null = 36 bytes, fits cleanly).

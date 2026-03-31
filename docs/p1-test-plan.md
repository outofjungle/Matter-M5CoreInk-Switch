# P1 Fixes — Manual Test & Verification Plan

Run the applicable checklist after each fix iteration.
**Build:** `make build && make flash` before each section.
**Monitor:** `make monitor` — look for log output tagged `app_serial`, `app_driver`, `app_main`.

---

## Fix 1 — `send_status()` CBOR map size off-by-one
**File:** `main/app_serial.cpp:116`

### What changed
`cbor_encoder_create_map(&enc, &map, msg ? 3 : 2)` → `msg ? 2 : 1`

### Setup
Device in **CONFIG mode** (hold EXT/GPIO5 at power-on).
Open `web/index.html` in Chrome/Edge and connect.

### Tests

**1a — Success path: `ping` response**
- Click **Connect** in the webapp.
- Expected in browser console log: `[rx cbor] {"status":"ok","mode":"config","fw":"..."}`
- Verify no `[rx decode error]` appears.

**1b — Success path: `read` response**
- Click **Read from Device**.
- Expected: table populates with 16 slot rows, status shows "Read complete."
- No CBOR decode errors in the console log.

**1c — Error path: write a slot with a missing field (manual console test)**
- In browser DevTools console, paste and run:
  ```js
  sendCommand({cmd:'write_slot', slot:0, l1a:'Test', l1b:'', l2:'Room', en:true})
    .then(r => console.log('resp', JSON.stringify(r)))
    .catch(e => console.error('err', e));
  ```
  *(Omit `icon` field to trigger "missing fields" error)*
- Expected response: `{"status":"error","msg":"missing fields"}`
- Before fix this was `{"status":"error"}` with a malformed CBOR map claiming 3 entries; confirm `msg` field is now present.

**1d — Normal write flow**
- Edit a slot name, click **Write to Device**, confirm the modal shows the correct values.
- Watch monitor output for `write_slot N: l1a=...` log lines with no errors.
- Device reboots, webapp disconnects cleanly.

---

## Fix 2 — LED polarity comment
**File:** `main/app_priv.h:23`

### What changed
Comment `// Green LED (G10), active HIGH` → `active LOW`

### Tests
This is a comment-only change. No functional test needed.

**2a — Build passes**
- `make build` completes with no warnings introduced.

**2b — Visual spot check**
- Power on in NORMAL mode, confirm LED behavior is unchanged (blinks fast when uncommissioned, off when commissioned).

---

## Fix 3 — `write_defaults_to_nvs()` NVS error handling
**File:** `main/app_driver.cpp:63–95`

### What changed
Each `nvs_set_str` / `nvs_set_u8` call now checks its return value. On first failure, commit is skipped and function logs an error (leaving the first-boot flag unset so it retries next boot).

### Tests

**3a — Normal first boot**
- Factory reset the device (hold EXT 5 s → ARMED → hold 10 s more).
- Power on in NORMAL mode.
- Monitor output should contain:
  ```
  I app_driver: NVS button defaults written (slots 0-3 enabled)
  I app_driver: Loaded 4 enabled buttons (of 16)
  ```
- No `E app_driver:` lines during init.

**3b — Simulate NVS full (hard to test directly)**
- Not practical to force NVS full in the field.
- Verify by code review that each `nvs_set_*` return value is checked and that `nvs_commit` is only called if all writes succeeded.

---

## Fix 4 — `app_button_nvs_write_slot()` NVS error handling
**File:** `main/app_driver.cpp:195–215`

### What changed
Each `nvs_set_str` / `nvs_set_u8` checks its return value. On failure, `nvs_close` is called and `ESP_FAIL` is returned (causing the serial layer to send `{status:"error",msg:"NVS write failed"}`).

### Tests

**4a — Successful write**
- In CONFIG mode, connect webapp, read, change a slot name, write.
- Monitor: `write_slot N: l1a=... l1b=... l2=... en=... icon=...`
- Webapp: "Write OK — rebooting device…"
- After reboot to NORMAL mode, reconnect in CONFIG mode and read — confirm new values are present.

**4b — Verify error path wiring (code review)**
- Confirm `app_button_nvs_write_slot()` returns `ESP_FAIL` on write error.
- Confirm `handle_write_slot()` in `app_serial.cpp` calls `send_status("error", "NVS write failed")` on `ESP_FAIL` return.
- This is the path that was previously masked (`ESP_OK` returned regardless).

---

## Fix 5 — `write_fixed_label()` skip-if-unchanged
**File:** `main/app_main.cpp:352–370`

### What changed
Before writing each key (`fl-sz`, `fl-k`, `fl-v`), the function reads the existing value. It only calls `nvs_set_*` if the value has changed, skipping `nvs_commit` entirely if all three match.

### Tests

**5a — First boot: labels are written**
- Factory reset, boot in NORMAL mode.
- Monitor: should see `Slot N fixed label 'name'='...' written to NVS` for each enabled slot.

**5b — Subsequent boot: no writes**
- Reboot without changing config (power off/on).
- Monitor: should NOT see `fixed label ... written to NVS` for any slot.
- Confirm in log output (look for absence, not a new log line — you may want to add a `ESP_LOGD` "label unchanged, skipping" line to make this testable).

**5c — After config change: labels update**
- In CONFIG mode, change a slot's name and write.
- Reboot to NORMAL mode.
- Monitor: should see `fixed label ... written to NVS` only for the changed slot.

---

## Fix 6 — Passcode log level
**File:** `main/app_main.cpp:533`

### What changed
`ESP_LOGI(TAG, "Passcode: %d", ...)` → `ESP_LOGD(TAG, "Passcode: %d", ...)` (or line removed)

### Tests

**6a — Default log level (INFO)**
- Boot in NORMAL mode.
- Run `make monitor`.
- Search output for `Passcode:` — **should NOT appear** at INFO level.
- The `=== Commissioning Info ===` block may still appear without the passcode line.

**6b — Debug log level (if changed to LOGD, not removed)**
- Set `CONFIG_LOG_DEFAULT_LEVEL=4` (DEBUG) in `sdkconfig` temporarily.
- Boot and monitor — `Passcode:` should now appear.
- Restore to INFO level after verifying.

---

## End-to-End Smoke Test (run after all fixes)

After all 6 fixes are applied and flashed:

1. **Factory reset** the device.
2. **Boot in CONFIG mode** (hold EXT at power-on).
3. **Connect webapp** — verify ping succeeds and table loads.
4. **Edit slot 0**: set l1a=`Living`, l1b=`Room`, l2=`Lounge`, enable.
5. **Write to device** — verify modal shows correct values, device reboots.
6. **Boot in NORMAL mode** — confirm:
   - LED blinks fast (not yet commissioned).
   - Display shows "Living Room / Lounge" for slot 0.
   - Monitor shows `Loaded N enabled buttons` with no NVS errors.
   - Monitor does NOT show passcode at INFO log level.
7. **Connect webapp again in CONFIG mode** — read back — confirm values match what was written.

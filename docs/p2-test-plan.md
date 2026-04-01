# P2 Fixes — Manual Test & Verification Plan

Run the applicable checklist after building and flashing the P2 changes.
**Build:** `make build && make flash` before starting.
**Monitor:** `make monitor` for device-side tests.
**Webapp:** Open `web/index.html` in Chrome/Edge; device in **CONFIG mode** (hold EXT/GPIO5 at power-on).

---

## Fix 1 — `handle_read()` CBOR overflow check
**File:** `main/app_serial.cpp` — after `cbor_encoder_close_container(&enc, &map)` in `handle_read()`

### What changed
After encoding the full read response, `cbor_encoder_get_extra_bytes_needed()` is checked. If non-zero, sends `{status:"error",msg:"response too large"}` instead of a truncated buffer.

### Tests

**1a — Normal read path**
- Connect webapp, click **Read from Device**.
- Expected: table populates with 16 slots. No errors in browser log.
- Monitor: `I app_serial: Read response sent (N CBOR bytes)` — confirm N is below 1024.

**1b — Overflow not triggered (verify buffer headroom)**
- With default slot names, the read response is ~700 bytes into a 1024-byte buffer.
- After reading, check monitor for the `Read response sent` log line — it should show the byte count, not an error.
- No `E app_serial: Read response truncated` should appear.

---

## Fix 2 — `handle_icons()` CBOR overflow check
**File:** `main/app_serial.cpp` — after `cbor_encoder_close_container(&enc, &map)` in `handle_icons()`

### What changed
Same pattern as Fix 1: checks `cbor_encoder_get_extra_bytes_needed()` after encoding the icons catalog, returns error if truncated.

### Tests

**2a — Normal icons path**
- Connect webapp (icons are fetched automatically during handshake).
- Expected: icon dropdown in the table populates. No errors in browser log.
- Monitor: `I app_serial: Icons response sent (12 icons)` — no `E app_serial: Icons response truncated`.

**2b — Buffer headroom check**
- With 12 icons each having short names (≤10 chars), the icons response should be well under 512 bytes.
- In browser DevTools console:
  ```js
  sendCommand({cmd:'icons'}, 3000).then(r => console.log('icons bytes approx:', JSON.stringify(r).length));
  ```
  Confirm response decoded successfully.

---

## Fix 3 — `handle_write_slot()` rejects oversized strings
**File:** `main/app_serial.cpp` — string copy in `handle_write_slot()`

### What changed
After each `cbor_value_copy_text_string` call for `l1a`, `l1b`, and `l2`, the return value is checked for `CborErrorOutOfMemory`. If the source string was longer than the field's buffer, the device responds `{status:"error",msg:"<field> too long"}` instead of silently truncating.

### Tests

**3a — Oversized l1a rejected**
- In browser DevTools console (device in CONFIG mode, webapp connected):
  ```js
  sendCommand({cmd:'write_slot', slot:0, l1a:'ToolongName', l1b:'', l2:'Room', en:true, icon:0})
    .then(r => console.log(JSON.stringify(r)));
  ```
  *(l1a is 11 chars, max is 8)*
- Expected response: `{"status":"error","msg":"l1a too long"}`

**3b — Oversized l2 rejected**
  ```js
  sendCommand({cmd:'write_slot', slot:0, l1a:'OK', l1b:'', l2:'ThisRoomNameIsWayTooLong', en:true, icon:0})
    .then(r => console.log(JSON.stringify(r)));
  ```
  *(l2 is 24 chars, max is 16)*
- Expected response: `{"status":"error","msg":"l2 too long"}`

**3c — Valid write still works**
  ```js
  sendCommand({cmd:'write_slot', slot:0, l1a:'Living', l1b:'Room', l2:'Lounge', en:true, icon:0})
    .then(r => console.log(JSON.stringify(r)));
  ```
- Expected response: `{"status":"ok"}`
- Monitor: `write_slot 0: l1a='Living' l1b='Room' l2='Lounge' en=1 icon=0`

---

## Fix 4 — Webapp `sendCommand` rejects orphaned promise
**File:** `web/index.html` — `sendCommand()`

### What changed
Before setting new `pendingResolve`/`pendingReject`, if a pending promise already exists it is rejected with `'Superseded by new command'` and its timeout is cleared.

### Tests

**4a — Normal flow unaffected**
- Connect, read, write — all existing flows serialize commands with `await`, so this guard is never triggered in normal use. Verify the normal full flow works without errors.

**4b — Rapid double-send (DevTools)**
- In browser DevTools console while connected:
  ```js
  const p1 = sendCommand({cmd:'ping'});
  const p2 = sendCommand({cmd:'ping'});
  p1.catch(e => console.log('p1 rejected:', e.message));
  p2.then(r => console.log('p2 ok:', r.status));
  ```
- Expected: `p1 rejected: Superseded by new command`, then `p2 ok: ok`.
- Before fix, `p1` would have silently leaked (never resolved or rejected).

---

## Fix 5 — Webapp `SlipDecoder` frame size limit
**File:** `web/index.html` — `SlipDecoder.feed()`

### What changed
Added `SLIP_MAX_FRAME = 4096`. If `this.buf` exceeds that during accumulation (malformed stream with no SLIP_END), the buffer is reset, `inFrame` is set false, and `[slip] frame overflow — discarding` is logged to the console.

### Tests

**5a — Normal frames unaffected**
- All device responses are well under 4096 bytes (read response is ~700 bytes encoded). Connect and use normally — no overflow logs should appear.

**5b — Overflow path (DevTools simulation)**
- In browser DevTools console while connected (read loop running):
  ```js
  // Feed > 4096 bytes with no SLIP_END to the decoder
  const big = new Uint8Array(5000).fill(0x41); // 5000 'A' bytes
  slipDecoder.inFrame = true;
  slipDecoder.feed(big);
  console.log('buf after overflow:', slipDecoder.buf.length, 'inFrame:', slipDecoder.inFrame);
  ```
- Expected: `buf after overflow: 0  inFrame: false`
- Expected in webapp console log: `[slip] frame overflow — discarding`

---

## End-to-End Smoke Test (run after all 5 fixes)

1. **Boot in CONFIG mode**, connect webapp.
2. **Handshake** — ping succeeds, icons load, table reads.
3. **Normal write** — edit slot 0 (`Living Room / Lounge`), click Write, confirm modal, reboot.
4. **Boot in NORMAL mode** — display shows correct slot, no `E app_serial` errors in monitor.
5. **Return to CONFIG mode**, re-read — values match what was written.
6. **Oversized field test** (Fix 3c) — confirm device returns appropriate error and does not write.

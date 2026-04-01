# TODO

## P1 — High

- [x] **`send_status()` CBOR map size off-by-one** — `app_serial.cpp:116`: `msg ? 3 : 2` should be `msg ? 2 : 1`. Encodes 2 pairs but declares 3 (or 1 pair declared as 2), producing malformed CBOR on every response.
- [x] **LED polarity comment wrong** — `app_priv.h:23` says `active HIGH` but driver implements active LOW (`gpio_set_level(LED_PIN, on ? 0 : 1)`). Change comment to `active LOW`.
- [x] **`write_defaults_to_nvs()` ignores NVS write errors** — `app_driver.cpp:75–89`: all `nvs_set_str`/`nvs_set_u8` return values discarded. If a write fails, partial config is committed and first-boot won't retry. Check each return value; skip commit on failure.
- [x] **`app_button_nvs_write_slot()` ignores NVS write errors** — `app_driver.cpp:203–212`: same issue as above; serial configurator returns `{status:"ok"}` even when NVS write partially failed.
- [x] **`write_fixed_label()` writes NVS unconditionally every boot** — `app_main.cpp:465`: called for every enabled endpoint on every normal-mode boot (12+ writes for 4 slots). Read existing value first; skip write if unchanged.
- [x] **Passcode logged in plaintext at INFO level** — `app_main.cpp:533`: `ESP_LOGI(TAG, "Passcode: %d", ...)` emits the Matter passcode on every boot. Drop the line or change to `ESP_LOGD`.

## P2 — Medium

- [ ] **No OTA update mechanism despite OTA partitions** — `partitions.csv` has `ota_0`/`ota_1` slots and `esp_matter_ota.h` is included, but no OTA logic is implemented. Add `esp_https_ota` or document the intended update path.
- [x] **`handle_read()` CBOR may silently truncate at 1024 bytes** — `app_serial.cpp:43,172`: TinyCBOR truncates silently when buffer fills. Add `cbor_encoder_get_extra_bytes_needed(&enc) == 0` check after encoding; send error response if non-zero.
- [x] **`handle_icons()` CBOR buffer has no overflow check** — `app_serial.cpp:141`: 512-byte static buffer, same silent-truncation risk as above.
- [ ] **Live credentials committed to git** — `CHIPPairingConfig.h` (passcode, discriminator) and `docs/img/pairing_qr.png` are real device credentials. Add both to `.gitignore`; document `make generate-pairing` as required before each deployment.
- [x] **`handle_write_slot()` silently truncates oversized strings** — `app_serial.cpp:234-261`: `cbor_value_copy_text_string` truncates strings that exceed field capacity without error. Compare `vlen` against buffer capacity after copy; if truncated, send `{status:"error",msg:"field too long"}`.
- [x] **Webapp `sendCommand` orphans pending promise** — `web/index.html:818`: if a second command is sent before the first resolves, old `pendingResolve`/`pendingReject` are silently overwritten. Reject the old promise before overwriting.
- [x] **Webapp `SlipDecoder` has no frame size limit** — `web/index.html:556`: `this.buf` grows unbounded on a malformed stream (no SLIP_END). Add a max frame size (e.g. 4096 bytes); reset and log on overflow.

## P3 — Low

- [ ] **`make monitor` logs accumulate without rotation** — `Makefile:86`: `screen -L` appends to `screenlog.0` forever. Add a `clean-logs` target or use a timestamped log filename.
- [ ] **`init_normal_mode()` is 134 lines** — `app_main.cpp:402–536`: extract endpoint-creation loop and display init into separate static functions.
- [ ] **`generate-pairing` uses `exec()` in Python one-liner** — `Makefile:110`: replace `exec('while ...')` with a proper loop or standalone script.
- [ ] **Remove unused `app_driver_handle_t` typedef** — `app_priv.h:129`: leftover from esp-matter template, never used.
- [ ] **Remove unused `LED_BLINK_SLOW_MS` constant** — `app_priv.h:59`: defined but never referenced; only `LED_BLINK_FAST_MS` is used.
- [ ] **Remove `led_set()` wrapper** — `app_driver.cpp:293`: trivial one-line wrapper around `app_driver_led_set()`; call it directly.
- [ ] **Remove duplicate `s_endpoint_ids` array** — `app_main.cpp:58`: static array becomes dead state after being copied into `app_driver.cpp`'s own array during init. Replace with a local variable.
- [ ] **Standardize constant naming** — mixed conventions: `k_timeout_seconds` (snake_case) vs `kDisplaySize` (CamelCase) vs `NVS_NS`/`NVS_SEL_KEY` (SCREAMING_CASE on non-macros). Pick one style and apply consistently.
- [ ] **Bump `label_val[35]` buffer** — `app_main.cpp:457`: buffer is exactly at capacity with max-length fields (zero margin). Increase to 36 or derive size from field constants.

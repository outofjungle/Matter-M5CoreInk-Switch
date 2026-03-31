# TODO

## P1 — High

- [ ] **LED polarity comment wrong** — `app_priv.h:23` says `active HIGH` but driver implements active LOW (`gpio_set_level(LED_PIN, on ? 0 : 1)`). Change comment to `active LOW`.
- [ ] **`write_defaults_to_nvs()` ignores NVS write errors** — `app_driver.cpp:75–89`: all `nvs_set_str`/`nvs_set_u8` return values discarded. If a write fails, partial config is committed and first-boot won't retry. Check each return value; skip commit on failure.
- [ ] **`app_button_nvs_write_slot()` ignores NVS write errors** — `app_driver.cpp:203–212`: same issue as above; serial configurator returns `{status:"ok"}` even when NVS write partially failed.
- [ ] **`write_fixed_label()` writes NVS unconditionally every boot** — `app_main.cpp:465`: called for every enabled endpoint on every normal-mode boot (12+ writes for 4 slots). Read existing value first; skip write if unchanged.
- [ ] **Passcode logged in plaintext at INFO level** — `app_main.cpp:533`: `ESP_LOGI(TAG, "Passcode: %d", ...)` emits the Matter passcode on every boot. Drop the line or change to `ESP_LOGD`.

## P2 — Medium

- [ ] **No OTA update mechanism despite OTA partitions** — `partitions.csv` has `ota_0`/`ota_1` slots and `esp_matter_ota.h` is included, but no OTA logic is implemented. Add `esp_https_ota` or document the intended update path.
- [ ] **`handle_read()` CBOR may silently truncate at 1024 bytes** — `app_serial.cpp:43,172`: TinyCBOR truncates silently when buffer fills. Add `cbor_encoder_get_extra_bytes_needed(&enc) == 0` check after encoding; send error response if non-zero.
- [ ] **`handle_icons()` CBOR buffer has no overflow check** — `app_serial.cpp:141`: 512-byte static buffer, same silent-truncation risk as above.
- [ ] **Live credentials committed to git** — `CHIPPairingConfig.h` (passcode, discriminator) and `docs/img/pairing_qr.png` are real device credentials. Add both to `.gitignore`; document `make generate-pairing` as required before each deployment.

## P3 — Low

- [ ] **`make monitor` logs accumulate without rotation** — `Makefile:86`: `screen -L` appends to `screenlog.0` forever. Add a `clean-logs` target or use a timestamped log filename.
- [ ] **`init_normal_mode()` is 134 lines** — `app_main.cpp:402–536`: extract endpoint-creation loop and display init into separate static functions.
- [ ] **`generate-pairing` uses `exec()` in Python one-liner** — `Makefile:110`: replace `exec('while ...')` with a proper loop or standalone script.

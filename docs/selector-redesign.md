# Selector + Action Button Redesign

**Beads issue**: Matter-M5CoreInk-Switch-76b

## Overview

Transform the device from a 1:1 button→switch mapping to a selector+action paradigm:

- **Up / Down** — navigate between Switch 1, 2, 3 (no Matter events fired)
- **Mid** — fire press event on whichever switch is currently selected
- **Display** — show QR code until commissioned; show active switch number after

## Files to change

| File | Change |
|------|--------|
| `main/app_priv.h` | Add display callback declaration, update `buttons_init` signature |
| `main/app_main.cpp` | Label names, boot commission check, `app_display_show_switch()`, event handler |
| `main/app_driver.cpp` | Navigation logic, dynamic endpoint for Mid, callback plumbing |

## Detailed changes

### `app_priv.h`

- Add declaration:
  ```c
  void app_display_show_switch(int switch_num);
  ```
- Update `app_driver_buttons_init` signature:
  ```c
  esp_err_t app_driver_buttons_init(uint16_t *endpoint_ids, void (*on_switch_selected)(int));
  ```
- `SWITCH_x_IDX` constants stay (used for GPIO init order)

### `app_main.cpp`

- Change switch labels: `{"Up", "Down", "Mid"}` → `{"1", "2", "3"}`
- Add `app_display_show_switch(int n)` — draws "Switch N" big + centered on e-ink
- After `esp_matter::start()`, check `FabricCount()`:
  - `== 0` → show QR (existing path, not yet commissioned)
  - `> 0` → call `app_display_show_switch(1)` (already commissioned at boot)
- In `kCommissioningComplete` event handler → call `app_display_show_switch(1)`
- Pass `app_display_show_switch` as callback to `app_driver_buttons_init`

### `app_driver.cpp`

- Add `static int s_selected_switch = 0;` (0-indexed: 0="Switch 1", 1="Switch 2", 2="Switch 3")
- Add `static void (*s_display_cb)(int) = nullptr;`
- **Up callback**: `s_selected_switch = (s_selected_switch + 1) % 3` → call `s_display_cb(s_selected_switch + 1)`. No Matter events.
- **Down callback**: `s_selected_switch = (s_selected_switch + 2) % 3` → call `s_display_cb(s_selected_switch + 1)`. No Matter events.
- **Mid callbacks**: fire `InitialPress` / `ShortRelease` on `s_endpoint_ids[s_selected_switch]` (dynamic)
- Long-press factory reset stays on Mid only
- LED brief-flash on Up/Down for tactile feedback

## What stays the same

- 3 Generic Switch endpoints with Fixed Label cluster
- InitialPress + ShortRelease event sequence (Apple Home compatible)
- Long-press Mid → factory reset (5 s)
- LED blink states: fast=uncommissioned, slow=commissioned
- NVS, QR rendering infrastructure

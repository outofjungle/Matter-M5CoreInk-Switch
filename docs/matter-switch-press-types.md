# Matter Generic Switch — Press Types

Research on what press types the Matter Switch cluster supports and what this project currently implements.

## Matter spec: Switch cluster feature flags

The Switch cluster (0x003B) defines five optional feature bits. Each unlocks additional event types:

| Bit  | Flag | Name                         | Events enabled                          |
|------|------|------------------------------|-----------------------------------------|
| 0x01 | LS   | Latching Switch              | SwitchLatched                           |
| 0x02 | MS   | Momentary Switch             | InitialPress                            |
| 0x04 | MSR  | Momentary Switch Release     | ShortRelease                            |
| 0x08 | MSL  | Momentary Switch Long Press  | LongPress, LongRelease                  |
| 0x10 | MSM  | Momentary Switch Multi-Press | MultiPressOngoing, MultiPressComplete   |

Flags are combined as a bitmask on the endpoint's feature map attribute.

## Current implementation (feature_flags = 0x06)

This project sets `feature_flags = 0x06` (MS | MSR) in `main/app_main.cpp` (`create_switch_endpoints()`), which enables:

- **InitialPress** — emitted on MID button press-down
- **ShortRelease** — emitted on MID button release

Single-press only. No long press or multi-press events are sent.

### Partial long-press infrastructure

`main/app_driver.cpp` has a `long_press_active` flag in `btn_ctx_t` and a `btn_long_press_mark_cb` callback, but these only suppress the ShortRelease when a long press is detected (used for Up/Down button factory reset behavior). No `LongPress` or `LongRelease` Matter events are emitted.

## What adding more press types would require

### Long press (MSL = 0x08)

1. Add 0x08 to `feature_flags` (making it 0x0E = MS | MSR | MSL)
2. In the MID button long-press callback, emit `Switch::Events::LongPress::Type`
3. On release after a long press, emit `Switch::Events::LongRelease::Type` instead of ShortRelease
4. The iot_button library already detects long press — the callback is wired up

### Multi-press / double tap (MSM = 0x10)

1. Add 0x10 to `feature_flags`
2. Set `NumberOfMultiPressMax` attribute (e.g., 2 for double-tap)
3. Implement a timing state machine: on each press, start a window (~400ms); if another press arrives within the window, increment count and emit `MultiPressOngoing`; when the window expires, emit `MultiPressComplete` with the final count
4. More complex — requires debounce/timing logic beyond what iot_button provides out of the box

## Controller support (as of 2026-03)

- **Apple Home**: Only supports single-press automations from Matter programmable buttons. Long press and multi-press events are accepted but cannot trigger separate automations.
- **Google Home**: Supports single-press. Multi-press support varies by firmware version.
- **Home Assistant (Matter integration)**: Exposes all event types — long press and multi-press can trigger separate automations.

Apple Home's limitation is the main reason this project only implements single-press for now.

## References

- Matter Application Cluster Specification, Section 1.12 (Switch Cluster)
- ESP-Matter SDK: `esp_matter_cluster.h` — `generic_switch::config_t`
- `iot_button` component: supports press-down, press-up, long-press, and single/double/triple click detection

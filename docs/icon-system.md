# Icon System

## Overview

Each button slot displays an icon in the upper portion of the selector panel, above the button name text. The user selects from a set of built-in icons via the web configurator. If no icon is set, the area is left blank (or shows a placeholder during development).

## Icon Area

The icon is rendered as a square, centered horizontally in the selector panel.

| Parameter | Value | Notes |
|-----------|-------|-------|
| `kIconX`  | 54    | Left edge (= kPanelCX - kIconSize/2) |
| `kIconY`  | 4     | Top edge (small margin from top of panel) |
| `kIconSize` | 78  | Width and height in pixels |

These values are to be confirmed after visual inspection on the device and may be adjusted.

The icon area sits between:
- **Above**: top of the display (y=0); badge circle is in the corner at (14,14) and does not constrain the centered icon box
- **Below**: button name text (18pt bold, top of glyphs at y≈85)

## NVS Storage

Each slot stores an icon identifier as a small integer (uint8):

| NVS key | Type | Description |
|---------|------|-------------|
| `sw/N/icon` | `uint8` | Icon index (0 = no icon / blank) |

## CBOR Protocol Extension

The read/write slot CBOR map gains an `icon` field:

```cbor
{ "l1a": "Kitchen", "l1b": "Light", "l2": "Living Rm", "en": true, "icon": 3 }
```

- `icon`: integer 0–N (0 = none)
- `icon` is optional on write; if absent, existing value is preserved
- `icon` is always present on read (0 if unset)

## Built-in Icon Set

Icons are 1-bit bitmaps sized to fit the icon area. The set is defined in firmware and indexed by integer ID.

| ID | Name | Description |
|----|------|-------------|
| 0  | (none) | Blank — no icon drawn |
| ... | ... | TBD |

Icons will be stored as `const uint8_t[]` bitmap arrays in a dedicated source file (e.g. `main/icons.h`), rendered via `M5GFX::drawBitmap()` or equivalent.

## Web Configurator

The web configurator will show a visual icon picker per slot. The user selects an icon from a grid of thumbnails. The selected icon index is sent in the CBOR write command.

## Future Work

- Define the icon catalog (symbols for lights, fans, locks, scenes, etc.)
- Implement bitmap rendering in firmware
- Add icon picker UI to the web configurator
- Decide on icon size: currently 55×55 px placeholder (to be confirmed on device)

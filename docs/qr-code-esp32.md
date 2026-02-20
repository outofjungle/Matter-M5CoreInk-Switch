# QR Code Generation on ESP32 for Matter Devices

## Recommended Library: `espressif/qrcode`

Official ESP-IDF component wrapping [Nayuki's QR-Code-generator](https://github.com/nayuki/QR-Code-generator). Available on the [ESP Component Registry](https://components.espressif.com/components/espressif/qrcode).

| Property | Value |
|----------|-------|
| Version | 0.2.0 |
| License | Apache-2.0 |
| Archive size | ~24.5 KB |
| Dependencies | None |
| Downloads | 405,000+ |

### Installation

Add to `main/idf_component.yml`:
```yaml
dependencies:
  espressif/qrcode: "^0.2.0"
```

### ESP-IDF Wrapper API

```c
#include "qrcode.h"

// Configuration
typedef struct {
    void (*display_func)(esp_qrcode_handle_t qrcode);
    int max_qrcode_version;   // 2-40
    int qrcode_ecc_level;     // ESP_QRCODE_ECC_LOW / MED / QUART / HIGH
    void *user_data;
} esp_qrcode_config_t;

// Default (prints QR to serial console)
#define ESP_QRCODE_CONFIG_DEFAULT() ...

// Key functions
esp_err_t esp_qrcode_generate(esp_qrcode_config_t *cfg, const char *text);
int  esp_qrcode_get_size(esp_qrcode_handle_t qrcode);          // returns 21-177
bool esp_qrcode_get_module(esp_qrcode_handle_t qrcode, int x, int y);  // true=black
```

The design uses a **callback pattern**: `esp_qrcode_generate()` encodes the text, then calls your `display_func`. Inside the callback, iterate pixels with `esp_qrcode_get_module()`.

### Raw Nayuki API (also available through the same component)

```c
#include "qrcodegen.h"

uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];    // ~172 bytes
uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];   // ~172 bytes

bool ok = qrcodegen_encodeText(
    "MT:-24J0S0.030UU741J00",
    temp, qr,
    qrcodegen_Ecc_MEDIUM,
    qrcodegen_VERSION_MIN, 5,
    qrcodegen_Mask_AUTO, true
);

if (ok) {
    int size = qrcodegen_getSize(qr);  // likely 25 (version 2)
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if (qrcodegen_getModule(qr, x, y))
                /* draw black pixel at (x, y) */;
}
```

---

## Memory Requirements

The buffer formula is `((4n+17)^2 + 7) / 8 + 1` bytes; two buffers needed (qrcode + temp):

| QR Version | Grid | Bytes/Buffer | 2 Buffers | Max Alphanumeric |
|------------|------|-------------|-----------|------------------|
| 1 | 21x21 | 57 | 114 | 25 chars |
| 2 | 25x25 | 79 | 158 | 47 chars |
| 3 | 29x29 | 106 | 212 | 77 chars |
| 5 | 37x37 | 172 | 344 | 154 chars |

A Matter QR payload like `MT:-24J0S0.030UU741J00` (22 chars) fits in **Version 2** (25x25, 158 bytes total). The qrcodegen library adds ~4-6 KB of flash.

---

## Matter SDK QR Code Support

### QR Payload Format

The Matter SDK encodes commissioning data into a Base38 string prefixed with `MT:`:

1. A 100-bit binary payload is packed: version (3 bits) + vendorID (16) + productID (16) + commissioningFlow (2) + rendezvousInfo (8) + discriminator (12) + setupPINCode (27) + padding (4)
2. Base38-encoded to produce the string after `MT:`
3. Result: `MT:-24J0S0.030UU741J00`

### Getting the QR String at Runtime

```cpp
#include <app/server/OnboardingCodesUtil.h>

// Print to console
PrintOnboardingCodes(chip::RendezvousInformationFlag::kBLE);

// Get the string programmatically
char buffer[64];
chip::MutableCharSpan qrCodeBuffer(buffer, sizeof(buffer));
GetQRCode(qrCodeBuffer, chip::RendezvousInformationFlag::kBLE);
// buffer now contains "MT:-24J0S0.030UU741J00"
```

### Compile-Time Alternative

The QR code string is already stored in `CHIPPairingConfig.h` as a comment:
```c
/* QR Code: MT:-24J0S0.030UU741J00 */
```
The string could also be embedded as a `#define` for compile-time use.

**The Matter SDK does NOT include bitmap rendering** — only the Base38 text string. A separate library (like `espressif/qrcode`) is needed to convert to pixels.

---

## Rendering on M5CoreInk (200x200 e-ink)

### Scaling

| QR Version | Modules | Scale | QR Size | Quiet Zone |
|------------|---------|-------|---------|------------|
| 2 (25x25) | 25 | 7px | 175x175 | 12px/side |
| 2 (25x25) | 25 | 8px | 200x200 | 0px (tight) |
| 3 (29x29) | 29 | 6px | 174x174 | 13px/side |

**Recommendation:** Version 2 at 7px scale = 175x175 with 12.5px quiet zone. The QR spec recommends a 4-module quiet zone (28px at 7px scale), which fits within the available margin.

### Rendering with Custom Display Callback

```c
static void qr_display_on_eink(esp_qrcode_handle_t qrcode)
{
    int size = esp_qrcode_get_size(qrcode);  // 25 for Version 2
    int scale = 200 / size;                   // 8 (tight) or 7 (with margins)
    int offset = (200 - size * scale) / 2;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                eink_fill_rect(offset + x * scale, offset + y * scale,
                               scale, scale, COLOR_BLACK);
            }
        }
    }
    eink_refresh();
}

void show_matter_qr_code(void)
{
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qr_display_on_eink;
    cfg.max_qrcode_version = 5;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;

    esp_qrcode_generate(&cfg, "MT:-24J0S0.030UU741J00");
}
```

---

## Other Libraries (not recommended)

| Library | Notes |
|---------|-------|
| LVGL `lv_qrcode` | Built into LVGL; enable `LV_USE_QRCODE`. Only relevant if using LVGL. |
| [yoprogramo/ESP_QRcode](https://github.com/yoprogramo/ESP_QRcode) | Arduino-only |
| [nopnop2002/esp-idf-qr-code-generator](https://github.com/nopnop2002/esp-idf-qr-code-generator) | ESP-IDF but not a registered component; uses nayuki underneath |

---

## Sources

- [ESP Component Registry: espressif/qrcode](https://components.espressif.com/components/espressif/qrcode)
- [Nayuki QR-Code-generator](https://github.com/nayuki/QR-Code-generator)
- [Matter SDK: src/setup_payload/](https://github.com/project-chip/connectedhomeip/tree/master/src/setup_payload)
- [esp-matter FAQ](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/faq.html)

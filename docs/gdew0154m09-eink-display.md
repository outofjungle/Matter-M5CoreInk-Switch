# GDEW0154M09 E-Ink Display Driver Reference

## Display Specifications

| Parameter | Value |
|-----------|-------|
| Panel Model | GDEW0154M09 (GoodDisplay) |
| Size | 1.54 inches diagonal |
| Resolution | 200 x 200 pixels |
| Pixel Format | 1-bit Black & White |
| DPI | 184 ppi |
| Active Area | 27.6 x 27.6 mm |
| Interface | 4-wire SPI |
| Controller IC | **JD79653A** (FitiPower / Jadard) |
| Full Refresh Time | ~0.82 seconds |
| Partial Refresh Time | ~0.24-0.3 seconds |
| Operating Voltage | 3.3V |

The controller IC is the **JD79653A** — NOT an SSD1681 or UC8151, though many commands are similar.

---

## M5Stack Core Ink GPIO Mapping

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| MOSI (SDA/DIN) | GPIO 23 | Output | SPI data out |
| SCK (CLK) | GPIO 18 | Output | SPI clock |
| CS (Chip Select) | GPIO 9 | Output | Active low |
| DC (Data/Command) | GPIO 15 | Output | Low=command, High=data |
| RST (Reset) | GPIO 0 | Output | Active low reset |
| BUSY | GPIO 4 | Input | **INVERTED: LOW=busy, HIGH=ready** |

### Critical Notes

- **BUSY is inverted** — LOW means busy, HIGH means ready. Getting this wrong risks display damage.
- **RST shares GPIO 0** — the ESP32 boot strapping pin. M5Stack hardware has appropriate pull-ups, but be aware during flashing.
- **SPI bus**: VSPI (SPI3). **1 MHz** is a conservative safe default; M5Core-Ink reference uses 10 MHz. Partial refresh failures are not caused by clock speed alone.

---

## SPI Communication Protocol

4-wire SPI, **Mode 3** (CPOL=1, CPHA=1) — JD79653A samples data on the rising edge of an idle-high clock:

```
Command write:  CS=LOW → DC=LOW  → SPI_write(cmd_byte)  → CS=HIGH
Data write:     CS=LOW → DC=HIGH → SPI_write(data_bytes) → CS=HIGH
Combined:       CS=LOW → DC=LOW → SPI_write(cmd) → DC=HIGH → SPI_write(data...) → CS=HIGH
```

Always check BUSY before sending commands:
```c
static void wait_busy(void) {
    while (gpio_get_level(EINK_BUSY_PIN) == 0) {  // LOW = busy
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## ESP-IDF SPI Setup

```c
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define EINK_MOSI  23
#define EINK_SCK   18
#define EINK_CS     9
#define EINK_DC    15
#define EINK_RST    0
#define EINK_BUSY   4

spi_bus_config_t bus_cfg = {
    .mosi_io_num = EINK_MOSI,
    .miso_io_num = -1,
    .sclk_io_num = EINK_SCK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 5000,
};

spi_device_interface_config_t dev_cfg = {
    .clock_speed_hz = 1000000,  // 1 MHz conservative; reference uses 10 MHz
    .mode = 3,                  // CRITICAL: JD79653A requires Mode 3 (CPOL=1, CPHA=1)
    .spics_io_num = EINK_CS,
    .queue_size = 1,
};
```

---

## JD79653A Command Reference

| Cmd | Name | Data Bytes | Description |
|-----|------|------------|-------------|
| `0x00` | Panel Setting (PSR) | 2 | Panel config and orientation |
| `0x02` | Power OFF | 0 | Turn off charge pump |
| `0x04` | Power ON (PON) | 0 | Turn on charge pump |
| `0x07` | Deep Sleep | 1 (`0xA5`) | Enter deep sleep |
| `0x10` | DTM1 | 5000 | Old/previous framebuffer |
| `0x12` | Display Refresh (DRF) | 0 | Trigger display update |
| `0x13` | DTM2 | 5000 | New framebuffer |
| `0x20` | LUT VCOM DC | 56 | VCOM waveform table |
| `0x21` | LUT WW | 42 | White-to-White waveform |
| `0x22` | LUT BW | 56 | Black-to-White waveform |
| `0x23` | LUT WB | 56 | White-to-Black waveform |
| `0x24` | LUT BB | 56 | Black-to-Black waveform |
| `0x4D` | FITI Internal | 1 | FitiPower internal |
| `0x50` | VCOM/Data Interval | 1 | VCOM and data interval |
| `0x60` | TCON Setting | 1 | Timing control |
| `0x61` | Resolution Setting | 3 | Set display resolution |
| `0x90` | Partial Window | 7 | Define partial update area |
| `0x91` | Partial In | 0 | Enter partial update mode |
| `0x92` | Partial Out | 0 | Exit partial update mode |
| `0xAA` | Internal | 1 | FitiPower internal |
| `0xB6` | Internal | 1 | FitiPower internal |
| `0xE3` | Internal | 1 | Power sequence timing |
| `0xE9` | Internal | 1 | FitiPower internal |
| `0xF3` | Internal | 1 | FitiPower internal |

---

## Initialization Sequence

From M5Core-Ink Arduino library, LovyanGFX, and LVGL jd79653a driver:

```c
// 1. Hardware reset: HIGH→LOW→HIGH for clean pulse; wait_busy for post-reset init
gpio_set_level(EINK_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
gpio_set_level(EINK_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
gpio_set_level(EINK_RST, 1); vTaskDelay(pdMS_TO_TICKS(100));
wait_busy();

// 2. Panel Setting
cmd(0x00); data(0xDF); data(0x0E);
// For 180-degree rotation: data(0xD3); data(0x0E);

// 3. FitiPower internal registers
cmd(0x4D); data(0x55);
cmd(0xAA); data(0x0F);
cmd(0xE9); data(0x02);
cmd(0xB6); data(0x11);
cmd(0xF3); data(0x0A);

// 4. Resolution (200x200)
cmd(0x61); data(0xC8); data(0x00); data(0xC8);

// 5. TCON
cmd(0x60); data(0x00);

// 6. VCOM / Data interval
cmd(0x50); data(0xD7);

// 7. Power sequence
cmd(0xE3); data(0x00);

// 8. Power ON
cmd(0x04);
wait_busy();

// 9. Load LUT waveform tables
cmd(0x20); send_data(lut_vcom_dc1, 56);
cmd(0x21); send_data(lut_ww1, 42);
cmd(0x22); send_data(lut_bw1, 56);
cmd(0x23); send_data(lut_wb1, 56);
cmd(0x24); send_data(lut_bb1, 56);
```

---

## Look-Up Tables (LUT)

```c
static const uint8_t lut_vcom_dc1[] = {  // 56 bytes
    0x01, 0x04, 0x04, 0x03, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_ww1[] = {  // 42 bytes
    0x01, 0x04, 0x04, 0x03, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_bw1[] = {  // 56 bytes
    0x01, 0x84, 0x84, 0x83, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_wb1[] = {  // 56 bytes
    0x01, 0x44, 0x44, 0x43, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_bb1[] = {  // 56 bytes
    0x01, 0x04, 0x04, 0x03, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
```

LUT byte 2 key: `0x04`=no change, `0x84`=negative voltage (B→W), `0x44`=positive voltage (W→B).

---

## Framebuffer Format

- **1 bit per pixel**, MSB first, row-major
- Row length: `200 / 8 = 25 bytes`
- Total: `25 * 200 = 5000 bytes`
- Bit value: `1` = black, `0` = white

```c
#define EPD_WIDTH   200
#define EPD_HEIGHT  200
#define EPD_ROW_LEN (EPD_WIDTH / 8)  // 25
#define EPD_BUF_SIZE (EPD_ROW_LEN * EPD_HEIGHT)  // 5000

void set_pixel(uint8_t *buf, uint16_t x, uint16_t y, bool black) {
    if (x >= EPD_WIDTH || y >= EPD_HEIGHT) return;
    uint16_t byte_idx = (x >> 3) + (y * EPD_ROW_LEN);
    uint8_t  bit_idx  = 7 - (x & 0x07);  // MSB first
    if (black)
        buf[byte_idx] |=  (1U << bit_idx);
    else
        buf[byte_idx] &= ~(1U << bit_idx);
}
```

### Dual Buffer System

The JD79653A uses two data channels:
- **DTM1 (cmd 0x10)**: Old/previous frame data
- **DTM2 (cmd 0x13)**: New/current frame data

The controller compares old vs new to determine pixel transitions, applying the appropriate LUT waveform.

---

## Full Refresh

```c
void epd_full_refresh(uint8_t *old_buf, uint8_t *new_buf) {
    cmd(0x50); data(0x97);       // VCOM for full refresh mode
    cmd(0x04); wait_busy();      // Power ON

    cmd(0x10);
    spi_send_data(old_buf, 5000);  // DTM1: old frame

    cmd(0x13);
    spi_send_data(new_buf, 5000);  // DTM2: new frame

    cmd(0x12); wait_busy();      // Refresh (~820ms)
}
```

---

## Partial Refresh

```c
void epd_partial_refresh(uint8_t *new_buf) {
    cmd(0x50); data(0xD7);       // VCOM for partial mode
    cmd(0x91);                    // Enter partial mode

    cmd(0x90);                    // Partial window (full screen)
    data(0x00);                   // x start
    data(0xC7);                   // x end (199)
    data(0x00); data(0x00);       // y start (high, low)
    data(0x00); data(0xC7);       // y end (high, low=199)
    data(0x01);                   // scan direction

    cmd(0x13);
    spi_send_data(new_buf, 5000);  // DTM2 only

    cmd(0x12); wait_busy();      // Refresh (~240ms)
    cmd(0x92);                    // Exit partial mode
}
```

**Ghosting rule**: After ~5 partial refreshes, perform a full refresh to clear accumulated ghosts.

---

## Power Management

### Power Off
```c
cmd(0x50); data(0xF7);
cmd(0x02); wait_busy();
```

### Deep Sleep
```c
cmd(0x50); data(0xF7);
cmd(0x02); wait_busy();
cmd(0x07); data(0xA5);  // 0xA5 is the required check code
```
After deep sleep, a hardware reset + full re-initialization is required.

---

## Existing Drivers

| Driver | Language | Notes |
|--------|----------|-------|
| [LVGL jd79653a.c](https://github.com/lvgl/lvgl_esp32_drivers) | C | Best for pure ESP-IDF. Standalone SPI logic. |
| [LovyanGFX Panel_GDEW0154M09](https://github.com/lovyan03/LovyanGFX) | C++ | Full-featured: quality mode, dithering, partial. |
| [M5GFX](https://github.com/m5stack/M5GFX) | C++ | Fork of LovyanGFX with M5Stack defaults. |
| [CalEPD](https://github.com/martinberlin/CalEPD) | C++ | ESP-IDF component, 22+ e-paper models. |
| [witasekl/gdew0154m09](https://github.com/witasekl/gdew0154m09) | C++ | ESPHome external component. |

**Recommendation**: For a minimal standalone driver, extract the core SPI logic from the LVGL `jd79653a.c` driver. It's pure C, uses ESP-IDF SPI APIs, and has no external dependencies.

---

## Key Gotchas

1. **BUSY pin is inverted** — LOW=busy, HIGH=ready. Wrong polarity risks display damage.
2. **SPI max 1 MHz** for reliable partial refresh. Full refresh may tolerate higher.
3. **RST is GPIO 0** — ESP32 boot strapping pin. Hardware has pull-ups but beware during dev.
4. **Display inversion** — M5Core-Ink Arduino library inverts display by default. Verify B/W mapping.
5. **Periodic full refresh** — every ~5 partial refreshes to clear ghosting.
6. **Power hold** — GPIO 12 must be HIGH to keep device powered on battery.

---

## Sources

- [M5Stack Core Ink Docs](https://docs.m5stack.com/en/core/coreink)
- [M5Core-Ink Arduino Library](https://github.com/m5stack/M5Core-Ink)
- [LVGL ESP32 Drivers (jd79653a.c)](https://github.com/lvgl/lvgl_esp32_drivers)
- [LovyanGFX Panel_GDEW0154M09](https://github.com/lovyan03/LovyanGFX)
- [CursedHardware EPD Driver IC Collection](https://github.com/CursedHardware/epd-driver-ic)
- GDEW0154M09 datasheet (GDEW0154M09-200709.pdf)

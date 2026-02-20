# ESP32-PICO-D4 + M5Stack Core Ink: Matter over WiFi Research

Research notes for setting up a Matter over WiFi device on the M5Stack Core Ink
(ESP32-PICO-D4) using Espressif's esp-matter SDK.

---

## Table of Contents

1. [M5Stack Core Ink Hardware](#1-m5stack-core-ink-hardware)
2. [ESP32-PICO-D4 Chip Details](#2-esp32-pico-d4-chip-details)
3. [Differences vs ESP32-C6](#3-differences-vs-esp32-c6)
4. [Matter over WiFi on ESP32](#4-matter-over-wifi-on-esp32)
5. [sdkconfig.defaults for Matter WiFi](#5-sdkconfigdefaults-for-matter-wifi)
6. [Docker Build Environment](#6-docker-build-environment)
7. [USB/Serial & Flashing](#7-usbserial--flashing)
8. [Known Issues & Gotchas](#8-known-issues--gotchas)

---

## 1. M5Stack Core Ink Hardware

**Docs:** https://docs.m5stack.com/en/core/coreink

### Main SoC

- **Chip:** ESP32-PICO-D4 (System-in-Package)
- **CPU:** Dual-core Xtensa LX6 @ up to 240 MHz
- **Flash:** 4 MB embedded SPI flash (inside the SiP — not a separate chip)
- **SRAM:** 520 KB internal + 16 KB RTC SRAM

### Display

- **Panel:** GDEW0154M09 E-Ink
- **Resolution:** 200 × 200 px, 1-bit (black/white)
- **Interface:** SPI

### GPIO Pin Map

```c
// E-Ink Display (SPI)
#define INK_SPI_CS      9    // Chip Select
#define INK_SPI_SCK    18    // SPI Clock
#define INK_SPI_MOSI   23    // SPI Data
#define INK_SPI_DC     15    // Data/Command
#define INK_SPI_BUSY    4    // Busy signal
#define INK_SPI_RST     0    // Reset

// Buttons
#define BUTTON_UP_PIN  37    // Rotary Up
#define BUTTON_DOWN_PIN 39   // Rotary Down
#define BUTTON_MID_PIN 38    // Rotary Press (Middle)
#define BUTTON_EXT_PIN  5    // External button
#define BUTTON_PWR_PIN 27    // Power button

// Output peripherals
#define LED_EXT_PIN    10    // Green LED (G10) — active HIGH
#define SPEAKER_PIN     2    // Passive buzzer

// Power management
#define POWER_HOLD_PIN 12    // Must be HIGH to stay powered on battery

// I2C (RTC BM8563)
#define I2C_SDA        32
#define I2C_SCL        33
// RTC wakeup interrupt: GPIO19
```

**Source:** [M5Core-Ink config.h](https://github.com/m5stack/M5Core-Ink/blob/master/src/utility/config.h)

### LED

- **Pin:** GPIO10 (G10)
- **Type:** Green LED
- **Logic:** Active HIGH (set HIGH = LED on)
- **Note:** GPIO10 on a standard ESP32 module is the SPIQWP (flash Write Protect)
  pin, but on the ESP32-PICO-D4 the internal flash uses dedicated internal routing,
  so external GPIO10 is free to use as a regular output.

### Power Management

- **Battery:** 390 mAh @ 3.7 V lithium
- **USB:** 5 V via USB Type-C
- **POWER_HOLD (GPIO12):** Must be driven HIGH at startup to stay powered when
  running from battery. If running only from USB this is not strictly required,
  but should be set HIGH as a safety measure.
- **Shutdown:** `gpio_set_level(POWER_HOLD_PIN, 0)` cuts power when on battery.

### I2C / RTC

- **RTC chip:** BM8563
- **SDA:** GPIO32, **SCL:** GPIO33, **Freq:** 100 kHz
- **Wake interrupt:** GPIO19 (active LOW)

### Available Breakout Pins

G25, G26, G36, G23, G34, G18, G21, G22, G14, G13

---

## 2. ESP32-PICO-D4 Chip Details

**Datasheet:** https://www.espressif.com/sites/default/files/documentation/esp32-pico-d4_datasheet_en.pdf

### Architecture

| Property | Value |
|---|---|
| CPU | Xtensa dual-core LX6 (not RISC-V) |
| Max clock | 240 MHz |
| ROM | 448 KB (bootloader + basic libs) |
| Internal SRAM | 520 KB |
| RTC SRAM | 16 KB (8 KB fast + 8 KB slow) |
| Embedded flash | 4 MB SiP (no external flash needed) |
| Package | QFN 7×7 mm (SiP includes crystal, flash, caps, RF match) |

### Wireless

| Feature | Support |
|---|---|
| WiFi | 802.11 b/g/n, 2.4 GHz only |
| Bluetooth | BLE 4.2 + Classic BR/EDR |
| Thread / 802.15.4 | **NOT supported** |
| Zigbee | **NOT supported** |

### IDF Target Name

```bash
idf.py set-target esp32
```

> The PICO-D4 is just a different packaging of the ESP32 SoC — the IDF target is
> the same as any other ESP32 module.

---

## 3. Differences vs ESP32-C6

| Feature | ESP32-PICO-D4 (CoreInk) | ESP32-C6 (NanoC6) |
|---|---|---|
| Architecture | Xtensa LX6, dual-core | RISC-V, single-core |
| IDF target | `esp32` | `esp32c6` |
| WiFi | 802.11 b/g/n (2.4 GHz) | 802.11 b/g/n/ax (WiFi 6) |
| Bluetooth | BLE 4.2 | BLE 5.0/5.3 |
| Thread / 802.15.4 | **None** | Yes (integrated) |
| SRAM | 520 KB | 512 KB |
| Flash | 4 MB embedded SiP | External (varies by module) |
| Matter transport | **WiFi only** | WiFi or Thread |
| PSRAM | None | None |
| Dual core | Yes (LX6 × 2) | No (single RISC-V) |
| sdkconfig Thread | Not applicable | Thread/OpenThread options needed |

### Implications for Matter

- **No Thread:** Matter over Thread is impossible. WiFi is the only option.
- **BLE 4.2 is sufficient:** Matter commissioning uses BLE advertisements + GATT,
  which BLE 4.2 fully supports.
- **Dual core helps:** WiFi stack and BLE commissioning stack can run on separate
  Xtensa cores, improving stability vs single-core C6.
- **Memory fragmentation:** Despite 520 KB SRAM, effective heap is smaller due to
  ESP32 ROM fragmentation — see Section 4.

---

## 4. Matter over WiFi on ESP32

### Support Status

The original `esp32` (Xtensa) target is **fully supported** by esp-matter for
WiFi-based Matter. It is the first target listed in Espressif's supported list:

```
esp32, esp32s3, esp32c2, esp32c3, esp32c5, esp32c6, esp32c61, esp32h2, esp32p4
```

### Build Command

```bash
idf.py set-target esp32
idf.py build
```

No Thread overlay needed (unlike C6 which uses
`sdkconfig.defaults;sdkconfig.defaults.esp32c6_thread`).

### Memory Architecture

The ESP32 SRAM layout is fragmented:

```
Total SRAM: 520 KB
  DRAM:   320 KB total
    Static allocation hard limit: ~160 KB
    Runtime heap:                 ~160 KB
    With BLE enabled:             -64 KB reserved → ~96 KB startup heap
  IRAM:   ~200 KB (first 64 KB used for MMU/caches)
```

After WiFi connects: WiFi stack consumes ~58 KB heap.
After BLE commissioning completes: BLE stack is released → heap actually **increases**.
This is documented expected behavior in the esp-matter FAQ.

### Effective Heap During Operation (rough estimates)

| State | Available Heap |
|---|---|
| After boot | ~96 KB |
| WiFi connected | ~40 KB |
| Commissioning (BLE active) | ~30 KB (tight) |
| After commissioning (BLE released) | ~60–80 KB |

### 4 MB Flash Partition Considerations

The PICO-D4 has exactly 4 MB. A practical partition layout for Matter with OTA:

```
nvs,        data, nvs,     0x9000,   0x6000   # 24 KB
otadata,    data, ota,     0xF000,   0x2000   # 8 KB
phy_init,   data, phy,     0x11000,  0x1000   # 4 KB
fctry,      data, nvs,     0x340000, 0x6000   # Factory data (optional)
factory,    app,  factory, 0x20000,  0x200000 # 2 MB app
ota_0,      app,  ota_0,   0x220000, 0x100000 # 1 MB OTA (tight but possible)
```

Dual OTA at full size is very tight with 4 MB. Single-bank OTA or no-OTA is
more practical for initial development.

---

## 5. sdkconfig.defaults for Matter WiFi

For Matter over WiFi on ESP32 (not Thread, no OpenThread):

```ini
# Target
CONFIG_IDF_TARGET="esp32"

# WiFi (Matter uses WiFi for data transport)
CONFIG_ENABLE_WIFI_STATION=y

# Disable Thread (not available on ESP32, would cause build errors if enabled)
CONFIG_OPENTHREAD_ENABLED=n

# BLE (needed for Matter commissioning)
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
# Disable unused NimBLE roles to save memory
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n

# Matter commissioning uses BLE + WiFi (not Thread)
CONFIG_USE_MINIMAL_MDNS=n

# Memory optimizations (important on ESP32 — see Section 4)
CONFIG_NEWLIB_NANO_FORMAT=y
CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=2
CONFIG_ENABLE_CHIP_SHELL=n

# Move BT controller to flash (saves 19 KB IRAM at cost of 143 KB flash)
CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y

# Move FreeRTOS to flash (saves ~8 KB IRAM)
CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y

# Reduce stack sizes
CONFIG_ESP_MAIN_TASK_STACK_SIZE=3072
CONFIG_CHIP_TASK_STACK_SIZE=6144

# Flash SPI implementation from ROM (saves 9.5 KB IRAM)
CONFIG_SPI_FLASH_ROM_IMPL=y

# Factory data provider (production)
# CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER=y
# CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER=y
# CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="fctry"

# Log level (reduce in production)
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

> **Key difference from C6 Thread config:** No `CONFIG_OPENTHREAD_*` options,
> `CONFIG_ENABLE_WIFI_STATION=y` instead of `=n`, no Thread partition in
> `partitions.csv`.

---

## 6. Docker Build Environment

Uses the same `espressif/esp-matter:release-v1.5_idf_v5.4.1` image as the
NanoC6-Switch project. The only change is `IDF_TARGET=esp32`.

The image contains:
- ESP-IDF v5.4.1 at `/opt/espressif/esp-idf`
- esp-matter SDK at `/opt/espressif/esp-matter`
- All toolchains (xtensa-esp32, riscv32-esp, etc.)

### Build in Docker, Flash on Host

Docker for Mac has no USB device passthrough (runs in a VM). Strategy:
- `make build` → idf.py runs inside Docker container
- `make flash` → esptool runs natively on macOS host

### CMakeLists.txt for Matter (future)

When Matter is added, the root CMakeLists.txt will need:

```cmake
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH")
endif()
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/examples/common/utils")

project(M5CoreInk-Switch)

# ESP32 Xtensa compiler flags (same as NanoC6 but for xtensa target)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
```

---

## 7. USB/Serial & Flashing

### USB-Serial Chip Variants

M5Stack ships two hardware revisions of the Core Ink with different USB-serial chips:

| Version | Chip | macOS Port | Driver |
|---|---|---|---|
| Older (v1.x) | Silicon Labs CP2104 | `/dev/cu.usbserial-XXXXXXXX` | Built-in (macOS Catalina+) |
| Older macOS | CP2104 | `/dev/cu.SLAB_USBtoUART` | Built-in |
| Newer | WCH CH9102F | `/dev/cu.usbserial-XXXXXXXX` | Built-in (macOS Ventura+) |
| Ventura- + CH9102F | CH9102F | `/dev/cu.wchusbserial*` | Needs [WCH driver](https://github.com/WCHSoftGroup/ch34xser_macos) |

**To identify your chip:** Apple menu → About This Mac → System Report → USB.
Look for "CP210x" (Silicon Labs) or "USB Single Serial" / "USB2.0-Serial" (WCH).

### Flashing

```bash
# Auto-detect port (Makefile does this automatically)
ls /dev/cu.usbserial*

# Flash with esptool (host)
cd build && esptool --port /dev/cu.usbserial-0001 write_flash @flash_args

# If CH9102F issues on older macOS, reduce baud rate:
cd build && esptool --port /dev/cu.usbserial-0001 -b 115200 write_flash @flash_args
```

### Install Host Tools

```bash
brew install esptool
pip3 install --user esp-idf-monitor   # optional, for better serial monitor
```

---

## 8. Known Issues & Gotchas

### GPIO10 / G10 on PICO-D4

On a standard ESP32 module, GPIO6–11 are used for the external SPI flash. On the
ESP32-PICO-D4, the 4 MB flash is embedded inside the SiP using internal connections,
so **external GPIO10 is available** as a regular output — M5Stack uses it for the
green LED. Safe to use.

### POWER_HOLD (GPIO12)

When running on battery, GPIO12 must be driven HIGH at firmware startup or the
device will power off immediately. USB power bypasses this. Always set it HIGH
at boot for robustness:

```c
gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_12, 1);
```

### Memory Tighter Than It Looks

520 KB total SRAM sounds generous but the effective heap during Matter operation
is 30–80 KB. Apply optimizations from Section 5 (move BT/FreeRTOS to flash, etc.).

### No PSRAM

The Core Ink / PICO-D4 has no PSRAM. Unlike ESP32-S3 modules with 8 MB PSRAM,
all heap is from the 520 KB internal SRAM. Be conservative with allocations.

### BLE 4.2 (not 5.x)

Matter commissioning works fine with BLE 4.2. No issue here.

### CH9102F "Failed to write to target RAM"

On macOS Monterey and earlier with CH9102F chip, the built-in driver may fail.
Install the [WCH CH34x driver](https://github.com/WCHSoftGroup/ch34xser_macos)
or lower the baud rate to 115200.

### Dual-core Advantage

The PICO-D4 has two LX6 cores. PRO_CPU (core 0) runs WiFi/BLE stack; APP_CPU
(core 1) is available for application code. This helps Matter's concurrent
networking more than a single-core RISC-V setup (ESP32-C6).

---

## Sources

- [M5Stack Core Ink Docs](https://docs.m5stack.com/en/core/coreink)
- [M5Core-Ink GitHub (config.h GPIO defs)](https://github.com/m5stack/M5Core-Ink)
- [ESP32-PICO-D4 Datasheet (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32-pico-d4_datasheet_en.pdf)
- [esp-matter Developing Guide](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html)
- [esp-matter RAM & Flash Optimizations](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/optimizations.html)
- [ESP-IDF Memory Types (ESP32)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/memory-types.html)
- [WCH CH34x macOS Driver](https://github.com/WCHSoftGroup/ch34xser_macos)

# Building and Flashing

This guide covers building the firmware from source and flashing it to the device. These are developer tasks — end users with a pre-flashed device can skip this entirely.

---

## Prerequisites

| Tool | Purpose |
|------|---------|
| [Docker](https://www.docker.com/) | Runs the ESP-IDF / ESP-Matter build environment (no local toolchain needed) |
| [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/) | Flashes the firmware over USB (installed on the host, not in Docker) |
| `make` | Runs build targets |
| USB-C cable | Connects the device for flashing and monitoring |

---

## First-time setup

### 1. Build the Docker image

The firmware compiles inside a Docker container with ESP-IDF v5.4.1 and the ESP-Matter SDK. Build the image once:

```bash
make image-build
```

This takes 10–20 minutes on first run. Subsequent builds use the cached image.

### 2. Generate pairing credentials

Each device should have a unique pairing passcode and discriminator. Generate them:

```bash
make generate-pairing
```

This will:
- Generate a random discriminator and passcode
- Prompt you to confirm before applying
- Update `main/include/CHIPPairingConfig.h` with the new credentials
- Generate a QR code image at `docs/img/pairing_qr.png`

> **Important:** You must rebuild and reflash after generating new pairing credentials. The QR code shown on the device display is baked into the firmware at build time.

---

## Build

```bash
make build
```

Compiles the firmware inside the Docker container. Output is written to `build/`.

For a full clean rebuild:

```bash
make rebuild
```

---

## Flash

Connect the device via USB-C, then:

```bash
make flash
```

The Makefile auto-detects the serial port (scans for `/dev/cu.usbserial*`, `/dev/ttyUSB*`, etc.). To override:

```bash
make flash PORT=/dev/cu.usbserial-XXXX
```

The device reboots after flashing and shows the QR code for pairing.

---

## Monitor serial output

```bash
make monitor
```

Opens a serial monitor at 115200 baud via `screen`, logging to `screenlog.0`. Previous logs are archived to `screenlog.YYYYMMDDHHMMSS` automatically.

---

## Full factory erase

Erases all flash (firmware + NVS + pairing data):

```bash
make erase
```

---

## All targets

```bash
make help
```

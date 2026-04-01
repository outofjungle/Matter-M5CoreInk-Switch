# M5 Multipass

A physical smart home button panel built on the M5Stack Core Ink — turn any Apple Home or Google Home automation into a single button press.

> **Photo placeholder** — add a photo of your device here.

---

## What it does

M5 Multipass turns an M5Stack Core Ink into a handheld remote for your smart home. It holds up to 16 configurable buttons, each linked to an automation in your smart home app. The e-ink display shows the name, room, and icon of the currently selected button.

- Scroll through buttons with the rotary encoder (Up / Down)
- Press the middle button to trigger the selected automation
- E-ink display never needs backlight — readable in any lighting
- Works with **Apple Home**, **Google Home**, and any Matter-compatible controller
- Each button fires a momentary switch event — your smart home app decides what happens (toggle a light, run a scene, lock a door, etc.)

---

## What you need

| Item | Notes |
|------|-------|
| M5Stack Core Ink | The device this firmware runs on |
| USB-C cable | For setup, configuration, and charging |
| Smart home hub | Apple Home, Google Home, or any Matter controller |
| Chrome or Edge browser | Required for the [web configurator](https://outofjungle.github.io/Matter-M5CoreInk-Switch/) |

---

## First-time setup

### 1. Power on

Press the power button on the side of the M5Stack Core Ink. A QR code appears on the e-ink display, along with a numeric pairing code below it.

The green LED blinks about twice a second while the device is waiting to be paired.

Out of the box, four buttons are enabled with default names (Button 01 through Button 04). You can customize them later — see [Configuring your buttons](#configuring-your-buttons).

### 2. Add to your smart home app

**Apple Home:**
1. Open the Home app → tap **+** → **Add Accessory**
2. Scan the QR code shown on the device display
3. Follow the prompts — the device will appear as a switch with multiple buttons

**Google Home / other Matter controllers:**
1. Use your controller's "Add device" flow
2. Scan the QR code or enter the pairing code manually

### 3. Paired!

Once pairing is complete, the LED turns off and the display switches to the button selector showing your first enabled button. You're ready to use the device.

> If pairing fails, see [Troubleshooting](#troubleshooting).

---

## Configuring your buttons

Out of the box, four button slots are enabled with default names. You can rename them, assign rooms, choose icons, and enable or disable individual slots (up to 16 total) using the web configurator.

### Enter config mode

Hold the **top button** (the small button on the side) while pressing the power button. Keep holding until the display says "Config Mode" and the LED turns solid green.

> Config mode is only for setup. Matter and button navigation are not active in this mode.

### Open the configurator

1. Connect the device to your computer via USB-C
2. Open the [M5 Multipass Configurator](https://outofjungle.github.io/Matter-M5CoreInk-Switch/) in **Chrome or Edge** (other browsers don't support Web Serial)
3. Click **Connect** and select the USB serial port from the popup

### Edit and save

1. Click **Read from Device** — all 16 slots load into the table
2. Edit any slot:
   - **Name**: up to two words (8 characters each), shown on the display
   - **Room**: shown below the name on the display (up to 16 characters)
   - **Icon**: choose from the available icons (see [Icons](#icons))
   - **Enabled**: uncheck to hide a slot from the button cycle
3. Click **Write to Device** — changes are saved and the device reboots automatically

---

## Using the device

| Control | Action |
|---------|--------|
| Rotate Down | Move to the next enabled button |
| Rotate Up | Move to the previous enabled button |
| Press Middle | Fire the current button (triggers your automation) |

The device remembers which button was last selected, even after a reboot.

---

## LED indicator

| LED state | Meaning |
|-----------|---------|
| Blinking (twice a second) | Waiting to be paired with a smart home controller |
| Solid green | Config mode active |
| Off | Normal operation — paired and ready |

---

## Icons

Twelve icons are available in the web configurator. You can preview the [icon SVG files](icons/) in this repository.

| Name | Description |
|------|-------------|
| [`button`](icons/00_button.svg) | Generic button / switch |
| [`bulb`](icons/01_bulb.svg) | Light bulb |
| [`plug`](icons/02_plug.svg) | Power outlet / plug |
| [`tv`](icons/03_tv.svg) | Television |
| [`fan`](icons/04_fan.svg) | Fan or ventilation |
| [`movie`](icons/05_movie.svg) | Movie / cinema |
| [`games`](icons/06_games.svg) | Gamepad / entertainment |
| [`reading`](icons/07_reading.svg) | Book / reading light |
| [`relax`](icons/08_relax.svg) | Relaxation / cocktail |
| [`morning`](icons/09_morning.svg) | Morning / sunrise |
| [`night`](icons/10_night.svg) | Night / moon |
| [`danger`](icons/11_danger.svg) | Warning / experimental |

---

## Factory reset

A factory reset clears all button configuration and removes the device from your smart home controller's memory. Use this if you want to re-pair or start fresh.

1. Hold the **top button** while pressing power (same as entering config mode)
2. **Keep holding** the top button — do not release
3. After about 5 seconds, the LED starts blinking rapidly — this means the reset is armed
4. Keep holding for another 10 seconds
5. The device resets and reboots — the QR code reappears on the display

Release the button at any point before the countdown finishes to cancel and stay in config mode.

> Total hold time to confirm reset: approximately 15 seconds.

---

## Troubleshooting

**The device won't power on**
The battery may be dead. Press the side power button to wake the device, wait a couple seconds, then press or move the rotary switch. If the green LED does not flash, the battery is empty. Connect the device to a USB-C power source — this will charge the battery and boot the device at the same time.

**A button press does nothing**
The device only sends an event to your smart home controller — you need an automation set up in your smart home app that responds to that button. Check that the button's endpoint has an automation assigned in Apple Home or Google Home.

**The configurator won't connect**
Make sure the device is in config mode (hold the top button while powering on). The display should say "Config Mode" and the LED should be solid green. Only Chrome and Edge support Web Serial.

**The QR code won't scan** *(advanced — requires developer tools)*
Try the manual pairing code shown below the QR code on the display first. If that doesn't work, the pairing credentials may need to be regenerated. This requires a computer with the build tools set up: run `make generate-pairing` to create new credentials, then `make build && make flash` to compile and flash the updated firmware. See [Building and flashing](docs/building-and-flashing.md) for details.

---

## For developers

Building from source, flashing firmware, and regenerating pairing credentials are covered in [docs/building-and-flashing.md](docs/building-and-flashing.md).

/*
   M5 Multipass - Hardware Configuration & Private Declarations
   M5Stack Core Ink (ESP32-PICO-D4)
*/

#pragma once

#include <esp_err.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// GPIO Pin Definitions (M5Stack Core Ink)
// Source: https://github.com/m5stack/M5Core-Ink/blob/master/src/utility/config.h
// ---------------------------------------------------------------------------

// Buttons (active LOW — internal pull-up)
#define BUTTON_UP_PIN    GPIO_NUM_37   // Rotary encoder: Up direction
#define BUTTON_DOWN_PIN  GPIO_NUM_39   // Rotary encoder: Down direction
#define BUTTON_MID_PIN   GPIO_NUM_38   // Rotary encoder: Push (Middle)

// Status LED
#define LED_PIN          GPIO_NUM_10   // Green LED (G10), active HIGH

// Power management (MUST be driven HIGH at boot to stay on battery)
#define POWER_HOLD_PIN   GPIO_NUM_12

// ---------------------------------------------------------------------------
// Switch Configuration
// ---------------------------------------------------------------------------

#define NUM_SWITCHES     3

// Indices into s_endpoint_ids[] — match button order UP/DOWN/MID
#define SWITCH_UP_IDX    0
#define SWITCH_DOWN_IDX  1
#define SWITCH_MID_IDX   2

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// Long press duration (ms) before factory reset is triggered on MID button
#define FACTORY_RESET_LONG_PRESS_MS  5000

// LED blink half-periods (ms) — time LED spends in each on/off state
#define LED_BLINK_FAST_MS    250   // 2 Hz  — uncommissioned / pairing mode
#define LED_BLINK_SLOW_MS   1000   // 0.5 Hz — commissioned

// ---------------------------------------------------------------------------
// Driver API
// ---------------------------------------------------------------------------

typedef void *app_driver_handle_t;

/**
 * @brief Initialize all three switch buttons.
 *
 * Registers press/release Matter event callbacks and factory-reset handler.
 *
 * @param endpoint_ids  Array of NUM_SWITCHES endpoint IDs, indexed by
 *                      SWITCH_UP_IDX / SWITCH_DOWN_IDX / SWITCH_MID_IDX.
 * @return ESP_OK on success
 */
esp_err_t app_driver_buttons_init(uint16_t *endpoint_ids);

/**
 * @brief Set the status LED on or off directly (raw GPIO, no timer).
 *        Used by app_reset for its blocking countdown sequence.
 */
void app_driver_led_set(bool on);

/**
 * @brief Start (or change rate of) the LED blink timer.
 *
 * @param half_period_ms  Time (ms) the LED stays on or off per half-cycle.
 *                        Use LED_BLINK_FAST_MS or LED_BLINK_SLOW_MS.
 */
void app_driver_led_blink_start(uint32_t half_period_ms);

/**
 * @brief Stop the LED blink timer and turn the LED off.
 */
void app_driver_led_blink_stop(void);

#ifdef __cplusplus
}
#endif

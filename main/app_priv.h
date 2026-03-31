/*
   M5 Multipass - Hardware Configuration & Private Declarations
   M5Stack Core Ink (ESP32-PICO-D4)
*/

#pragma once

#include <esp_err.h>
#include <driver/gpio.h>

// ---------------------------------------------------------------------------
// GPIO Pin Definitions (M5Stack Core Ink)
// Source: https://github.com/m5stack/M5Core-Ink/blob/master/src/utility/config.h
// ---------------------------------------------------------------------------

// Buttons (active LOW — internal pull-up)
#define BUTTON_UP_PIN    GPIO_NUM_37   // Rotary encoder: Up direction
#define BUTTON_DOWN_PIN  GPIO_NUM_39   // Rotary encoder: Down direction
#define BUTTON_MID_PIN   GPIO_NUM_38   // Rotary encoder: Push (Middle)
#define BUTTON_EXT_PIN   GPIO_NUM_5    // External button (also CONFIG_MODE_PIN at boot)

// Status LED
#define LED_PIN          GPIO_NUM_10   // Green LED (G10), active HIGH

// Power management (MUST be driven HIGH at boot to stay on battery)
#define POWER_HOLD_PIN   GPIO_NUM_12

// Configuration mode entry (hold LOW at boot → CONFIG mode; floating/HIGH → NORMAL mode)
#define CONFIG_MODE_PIN  GPIO_NUM_5

// ---------------------------------------------------------------------------
// Device Mode FSM
// Determined once at boot by reading CONFIG_MODE_PIN (GPIO 5).
// ---------------------------------------------------------------------------

typedef enum {
    DEVICE_MODE_NORMAL,   // Full Matter stack; serial task disabled
    DEVICE_MODE_CONFIG,   // Serial configurator only; Matter/buttons disabled
} device_mode_t;

// ---------------------------------------------------------------------------
// Button Configuration
// ---------------------------------------------------------------------------

// Total configurable button slots (statically defined; subset are enabled via NVS)
#define MAX_BUTTONS      16

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// Delay (ms) after config mode init: hold EXT this long to arm factory reset
#define FACTORY_RESET_ARM_DELAY_MS    5000
// Armed window (ms): hold EXT through this after ARMED to confirm reset
#define FACTORY_RESET_CANCEL_WINDOW_MS  10000

// LED blink half-periods (ms) — time LED spends in each on/off state
#define LED_BLINK_FAST_MS    250   // 2 Hz  — uncommissioned / pairing mode
#define LED_BLINK_SLOW_MS   1000   // 0.5 Hz — commissioned

// ---------------------------------------------------------------------------
// Button slot config struct
// Populated from NVS at boot. One entry per slot (0..MAX_BUTTONS-1).
// ---------------------------------------------------------------------------

struct button_slot_t {
    char button_name[2][9]; // two display lines: [0] top word, [1] bottom word (each max 8 chars + null)
                            // button_name[1] may be empty (single-line button name)
    char room_name[17];     // room label, lower display (max 16 chars + null)
    bool enabled;           // if false, no Matter endpoint is created for this slot
    uint8_t icon_idx;       // index into icon_list[] in icons.h (0..ICON_COUNT-1)
};

// ---------------------------------------------------------------------------
// Button config API (implemented in app_driver.cpp)
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load button configs from NVS; write defaults on first boot.
 *        Must be called after nvs_flash_init() and before endpoint creation.
 */
esp_err_t app_button_config_init(void);

/**
 * @brief Number of slots with enabled=true.
 */
int app_button_get_enabled_count(void);

/**
 * @brief Config for a given slot index (0..MAX_BUTTONS-1).
 *        Returns nullptr if idx is out of range.
 */
const button_slot_t *app_button_get_config(int slot);

/**
 * @brief Slot index of the nth enabled button (0-based n).
 *        Returns -1 if n >= enabled count.
 */
int app_button_get_enabled_slot(int n);

/**
 * @brief Current 0-based index into the enabled-button list.
 *        Useful for the boot display when already commissioned.
 */
int app_driver_get_selected_button(void);

/**
 * @brief Write one slot's config to NVS.
 *        Does not update in-memory state — caller should esp_restart() after.
 *
 * @param slot  0..MAX_BUTTONS-1
 * @param l1a   Button name word 1 (max 8 chars, non-empty)
 * @param l1b   Button name word 2 (max 8 chars, may be empty)
 * @param l2    Room name text (max 16 chars, non-empty)
 * @param en       Enabled flag
 * @param icon_idx Icon index (0..ICON_COUNT-1)
 */
esp_err_t app_button_nvs_write_slot(int slot, const char *l1a, const char *l1b,
                                     const char *l2, bool en, uint8_t icon_idx);

// ---------------------------------------------------------------------------
// Driver API
// ---------------------------------------------------------------------------

typedef void *app_driver_handle_t;

/**
 * @brief Initialize all three physical buttons.
 *
 * Up/Down navigate through enabled buttons; Mid fires Matter events on the
 * currently selected one. on_button_selected is called with the 0-based
 * enabled-list index whenever the selection changes.
 *
 * @param endpoint_ids    Endpoint IDs for enabled buttons (length = endpoint_count).
 * @param endpoint_count  Number of enabled buttons.
 * @param on_button_selected Callback invoked with enabled-list index on navigation.
 * @return ESP_OK on success
 */
esp_err_t app_driver_buttons_init(uint16_t *endpoint_ids, int endpoint_count,
                                   void (*on_button_selected)(int));

/**
 * @brief Render the selected button on the e-ink display.
 *        Implemented in app_main.cpp; called by app_driver on Up/Down press
 *        and by app_main on commissioning events.
 *
 * @param enabled_index  0-based index into the enabled-button list.
 */
void app_display_show_button(int enabled_index);

/**
 * @brief Render the configuration mode screen on the e-ink display.
 *        Implemented in app_main.cpp; called when booting in CONFIG mode.
 */
void app_display_show_config_mode(void);

/**
 * @brief Return the device mode determined at boot (NORMAL or CONFIG).
 */
device_mode_t app_get_device_mode(void);

/**
 * @brief Configure the LED GPIO (output, initially off).
 *        Called by app_driver_buttons_init() in NORMAL mode, and directly
 *        by app_main in CONFIG mode (before buttons are initialized).
 */
void app_driver_led_init(void);

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

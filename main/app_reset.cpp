/*
   M5 Multipass - Factory Reset Handler

   Factory reset is chained through the config mode boot path:

     1. Hold EXT (GPIO 5) at power-on → device enters config mode (normal boot behavior)
     2. Keep holding for FACTORY_RESET_ARM_DELAY_MS (5 s) → ARMED (LED blinks)
     3. Keep holding for FACTORY_RESET_CANCEL_WINDOW_MS (10 s) → factory reset

   Release at any point before the cancel window expires to cancel and remain
   in config mode. Not available during normal runtime.

   FSM (polling-based — button is already held at boot, so iot_button events
   cannot be used):

     CONFIG_BOOT ──(held 5s)──► ARMED ──(held 10s)──► COMMITTED ──► RESETTING
          │                       │
          └──(released)           └──(released)──► back to config mode
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "app_priv.h"
#include "app_reset.h"

static const char *TAG = "app_reset";

enum class ResetState : uint8_t { IDLE, ARMED, COMMITTED, RESETTING };

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool ext_is_held(void)
{
    // Active-low button with internal pull-up: LOW = pressed/held
    return gpio_get_level(BUTTON_EXT_PIN) == 0;
}

// Poll GPIO for up to duration_ms in poll_ms increments.
// Returns true if button was held the entire duration, false if released early.
static bool wait_held(uint32_t duration_ms, uint32_t poll_ms = 100)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += poll_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        if (!ext_is_held()) {
            return false;
        }
    }
    return true;
}

// Blink LED rapidly while polling for release.
// Returns true if button was held the entire duration, false if released early.
static bool blink_held(uint32_t duration_ms, uint32_t period_ms = 100)
{
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        if (!ext_is_held()) {
            return false;
        }
        app_driver_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
        app_driver_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
        elapsed += period_ms;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" esp_err_t app_reset_check_config_boot(void)
{
    // If EXT was already released (user just tapped to enter config mode), do nothing.
    if (!ext_is_held()) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "EXT held — hold %d s to arm factory reset",
             FACTORY_RESET_ARM_DELAY_MS / 1000);

    // CONFIG_BOOT: wait for arm delay.
    if (!wait_held(FACTORY_RESET_ARM_DELAY_MS)) {
        ESP_LOGI(TAG, "EXT released — staying in config mode");
        return ESP_OK;
    }

    // ARMED: blink LED for cancel window.
    ESP_LOGW(TAG, "Factory reset ARMED — release within %d s to cancel",
             FACTORY_RESET_CANCEL_WINDOW_MS / 1000);

    if (!blink_held(FACTORY_RESET_CANCEL_WINDOW_MS)) {
        ESP_LOGI(TAG, "Factory reset cancelled — staying in config mode");
        // Restore solid LED (config mode indicator).
        app_driver_led_set(true);
        return ESP_OK;
    }

    // COMMITTED: button held through full cancel window.
    ESP_LOGW(TAG, "Factory reset COMMITTED — resetting now");
    app_driver_led_set(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    // RESETTING — Matter stack is not running in config mode, so erase NVS
    // partitions directly (same effect as esp_matter::factory_reset()).
    ESP_LOGW(TAG, "Erasing NVS partitions...");
    nvs_flash_erase();
    nvs_flash_erase_partition("fctry");
    ESP_LOGW(TAG, "Done — rebooting");
    esp_restart();

    return ESP_OK;  // unreachable
}

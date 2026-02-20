/*
   M5 Multipass - Factory Reset Handler

   Hold BUTTON_MID_PIN for FACTORY_RESET_LONG_PRESS_MS (5 s) to trigger a
   factory reset. The green LED blinks rapidly as a countdown indicator.
   Release before the timer fires to cancel.
*/

#include <atomic>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iot_button.h>

#include "app_priv.h"
#include "app_reset.h"

static const char *TAG = "app_reset";

// Simple two-state machine to handle concurrent press-up vs long-press.
enum class ResetState : uint8_t { IDLE, ARMED };
static std::atomic<ResetState> s_state{ResetState::IDLE};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool button_is_pressed(void)
{
    // Active-low buttons with internal pull-up: LOW = pressed
    return gpio_get_level(BUTTON_MID_PIN) == 0;
}

// Blink LED rapidly for the given total duration (ms).
// Returns true if duration completed, false if reset_state left ARMED early.
static bool blink_countdown(uint32_t duration_ms, uint32_t period_ms = 100)
{
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        if (s_state.load() != ResetState::ARMED) {
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
// Button callbacks
// ---------------------------------------------------------------------------

// Fired by iot_button after the button has been held for long_press_time ms.
static void long_press_start_cb(void *arg, void *data)
{
    // Transition IDLE -> ARMED; bail out if already in progress.
    ResetState expected = ResetState::IDLE;
    if (!s_state.compare_exchange_strong(expected, ResetState::ARMED)) {
        return;
    }

    ESP_LOGW(TAG, "Factory reset: long press detected — release within 2 s to cancel");

    // 2-second cancellation window with rapid LED blink.
    if (!blink_countdown(2000, 100)) {
        ESP_LOGI(TAG, "Factory reset cancelled (button released)");
        app_driver_led_set(false);
        return;
    }

    // Still held after cancellation window — commit reset.
    if (!button_is_pressed()) {
        ESP_LOGI(TAG, "Factory reset cancelled (button released after blink)");
        app_driver_led_set(false);
        s_state = ResetState::IDLE;
        return;
    }

    ESP_LOGW(TAG, "Factory reset confirmed — resetting now");
    app_driver_led_set(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    s_state = ResetState::IDLE;
    esp_matter::factory_reset();
}

// Fired whenever the button is released.
static void press_up_cb(void *arg, void *data)
{
    ResetState expected = ResetState::ARMED;
    s_state.compare_exchange_strong(expected, ResetState::IDLE);
    // The blink_countdown loop will notice the state change and return false.
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" esp_err_t app_reset_button_register(button_handle_t handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Button handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    err = iot_button_register_cb(handle, BUTTON_LONG_PRESS_START,
                                  long_press_start_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register long press callback: %d", err);
        return err;
    }

    err = iot_button_register_cb(handle, BUTTON_PRESS_UP,
                                  press_up_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register press-up callback: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Factory reset handler registered (hold MID %d ms to reset)",
             FACTORY_RESET_LONG_PRESS_MS);
    return ESP_OK;
}

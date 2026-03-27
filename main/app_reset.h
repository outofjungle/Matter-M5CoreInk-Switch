/*
   M5 Multipass - Factory Reset Handler Header
*/

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check whether factory reset should be triggered from the config mode
 *        boot path and run the FSM if so.
 *
 * Call this after config mode init completes. If EXT (GPIO 5) is still held,
 * the function waits FACTORY_RESET_ARM_DELAY_MS (5 s); if still held, it arms
 * the reset and blinks the LED for FACTORY_RESET_CANCEL_WINDOW_MS (10 s).
 * Release at any point to cancel. If held through the full window, factory
 * reset executes (does not return).
 *
 * If EXT is already released when called, returns immediately.
 *
 * @return ESP_OK (always, unless factory_reset() is called)
 */
esp_err_t app_reset_check_config_boot(void);

#ifdef __cplusplus
}
#endif

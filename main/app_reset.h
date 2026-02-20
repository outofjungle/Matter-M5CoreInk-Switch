/*
   M5 Multipass - Factory Reset Handler Header
*/

#pragma once

#include <esp_err.h>
#include <iot_button.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register factory reset callbacks on the given button handle.
 *
 * Holding the button for FACTORY_RESET_LONG_PRESS_MS (5 s) triggers a
 * factory reset. The green LED flashes rapidly as a countdown indicator.
 *
 * @param handle  iot_button handle
 * @return ESP_OK on success
 */
esp_err_t app_reset_button_register(button_handle_t handle);

#ifdef __cplusplus
}
#endif

/*
   M5 Multipass - Serial Configurator API
   Listens on UART0 for CBOR-over-SLIP frames from the web configurator.
*/

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install UART driver on UART0 and start the serial command handler task.
 *        Must be called after nvs_flash_init() and app_switch_config_init().
 */
esp_err_t app_serial_init(void);

#ifdef __cplusplus
}
#endif

/*
 * QR Code renderer — renders a Matter pairing payload onto the e-ink display.
 */

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Render a QR code for the given payload string onto the e-ink display.
 *
 *   1. Clears the display to white.
 *   2. Encodes the payload as a QR code (ECC_MED, max version 5).
 *   3. Draws scaled modules centred on the 200×200 panel.
 *   4. Triggers a full refresh (~820 ms).
 *
 * @param payload  QR payload string, e.g. "MT:-24J0S0.030UU741J00"
 * @return ESP_OK on success, ESP_FAIL / ESP_ERR_NO_MEM on error.
 */
esp_err_t qr_render_to_eink(const char *payload);

#ifdef __cplusplus
}
#endif

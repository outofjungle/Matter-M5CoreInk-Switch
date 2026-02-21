/*
 * QR Code renderer for M5Stack Core Ink e-ink display.
 *
 * Uses espressif/qrcode (Nayuki wrapper) to encode the payload,
 * then draws scaled modules via eink_fill_rect().
 */

#include "qr_render.h"
#include "eink_display.h"
#include "qrcode.h"

#include <esp_log.h>

static const char *TAG = "qr_render";

/* ---- Display callback ---- */

static void qr_display_cb(esp_qrcode_handle_t qrcode)
{
    int size = esp_qrcode_get_size(qrcode);  /* e.g. 25 for version 2 */

    /* Scale: at most 7 px/module so there is a quiet-zone margin.
     * Also cap at floor(200/size) to ensure the image fits the panel. */
    int scale = 200 / size;
    if (scale > 7) scale = 7;
    int offset = (200 - size * scale) / 2;  /* centre horizontally and vertically */

    ESP_LOGI(TAG, "QR size=%d scale=%d offset=%d", size, scale, offset);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                eink_fill_rect(
                    (uint16_t)(offset + x * scale),
                    (uint16_t)(offset + y * scale),
                    (uint16_t)scale,
                    (uint16_t)scale,
                    true   /* black */
                );
            }
        }
    }

    eink_refresh();
}

/* ---- Public API ---- */

esp_err_t qr_render_to_eink(const char *payload)
{
    ESP_LOGI(TAG, "rendering: %s", payload);

    eink_clear(false);  /* white background */

    esp_qrcode_config_t cfg = {
        .display_func       = qr_display_cb,
        .max_qrcode_version = 5,
        .qrcode_ecc_level   = ESP_QRCODE_ECC_MED,
        .user_data          = NULL,
    };

    esp_err_t err = esp_qrcode_generate(&cfg, payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_qrcode_generate failed: %d", err);
    }
    return err;
}

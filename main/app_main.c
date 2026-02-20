#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "blink";

/*
 * M5Stack Core Ink pin definitions
 * Reference: https://github.com/m5stack/M5Core-Ink/blob/master/src/utility/config.h
 */
#define LED_PIN         GPIO_NUM_10  /* Green LED on G10, active HIGH */
#define POWER_HOLD_PIN  GPIO_NUM_12  /* Must be HIGH to stay powered on battery */

void app_main(void)
{
    ESP_LOGI(TAG, "M5Stack Core Ink - LED blink test");
    ESP_LOGI(TAG, "ESP32-PICO-D4 build verification");

    /*
     * Hold power on.
     * Required when running on battery — GPIO12 must be HIGH or the device
     * powers off immediately. Safe to set when on USB as well.
     */
    gpio_reset_pin(POWER_HOLD_PIN);
    gpio_set_direction(POWER_HOLD_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(POWER_HOLD_PIN, 1);

    /* Configure LED pin */
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "Blinking LED on GPIO%d every 500 ms", LED_PIN);

    int led_state = 0;
    while (1) {
        led_state = !led_state;
        gpio_set_level(LED_PIN, led_state);
        ESP_LOGI(TAG, "LED %s", led_state ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

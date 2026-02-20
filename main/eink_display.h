/*
 * JD79653A E-Ink Display Driver — Public API
 * M5Stack Core Ink (GDEW0154M09, 200×200 B/W, SPI)
 *
 * GPIO map (from M5Stack Core Ink hardware docs):
 *   MOSI=GPIO23  SCK=GPIO18  CS=GPIO9  DC=GPIO15  RST=GPIO0  BUSY=GPIO4
 *
 * BUSY pin polarity: LOW = busy, HIGH = ready  (inverted vs most displays)
 */

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EINK_WIDTH   200
#define EINK_HEIGHT  200

/**
 * @brief Initialize GPIO, SPI bus, hardware reset, and JD79653A init sequence.
 *        Must be called once before any other eink_ function.
 */
esp_err_t eink_init(void);

/**
 * @brief Fill the entire framebuffer with white (false) or black (true).
 */
void eink_clear(bool black);

/**
 * @brief Set a single pixel in the framebuffer (not yet sent to display).
 */
void eink_set_pixel(uint16_t x, uint16_t y, bool black);

/**
 * @brief Fill a rectangle in the framebuffer (not yet sent to display).
 */
void eink_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool black);

/**
 * @brief Full refresh: transmit framebuffer to display and trigger update (~820 ms).
 *        Saves current frame as "old" frame for next refresh.
 */
esp_err_t eink_refresh(void);

/**
 * @brief Power off the display charge pump.  Display retains its image.
 *        Call after eink_refresh() to save power.
 */
void eink_power_off(void);

/**
 * @brief Enter deep sleep.  Requires hardware reset + full re-init to wake.
 */
void eink_deep_sleep(void);

#ifdef __cplusplus
}
#endif

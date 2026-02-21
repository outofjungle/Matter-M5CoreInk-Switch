/*
 * JD79653A E-Ink Display Driver
 * M5Stack Core Ink (GDEW0154M09, 200×200 B/W, SPI)
 *
 * Full-refresh LUTs sourced from GxEPD2_154_M09.cpp (ZinggJM/GxEPD2).
 */

#include "eink_display.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char *TAG = "eink";

/* ---- GPIO pins ---- */
#define EINK_MOSI   23
#define EINK_SCK    18
#define EINK_CS      9
#define EINK_DC     15
#define EINK_RST     0
#define EINK_BUSY    4

/* ---- Framebuffer geometry ---- */
#define EINK_ROW_BYTES  (EINK_WIDTH / 8)           /* 25 */
#define EINK_BUF_SIZE   (EINK_ROW_BYTES * EINK_HEIGHT)  /* 5000 */

/* ---- Module state ---- */
static spi_device_handle_t s_spi   = NULL;
static uint8_t            *s_buf_new = NULL;   /* current frame */
static uint8_t            *s_buf_old = NULL;   /* previous frame (DTM1) */

/* ====================================================================
 * Full-refresh LUTs from GxEPD2_154_M09 (ZinggJM/GxEPD2)
 * ==================================================================== */

static const uint8_t lut_vcom_dc[56] = {  /* cmd 0x20 */
    0x01, 0x05, 0x05, 0x05, 0x05, 0x01, 0x01,
    0x01, 0x05, 0x05, 0x05, 0x05, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lut_ww[42] = {  /* cmd 0x21 */
    0x01, 0x45, 0x45, 0x43, 0x44, 0x01, 0x01,
    0x01, 0x87, 0x83, 0x87, 0x06, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lut_bw[56] = {  /* cmd 0x22 */
    0x01, 0x05, 0x05, 0x45, 0x42, 0x01, 0x01,
    0x01, 0x87, 0x85, 0x85, 0x85, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lut_wb[56] = {  /* cmd 0x23 */
    0x01, 0x08, 0x08, 0x82, 0x42, 0x01, 0x01,
    0x01, 0x45, 0x45, 0x45, 0x45, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lut_bb[56] = {  /* cmd 0x24 */
    0x01, 0x85, 0x85, 0x85, 0x83, 0x01, 0x01,
    0x01, 0x45, 0x45, 0x04, 0x48, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ====================================================================
 * Low-level SPI helpers
 * ==================================================================== */

static void send_cmd(uint8_t cmd)
{
    gpio_set_level(EINK_DC, 0);
    spi_transaction_t t = {
        .flags     = SPI_TRANS_USE_TXDATA,
        .length    = 8,
        .tx_data   = { cmd },
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void send_buf(const uint8_t *data, size_t len)
{
    if (!len) return;
    gpio_set_level(EINK_DC, 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void send_byte(uint8_t b)
{
    gpio_set_level(EINK_DC, 1);
    spi_transaction_t t = {
        .flags   = SPI_TRANS_USE_TXDATA,
        .length  = 8,
        .tx_data = { b },
    };
    spi_device_polling_transmit(s_spi, &t);
}

/* BUSY pin polarity: LOW = busy, HIGH = ready */
static void wait_busy(void)
{
    while (gpio_get_level(EINK_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ====================================================================
 * Public API
 * ==================================================================== */

esp_err_t eink_init(void)
{
    /* Allocate DMA-capable framebuffers */
    s_buf_new = heap_caps_calloc(1, EINK_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_buf_old = heap_caps_calloc(1, EINK_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_buf_new || !s_buf_old) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* Configure DC and RST as outputs */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << EINK_DC) | (1ULL << EINK_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    /* Configure BUSY as input (hardware pull-up on board) */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << EINK_BUSY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    /* SPI bus (VSPI / SPI3) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = EINK_MOSI,
        .miso_io_num   = -1,
        .sclk_io_num   = EINK_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EINK_BUF_SIZE + 16,
    };
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %d", err);
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1000000,  /* 1 MHz — safe for partial refresh */
        .mode           = 0,
        .spics_io_num   = EINK_CS,
        .queue_size     = 1,
    };
    err = spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %d", err);
        return err;
    }

    /* Hardware reset */
    gpio_set_level(EINK_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EINK_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();

    /* Panel Setting (PSR) */
    send_cmd(0x00); send_byte(0xDF); send_byte(0x0E);

    /* FitiPower internal registers */
    send_cmd(0x4D); send_byte(0x55);
    send_cmd(0xAA); send_byte(0x0F);
    send_cmd(0xE9); send_byte(0x02);
    send_cmd(0xB6); send_byte(0x11);
    send_cmd(0xF3); send_byte(0x0A);

    /* Resolution: 200×200 */
    send_cmd(0x61); send_byte(0xC8); send_byte(0x00); send_byte(0xC8);

    /* TCON */
    send_cmd(0x60); send_byte(0x00);

    /* VCOM / Data interval (partial mode default) */
    send_cmd(0x50); send_byte(0xD7);

    /* Power sequence timing */
    send_cmd(0xE3); send_byte(0x00);

    /* Power ON */
    send_cmd(0x04);
    wait_busy();

    /* Load full-refresh LUTs */
    send_cmd(0x20); send_buf(lut_vcom_dc, sizeof(lut_vcom_dc));
    send_cmd(0x21); send_buf(lut_ww,      sizeof(lut_ww));
    send_cmd(0x22); send_buf(lut_bw,      sizeof(lut_bw));
    send_cmd(0x23); send_buf(lut_wb,      sizeof(lut_wb));
    send_cmd(0x24); send_buf(lut_bb,      sizeof(lut_bb));

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

void eink_clear(bool black)
{
    memset(s_buf_new, black ? 0xFF : 0x00, EINK_BUF_SIZE);
}

void eink_set_pixel(uint16_t x, uint16_t y, bool black)
{
    if (x >= EINK_WIDTH || y >= EINK_HEIGHT) return;
    uint16_t byte_idx = (x >> 3) + (uint16_t)(y * EINK_ROW_BYTES);
    uint8_t  bit_idx  = 7 - (x & 0x07);
    if (black)
        s_buf_new[byte_idx] |=  (1U << bit_idx);
    else
        s_buf_new[byte_idx] &= ~(1U << bit_idx);
}

void eink_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool black)
{
    for (uint16_t dy = 0; dy < h; dy++)
        for (uint16_t dx = 0; dx < w; dx++)
            eink_set_pixel((uint16_t)(x + dx), (uint16_t)(y + dy), black);
}

esp_err_t eink_refresh(void)
{
    /* Set VCOM for full-refresh mode */
    send_cmd(0x50); send_byte(0x97);

    /* Power ON (idempotent if already on) */
    send_cmd(0x04);
    wait_busy();

    /* DTM1: old frame */
    send_cmd(0x10);
    send_buf(s_buf_old, EINK_BUF_SIZE);

    /* DTM2: new frame */
    send_cmd(0x13);
    send_buf(s_buf_new, EINK_BUF_SIZE);

    /* Trigger display refresh (~820 ms) */
    send_cmd(0x12);
    wait_busy();

    /* Save current frame as old for the next refresh */
    memcpy(s_buf_old, s_buf_new, EINK_BUF_SIZE);

    ESP_LOGI(TAG, "refresh done");
    return ESP_OK;
}

void eink_power_off(void)
{
    send_cmd(0x50); send_byte(0xF7);
    send_cmd(0x02);
    wait_busy();
}

void eink_deep_sleep(void)
{
    eink_power_off();
    send_cmd(0x07); send_byte(0xA5);
}

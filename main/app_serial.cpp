/*
   M5 Multipass - Serial Configurator

   Listens on UART0 for CBOR-over-SLIP frames from the web configurator.

   Protocol (SLIP-framed CBOR maps):
     {cmd:"ping"}                                              → {status:"ok", mode:"config", fw:"<version>"}
     {cmd:"read"}                                              → {status:"ok", slots:[{l1a,l1b,l2,en,icon}×16]}
     {cmd:"write_slot", slot:N, l1a,l1b,l2,en,icon}           → {status:"ok"} or {status:"error",msg:"..."}
     {cmd:"reboot"}                                            → {status:"ok"} then esp_restart()

   SLIP (RFC 1055): 0xC0 = frame delimiter, 0xDB 0xDC = escaped 0xC0,
   0xDB 0xDD = escaped 0xDB. Binary frames are distinct from ASCII log output.
*/

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <cbor.h>

#include "app_priv.h"
#include "app_serial.h"
#include "icons.h"

static const char *TAG = "app_serial";

// ---------------------------------------------------------------------------
// SLIP constants
// ---------------------------------------------------------------------------

#define SLIP_END     0xC0u
#define SLIP_ESC     0xDBu
#define SLIP_ESC_END 0xDCu
#define SLIP_ESC_ESC 0xDDu

#define RX_BUF_SIZE   512
#define TX_BUF_SIZE   2048
#define CBOR_RSP_MAX  1024   // max CBOR response bytes (read response ~700)
#define SLIP_RSP_MAX  2200   // max SLIP-encoded response (worst case 2× CBOR + 2)
#define FRAME_MAX     256    // max incoming SLIP-decoded frame (write_slot cmd ~80 bytes)

// ---------------------------------------------------------------------------
// SLIP helpers
// ---------------------------------------------------------------------------

static size_t slip_encode(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t out_max)
{
    size_t o = 0;
    if (o < out_max) out[o++] = SLIP_END;
    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == SLIP_END) {
            if (o + 2 > out_max) return 0;
            out[o++] = SLIP_ESC;
            out[o++] = SLIP_ESC_END;
        } else if (in[i] == SLIP_ESC) {
            if (o + 2 > out_max) return 0;
            out[o++] = SLIP_ESC;
            out[o++] = SLIP_ESC_ESC;
        } else {
            if (o >= out_max) return 0;
            out[o++] = in[i];
        }
    }
    if (o < out_max) out[o++] = SLIP_END;
    return o;
}

static size_t slip_decode(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t out_max)
{
    size_t o = 0;
    bool esc = false;
    for (size_t i = 0; i < in_len; i++) {
        uint8_t b = in[i];
        if (esc) {
            if (o >= out_max) return 0;
            out[o++] = (b == SLIP_ESC_END) ? (uint8_t)SLIP_END
                     : (b == SLIP_ESC_ESC) ? (uint8_t)SLIP_ESC
                     : b;
            esc = false;
        } else if (b == SLIP_ESC) {
            esc = true;
        } else {
            if (o >= out_max) return 0;
            out[o++] = b;
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

static void send_response(const uint8_t *cbor_buf, size_t cbor_len)
{
    static uint8_t slip_buf[SLIP_RSP_MAX];
    size_t slip_len = slip_encode(cbor_buf, cbor_len, slip_buf, sizeof(slip_buf));
    if (slip_len > 0) {
        uart_write_bytes(UART_NUM_0, slip_buf, slip_len);
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
    }
}

static void send_status(const char *status, const char *msg)
{
    uint8_t cbor_buf[128];
    CborEncoder enc, map;
    cbor_encoder_init(&enc, cbor_buf, sizeof(cbor_buf), 0);
    cbor_encoder_create_map(&enc, &map, msg ? 3 : 2);
    cbor_encode_text_stringz(&map, "status");
    cbor_encode_text_stringz(&map, status);
    if (msg) {
        cbor_encode_text_stringz(&map, "msg");
        cbor_encode_text_stringz(&map, msg);
    }
    cbor_encoder_close_container(&enc, &map);
    size_t len = cbor_encoder_get_buffer_size(&enc, cbor_buf);
    send_response(cbor_buf, len);
}

// ---------------------------------------------------------------------------
// Command: icons — return available icon catalog + default index
// ---------------------------------------------------------------------------

static void handle_icons(void)
{
    // Find the default icon index ("power", fallback 0)
    uint8_t def_idx = 0;
    for (int i = 0; i < ICON_COUNT; i++) {
        if (strcmp(icon_names[i], "power") == 0) { def_idx = (uint8_t)i; break; }
    }

    // Response: { status, default, icons: [ {name, idx} x ICON_COUNT ] }
    static uint8_t cbor_buf[512];
    CborEncoder enc, map, arr, item;
    cbor_encoder_init(&enc, cbor_buf, sizeof(cbor_buf), 0);
    cbor_encoder_create_map(&enc, &map, 3);
    cbor_encode_text_stringz(&map, "status");
    cbor_encode_text_stringz(&map, "ok");
    cbor_encode_text_stringz(&map, "default");
    cbor_encode_int(&map, def_idx);
    cbor_encode_text_stringz(&map, "icons");
    cbor_encoder_create_array(&map, &arr, ICON_COUNT);
    for (int i = 0; i < ICON_COUNT; i++) {
        cbor_encoder_create_map(&arr, &item, 2);
        cbor_encode_text_stringz(&item, "name");
        cbor_encode_text_stringz(&item, icon_names[i]);
        cbor_encode_text_stringz(&item, "idx");
        cbor_encode_int(&item, i);
        cbor_encoder_close_container(&arr, &item);
    }
    cbor_encoder_close_container(&map, &arr);
    cbor_encoder_close_container(&enc, &map);
    size_t len = cbor_encoder_get_buffer_size(&enc, cbor_buf);
    send_response(cbor_buf, len);
    ESP_LOGI(TAG, "Icons response sent (%d icons)", ICON_COUNT);
}

// ---------------------------------------------------------------------------
// Command: read
// ---------------------------------------------------------------------------

static void handle_read(void)
{
    static uint8_t cbor_buf[CBOR_RSP_MAX];
    CborEncoder enc, map, arr, slot_enc;
    cbor_encoder_init(&enc, cbor_buf, sizeof(cbor_buf), 0);
    cbor_encoder_create_map(&enc, &map, 2);
    cbor_encode_text_stringz(&map, "status");
    cbor_encode_text_stringz(&map, "ok");
    cbor_encode_text_stringz(&map, "slots");
    cbor_encoder_create_array(&map, &arr, MAX_BUTTONS);
    for (int i = 0; i < MAX_BUTTONS; i++) {
        const button_slot_t *cfg = app_button_get_config(i);
        cbor_encoder_create_map(&arr, &slot_enc, 5);
        cbor_encode_text_stringz(&slot_enc, "l1a");
        cbor_encode_text_stringz(&slot_enc, cfg ? cfg->button_name[0] : "Button");
        cbor_encode_text_stringz(&slot_enc, "l1b");
        cbor_encode_text_stringz(&slot_enc, cfg ? cfg->button_name[1] : "");
        cbor_encode_text_stringz(&slot_enc, "l2");
        cbor_encode_text_stringz(&slot_enc, cfg ? cfg->room_name : "?");
        cbor_encode_text_stringz(&slot_enc, "en");
        cbor_encode_boolean(&slot_enc, cfg && cfg->enabled);
        cbor_encode_text_stringz(&slot_enc, "icon");
        cbor_encode_int(&slot_enc, cfg ? cfg->icon_idx : 0);
        cbor_encoder_close_container(&arr, &slot_enc);
    }
    cbor_encoder_close_container(&map, &arr);
    cbor_encoder_close_container(&enc, &map);
    size_t len = cbor_encoder_get_buffer_size(&enc, cbor_buf);
    send_response(cbor_buf, len);
    ESP_LOGI(TAG, "Read response sent (%d CBOR bytes)", (int)len);
}

// ---------------------------------------------------------------------------
// Command: write_slot — write a single slot to NVS
// ---------------------------------------------------------------------------

static void handle_write_slot(CborValue *frame_map)
{
    // Parse flat map: { cmd, slot, l1a, l1b, l2, en, icon }
    int  slot     = -1;
    char l1a[9]   = {};
    char l1b[9]   = {};
    char l2[17]   = {};
    bool en       = false;
    uint8_t icon  = 0;
    bool has_slot = false, has_l1a = false, has_l1b = false;
    bool has_l2   = false, has_en  = false, has_icon = false;

    CborValue it = *frame_map;
    while (!cbor_value_at_end(&it)) {
        if (!cbor_value_is_text_string(&it)) {
            cbor_value_advance(&it);
            cbor_value_advance(&it);
            continue;
        }
        char key[12];
        size_t key_len = sizeof(key) - 1;
        cbor_value_copy_text_string(&it, key, &key_len, &it);
        key[key_len] = '\0';

        if (strcmp(key, "slot") == 0 && cbor_value_is_integer(&it)) {
            cbor_value_get_int(&it, &slot);
            cbor_value_advance(&it);
            has_slot = true;
        } else if (strcmp(key, "l1a") == 0 && cbor_value_is_text_string(&it)) {
            size_t vlen = sizeof(l1a) - 1;
            cbor_value_copy_text_string(&it, l1a, &vlen, &it);
            l1a[vlen] = '\0';
            has_l1a = true;
        } else if (strcmp(key, "l1b") == 0 && cbor_value_is_text_string(&it)) {
            size_t vlen = sizeof(l1b) - 1;
            cbor_value_copy_text_string(&it, l1b, &vlen, &it);
            l1b[vlen] = '\0';
            has_l1b = true;
        } else if (strcmp(key, "l2") == 0 && cbor_value_is_text_string(&it)) {
            size_t vlen = sizeof(l2) - 1;
            cbor_value_copy_text_string(&it, l2, &vlen, &it);
            l2[vlen] = '\0';
            has_l2 = true;
        } else if (strcmp(key, "en") == 0 && cbor_value_is_boolean(&it)) {
            cbor_value_get_boolean(&it, &en);
            cbor_value_advance(&it);
            has_en = true;
        } else if (strcmp(key, "icon") == 0 && cbor_value_is_integer(&it)) {
            int icon_val = 0;
            cbor_value_get_int(&it, &icon_val);
            cbor_value_advance(&it);
            icon = (icon_val >= 0 && icon_val < ICON_COUNT) ? (uint8_t)icon_val : 0;
            has_icon = true;
        } else {
            cbor_value_advance(&it);  // skip cmd and unknown keys
        }
    }

    if (!has_slot || !has_l1a || !has_l1b || !has_l2 || !has_en || !has_icon) {
        send_status("error", "missing fields");
        return;
    }
    if (slot < 0 || slot >= MAX_BUTTONS) {
        send_status("error", "slot out of range");
        return;
    }
    if (l1a[0] == '\0') { send_status("error", "l1a empty"); return; }
    if (l2[0]  == '\0') { send_status("error", "l2 empty");  return; }

    esp_err_t err = app_button_nvs_write_slot(slot, l1a, l1b, l2, en, icon);
    if (err != ESP_OK) {
        send_status("error", "NVS write failed");
        return;
    }

    send_status("ok", nullptr);
    ESP_LOGI(TAG, "write_slot %d: l1a='%s' l1b='%s' l2='%s' en=%d icon=%d",
             slot, l1a, l1b, l2, (int)en, (int)icon);
}

// ---------------------------------------------------------------------------
// Frame dispatcher
// ---------------------------------------------------------------------------

static void process_frame(const uint8_t *data, size_t len)
{
    CborParser parser;
    CborValue root;
    if (cbor_parser_init(data, len, 0, &parser, &root) != CborNoError
            || !cbor_value_is_map(&root)) {
        send_status("error", "bad frame");
        return;
    }

    CborValue map;
    cbor_value_enter_container(&root, &map);

    // Find the "cmd" key first; pass the full map iterator to write_slot
    char cmd[16] = {};
    bool cmd_found = false;

    while (!cbor_value_at_end(&map)) {
        if (!cbor_value_is_text_string(&map)) {
            cbor_value_advance(&map);
            cbor_value_advance(&map);
            continue;
        }
        char key[16];
        size_t key_len = sizeof(key) - 1;
        cbor_value_copy_text_string(&map, key, &key_len, &map);
        key[key_len] = '\0';

        if (strcmp(key, "cmd") == 0 && cbor_value_is_text_string(&map)) {
            size_t cmd_len = sizeof(cmd) - 1;
            cbor_value_copy_text_string(&map, cmd, &cmd_len, &map);
            cmd[cmd_len] = '\0';
            cmd_found = true;
        } else {
            cbor_value_advance(&map);
        }
    }

    if (!cmd_found) { send_status("error", "missing cmd"); return; }

    ESP_LOGI(TAG, "cmd='%s'", cmd);

    if (strcmp(cmd, "icons") == 0) {
        handle_icons();
    } else if (strcmp(cmd, "read") == 0) {
        handle_read();
    } else if (strcmp(cmd, "write_slot") == 0) {
        // Re-parse the frame map so write_slot can walk all keys
        CborParser p2; CborValue r2, m2;
        cbor_parser_init(data, len, 0, &p2, &r2);
        cbor_value_enter_container(&r2, &m2);
        handle_write_slot(&m2);
    } else if (strcmp(cmd, "ping") == 0) {
        const esp_app_desc_t *desc = esp_app_get_description();
        uint8_t cbor_buf[128];
        CborEncoder enc, map;
        cbor_encoder_init(&enc, cbor_buf, sizeof(cbor_buf), 0);
        cbor_encoder_create_map(&enc, &map, 3);
        cbor_encode_text_stringz(&map, "status");
        cbor_encode_text_stringz(&map, "ok");
        cbor_encode_text_stringz(&map, "mode");
        cbor_encode_text_stringz(&map, "config");
        cbor_encode_text_stringz(&map, "fw");
        cbor_encode_text_stringz(&map, desc->version);
        cbor_encoder_close_container(&enc, &map);
        send_response(cbor_buf, cbor_encoder_get_buffer_size(&enc, cbor_buf));
    } else if (strcmp(cmd, "reboot") == 0) {
        send_status("ok", nullptr);
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(200));
        esp_restart();
    } else {
        send_status("error", "unknown cmd");
    }
}

// ---------------------------------------------------------------------------
// UART RX task
// ---------------------------------------------------------------------------

static void serial_task(void *arg)
{
    static uint8_t rx_chunk[64];
    static uint8_t frame_raw[FRAME_MAX];   // raw SLIP bytes (between delimiters)
    static uint8_t frame_dec[FRAME_MAX];   // SLIP-decoded payload

    size_t frame_len = 0;
    bool in_frame = false;

    for (;;) {
        int n = uart_read_bytes(UART_NUM_0, rx_chunk, sizeof(rx_chunk),
                                pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            uint8_t b = rx_chunk[i];
            if (b == SLIP_END) {
                if (in_frame && frame_len > 0) {
                    size_t dec_len = slip_decode(frame_raw, frame_len,
                                                 frame_dec, sizeof(frame_dec));
                    if (dec_len > 0) {
                        process_frame(frame_dec, dec_len);
                    }
                }
                frame_len = 0;
                in_frame = true;
            } else if (in_frame) {
                if (frame_len < FRAME_MAX) {
                    frame_raw[frame_len++] = b;
                } else {
                    ESP_LOGW(TAG, "Frame overflow — discarding");
                    frame_len = 0;
                    in_frame = false;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------

esp_err_t app_serial_init(void)
{
    esp_err_t err = uart_driver_install(UART_NUM_0, RX_BUF_SIZE, TX_BUF_SIZE,
                                        0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %d", err);
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreate(serial_task, "serial_cfg", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serial configurator ready (UART0, CBOR/SLIP)");
    return ESP_OK;
}

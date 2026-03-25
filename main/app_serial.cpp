/*
   M5 Multipass - Serial Configurator

   Listens on UART0 for CBOR-over-SLIP frames from the web configurator.

   Protocol (SLIP-framed CBOR maps):
     {cmd:"read"}                      → {status:"ok", slots:[{l1,l2,en}×16]}
     {cmd:"write", slots:[{l1,l2,en}×16]} → {status:"ok"} or {status:"error",msg:"..."}
     {cmd:"reboot"}                    → {status:"ok"} then esp_restart()

   SLIP (RFC 1055): 0xC0 = frame delimiter, 0xDB 0xDC = escaped 0xC0,
   0xDB 0xDD = escaped 0xDB. Binary frames are distinct from ASCII log output.
*/

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <cbor.h>

#include "app_priv.h"
#include "app_serial.h"

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
#define CBOR_RSP_MAX  1024   // max CBOR response bytes (read response ~500)
#define SLIP_RSP_MAX  2200   // max SLIP-encoded response (worst case 2× CBOR + 2)
#define FRAME_MAX     600    // max incoming SLIP-decoded frame (write cmd ~500)

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
    cbor_encoder_create_array(&map, &arr, MAX_SWITCHES);
    for (int i = 0; i < MAX_SWITCHES; i++) {
        const switch_config_t *cfg = app_switch_get_config(i);
        cbor_encoder_create_map(&arr, &slot_enc, 3);
        cbor_encode_text_stringz(&slot_enc, "l1");
        cbor_encode_text_stringz(&slot_enc, cfg ? cfg->line1 : "Switch");
        cbor_encode_text_stringz(&slot_enc, "l2");
        cbor_encode_text_stringz(&slot_enc, cfg ? cfg->line2 : "?");
        cbor_encode_text_stringz(&slot_enc, "en");
        cbor_encode_boolean(&slot_enc, cfg && cfg->enabled);
        cbor_encoder_close_container(&arr, &slot_enc);
    }
    cbor_encoder_close_container(&map, &arr);
    cbor_encoder_close_container(&enc, &map);
    size_t len = cbor_encoder_get_buffer_size(&enc, cbor_buf);
    send_response(cbor_buf, len);
    ESP_LOGI(TAG, "Read response sent (%d CBOR bytes)", (int)len);
}

// ---------------------------------------------------------------------------
// Command: write
// ---------------------------------------------------------------------------

struct incoming_slot_t {
    char l1[9];
    char l2[9];
    bool en;
    bool valid;
};

static void handle_write(CborValue *slots_val)
{
    if (!cbor_value_is_array(slots_val)) {
        send_status("error", "slots must be array");
        return;
    }

    static incoming_slot_t incoming[MAX_SWITCHES];
    memset(incoming, 0, sizeof(incoming));

    CborValue arr;
    cbor_value_enter_container(slots_val, &arr);

    for (int slot = 0; slot < MAX_SWITCHES && !cbor_value_at_end(&arr); slot++) {
        if (!cbor_value_is_map(&arr)) {
            cbor_value_advance(&arr);
            continue;
        }
        CborValue slot_map;
        cbor_value_enter_container(&arr, &slot_map);

        bool has_l1 = false, has_l2 = false, has_en = false;

        while (!cbor_value_at_end(&slot_map)) {
            if (!cbor_value_is_text_string(&slot_map)) {
                cbor_value_advance(&slot_map);
                cbor_value_advance(&slot_map);
                continue;
            }
            char key[8];
            size_t key_len = sizeof(key) - 1;
            cbor_value_copy_text_string(&slot_map, key, &key_len, &slot_map);
            key[key_len] = '\0';

            if (strcmp(key, "l1") == 0 && cbor_value_is_text_string(&slot_map)) {
                size_t vlen = sizeof(incoming[slot].l1) - 1;
                cbor_value_copy_text_string(&slot_map, incoming[slot].l1, &vlen, &slot_map);
                incoming[slot].l1[vlen] = '\0';
                has_l1 = true;
            } else if (strcmp(key, "l2") == 0 && cbor_value_is_text_string(&slot_map)) {
                size_t vlen = sizeof(incoming[slot].l2) - 1;
                cbor_value_copy_text_string(&slot_map, incoming[slot].l2, &vlen, &slot_map);
                incoming[slot].l2[vlen] = '\0';
                has_l2 = true;
            } else if (strcmp(key, "en") == 0 && cbor_value_is_boolean(&slot_map)) {
                cbor_value_get_boolean(&slot_map, &incoming[slot].en);
                cbor_value_advance(&slot_map);
                has_en = true;
            } else {
                cbor_value_advance(&slot_map);  // skip unknown value
            }
        }
        cbor_value_leave_container(&arr, &slot_map);
        incoming[slot].valid = has_l1 && has_l2 && has_en;
    }

    // Validate all slots
    int enabled_count = 0;
    for (int i = 0; i < MAX_SWITCHES; i++) {
        if (!incoming[i].valid) {
            char msg[32];
            snprintf(msg, sizeof(msg), "slot %d missing fields", i);
            send_status("error", msg);
            return;
        }
        if (incoming[i].l1[0] == '\0') {
            char msg[32];
            snprintf(msg, sizeof(msg), "slot %d l1 empty", i);
            send_status("error", msg);
            return;
        }
        if (incoming[i].l2[0] == '\0') {
            char msg[32];
            snprintf(msg, sizeof(msg), "slot %d l2 empty", i);
            send_status("error", msg);
            return;
        }
        if (incoming[i].en) enabled_count++;
    }
    if (enabled_count == 0) {
        send_status("error", "at least 1 slot must be enabled");
        return;
    }

    // Write all slots to NVS
    for (int i = 0; i < MAX_SWITCHES; i++) {
        esp_err_t err = app_switch_nvs_write_slot(i,
                            incoming[i].l1, incoming[i].l2, incoming[i].en);
        if (err != ESP_OK) {
            send_status("error", "NVS write failed");
            return;
        }
    }

    send_status("ok", nullptr);
    ESP_LOGI(TAG, "Write: %d slots enabled, NVS updated", enabled_count);
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

    char cmd[16] = {};
    bool cmd_found = false;
    CborValue slots_val = {};
    bool slots_found = false;

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
        } else if (strcmp(key, "slots") == 0) {
            slots_val = map;
            slots_found = true;
            cbor_value_advance(&map);
        } else {
            cbor_value_advance(&map);
        }
    }

    if (!cmd_found) { send_status("error", "missing cmd"); return; }

    ESP_LOGI(TAG, "cmd='%s'", cmd);

    if (strcmp(cmd, "read") == 0) {
        handle_read();
    } else if (strcmp(cmd, "write") == 0) {
        if (!slots_found) { send_status("error", "missing slots"); return; }
        handle_write(&slots_val);
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

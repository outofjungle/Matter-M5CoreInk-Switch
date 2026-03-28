/*
   M5 Multipass - Main Application

   Creates a Matter device with up to MAX_BUTTONS (16) Generic Switch endpoints.
   Which buttons are active is controlled by NVS (button_name, room_name, enabled per slot).
   UP / DOWN buttons navigate enabled buttons shown on the e-ink display.
   MID button fires InitialPress + ShortRelease on the currently selected button.

   Display shows the commissioning QR code until commissioned,
   then shows button_name/room_name of the selected button from NVS.

   Hardware: M5Stack Core Ink (ESP32-PICO-D4), WiFi-only Matter transport.
*/

#include <M5GFX.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>

#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <common_macros.h>
#include <app_priv.h>

// WiFi-only build
#include <esp_wifi.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#include "include/CHIPProjectConfig.h"
#include <esp_app_desc.h>
#include "app_serial.h"
#include "app_reset.h"

#include <setup_payload/OnboardingCodesUtil.h>
#include <qrcode.h>

static const char *TAG = "app_main";

static M5GFX display;
static device_mode_t s_device_mode = DEVICE_MODE_NORMAL;

device_mode_t app_get_device_mode(void) { return s_device_mode; }

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

// Endpoint IDs for enabled buttons, built dynamically at boot
static uint16_t s_endpoint_ids[MAX_BUTTONS] = {0};
static int s_ep_count = 0;

// ---------------------------------------------------------------------------
// E-ink QR code renderer — called by esp_qrcode_generate via display_func
// ---------------------------------------------------------------------------

// Set before calling esp_qrcode_generate; read inside the callback.
static const char *s_manual_pairing_code = nullptr;

static void render_qr_on_display(esp_qrcode_handle_t qrcode)
{
    constexpr int kDisplaySize = 200;
    constexpr int kGap         = 4;   // gap between QR bottom and text
    constexpr int kTextH       = 18;  // approx height of FreeSansBold12pt7b

    int size     = esp_qrcode_get_size(qrcode);
    int scale    = kDisplaySize / (size + 4);  // fit QR to full width
    int qr_px    = size * scale;

    // Center the whole block (QR + gap + text) vertically for equal top/bottom margins
    int margin   = (kDisplaySize - (qr_px + kGap + kTextH)) / 2;
    if (margin < 0) margin = 0;

    int offset_x = (kDisplaySize - qr_px) / 2;
    int offset_y = margin;
    int text_y   = margin + qr_px + kGap + kTextH / 2;

    display.startWrite();           // begin buffered drawing
    display.fillScreen(TFT_WHITE);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                display.fillRect(offset_x + x * scale, offset_y + y * scale,
                                 scale, scale, TFT_BLACK);
            }
        }
    }

    // Draw manual pairing code centered below the QR
    if (s_manual_pairing_code) {
        display.setFont(&fonts::FreeSansBold12pt7b);
        display.setTextDatum(textdatum_t::middle_center);
        display.setTextColor(TFT_BLACK);
        display.drawString(s_manual_pairing_code, kDisplaySize / 2, text_y);
    }

    display.endWrite();             // flush to e-ink (auto-display)
    display.waitDisplay();          // wait for physical refresh
    ESP_LOGI("qr_render", "QR code rendered on e-ink (%dx%d, scale=%d)", size, size, scale);
}

// ---------------------------------------------------------------------------
// E-ink button selector renderer
// Draws room_name (small) and button_name (large) from NVS config, centered.
// Called post-commissioning with a 0-based enabled-list index.
// ---------------------------------------------------------------------------

void app_display_show_button(int enabled_index)
{
    constexpr int kDisplaySize = 200;

    int slot = app_button_get_enabled_slot(enabled_index);
    const button_slot_t *cfg = app_button_get_config(slot);
    if (!cfg) {
        ESP_LOGW("display", "No config for enabled_index=%d", enabled_index);
        return;
    }

    display.startWrite();
    display.fillScreen(TFT_WHITE);

    // button number badge: black filled circle with white number, top-left corner
    char btn_num[12];
    snprintf(btn_num, sizeof(btn_num), "%d", enabled_index + 1);
    display.fillCircle(14, 14, 12, TFT_BLACK);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_WHITE);
    display.drawString(btn_num, 14, 14);

    display.setTextColor(TFT_BLACK);

    // button_name: large font, upper half
    display.setFont(&fonts::FreeSansBold24pt7b);
    display.drawString(cfg->button_name, kDisplaySize / 2, kDisplaySize / 2 - 30);

    // room_name: white text on black rounded-rect badge, lower half
    display.setFont(&fonts::FreeSansBold12pt7b);
    {
        constexpr int kPadX = 10;
        constexpr int kPadY = 6;
        int tw = display.textWidth(cfg->room_name);
        int th = display.fontHeight();
        int cy = kDisplaySize / 2 + 20;
        int rx = kDisplaySize / 2 - tw / 2 - kPadX;
        int ry = cy - th / 2 - kPadY;
        int rw = tw + kPadX * 2;
        int rh = th + kPadY * 2;
        display.fillRoundRect(rx, ry, rw, rh, 6, TFT_BLACK);
        display.setTextColor(TFT_WHITE);
        display.drawString(cfg->room_name, kDisplaySize / 2, cy);
        display.setTextColor(TFT_BLACK);
    }

    display.endWrite();
    display.waitDisplay();
    ESP_LOGI("display", "Showing slot %d: '%s' / '%s'", slot, cfg->button_name, cfg->room_name);
}

// ---------------------------------------------------------------------------
// E-ink configuration mode screen
// ---------------------------------------------------------------------------

void app_display_show_config_mode(void)
{
    constexpr int kDisplaySize = 200;

    display.startWrite();
    display.fillScreen(TFT_WHITE);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(TFT_BLACK);

    display.setFont(&fonts::FreeSansBold24pt7b);
    display.drawString("Config", kDisplaySize / 2, kDisplaySize / 2 - 30);

    display.setFont(&fonts::FreeSansBold24pt7b);
    display.drawString("Mode", kDisplaySize / 2, kDisplaySize / 2 + 30);

    display.setFont(&fonts::FreeSans9pt7b);
    display.drawString("Serial ready", kDisplaySize / 2, kDisplaySize - 18);

    display.endWrite();
    display.waitDisplay();
    ESP_LOGI("display", "Config mode screen shown");
}

// ---------------------------------------------------------------------------
// Matter event callback
// ---------------------------------------------------------------------------

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        app_driver_led_blink_stop();
        app_display_show_button(app_driver_get_selected_button());
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail-safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        app_driver_led_blink_start(LED_BLINK_FAST_MS);
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        app_driver_led_blink_stop();
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissionMgr =
                chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
                    kTimeoutSeconds,
                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT,
                             err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Identification callback — blink LED
// ---------------------------------------------------------------------------

static esp_err_t app_identification_cb(identification::callback_type_t type,
                                        uint16_t endpoint_id,
                                        uint8_t effect_id,
                                        uint8_t effect_variant,
                                        void *priv_data)
{
    ESP_LOGI(TAG, "Identify ep=%u type=%u effect=%u", endpoint_id, type, effect_id);

    if (type == identification::callback_type_t::START ||
        type == identification::callback_type_t::EFFECT) {
        app_driver_led_blink_start(LED_BLINK_FAST_MS);
    } else if (type == identification::callback_type_t::STOP) {
        app_driver_led_blink_stop();
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Attribute update callback
// ---------------------------------------------------------------------------

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                          uint16_t endpoint_id,
                                          uint32_t cluster_id,
                                          uint32_t attribute_id,
                                          esp_matter_attr_val_t *val,
                                          void *priv_data)
{
    // Generic switches are stateless — no action needed on remote attribute writes.
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Fixed Label NVS helper
// Writes a single label/value pair into the chip-factory NVS namespace so
// the DeviceInfoProvider can serve it via the Fixed Label cluster.
// ---------------------------------------------------------------------------

static void write_fixed_label(uint16_t endpoint_id, const char *label, const char *value)
{
    nvs_handle_t h;
    if (nvs_open("chip-factory", NVS_READWRITE, &h) != ESP_OK) return;

    char key[20];
    // Count of labels for this endpoint
    snprintf(key, sizeof(key), "fl-sz/%x", endpoint_id);
    nvs_set_u32(h, key, 1);
    // Label name (index 0)
    snprintf(key, sizeof(key), "fl-k/%x/0", endpoint_id);
    nvs_set_str(h, key, label);
    // Label value (index 0)
    snprintf(key, sizeof(key), "fl-v/%x/0", endpoint_id);
    nvs_set_str(h, key, value);

    nvs_commit(h);
    nvs_close(h);
}

// ---------------------------------------------------------------------------
// CONFIG mode init (serial configurator only, no Matter)
// ---------------------------------------------------------------------------

static void init_config_mode(void)
{
    esp_err_t err = app_button_config_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button config init failed in CONFIG mode: %d", err);
    }

    app_driver_led_init();
    app_driver_led_set(true);   // solid ON — visual indicator of config mode

    err = app_serial_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Serial configurator init failed: %d", err);
    }

    app_display_show_config_mode();
    ESP_LOGI(TAG, "CONFIG MODE — serial configurator ready, Matter disabled");

    // Check for factory reset: if EXT is still held, run the armed countdown.
    app_reset_check_config_boot();
}

// ---------------------------------------------------------------------------
// NORMAL mode init (full Matter stack)
// ---------------------------------------------------------------------------

static void init_normal_mode(void)
{
    esp_err_t err = ESP_OK;

    // ----------------------------------------------------------------
    // Create Matter node
    // ----------------------------------------------------------------
    node::config_t node_config = {};
    strncpy(node_config.root_node.basic_information.node_label, "M5 Multipass",
            sizeof(node_config.root_node.basic_information.node_label) - 1);

    node_t *node = node::create(&node_config,
                                 app_attribute_update_cb,
                                 app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr,
                         ESP_LOGE(TAG, "Failed to create Matter node"));

    // ----------------------------------------------------------------
    // Load button config from NVS (must be after nvs_flash_init)
    // ----------------------------------------------------------------
    err = app_button_config_init();
    ABORT_APP_ON_FAILURE(err == ESP_OK,
                         ESP_LOGE(TAG, "Failed to init button config: %d", err));

    // ----------------------------------------------------------------
    // Create Generic Switch endpoints for enabled slots only
    // ----------------------------------------------------------------
    s_ep_count = 0;
    for (int slot = 0; slot < MAX_BUTTONS; slot++) {
        const button_slot_t *cfg = app_button_get_config(slot);
        if (!cfg || !cfg->enabled) continue;

        generic_switch::config_t sw_cfg = {};
        // Feature map: MS (MomentarySwitch=0x02) | MSR (MomentarySwitchRelease=0x04)
        // Enables InitialPress + ShortRelease — required for Apple Home single-press automations.
        sw_cfg.switch_cluster.feature_flags       = 0x06;  // MS | MSR
        sw_cfg.switch_cluster.number_of_positions = 2;
        sw_cfg.switch_cluster.current_position    = 0;

        endpoint_t *ep = generic_switch::create(node, &sw_cfg,
                                                 ENDPOINT_FLAG_NONE, nullptr);
        ABORT_APP_ON_FAILURE(ep != nullptr,
                             ESP_LOGE(TAG, "Failed to create button endpoint slot=%d", slot));

        s_endpoint_ids[s_ep_count] = endpoint::get_id(ep);
        ESP_LOGI(TAG, "Slot %d '%s %s' → endpoint %d",
                 slot, cfg->button_name, cfg->room_name, s_endpoint_ids[s_ep_count]);

        // Fixed Label cluster — label value is "button_name room_name" (e.g. "Button 1")
        cluster::fixed_label::config_t fl_cfg = {};
        cluster_t *fl = cluster::fixed_label::create(ep, &fl_cfg, CLUSTER_FLAG_SERVER);
        ABORT_APP_ON_FAILURE(fl != nullptr,
                             ESP_LOGE(TAG, "Failed to create fixed_label cluster slot=%d", slot));

        char label_val[26];  // 8 (button) + 1 (space) + 16 (room) + 1 (null)
        snprintf(label_val, sizeof(label_val), "%s %s", cfg->button_name, cfg->room_name);
        write_fixed_label(s_endpoint_ids[s_ep_count], "name", label_val);
        ESP_LOGI(TAG, "Slot %d fixed label 'name'='%s' written to NVS", slot, label_val);

        s_ep_count++;
    }

    ESP_LOGI(TAG, "Created %d Generic Switch endpoints", s_ep_count);

    // ----------------------------------------------------------------
    // Initialise buttons
    // ----------------------------------------------------------------
    err = app_driver_buttons_init(s_endpoint_ids, s_ep_count, app_display_show_button);
    ABORT_APP_ON_FAILURE(err == ESP_OK,
                         ESP_LOGE(TAG, "Failed to init buttons: %d", err));

    // ----------------------------------------------------------------
    // Start Matter
    // ----------------------------------------------------------------
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK,
                         ESP_LOGE(TAG, "Failed to start Matter: %d", err));

    if (!chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned()) {
        ESP_LOGI(TAG, "WiFi not yet provisioned — commissioning window open");
        app_driver_led_blink_start(LED_BLINK_FAST_MS);
    }

    // ----------------------------------------------------------------
    // Display: QR code if not commissioned, switch selector if already commissioned
    // ----------------------------------------------------------------
    {
        bool already_commissioned =
            chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;

        if (already_commissioned) {
            ESP_LOGI(TAG, "Already commissioned — showing button selector");
            app_display_show_button(app_driver_get_selected_button());
        } else {
            // Print QR payload to serial and render on e-ink
            char qr_buf[128];
            chip::MutableCharSpan qr_span(qr_buf);
            CHIP_ERROR chip_err = GetQRCode(qr_span, chip::RendezvousInformationFlags(
                chip::RendezvousInformationFlag::kBLE));
            if (chip_err == CHIP_NO_ERROR) {
                ESP_LOGI(TAG, "Matter QR payload: %.*s", (int)qr_span.size(), qr_span.data());
                s_manual_pairing_code = CHIP_DEVICE_CONFIG_MANUAL_PAIRING_CODE;
                esp_qrcode_config_t qr_cfg = {
                    .display_func        = render_qr_on_display,
                    .max_qrcode_version  = 10,
                    .qrcode_ecc_level    = ESP_QRCODE_ECC_MED,
                    .user_data           = nullptr,
                };
                if (esp_qrcode_generate(&qr_cfg, qr_buf) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to render QR code on display");
                }
            } else {
                ESP_LOGW(TAG, "Failed to get QR payload: %" CHIP_ERROR_FORMAT, chip_err.Format());
            }
        }
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "M5 Multipass v%s started — %d Generic Switch endpoints",
             app_desc->version, s_ep_count);
    ESP_LOGI(TAG, "=== Commissioning Info ===");
    ESP_LOGI(TAG, "Discriminator: %d (0x%03X)",
             CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR,
             CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR);
    ESP_LOGI(TAG, "Passcode: %d", CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE);
    ESP_LOGI(TAG, "See docs/img/pairing_qr.png or run: make generate-pairing");
    ESP_LOGI(TAG, "==========================");
}

// ---------------------------------------------------------------------------
// app_main — shared early init, then branch on CONFIG_MODE_PIN
// ---------------------------------------------------------------------------

extern "C" void app_main()
{
    // ----------------------------------------------------------------
    // Power hold — MUST be set HIGH immediately to stay on battery
    // ----------------------------------------------------------------
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = (1ULL << POWER_HOLD_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_cfg);
    gpio_set_level(POWER_HOLD_PIN, 1);

    // ----------------------------------------------------------------
    // Display — initialise e-ink
    // ----------------------------------------------------------------
    display.begin();
    display.setRotation(0);
    display.setEpdMode(epd_mode_t::epd_quality);

    // ----------------------------------------------------------------
    // NVS
    // ----------------------------------------------------------------
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupted — erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ----------------------------------------------------------------
    // Read GPIO 5 to determine boot mode
    // Hold LOW at boot → CONFIG mode; floating/HIGH → NORMAL mode
    // ----------------------------------------------------------------
    gpio_config_t cfg5 = {
        .pin_bit_mask = (1ULL << CONFIG_MODE_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg5);
    vTaskDelay(pdMS_TO_TICKS(10));   // let pull-up settle

    s_device_mode = (gpio_get_level(CONFIG_MODE_PIN) == 0)
                        ? DEVICE_MODE_CONFIG
                        : DEVICE_MODE_NORMAL;

    ESP_LOGI(TAG, "Boot mode: %s (GPIO5=%d)",
             s_device_mode == DEVICE_MODE_CONFIG ? "CONFIG" : "NORMAL",
             gpio_get_level(CONFIG_MODE_PIN));

    // ----------------------------------------------------------------
    // Branch
    // ----------------------------------------------------------------
    if (s_device_mode == DEVICE_MODE_CONFIG) {
        init_config_mode();
    } else {
        init_normal_mode();
    }
}

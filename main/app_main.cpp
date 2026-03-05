/*
   M5 Multipass - Main Application

   Creates a Matter device with three stateless Generic Switch endpoints
   (one per physical button: UP, DOWN, MID).  Each switch supports single-press
   (InitialPress + ShortRelease events) compatible with Apple Home automations.

   Hardware: M5Stack Core Ink (ESP32-PICO-D4), WiFi-only Matter transport.
*/

#include <M5GFX.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
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

#include <setup_payload/OnboardingCodesUtil.h>
#include <qrcode.h>

static const char *TAG = "app_main";

static M5GFX display;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

// Endpoint IDs for each switch — indexed by SWITCH_UP/DOWN/MID_IDX
static uint16_t s_endpoint_ids[NUM_SWITCHES] = {0};

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
        app_driver_led_blink_start(LED_BLINK_SLOW_MS);
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
// app_main
// ---------------------------------------------------------------------------

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

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
    // Display — initialise e-ink (rendering happens after Matter starts)
    // ----------------------------------------------------------------
    display.begin();
    display.setRotation(0);
    display.setEpdMode(epd_mode_t::epd_quality);

    // ----------------------------------------------------------------
    // NVS
    // ----------------------------------------------------------------
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupted — erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

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
    // Create Generic Switch endpoints (one per button)
    // ----------------------------------------------------------------
    const char *switch_labels[NUM_SWITCHES] = { "Up", "Down", "Mid" };

    for (int i = 0; i < NUM_SWITCHES; i++) {
        generic_switch::config_t sw_cfg = {};
        // Feature map: MS (MomentarySwitch=0x02) | MSR (MomentarySwitchRelease=0x04)
        // This enables InitialPress + ShortRelease events, required for Apple Home
        // single-press automations.
        sw_cfg.switch_cluster.feature_flags       = 0x06;  // MS | MSR
        sw_cfg.switch_cluster.number_of_positions = 2;
        sw_cfg.switch_cluster.current_position    = 0;

        endpoint_t *ep = generic_switch::create(node, &sw_cfg,
                                                 ENDPOINT_FLAG_NONE, nullptr);
        ABORT_APP_ON_FAILURE(ep != nullptr,
                             ESP_LOGE(TAG, "Failed to create switch endpoint[%d]", i));

        s_endpoint_ids[i] = endpoint::get_id(ep);
        ESP_LOGI(TAG, "Switch[%d] '%s' → endpoint %d",
                 i, switch_labels[i], s_endpoint_ids[i]);
    }

    // ----------------------------------------------------------------
    // Initialise buttons
    // ----------------------------------------------------------------
    err = app_driver_buttons_init(s_endpoint_ids);
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
    // Print QR code to serial and render on e-ink display
    // ----------------------------------------------------------------
    {
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

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "M5 Multipass v%s started — %d Generic Switch endpoints",
             app_desc->version, NUM_SWITCHES);
    ESP_LOGI(TAG, "=== Commissioning Info ===");
    ESP_LOGI(TAG, "Discriminator: %d (0x%03X)",
             CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR,
             CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR);
    ESP_LOGI(TAG, "Passcode: %d", CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE);
    ESP_LOGI(TAG, "See docs/img/pairing_qr.png or run: make generate-pairing");
    ESP_LOGI(TAG, "==========================");
}

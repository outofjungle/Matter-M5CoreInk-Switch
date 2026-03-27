/*
   M5 Multipass - Button Driver & Button Config

   Manages NVS-backed button configuration (16 slots, each with button_name/room_name/enabled)
   and the three physical buttons.

   UP / DOWN  → navigate enabled buttons; calls display callback; no Matter events
   MID press  → emits InitialPress + ShortRelease on the currently selected button endpoint
   MID long-hold → factory reset (delegated to app_reset)
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_timer.h>
#include <nvs.h>
#include <iot_button.h>

// CHIP event logging
#include <app/EventLogging.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <esp_matter_core.h>

#include "app_priv.h"
#include "app_reset.h"

static const char *TAG = "app_driver";

using namespace esp_matter;
using namespace chip::app::Clusters;

// ---------------------------------------------------------------------------
// NVS keys
// ---------------------------------------------------------------------------

static const char *NVS_NS       = "app_state";
static const char *NVS_SEL_KEY  = "sel_sw";

// Key builders — caller owns the buffer
static void sw_key(char *buf, size_t len, int slot, const char *field)
{
    snprintf(buf, len, "sw/%d/%s", slot, field);
}

// ---------------------------------------------------------------------------
// Button config state
// ---------------------------------------------------------------------------

static button_slot_t s_configs[MAX_BUTTONS];
static int s_enabled_slots[MAX_BUTTONS];  // slot indices of enabled buttons
static int s_enabled_count = 0;

static void write_defaults_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for default write");
        return;
    }

    char key[16];
    char num[9];
    for (int i = 0; i < MAX_BUTTONS; i++) {
        sw_key(key, sizeof(key), i, "l1");
        nvs_set_str(h, key, "Button");

        snprintf(num, sizeof(num), "%d", i + 1);
        sw_key(key, sizeof(key), i, "l2");
        nvs_set_str(h, key, num);

        sw_key(key, sizeof(key), i, "en");
        nvs_set_u8(h, key, (i < 4) ? 1 : 0);
    }

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "NVS button defaults written (slots 0-3 enabled)");
}

esp_err_t app_button_config_init(void)
{
    nvs_handle_t h;
    char key[16];

    // First-boot detection: check if slot 0 enabled key exists
    esp_err_t probe_err = nvs_open(NVS_NS, NVS_READONLY, &h);
    bool first_boot = true;
    if (probe_err == ESP_OK) {
        uint8_t dummy;
        sw_key(key, sizeof(key), 0, "en");
        first_boot = (nvs_get_u8(h, key, &dummy) == ESP_ERR_NVS_NOT_FOUND);
        nvs_close(h);
    }

    if (first_boot) {
        write_defaults_to_nvs();
    }

    // Load all 16 configs
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for config load");
        return ESP_FAIL;
    }

    s_enabled_count = 0;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        size_t sz;
        uint8_t en = 0;

        sw_key(key, sizeof(key), i, "l1");
        sz = sizeof(s_configs[i].button_name);
        if (nvs_get_str(h, key, s_configs[i].button_name, &sz) != ESP_OK) {
            strncpy(s_configs[i].button_name, "Button", sizeof(s_configs[i].button_name));
        }

        sw_key(key, sizeof(key), i, "l2");
        sz = sizeof(s_configs[i].room_name);
        char num_buf[9];
        snprintf(num_buf, sizeof(num_buf), "%d", i + 1);
        if (nvs_get_str(h, key, s_configs[i].room_name, &sz) != ESP_OK) {
            strncpy(s_configs[i].room_name, num_buf, sizeof(s_configs[i].room_name));
        }

        sw_key(key, sizeof(key), i, "en");
        nvs_get_u8(h, key, &en);
        s_configs[i].enabled = (en != 0);

        if (s_configs[i].enabled) {
            s_enabled_slots[s_enabled_count++] = i;
        }
    }

    nvs_close(h);

    // Sanitize in-memory values (NVS write may have stored bad data via serial)
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (s_configs[i].button_name[0] == '\0') {
            strncpy(s_configs[i].button_name, "Button", sizeof(s_configs[i].button_name));
        }
        if (s_configs[i].room_name[0] == '\0') {
            snprintf(s_configs[i].room_name, sizeof(s_configs[i].room_name), "%d", i + 1);
        }
    }

    // Force-enable slot 0 if nothing is enabled (prevent blank device)
    if (s_enabled_count == 0) {
        ESP_LOGW(TAG, "No enabled buttons — force-enabling slot 0");
        s_configs[0].enabled = true;
        s_enabled_slots[0] = 0;
        s_enabled_count = 1;
        // Write correction back to NVS
        nvs_handle_t fix_h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &fix_h) == ESP_OK) {
            char fix_key[16];
            sw_key(fix_key, sizeof(fix_key), 0, "en");
            nvs_set_u8(fix_h, fix_key, 1);
            nvs_commit(fix_h);
            nvs_close(fix_h);
        }
    }

    ESP_LOGI(TAG, "Loaded %d enabled buttons (of %d)", s_enabled_count, MAX_BUTTONS);
    return ESP_OK;
}

esp_err_t app_button_nvs_write_slot(int slot, const char *l1, const char *l2, bool en)
{
    if (slot < 0 || slot >= MAX_BUTTONS) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    char key[16];
    sw_key(key, sizeof(key), slot, "l1");
    nvs_set_str(h, key, l1);
    sw_key(key, sizeof(key), slot, "l2");
    nvs_set_str(h, key, l2);
    sw_key(key, sizeof(key), slot, "en");
    nvs_set_u8(h, key, en ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

int app_button_get_enabled_count(void)   { return s_enabled_count; }

const button_slot_t *app_button_get_config(int slot)
{
    if (slot < 0 || slot >= MAX_BUTTONS) return nullptr;
    return &s_configs[slot];
}

int app_button_get_enabled_slot(int n)
{
    if (n < 0 || n >= s_enabled_count) return -1;
    return s_enabled_slots[n];
}

// ---------------------------------------------------------------------------
// Button selection state
// ---------------------------------------------------------------------------

static uint16_t s_endpoint_ids[MAX_BUTTONS] = {0};
static int s_endpoint_count = 0;

// 0-indexed into the enabled-button list
static int s_selected_button = 0;

// Display callback
static void (*s_display_cb)(int) = nullptr;

int app_driver_get_selected_button(void) { return s_selected_button; }

static void save_selected_button(int idx)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_SEL_KEY, (uint8_t)idx);
    nvs_commit(h);
    nvs_close(h);
}

static int load_selected_button(void)
{
    nvs_handle_t h;
    uint8_t val = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_SEL_KEY, &val);
        nvs_close(h);
    }
    return (val < (uint8_t)s_endpoint_count) ? (int)val : 0;
}

static uint16_t selected_ep_id(void)
{
    return s_endpoint_ids[s_selected_button];
}

// ---------------------------------------------------------------------------
// LED — low-level primitive (file-local)
// ---------------------------------------------------------------------------

void app_driver_led_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_PIN, 0);
}

void app_driver_led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 1 : 0);
}

static void led_set(bool on) { app_driver_led_set(on); }

// ---------------------------------------------------------------------------
// LED blink (esp_timer)
// ---------------------------------------------------------------------------

static esp_timer_handle_t s_blink_timer = NULL;
static bool s_blink_state = false;

static void blink_timer_cb(void *arg)
{
    s_blink_state = !s_blink_state;
    led_set(s_blink_state);
}

void app_driver_led_blink_start(uint32_t half_period_ms)
{
    if (s_blink_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = blink_timer_cb,
            .name     = "led_blink",
        };
        esp_timer_create(&args, &s_blink_timer);
    } else {
        esp_timer_stop(s_blink_timer);
    }
    s_blink_state = false;
    led_set(false);
    esp_timer_start_periodic(s_blink_timer, (uint64_t)half_period_ms * 1000ULL);
}

void app_driver_led_blink_stop(void)
{
    if (s_blink_timer) {
        esp_timer_stop(s_blink_timer);
    }
    led_set(false);
}

// ---------------------------------------------------------------------------
// Per-button context
// ---------------------------------------------------------------------------

struct btn_ctx_t {
    int  index;              // 0=Up, 1=Down, 2=Mid
    bool long_press_active;  // set true when long-press fires, blocks ShortRelease
};

static btn_ctx_t s_ctx[3];
static button_handle_t s_handles[3];

// ---------------------------------------------------------------------------
// Button callbacks
// ---------------------------------------------------------------------------

// --- Up button: retreat selection ---

static void btn_up_press_cb(void *arg, void *data)
{
    led_set(true);
    s_selected_button = (s_selected_button + s_endpoint_count - 1) % s_endpoint_count;
    ESP_LOGI(TAG, "Nav UP → enabled[%d] (slot %d)", s_selected_button,
             app_button_get_enabled_slot(s_selected_button));
    save_selected_button(s_selected_button);
    if (s_display_cb) s_display_cb(s_selected_button);
}

static void btn_up_release_cb(void *arg, void *data)
{
    led_set(false);
}

// --- Down button: advance selection ---

static void btn_down_press_cb(void *arg, void *data)
{
    led_set(true);
    s_selected_button = (s_selected_button + 1) % s_endpoint_count;
    ESP_LOGI(TAG, "Nav DOWN → enabled[%d] (slot %d)", s_selected_button,
             app_button_get_enabled_slot(s_selected_button));
    save_selected_button(s_selected_button);
    if (s_display_cb) s_display_cb(s_selected_button);
}

static void btn_down_release_cb(void *arg, void *data)
{
    led_set(false);
}

// --- Mid button: fire Matter event on selected button ---

static void btn_mid_press_down_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);
    ctx->long_press_active = false;

    uint16_t ep = selected_ep_id();
    int slot = app_button_get_enabled_slot(s_selected_button);
    ESP_LOGD(TAG, "Mid press down → enabled[%d] slot %d (ep %d)",
             s_selected_button, slot, ep);

    led_set(true);

    {
        esp_matter::lock::ScopedChipStackLock chip_lock(portMAX_DELAY);
        esp_matter_attr_val_t val = esp_matter_uint8(1);
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &val);

        Switch::Events::InitialPress::Type event_data;
        event_data.newPosition = 1;
        chip::EventNumber event_number;
        chip::app::LogEvent(event_data, ep, event_number);
    }

    ESP_LOGI(TAG, "Button slot %d InitialPress sent", slot);
}

static void btn_mid_press_up_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);

    led_set(false);

    if (ctx->long_press_active) {
        ctx->long_press_active = false;
        return;
    }

    uint16_t ep = selected_ep_id();
    int slot = app_button_get_enabled_slot(s_selected_button);
    ESP_LOGD(TAG, "Mid press up → enabled[%d] slot %d (ep %d)",
             s_selected_button, slot, ep);

    {
        esp_matter::lock::ScopedChipStackLock chip_lock(portMAX_DELAY);
        esp_matter_attr_val_t val = esp_matter_uint8(0);
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &val);

        Switch::Events::ShortRelease::Type event_data;
        event_data.previousPosition = 1;
        chip::EventNumber event_number;
        chip::app::LogEvent(event_data, ep, event_number);
    }

    ESP_LOGI(TAG, "Button slot %d ShortRelease sent", slot);
}

static void btn_long_press_mark_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);
    ctx->long_press_active = true;
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------

static const gpio_num_t k_button_pins[3] = {
    BUTTON_UP_PIN,
    BUTTON_DOWN_PIN,
    BUTTON_MID_PIN,
};

esp_err_t app_driver_buttons_init(uint16_t *endpoint_ids, int endpoint_count,
                                   void (*on_button_selected)(int))
{
    // Store endpoint IDs, count, and display callback
    for (int i = 0; i < endpoint_count && i < MAX_BUTTONS; i++) {
        s_endpoint_ids[i] = endpoint_ids[i];
    }
    s_endpoint_count = endpoint_count;
    s_display_cb = on_button_selected;
    s_selected_button = load_selected_button();

    ESP_LOGI(TAG, "Starting on enabled[%d] (slot %d)",
             s_selected_button, app_button_get_enabled_slot(s_selected_button));

    // Configure LED GPIO
    app_driver_led_init();

    for (int i = 0; i < 3; i++) {
        s_ctx[i].index             = i;
        s_ctx[i].long_press_active = false;

        button_config_t btn_cfg = {};
        btn_cfg.type = BUTTON_TYPE_GPIO;
        btn_cfg.long_press_time  = FACTORY_RESET_LONG_PRESS_MS;
        btn_cfg.short_press_time = 50;
        btn_cfg.gpio_button_config.gpio_num     = k_button_pins[i];
        btn_cfg.gpio_button_config.active_level = 0;  // Active LOW

        s_handles[i] = iot_button_create(&btn_cfg);
        if (!s_handles[i]) {
            ESP_LOGE(TAG, "Failed to create button[%d] on GPIO%d", i, k_button_pins[i]);
            return ESP_FAIL;
        }

        if (i == 0) {  // Up
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_DOWN, btn_up_press_cb,   nullptr);
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_UP,   btn_up_release_cb, nullptr);
        } else if (i == 1) {  // Down
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_DOWN, btn_down_press_cb,   nullptr);
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_UP,   btn_down_release_cb, nullptr);
        } else {  // Mid
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_DOWN, btn_mid_press_down_cb, &s_ctx[i]);
            iot_button_register_cb(s_handles[i], BUTTON_PRESS_UP,   btn_mid_press_up_cb,   &s_ctx[i]);
            iot_button_register_cb(s_handles[i], BUTTON_LONG_PRESS_START,
                                   btn_long_press_mark_cb, &s_ctx[i]);
            esp_err_t err = app_reset_button_register(s_handles[i]);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "app_reset_button_register failed: %d", err);
            }
        }

        ESP_LOGI(TAG, "Button[%d] (GPIO%d) initialised", i, k_button_pins[i]);
    }

    return ESP_OK;
}

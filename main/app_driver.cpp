/*
   M5 Multipass - Button Driver

   Initialises three momentary-switch buttons (UP, DOWN, MID) and maps them
   to Matter Generic Switch endpoints.

   On press  → updates CurrentPosition attribute to 1 + emits InitialPress event
   On release → updates CurrentPosition attribute to 0 + emits ShortRelease event
   MID long-hold → factory reset (delegated to app_reset)
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_timer.h>
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
// Per-button context
// ---------------------------------------------------------------------------

struct btn_ctx_t {
    uint16_t endpoint_id;
    int      index;          // SWITCH_UP_IDX / SWITCH_DOWN_IDX / SWITCH_MID_IDX
    bool     long_press_active;  // set true when long-press fires, blocks ShortRelease
};

static btn_ctx_t s_ctx[NUM_SWITCHES];
static button_handle_t s_handles[NUM_SWITCHES];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void update_current_position(uint16_t ep_id, uint8_t position)
{
    esp_matter_attr_val_t val = esp_matter_uint8(position);
    attribute::update(ep_id,
                      Switch::Id,
                      Switch::Attributes::CurrentPosition::Id,
                      &val);
}

// ---------------------------------------------------------------------------
// LED — low-level primitive (file-local)
// ---------------------------------------------------------------------------

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
        esp_timer_stop(s_blink_timer);  // may return error if not running; ignore
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
// Button callbacks
// ---------------------------------------------------------------------------

static void btn_press_down_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);
    ctx->long_press_active = false;

    ESP_LOGD(TAG, "Switch[%d] press down (ep %d)", ctx->index, ctx->endpoint_id);

    // Brief LED flash for tactile feedback
    led_set(true);
    // Non-blocking: LED will be cleared in press_up_cb

    {
        esp_matter::lock::ScopedChipStackLock chip_lock(portMAX_DELAY);
        update_current_position(ctx->endpoint_id, 1);

        // Emit InitialPress event
        Switch::Events::InitialPress::Type event_data;
        event_data.newPosition = 1;
        chip::EventNumber event_number;
        chip::app::LogEvent(event_data, ctx->endpoint_id, event_number);
    }

    ESP_LOGI(TAG, "Switch[%d] InitialPress sent", ctx->index);
}

static void btn_press_up_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);

    led_set(false);

    // Suppress ShortRelease if a long-press (factory reset) was in progress
    if (ctx->long_press_active) {
        ctx->long_press_active = false;
        return;
    }

    ESP_LOGD(TAG, "Switch[%d] press up (ep %d)", ctx->index, ctx->endpoint_id);

    {
        esp_matter::lock::ScopedChipStackLock chip_lock(portMAX_DELAY);
        update_current_position(ctx->endpoint_id, 0);

        // Emit ShortRelease event
        Switch::Events::ShortRelease::Type event_data;
        event_data.previousPosition = 1;
        chip::EventNumber event_number;
        chip::app::LogEvent(event_data, ctx->endpoint_id, event_number);
    }

    ESP_LOGI(TAG, "Switch[%d] ShortRelease sent", ctx->index);
}

// Mark that a long-press fired so press_up_cb skips ShortRelease
static void btn_long_press_mark_cb(void *arg, void *data)
{
    btn_ctx_t *ctx = static_cast<btn_ctx_t *>(data);
    ctx->long_press_active = true;
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------

static const gpio_num_t k_button_pins[NUM_SWITCHES] = {
    BUTTON_UP_PIN,
    BUTTON_DOWN_PIN,
    BUTTON_MID_PIN,
};

esp_err_t app_driver_buttons_init(uint16_t *endpoint_ids)
{
    // Configure LED GPIO
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    led_set(false);

    for (int i = 0; i < NUM_SWITCHES; i++) {
        s_ctx[i].endpoint_id      = endpoint_ids[i];
        s_ctx[i].index            = i;
        s_ctx[i].long_press_active = false;

        button_config_t btn_cfg = {};
        btn_cfg.type = BUTTON_TYPE_GPIO;
        btn_cfg.long_press_time  = FACTORY_RESET_LONG_PRESS_MS;
        btn_cfg.short_press_time = 50;
        btn_cfg.gpio_button_config.gpio_num    = k_button_pins[i];
        btn_cfg.gpio_button_config.active_level = 0;  // Active LOW

        s_handles[i] = iot_button_create(&btn_cfg);
        if (!s_handles[i]) {
            ESP_LOGE(TAG, "Failed to create button[%d] on GPIO%d", i, k_button_pins[i]);
            return ESP_FAIL;
        }

        iot_button_register_cb(s_handles[i], BUTTON_PRESS_DOWN,
                               btn_press_down_cb, &s_ctx[i]);
        iot_button_register_cb(s_handles[i], BUTTON_PRESS_UP,
                               btn_press_up_cb,   &s_ctx[i]);

        // Register factory reset only on the MID button
        if (i == SWITCH_MID_IDX) {
            // Mark long-press active before app_reset's long_press_start fires
            iot_button_register_cb(s_handles[i], BUTTON_LONG_PRESS_START,
                                   btn_long_press_mark_cb, &s_ctx[i]);
            esp_err_t err = app_reset_button_register(s_handles[i]);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "app_reset_button_register failed: %d", err);
            }
        }

        ESP_LOGI(TAG, "Button[%d] (GPIO%d) → endpoint %d",
                 i, k_button_pins[i], endpoint_ids[i]);
    }

    return ESP_OK;
}

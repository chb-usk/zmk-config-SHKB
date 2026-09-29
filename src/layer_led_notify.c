/*
 * Split central side of the layer LED: on each change of the highest active
 * layer, invoke &lyr_led <layer> on every half (GLOBAL locality), so the
 * left half can light its LED even though it has no layer state.
 */

#include <zephyr/kernel.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t last_layer;

static void layer_notify_cb(struct k_work *work) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    if (layer == last_layer) {
        return;
    }
    last_layer = layer;

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(lyr_led)),
        .param1 = layer,
    };
    struct zmk_behavior_binding_event event = {
        .layer = layer,
        .position = UINT32_MAX,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    int err = zmk_behavior_invoke_binding(&binding, event, true);
    if (err < 0) {
        LOG_WRN("Failed to send layer %d to layer LED (err %d)", layer, err);
    }
}
static K_WORK_DEFINE(layer_notify_work, layer_notify_cb);

static int layer_notify_listener(const zmk_event_t *eh) {
    k_work_submit(&layer_notify_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(shkb_layer_led_notify, layer_notify_listener);
ZMK_SUBSCRIPTION(shkb_layer_led_notify, zmk_layer_state_changed);

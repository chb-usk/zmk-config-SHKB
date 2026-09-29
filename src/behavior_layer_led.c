/*
 * &lyr_led <layer>: show <layer>'s color on this half's layer LED.
 *
 * GLOBAL locality makes the split central run it on every peripheral as
 * well as locally; halves built without CONFIG_SHKB_LAYER_LED_SHOW_LAYERS
 * ignore it.
 */

#define DT_DRV_COMPAT zmk_behavior_shkb_layer_led

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <shkb/layer_led.h>

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_SHOW_LAYERS)
    shkb_layer_led_show(binding->param1);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_layer_led_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define LAYER_LED_INST(n)                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_layer_led_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LAYER_LED_INST)

/*
 * Layer indicator for the XIAO BLE onboard RGB LED.
 *
 * shkb_layer_led_show() lights the LED in the layer's color for
 * CONFIG_SHKB_LAYER_LED_DURATION_MS, then turns it off to save battery.
 * A new call while lit switches to the new color and restarts the timer.
 * Layers without a color turn the LED off.
 *
 * With CONFIG_SHKB_LAYER_LED_BATTERY, the first battery reading after power-on
 * is shown for CONFIG_SHKB_LAYER_LED_BATTERY_MS: green >= 50%, yellow >= 20%,
 * red below that.
 *
 * Only the split central knows the layer state, so it sends layer changes
 * here through the lyr_led behavior (see layer_led_notify.c). Layer colors
 * are shown only on halves with CONFIG_SHKB_LAYER_LED_SHOW_LAYERS (left);
 * every half with CONFIG_SHKB_LAYER_LED shows its own battery at power-on.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#include <shkb/layer_led.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define RED BIT(0)
#define GREEN BIT(1)
#define BLUE BIT(2)

/* The board DTS labels led1/led2 as blue/green, but on the XIAO BLE
 * P0.30 (led1) is green and P0.06 (led2) is blue. */
static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios), /* red */
    GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios), /* green */
    GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios), /* blue */
};

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_SHOW_LAYERS)
/* Index = layer number. Layers 0 (mac), 1 (win) and 2 (iPad) have no color. */
static const uint8_t layer_colors[] = {
    [3] = GREEN,        /* Function */
    [4] = RED | GREEN,  /* Bluetooth: yellow */
    [5] = GREEN | BLUE, /* Mouse: cyan */
    [6] = RED | BLUE,   /* Scroll: magenta */
};
#endif

static atomic_t pending_color;
static atomic_t pending_ms;

static void set_color(uint8_t color) {
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        gpio_pin_set_dt(&leds[i], (color & BIT(i)) ? 1 : 0);
    }
}

static void led_off_cb(struct k_work *work) { set_color(0); }
static K_WORK_DELAYABLE_DEFINE(led_off_work, led_off_cb);

/* Runs on the system workqueue together with led_off_work, so the two never race. */
static void led_show_cb(struct k_work *work) {
    uint8_t color = atomic_get(&pending_color);

    set_color(color);
    if (color) {
        k_work_reschedule(&led_off_work, K_MSEC(atomic_get(&pending_ms)));
    } else {
        k_work_cancel_delayable(&led_off_work);
    }
}
static K_WORK_DEFINE(led_show_work, led_show_cb);

static void show_color(uint8_t color, uint32_t duration_ms) {
    atomic_set(&pending_color, color);
    atomic_set(&pending_ms, duration_ms);
    k_work_submit(&led_show_work);
}

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_SHOW_LAYERS)
void shkb_layer_led_show(uint8_t layer) {
    uint8_t color = layer < ARRAY_SIZE(layer_colors) ? layer_colors[layer] : 0;

    LOG_DBG("Layer %d -> LED color %d", layer, color);
    show_color(color, CONFIG_SHKB_LAYER_LED_DURATION_MS);
}
#endif

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_BATTERY)
/* ZMK samples the battery asynchronously after boot, so wait for the first
 * reading instead of polling zmk_battery_state_of_charge() at init. */
static int battery_listener(const zmk_event_t *eh) {
    static bool shown;
    if (shown) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    shown = true;

    uint8_t soc = as_zmk_battery_state_changed(eh)->state_of_charge;
    uint8_t color = soc >= 50 ? GREEN : soc >= 20 ? (RED | GREEN) : RED;

    LOG_INF("Battery %d%% at power-on -> LED color %d", soc, color);
    show_color(color, CONFIG_SHKB_LAYER_LED_BATTERY_MS);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(shkb_layer_led_battery, battery_listener);
ZMK_SUBSCRIPTION(shkb_layer_led_battery, zmk_battery_state_changed);
#endif

static int layer_led_init(void) {
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            LOG_ERR("LED GPIO %d not ready", i);
            return -ENODEV;
        }
        gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
    }
    return 0;
}

/* POST_KERNEL so the pins are ready before ZMK's battery init (APPLICATION) reports. */
SYS_INIT(layer_led_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

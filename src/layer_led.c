/*
 * Layer / battery / BLE profile indicator for the XIAO BLE onboard RGB LED.
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
 * With CONFIG_SHKB_LAYER_LED_PROFILE (split central only), the active BLE
 * profile blinks in its color for CONFIG_SHKB_LAYER_LED_PROFILE_MS at
 * power-on (after the battery indication) and whenever the profile changes.
 *
 * Only the split central knows the layer state, so it sends layer changes
 * here through the lyr_led behavior (see layer_led_notify.c). Layer colors
 * are shown only on halves with CONFIG_SHKB_LAYER_LED_SHOW_LAYERS (left);
 * every half with CONFIG_SHKB_LAYER_LED shows its own battery at power-on.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#include <shkb/layer_led.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define RED BIT(0)
#define GREEN BIT(1)
#define BLUE BIT(2)

/* Pattern color resolved when the pattern starts playing, so the power-on
 * profile is read after ZMK has loaded it from settings. */
#define COLOR_ACTIVE_PROFILE 0xFF

/* Pause after a pattern so consecutive patterns are visually separate. */
#define PATTERN_GAP_MS 300

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

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
/* Index = BLE profile number. */
static const uint8_t profile_colors[] = {
    RED,          /* profile 0 */
    BLUE,         /* profile 1 */
    GREEN,        /* profile 2 */
    RED | BLUE,   /* profile 3: magenta */
    RED | GREEN,  /* profile 4: yellow */
};

#define PROFILE_BLINKS                                                                             \
    MAX(1, CONFIG_SHKB_LAYER_LED_PROFILE_MS / (2 * CONFIG_SHKB_LAYER_LED_PROFILE_BLINK_MS))
#endif

/* Light `color` for on_ms, then stay off for off_ms; repeated `count` times.
 * A solid indication is count = 1. color 0 just turns the LED off. */
struct led_pattern {
    uint8_t color;
    uint8_t count;
    uint16_t on_ms;
    uint16_t off_ms;
};

struct led_request {
    struct led_pattern pattern;
    bool replace; /* drop whatever is playing or queued */
};

K_MSGQ_DEFINE(led_requests, sizeof(struct led_request), 4, 4);

/* Player state; only touched from the system workqueue. */
static struct led_pattern queue[2];
static uint8_t queue_len;
static struct led_pattern cur;
static uint8_t remaining;
static bool lit;
static bool playing;

static void set_color(uint8_t color) {
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        gpio_pin_set_dt(&leds[i], (color & BIT(i)) ? 1 : 0);
    }
}

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
static atomic_t boot_profile_queued;
static atomic_t boot_profile_shown;
static atomic_t last_profile = ATOMIC_INIT(-1);
#endif

static uint8_t resolve_color(uint8_t color) {
#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
    if (color == COLOR_ACTIVE_PROFILE) {
        int profile = zmk_ble_active_profile_index();
        LOG_INF("Active BLE profile %d at power-on", profile);
        /* Profile switches are detected from here on. */
        atomic_set(&last_profile, profile);
        atomic_set(&boot_profile_shown, 1);
        return profile < ARRAY_SIZE(profile_colors) ? profile_colors[profile] : 0;
    }
#endif
    return color;
}

static void step_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(step_work, step_cb);

static void start_next(void) {
    while (queue_len > 0) {
        cur = queue[0];
        memmove(&queue[0], &queue[1], (queue_len - 1) * sizeof(queue[0]));
        queue_len--;

        cur.color = resolve_color(cur.color);
        if (cur.color == 0 || cur.count == 0) {
            continue;
        }
        playing = true;
        lit = true;
        remaining = cur.count;
        set_color(cur.color);
        k_work_reschedule(&step_work, K_MSEC(cur.on_ms));
        return;
    }
    playing = false;
    lit = false;
    set_color(0);
}

static void step_cb(struct k_work *work) {
    if (!playing) {
        return;
    }
    if (lit) {
        set_color(0);
        lit = false;
        if (--remaining > 0) {
            k_work_reschedule(&step_work, K_MSEC(cur.off_ms));
        } else {
            /* Finished: start the next pattern after a short pause. */
            playing = queue_len > 0;
            if (playing) {
                k_work_reschedule(&step_work, K_MSEC(MAX(cur.off_ms, PATTERN_GAP_MS)));
            }
        }
    } else if (remaining > 0) {
        set_color(cur.color);
        lit = true;
        k_work_reschedule(&step_work, K_MSEC(cur.on_ms));
    } else {
        start_next();
    }
}

static void request_cb(struct k_work *work) {
    struct led_request req;

    while (k_msgq_get(&led_requests, &req, K_NO_WAIT) == 0) {
        if (req.replace) {
            queue_len = 0;
            playing = false;
            k_work_cancel_delayable(&step_work);
        }
        if (queue_len < ARRAY_SIZE(queue)) {
            queue[queue_len++] = req.pattern;
        }
    }
    if (!playing) {
        start_next();
    }
}
static K_WORK_DEFINE(request_work, request_cb);

static void play(struct led_pattern pattern, bool replace) {
    struct led_request req = {.pattern = pattern, .replace = replace};

    if (k_msgq_put(&led_requests, &req, K_NO_WAIT) != 0) {
        LOG_WRN("LED request queue full, dropping request");
        return;
    }
    k_work_submit(&request_work);
}

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_SHOW_LAYERS)
void shkb_layer_led_show(uint8_t layer) {
    uint8_t color = layer < ARRAY_SIZE(layer_colors) ? layer_colors[layer] : 0;

    LOG_DBG("Layer %d -> LED color %d", layer, color);
    play((struct led_pattern){.color = color, .count = 1,
                              .on_ms = CONFIG_SHKB_LAYER_LED_DURATION_MS},
         true);
}
#endif

#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
static const struct led_pattern profile_blink = {
    .count = PROFILE_BLINKS,
    .on_ms = CONFIG_SHKB_LAYER_LED_PROFILE_BLINK_MS,
    .off_ms = CONFIG_SHKB_LAYER_LED_PROFILE_BLINK_MS,
};

/* Queue the power-on profile blink once, behind the battery indication. */
static void queue_boot_profile(void) {
    if (atomic_set(&boot_profile_queued, 1)) {
        return;
    }
    struct led_pattern p = profile_blink;
    p.color = COLOR_ACTIVE_PROFILE;
    play(p, false);
}

static void boot_profile_fallback_cb(struct k_work *work) { queue_boot_profile(); }
static K_WORK_DELAYABLE_DEFINE(boot_profile_fallback_work, boot_profile_fallback_cb);

static int profile_listener(const zmk_event_t *eh) {
    uint8_t index = as_zmk_ble_active_profile_changed(eh)->index;

    /* The event also fires on connect/disconnect/pairing; only a different
     * profile counts as a switch. Until the power-on blink has read the
     * profile, changes are covered by that blink. */
    atomic_val_t prev = atomic_set(&last_profile, index);
    if (!atomic_get(&boot_profile_shown) || prev == index) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_INF("BLE profile switched to %d", index);
    struct led_pattern p = profile_blink;
    p.color = index < ARRAY_SIZE(profile_colors) ? profile_colors[index] : 0;
    play(p, true);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(shkb_layer_led_profile, profile_listener);
ZMK_SUBSCRIPTION(shkb_layer_led_profile, zmk_ble_active_profile_changed);
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
    play((struct led_pattern){.color = color, .count = 1,
                              .on_ms = CONFIG_SHKB_LAYER_LED_BATTERY_MS},
         false);
#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
    queue_boot_profile();
#endif
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
#if IS_ENABLED(CONFIG_SHKB_LAYER_LED_PROFILE)
    /* Show the power-on profile even if no battery reading ever arrives. */
    k_work_schedule(&boot_profile_fallback_work, K_MSEC(3000));
#endif
    return 0;
}

/* POST_KERNEL so the pins are ready before ZMK's battery init (APPLICATION) reports. */
SYS_INIT(layer_led_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

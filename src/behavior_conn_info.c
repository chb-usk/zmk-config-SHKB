/*
 * &conn_info: type the host BLE connection parameters as text (diagnostics).
 *
 * Output: i<interval>l<latency>t<timeout>p<tx phy>
 *   interval is in 1.25 ms units (i12 = 15 ms), timeout in 10 ms units.
 *   Types "none" when the active BT profile has no connection.
 */

#define DT_DRV_COMPAT zmk_behavior_shkb_conn_info

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/bluetooth/conn.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>

#include <zmk/behavior.h>

#define IS_CENTRAL_HALF                                                                            \
    (IS_ENABLED(CONFIG_ZMK_BLE) &&                                                                 \
     (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)))

#if IS_CENTRAL_HALF

#include <zmk/behavior_queue.h>
#include <zmk/ble.h>

static uint32_t keycode_for(char c) {
    static const uint32_t digits[] = {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9};
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    switch (c) {
    case 'i':
        return I;
    case 'l':
        return L;
    case 't':
        return T;
    case 'p':
        return P;
    case 'n':
        return N;
    case 'o':
        return O;
    case 'e':
        return E;
    case 'r':
        return R;
    case ' ':
        return SPACE;
    default:
        return 0;
    }
}

static void type_text(const struct zmk_behavior_binding_event *event, const char *text) {
    for (const char *c = text; *c; c++) {
        uint32_t keycode = keycode_for(*c);
        if (!keycode) {
            continue;
        }
        struct zmk_behavior_binding kp = {.behavior_dev = "key_press", .param1 = keycode};
        zmk_behavior_queue_add(event, kp, true, 15);
        zmk_behavior_queue_add(event, kp, false, 15);
    }
}

static void type_conn_info(const struct zmk_behavior_binding_event *event) {
    char text[32] = "none";
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (conn) {
        struct bt_conn_info info;
        if (bt_conn_get_info(conn, &info) == 0) {
            int phy = 0;
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
            if (info.le.phy) {
                phy = info.le.phy->tx_phy;
            }
#endif
            snprintf(text, sizeof(text), "i%ul%ut%up%d", info.le.interval, info.le.latency,
                     info.le.timeout, phy);
        }
        bt_conn_unref(conn);
    }
    type_text(event, text);
}

/* &conn_req*: ask the host for new parameters, type "r<errno> ", then type the
 * resulting parameters 2 s later (the host answers asynchronously). */
struct conn_req_config {
    uint16_t interval_min;
    uint16_t interval_max;
    uint16_t latency;
    uint16_t timeout;
};

static struct zmk_behavior_binding_event req_event;

static void req_result_work_cb(struct k_work *work) { type_conn_info(&req_event); }

static K_WORK_DELAYABLE_DEFINE(req_result_work, req_result_work_cb);

static int on_conn_req_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct conn_req_config *cfg = dev->config;
    char text[16] = "none";
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (conn) {
        int err = bt_conn_le_param_update(conn, BT_LE_CONN_PARAM(cfg->interval_min,
                                                                 cfg->interval_max, cfg->latency,
                                                                 cfg->timeout));
        bt_conn_unref(conn);
        snprintf(text, sizeof(text), "r%d ", err < 0 ? -err : err);
        req_event = event;
        k_work_reschedule(&req_result_work, K_MSEC(2000));
    }
    type_text(&event, text);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_conn_req_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_conn_req_driver_api = {
    .binding_pressed = on_conn_req_pressed,
    .binding_released = on_conn_req_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define CONN_REQ_INST(node)                                                                        \
    static const struct conn_req_config conn_req_config_##node = {                                 \
        .interval_min = DT_PROP(node, interval_min),                                               \
        .interval_max = DT_PROP(node, interval_max),                                               \
        .latency = DT_PROP(node, latency),                                                         \
        .timeout = DT_PROP(node, timeout),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_DEFINE(node, NULL, NULL, NULL, &conn_req_config_##node, POST_KERNEL,               \
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_conn_req_driver_api);

DT_FOREACH_STATUS_OKAY(zmk_behavior_shkb_conn_req, CONN_REQ_INST)

#endif

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_CENTRAL_HALF
    type_conn_info(&event);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_conn_info_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define CONN_INFO_INST(n)                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_conn_info_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CONN_INFO_INST)

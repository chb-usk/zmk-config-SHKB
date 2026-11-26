/*
 * Host connection interval fallback.
 *
 * The preferred parameters (CONFIG_BT_PERIPHERAL_PREF_*: 7.5-11.25 ms) are
 * requested by Zephyr ~5 s after connecting. macOS rejects that range and
 * stays at its initial 30 ms, which makes the trackball feel laggy over BLE.
 * If a host connection is still slower than 15 ms after that request, ask
 * for a fixed 15 ms, which macOS accepts (measured with &conn_info).
 *
 * macOS sometimes starts at 15 ms but with latency 0 / timeout 720 ms
 * (measured: i12l0t72). The interval is fine then, but latency 0 makes the
 * right half answer every connection event, which costs battery. In that case
 * keep the current interval and only ask for the preferred latency/timeout.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define FALLBACK_INTERVAL 12 /* 15 ms in 1.25 ms units */
#define MAX_ATTEMPTS 3

static uint8_t attempts;

static bool needs_fallback(uint16_t interval, uint16_t latency) {
    return interval > FALLBACK_INTERVAL || latency < CONFIG_BT_PERIPHERAL_PREF_LATENCY;
}

static void check_conn(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_PERIPHERAL ||
        info.state != BT_CONN_STATE_CONNECTED ||
        !needs_fallback(info.le.interval, info.le.latency)) {
        return;
    }
    /* Never ask for a slower interval than the host already gave us */
    uint16_t interval = MIN(info.le.interval, FALLBACK_INTERVAL);
    int err = bt_conn_le_param_update(
        conn, BT_LE_CONN_PARAM(interval, interval, CONFIG_BT_PERIPHERAL_PREF_LATENCY,
                               CONFIG_BT_PERIPHERAL_PREF_TIMEOUT));
    LOG_INF("Host params i%u l%u, requested i%u l%u (err %d)", info.le.interval, info.le.latency,
            interval, CONFIG_BT_PERIPHERAL_PREF_LATENCY, err);
}

static void fallback_work_cb(struct k_work *work) {
    if (attempts >= MAX_ATTEMPTS) {
        return;
    }
    attempts++;
    bt_conn_foreach(BT_CONN_TYPE_LE, check_conn, NULL);
}

static K_WORK_DELAYABLE_DEFINE(fallback_work, fallback_work_cb);

static void on_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;
    if (err || bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_PERIPHERAL) {
        return;
    }
    attempts = 0;
    /* After Zephyr's own preferred-parameter request (5 s) had time to finish */
    k_work_reschedule(&fallback_work, K_MSEC(8000));
}

static void on_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_PERIPHERAL) {
        return;
    }
    if (needs_fallback(interval, latency)) {
        k_work_reschedule(&fallback_work, K_MSEC(3000));
    }
}

BT_CONN_CB_DEFINE(conn_interval_fallback_cb) = {
    .connected = on_connected,
    .le_param_updated = on_param_updated,
};

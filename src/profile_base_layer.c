/*
 * Per-BLE-profile base layer.
 *
 * The BT0Mac..BT4iPad macros switch the base layer together with the profile
 * (&bt BT_SEL n + &to layer), but ZMK only restores the active profile at
 * power-on, so the keyboard always came back on layer 0. This module applies
 * the profile's base layer (CONFIG_SHKB_PROFILE_BASE_LAYER_<n>) with
 * zmk_keymap_layer_to(), the same call &to makes:
 *
 * - at power-on, once settings (including the saved active profile) are loaded
 * - whenever the active profile changes to a different index (BT_SEL, BT_NXT,
 *   BT_PRV, ...)
 *
 * zmk_ble_active_profile_changed also fires on connect/disconnect/pairing of
 * the same profile; those are ignored so a reconnect does not reset layers
 * the user has switched to on purpose.
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Index = BLE profile number. */
static const uint8_t base_layers[] = {
    CONFIG_SHKB_PROFILE_BASE_LAYER_0, CONFIG_SHKB_PROFILE_BASE_LAYER_1,
    CONFIG_SHKB_PROFILE_BASE_LAYER_2, CONFIG_SHKB_PROFILE_BASE_LAYER_3,
    CONFIG_SHKB_PROFILE_BASE_LAYER_4,
};

/* Profile whose base layer was last applied; only touched from the system workqueue. */
static int applied_profile = -1;

static void apply_cb(struct k_work *work) {
    int profile = zmk_ble_active_profile_index();
    if (profile == applied_profile) {
        return;
    }
    applied_profile = profile;

    if (profile < 0 || profile >= ARRAY_SIZE(base_layers)) {
        return;
    }
    uint8_t layer = base_layers[profile];
    if (layer >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_WRN("Base layer %d for BLE profile %d does not exist", layer, profile);
        return;
    }

    LOG_INF("BLE profile %d -> base layer %d", profile, layer);
    zmk_keymap_layer_to(layer);
}
static K_WORK_DEFINE(apply_work, apply_cb);

static int profile_listener(const zmk_event_t *eh) {
    /* Applied from the workqueue, which compares against the last applied
     * profile, so repeated events for the same profile do nothing. */
    k_work_submit(&apply_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(shkb_profile_base_layer, profile_listener);
ZMK_SUBSCRIPTION(shkb_profile_base_layer, zmk_ble_active_profile_changed);

/* settings_load() in ZMK's main() commits every handler after all values,
 * including ble/active_profile, have been read, so the profile is final here.
 * No profile-changed event is raised for it, and the host may not be in range
 * to trigger one by connecting. */
static int profile_base_layer_commit(void) {
    k_work_submit(&apply_work);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(shkb_profile_base_layer, "shkb_pbl", NULL, NULL, profile_base_layer_commit,
                               NULL);

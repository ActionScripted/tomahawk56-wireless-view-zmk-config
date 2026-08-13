/*
 * Tomahawk56 per-key layer lighting
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_layer_rgb_sync

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/rgb.h>
#include <zmk/activity.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#else
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#endif
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

#define LED_COUNT 28
#define MAIN_ROWS 4
#define MAIN_COLS 6
#define MAIN_KEY_COUNT (MAIN_ROWS * MAIN_COLS)
#define THUMB_COUNT 4
#define LRGB_RETRY_MS 50
#define LRGB_CONNECT_RETRY_MS 500
#define LRGB_CONNECT_ATTEMPTS 8
#define LRGB_ACTIVITY_SETTLE_MS 10

/*
 * The behavior is global, so one binding carries three kinds of param1:
 *   0x000-0x0FF  state sync, central -> peripheral: param1 is the active layer,
 *                param2 the brightness with the on/off flag in BIT(8). Sent by
 *                the work handler below; never bound in the keymap.
 *   0x100-0x1FF  RGB underglow command, forwarded to &rgb_ug (LRGB() in the keymap).
 *   0x200-0x2FF  output command, forwarded to &out on the central (LOUT()).
 */
#define LRGB_CONTROL_BASE 0x100
#define LRGB_OUTPUT_BASE 0x200

/* Settings is the only map that is not purely static: the Bluetooth profile row
 * and the USB/BLE pair report what is selected. Col 5 of the profile row is
 * BT_CLR_ALL and must never be recolored as a profile. */
#define LRGB_SETTINGS_LAYER 4
#define LRGB_BT_ROW 0
#define LRGB_BT_FIRST_COL 1
#define LRGB_BT_PROFILE_COUNT 4
#define LRGB_OUT_ROW 1
#define LRGB_OUT_USB_COL 1
#define LRGB_OUT_BLE_COL 2

#ifndef ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER 1
#endif

BUILD_ASSERT(MAIN_KEY_COUNT + THUMB_COUNT == LED_COUNT,
             "the LED chain is the 24 main keys followed by the 4 thumbs");
BUILD_ASSERT(LRGB_BT_FIRST_COL + LRGB_BT_PROFILE_COUNT <= MAIN_COLS,
             "the profile keys must fit the row without reaching BT_CLR_ALL");
/* Underglow rejects an out-of-range pixel index, and this handler would retry
 * that rejection every LRGB_RETRY_MS forever. Catch the mismatch at build time. */
BUILD_ASSERT(LED_COUNT == DT_PROP(DT_CHOSEN(zmk_underglow), chain_length),
             "LED_COUNT must match the led_strip chain-length in tomahawk56.keymap");

enum key_color {
    COLOR_OFF,
    COLOR_WHITE,
    COLOR_ORANGE,
    COLOR_TEAL,
    COLOR_YELLOW,
    COLOR_PURPLE,
    COLOR_GREEN,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_LIGHT_BLUE,
    COLOR_MAGENTA,
    COLOR_LIME,
};

/* Defy-inspired hues. Brightness comes from the user's underglow state. */
static const struct zmk_led_hsb palette[] = {
    [COLOR_OFF] = {0, 0, 0},
    [COLOR_WHITE] = {0, 0, 100},
    [COLOR_ORANGE] = {38, 100, 100},
    [COLOR_TEAL] = {174, 100, 100},
    [COLOR_YELLOW] = {55, 100, 100},
    [COLOR_PURPLE] = {275, 85, 100},
    [COLOR_GREEN] = {120, 100, 100},
    [COLOR_RED] = {0, 100, 100},
    [COLOR_BLUE] = {220, 100, 100},
    [COLOR_LIGHT_BLUE] = {213, 66, 100},
    [COLOR_MAGENTA] = {310, 100, 100},
    [COLOR_LIME] = {78, 100, 100},
};

/*
 * Per-layer color maps. Each row reads left-to-right as the user sees it; the
 * central (left) and peripheral (right) halves compile their own maps under the
 * same names.
 */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static const uint8_t base_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
};

static const uint8_t symbols_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE},
    {COLOR_OFF, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL},
    {COLOR_OFF, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW},
};

static const uint8_t functional_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE},
    {COLOR_OFF, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN},
    {COLOR_OFF, COLOR_ORANGE, COLOR_ORANGE, COLOR_TEAL, COLOR_TEAL, COLOR_LIME},
    {COLOR_OFF, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_LIME},
};

static const uint8_t magic_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_OFF},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
};

/* Red is irreversible (the bootloader corner and BT_CLR_ALL), blue is
 * Bluetooth, orange is unlock, white is the way out. */
static const uint8_t settings_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_RED, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_RED},
    {COLOR_ORANGE, COLOR_TEAL, COLOR_LIGHT_BLUE, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_WHITE, COLOR_OFF, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_OFF},
    {COLOR_WHITE, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_GREEN},
};

/* Thumb LEDs are chained from Opt/Enter back toward Ctrl/Tab. */
static const uint8_t base_thumbs[THUMB_COUNT] = {
    COLOR_GREEN, COLOR_MAGENTA, COLOR_ORANGE, COLOR_BLUE};
#else
static const uint8_t base_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
};

static const uint8_t symbols_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_OFF},
    {COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_OFF},
};

static const uint8_t functional_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE},
    {COLOR_RED, COLOR_RED, COLOR_RED, COLOR_RED, COLOR_ORANGE, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_ORANGE, COLOR_OFF},
    {COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_ORANGE, COLOR_OFF},
};

static const uint8_t magic_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_LIGHT_BLUE, COLOR_LIGHT_BLUE, COLOR_LIGHT_BLUE, COLOR_OFF, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_GREEN, COLOR_OFF},
    {COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_GREEN, COLOR_OFF},
};

/* Every Settings control lives on the left half; the right keeps its own
 * bootloader corner and the white pair that toggles the layer. */
static const uint8_t settings_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_RED},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
};

/* Mirrored: the right thumb chain runs from Ctrl/Tab toward Opt/Enter. */
static const uint8_t base_thumbs[THUMB_COUNT] = {
    COLOR_BLUE, COLOR_ORANGE, COLOR_MAGENTA, COLOR_GREEN};
#endif

/* In Settings every thumb is the same key: leave. */
static const uint8_t settings_thumbs[THUMB_COUNT] = {
    COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE};

/* Indexed by keymap layer number (the L_* defines in tomahawk56.keymap). */
static const uint8_t (*const main_maps[])[MAIN_COLS] = {
    base_main,
    symbols_main,
    functional_main,
    magic_main,
    settings_main,
};

static const uint8_t *const thumb_maps[] = {
    base_thumbs,
    base_thumbs,
    base_thumbs,
    base_thumbs,
    settings_thumbs,
};

BUILD_ASSERT(ARRAY_SIZE(thumb_maps) == ARRAY_SIZE(main_maps),
             "every layer needs both a main and a thumb color map");
BUILD_ASSERT(LRGB_SETTINGS_LAYER < ARRAY_SIZE(main_maps),
             "LRGB_SETTINGS_LAYER must match L_SET in tomahawk56.keymap");

/* Physical LED index for each main-key position above. Both halves chain the
 * same way, top row starting at the outer column. */
static const uint8_t main_pixels[MAIN_ROWS][MAIN_COLS] = {
    {5, 4, 3, 2, 1, 0},
    {6, 7, 8, 9, 10, 11},
    {17, 16, 15, 14, 13, 12},
    {18, 19, 20, 21, 22, 23},
};

static const uint16_t led_indices[LED_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
};

static struct zmk_led_hsb colors[LED_COUNT];
static uint8_t last_layer = UINT8_MAX;
static uint8_t last_brightness = UINT8_MAX;
static bool layer_channel_active;
static bool startup_rgb_forced_on;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static uint8_t last_synced_layer = UINT8_MAX;
static uint8_t last_synced_brightness = UINT8_MAX;
static bool last_synced_on;
static uint8_t connect_sync_attempts;
#else
/* State received from the central via the sync payload. */
static uint8_t remote_layer;
static uint8_t remote_brightness;
static bool remote_on;
static bool remote_initialized;
#endif

static const uint8_t (*main_map_for_layer(uint8_t layer))[MAIN_COLS] {
    return layer < ARRAY_SIZE(main_maps) ? main_maps[layer] : main_maps[0];
}

static const uint8_t *thumb_map_for_layer(uint8_t layer) {
    return layer < ARRAY_SIZE(thumb_maps) ? thumb_maps[layer] : thumb_maps[0];
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* The profile keys are the central's own, so their state needs no room in the
 * sync payload: the selected profile turns green once its host is connected,
 * white while it is still advertising, and the other three stay blue. */
static void mark_active_bt_profile(void) {
    int active = zmk_ble_active_profile_index();
    if (active < 0 || active >= LRGB_BT_PROFILE_COUNT) {
        return;
    }

    colors[main_pixels[LRGB_BT_ROW][LRGB_BT_FIRST_COL + active]] =
        palette[zmk_ble_active_profile_is_connected() ? COLOR_GREEN : COLOR_WHITE];
}

/* Follows the *preferred* transport, not the one currently carrying traffic, so
 * the key reports what you chose even when that transport is unavailable -
 * picking USB while unplugged still moves the light. */
static void mark_active_output(void) {
    uint8_t col = (zmk_endpoint_get_preferred_transport() == ZMK_TRANSPORT_USB)
                      ? LRGB_OUT_USB_COL
                      : LRGB_OUT_BLE_COL;

    colors[main_pixels[LRGB_OUT_ROW][col]] = palette[COLOR_GREEN];
}
#endif

static int render_layer(uint8_t layer, uint8_t brightness) {
    const uint8_t (*main_map)[MAIN_COLS] = main_map_for_layer(layer);
    const uint8_t *thumb_map = thumb_map_for_layer(layer);

    for (uint8_t row = 0; row < MAIN_ROWS; row++) {
        for (uint8_t col = 0; col < MAIN_COLS; col++) {
            colors[main_pixels[row][col]] = palette[main_map[row][col]];
        }
    }

    for (uint8_t thumb = 0; thumb < THUMB_COUNT; thumb++) {
        colors[MAIN_KEY_COUNT + thumb] = palette[thumb_map[thumb]];
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (layer == LRGB_SETTINGS_LAYER) {
        mark_active_bt_profile();
        mark_active_output();
    }
#endif

    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (colors[i].b > 0) {
            colors[i].b = brightness;
        }
    }

    int err = zmk_rgb_underglow_status_channel_pixels(
        ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER, led_indices, colors, LED_COUNT);
    if (err == 0) {
        layer_channel_active = true;
    }
    return err;
}

static void layer_rgb_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(layer_rgb_work, layer_rgb_work_handler);

/*
 * Never the system workqueue. That queue drains the split peripheral's key
 * events and timestamps them as it does so, so stalling it skews right-half
 * hold-tap timing and silently drops events. This handler does stall:
 * zmk_split_central_invoke_behavior() can sleep 100 ms on a full send queue.
 */
static void layer_rgb_schedule(k_timeout_t delay) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &layer_rgb_work, delay);
}

/* The central's activity represents the entire keyboard. The peripheral uses
 * its local activity only while disconnected; while connected, the central's
 * synced state must win so left-hand-only typing does not darken the right. */
static bool activity_requires_darkness(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE;
#else
    return !zmk_split_bt_peripheral_is_connected() &&
           zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE;
#endif
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void layer_rgb_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0) {
        return;
    }

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    /* GATT behavior discovery completes asynchronously after this callback. */
    last_synced_layer = UINT8_MAX;
    connect_sync_attempts = LRGB_CONNECT_ATTEMPTS;
    layer_rgb_schedule(K_MSEC(LRGB_CONNECT_RETRY_MS));
}

BT_CONN_CB_DEFINE(tomahawk56_layer_rgb_conn_callbacks) = {
    .connected = layer_rgb_connected,
};
#endif

/*
 * What this half should be showing. The central reads it live; the peripheral
 * only knows what the last sync payload told it. False means the underglow
 * subsystem is not ready yet and the caller should come back.
 */
static bool current_state(uint8_t *layer, uint8_t *brightness, bool *underglow_on) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (zmk_rgb_underglow_get_state(underglow_on) < 0) {
        return false;
    }

    *layer = zmk_keymap_highest_layer_active();
    *brightness = zmk_rgb_underglow_calc_brt(0).b;
#else
    /* Render Base locally while the central connects and discovers services. */
    if (!remote_initialized) {
        if (zmk_rgb_underglow_get_state(&remote_on) < 0) {
            return false;
        }
        remote_brightness = zmk_rgb_underglow_calc_brt(0).b;
        remote_initialized = true;
    }

    *layer = remote_layer;
    *brightness = remote_brightness;
    *underglow_on = remote_on;
#endif
    return true;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* Push the state the peripheral cannot see for itself. Returns the delay to try
 * again after, or 0 when the peripheral is up to date. */
static uint32_t sync_state_to_peripheral(uint8_t layer, uint8_t brightness, bool underglow_on) {
    if (layer == last_synced_layer && brightness == last_synced_brightness &&
        underglow_on == last_synced_on) {
        return 0;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(lrgb_sync)),
        .param1 = layer,
        .param2 = brightness | (underglow_on ? BIT(8) : 0),
    };
    struct zmk_behavior_binding_event event = {.source = 0, .position = 0,
                                               .timestamp = k_uptime_get()};

    /* Claimed before the send for the same reason as the paint in the work
     * handler, and the window is far wider here: the send blocks up to 100 ms
     * on a full queue. */
    last_synced_layer = layer;
    last_synced_brightness = brightness;
    last_synced_on = underglow_on;

    if (zmk_split_central_invoke_behavior(0, &binding, event, true) != 0) {
        last_synced_layer = UINT8_MAX;
        return LRGB_RETRY_MS;
    }

    if (connect_sync_attempts > 0) {
        connect_sync_attempts--;
        if (connect_sync_attempts > 0) {
            /* Queue acceptance is not a delivery acknowledgement. */
            last_synced_layer = UINT8_MAX;
            return LRGB_CONNECT_RETRY_MS;
        }
    }
    return 0;
}
#endif

static void layer_rgb_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    bool retry = false;
    uint32_t retry_delay_ms = LRGB_RETRY_MS;

    /* A persisted manual-off state must not carry across a power cycle. */
    if (!startup_rgb_forced_on) {
        if (zmk_rgb_underglow_on() < 0) {
            layer_rgb_schedule(K_MSEC(LRGB_RETRY_MS));
            return;
        }
        startup_rgb_forced_on = true;
    }

    uint8_t layer = 0;
    uint8_t brightness = 0;
    bool underglow_on = false;
    if (!current_state(&layer, &brightness, &underglow_on)) {
        layer_rgb_schedule(K_MSEC(LRGB_RETRY_MS));
        return;
    }

    bool effective_on = underglow_on && !activity_requires_darkness();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t sync_retry_ms = sync_state_to_peripheral(layer, brightness, effective_on);
    if (sync_retry_ms != 0) {
        retry = true;
        retry_delay_ms = sync_retry_ms;
    }
#else
    /* The peripheral does not run ZMK's independent idle auto-off: while the
     * link is up it follows the central, and while disconnected the activity
     * rule above supplies the fallback. Keep the real underglow state aligned
     * so the renderer's ordinary off state blanks every status channel too. */
    bool local_on;
    int align_err = zmk_rgb_underglow_get_state(&local_on);
    if (align_err == 0 && local_on != effective_on) {
        align_err = effective_on ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off();
    }
    if (align_err < 0) {
        layer_rgb_schedule(K_MSEC(LRGB_RETRY_MS));
        return;
    }
#endif

    if (!effective_on || brightness == 0) {
        if (layer_channel_active) {
            if (zmk_rgb_underglow_clear_status_channel(
                    ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER) == 0) {
                layer_channel_active = false;
            } else {
                retry = true;
            }
        }
    } else if (!layer_channel_active || layer != last_layer || brightness != last_brightness) {
        /*
         * Claim the frame before painting it, never after. An invalidation
         * raised while render_layer() runs then lands on top of the claim and
         * survives; committing afterwards would overwrite it and drop the
         * repaint. That is the only way a Settings-layer profile or endpoint
         * change - which moves the colors without moving the layer number -
         * can be lost.
         */
        last_layer = layer;
        last_brightness = brightness;

        if (render_layer(layer, brightness) != 0) {
            last_layer = UINT8_MAX;
            retry = true;
        }
    }

    if (retry) {
        layer_rgb_schedule(K_MSEC(retry_delay_ms));
    }
}

static int layer_rgb_listener(const zmk_event_t *event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    last_layer = UINT8_MAX;
    last_synced_layer = UINT8_MAX;
#endif
    layer_rgb_schedule(K_NO_WAIT);
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb, layer_rgb_listener);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb, zmk_layer_state_changed);

/* Only the Settings layer draws profile and endpoint state, and only on the
 * central's own LEDs, so nothing is invalidated or synced anywhere else. */
static int layer_rgb_ble_listener(const zmk_event_t *event) {
    ARG_UNUSED(event);
    if (zmk_keymap_highest_layer_active() != LRGB_SETTINGS_LAYER) {
        return 0;
    }

    last_layer = UINT8_MAX;
    layer_rgb_schedule(K_NO_WAIT);
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_ble, layer_rgb_ble_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_ble, zmk_ble_active_profile_changed);
/* Catches the output moving on its own - USB unplugged, a fallback - rather
 * than by a key press, which repaints itself in layer_rgb_sync_pressed(). */
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_ble, zmk_endpoint_changed);
#endif

/* The central's activity covers both halves and its sync normally carries the
 * effective on/off state to the peripheral. A disconnected peripheral falls
 * back to its own activity so it cannot remain lit forever, especially on USB
 * where ZMK does not enter deep sleep.
 *
 * The delay is the point. ZMK's own AUTO_OFF_IDLE listener flips the underglow
 * on this same event and ZMK_LISTENER order is link order, so running promptly
 * risks reading the state from before the flip - and on wake that reads as off,
 * clearing the channel with no further event to repaint it.
 */
static int layer_rgb_activity_listener(const zmk_event_t *event) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(event);
    if (ev == NULL) {
        return -ENOTSUP;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    last_synced_layer = UINT8_MAX;
#else
    /* Ignore the peripheral's independent idle transition while connected:
     * the central's synced state governs a connected half. Its ACTIVE
     * transition and all disconnected transitions still repaint or clear. */
    if (ev->state != ZMK_ACTIVITY_ACTIVE && zmk_split_bt_peripheral_is_connected()) {
        return 0;
    }
#endif
    last_layer = UINT8_MAX;
    layer_rgb_schedule(K_MSEC(LRGB_ACTIVITY_SETTLE_MS));
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_activity, layer_rgb_activity_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_activity, zmk_activity_state_changed);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* If the link drops after this half is already idle, no new activity event will
 * arrive to clear it. React to that edge directly; reconnect waits for the
 * central's authoritative sync before repainting. */
static int layer_rgb_peripheral_status_listener(const zmk_event_t *event) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(event);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    if (!ev->connected && zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        last_layer = UINT8_MAX;
        layer_rgb_schedule(K_MSEC(LRGB_ACTIVITY_SETTLE_MS));
    }
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_peripheral_status, layer_rgb_peripheral_status_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_peripheral_status,
                 zmk_split_peripheral_status_changed);
#endif

static int layer_rgb_control_convert(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    /* Only keymap-bound RGB commands need relative-to-absolute conversion: the
     * state sync range is built by the work handler and output commands are
     * already absolute. */
    if (binding->param1 < LRGB_CONTROL_BASE || binding->param1 >= LRGB_OUTPUT_BASE) {
        return 0;
    }

    struct zmk_behavior_binding underglow = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgb_ug)),
        .param1 = binding->param1 - LRGB_CONTROL_BASE,
        .param2 = binding->param2,
    };
    int err = behavior_keymap_binding_convert_central_state_dependent_params(&underglow, event);
    if (err == 0) {
        binding->param1 = LRGB_CONTROL_BASE + underglow.param1;
        binding->param2 = underglow.param2;
    }
    return err;
}

/* Press the behavior this payload really meant, with the range base stripped. */
static int press_forwarded(const char *behavior_dev, uint32_t param1, uint32_t param2,
                           struct zmk_behavior_binding_event event) {
    struct zmk_behavior_binding forwarded = {
        .behavior_dev = behavior_dev,
        .param1 = param1,
        .param2 = param2,
    };
    return behavior_keymap_binding_pressed(&forwarded, event);
}

static int layer_rgb_sync_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    if (binding->param1 >= LRGB_OUTPUT_BASE) {
        /* &out and the key showing which output is chosen are both central-only;
         * the peripheral is reached only because this behavior is global. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        int err = press_forwarded(DEVICE_DT_NAME(DT_NODELABEL(out)),
                                  binding->param1 - LRGB_OUTPUT_BASE, binding->param2, event);
        if (err < 0) {
            return err;
        }

        last_layer = UINT8_MAX;
        layer_rgb_schedule(K_NO_WAIT);
#endif
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (binding->param1 >= LRGB_CONTROL_BASE) {
        int err = press_forwarded(DEVICE_DT_NAME(DT_NODELABEL(rgb_ug)),
                                  binding->param1 - LRGB_CONTROL_BASE, binding->param2, event);
        if (err < 0) {
            return err;
        }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        last_synced_layer = UINT8_MAX;
#else
        zmk_rgb_underglow_get_state(&remote_on);
        remote_brightness = zmk_rgb_underglow_calc_brt(0).b;
#endif
        layer_rgb_schedule(K_NO_WAIT);
        return ZMK_BEHAVIOR_OPAQUE;
    }

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    remote_layer = binding->param1;
    remote_brightness = binding->param2 & 0xff;
    remote_on = (binding->param2 & BIT(8)) != 0;
    remote_initialized = true;
    layer_rgb_schedule(K_NO_WAIT);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

/* Every payload is edge-triggered on press; releases have nothing to undo. */
static int layer_rgb_sync_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api layer_rgb_sync_driver_api = {
    .binding_convert_central_state_dependent_params = layer_rgb_control_convert,
    .binding_pressed = layer_rgb_sync_pressed,
    .binding_released = layer_rgb_sync_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &layer_rgb_sync_driver_api);

static int layer_rgb_init(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    connect_sync_attempts = LRGB_CONNECT_ATTEMPTS;
#endif
    layer_rgb_schedule(K_MSEC(350));
    return 0;
}

SYS_INIT(layer_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

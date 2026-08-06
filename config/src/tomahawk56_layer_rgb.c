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
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
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

/*
 * lrgb_sync behavior param1 encoding. The behavior is global, so one binding
 * carries two kinds of traffic:
 *   0x000-0x0FF  state sync sent central -> peripheral: param1 is the active
 *                layer, param2 is brightness with the on/off flag in BIT(8).
 *                Sent from the work handler below; never bound in the keymap.
 *   0x100-0x1FF  RGB underglow control command (RGB_TOG etc.), forwarded to
 *                &rgb_ug. Must match the LRGB() macro in tomahawk56.keymap.
 *   0x200-0x2FF  output selection command (OUT_USB etc.), forwarded to &out on
 *                the central. Must match the LOUT() macro in tomahawk56.keymap.
 */
#define LRGB_CONTROL_BASE 0x100
#define LRGB_OUTPUT_BASE 0x200

/* Keep in sync with L_SET in tomahawk56.keymap. The Settings layer is the only
 * one whose map is not purely static: the Bluetooth profile row reports which
 * profile is selected. The four &bt BT_SEL keys are the number row's cols 1-4;
 * col 5 is BT_CLR_ALL and must never be recolored as a profile. */
#define LRGB_SETTINGS_LAYER 4
#define LRGB_BT_ROW 0
#define LRGB_BT_FIRST_COL 1
#define LRGB_BT_PROFILE_COUNT 4

/* The USB/BLE pair on the second row; the selected one is repainted green. */
#define LRGB_OUT_ROW 1
#define LRGB_OUT_USB_COL 1
#define LRGB_OUT_BLE_COL 2

#ifndef ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER 1
#endif

BUILD_ASSERT(MAIN_KEY_COUNT + THUMB_COUNT == LED_COUNT,
             "the LED chain is the 24 main keys followed by the 4 thumbs");

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

/* Defy-inspired hues. Brightness is supplied by the user's underglow state. */
static const struct zmk_led_hsb palette[] = {
    [COLOR_OFF] = {0, 0, 0},       [COLOR_WHITE] = {0, 0, 100},
    [COLOR_ORANGE] = {38, 100, 100}, [COLOR_TEAL] = {174, 100, 100},
    [COLOR_YELLOW] = {55, 100, 100}, [COLOR_PURPLE] = {275, 85, 100},
    [COLOR_GREEN] = {120, 100, 100}, [COLOR_RED] = {0, 100, 100},
    [COLOR_BLUE] = {220, 100, 100}, [COLOR_MAGENTA] = {310, 100, 100},
    /* Defy palette slot 7 is RGB (87, 164, 255). */
    [COLOR_LIGHT_BLUE] = {213, 66, 100},
    [COLOR_LIME] = {78, 100, 100},
};

/*
 * Per-layer color maps. Each row is left-to-right as seen by the user; the
 * central (left) and peripheral (right) halves each compile their own maps
 * under the same names.
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

/*
 * Settings reads as its own instrument panel: red is irreversible (the
 * bootloader corner and BT_CLR_ALL), blue is Bluetooth, white is the way out
 * (the entry pair and every thumb).
 */
static const uint8_t settings_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_RED, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_RED},
    /* Unlock is orange; the selected output is repainted green at render time. */
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

/* Settings is a left-handed panel. The right half keeps only its own bootloader
 * corner and the white pair that toggles the layer; every other key is dark and
 * dead, so the right hand's whole job here is leaving. */
static const uint8_t settings_main[MAIN_ROWS][MAIN_COLS] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_RED},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
};

/* The mirrored right thumb LED chain runs from Ctrl/Tab toward Opt/Enter. */
static const uint8_t base_thumbs[THUMB_COUNT] = {
    COLOR_BLUE, COLOR_ORANGE, COLOR_MAGENTA, COLOR_GREEN};
#endif

/* In Settings every thumb is the same key: leave. */
static const uint8_t settings_thumbs[THUMB_COUNT] = {
    COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE};

/* Indexed by keymap layer number (see the L_* defines in tomahawk56.keymap).
 * Thumbs keep their Base role colors on every layer. */
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

/* Physical LED index for each main-key position of the map above. Both halves
 * chain the same way, top row starting at the outer column. */
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
/*
 * The five Bluetooth profile keys sit on the central's home row, so the central
 * can report their state without extending the split sync payload: the selected
 * profile turns green once its host is connected, white while it is still
 * advertising. The other four stay blue.
 */
static void mark_active_bt_profile(void) {
    int active = zmk_ble_active_profile_index();
    if (active < 0 || active >= LRGB_BT_PROFILE_COUNT) {
        return;
    }

    uint8_t col = LRGB_BT_FIRST_COL + active;
    BUILD_ASSERT(LRGB_BT_FIRST_COL + LRGB_BT_PROFILE_COUNT <= MAIN_COLS,
                 "the profile keys must fit the row without reaching BT_CLR_ALL");

    colors[main_pixels[LRGB_BT_ROW][col]] =
        palette[zmk_ble_active_profile_is_connected() ? COLOR_GREEN : COLOR_WHITE];
}

/*
 * The selected output turns green; the other keeps its resting color. This
 * follows the *preferred* transport, not the one currently carrying traffic, so
 * the key reports what you chose even when that transport is unavailable -
 * picking USB while unplugged still moves the light.
 */
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

    for (uint8_t i = 0; i < LED_COUNT; i++) {
        colors[i] = palette[COLOR_OFF];
    }

    for (uint8_t row = 0; row < MAIN_ROWS; row++) {
        for (uint8_t col = 0; col < MAIN_COLS; col++) {
            colors[main_pixels[row][col]] = palette[main_map[row][col]];
        }
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (layer == LRGB_SETTINGS_LAYER) {
        mark_active_bt_profile();
        mark_active_output();
    }
#endif

    for (uint8_t thumb = 0; thumb < THUMB_COUNT; thumb++) {
        colors[MAIN_KEY_COUNT + thumb] = palette[thumb_map[thumb]];
    }

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

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    bool underglow_on = false;
    if (zmk_rgb_underglow_get_state(&underglow_on) < 0) {
        layer_rgb_schedule(K_MSEC(LRGB_RETRY_MS));
        return;
    }

    uint8_t layer = zmk_keymap_highest_layer_active();
    uint8_t brightness = zmk_rgb_underglow_calc_brt(0).b;

    if (layer != last_synced_layer || brightness != last_synced_brightness ||
        underglow_on != last_synced_on) {
        struct zmk_behavior_binding binding = {
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(lrgb_sync)),
            .param1 = layer,
            .param2 = brightness | (underglow_on ? BIT(8) : 0),
        };
        struct zmk_behavior_binding_event event = {.source = 0, .position = 0,
                                                   .timestamp = k_uptime_get()};

        if (zmk_split_central_invoke_behavior(0, &binding, event, true) == 0) {
            last_synced_layer = layer;
            last_synced_brightness = brightness;
            last_synced_on = underglow_on;
            if (connect_sync_attempts > 0) {
                connect_sync_attempts--;
                if (connect_sync_attempts > 0) {
                    /* Queue acceptance is not a delivery acknowledgement. */
                    last_synced_layer = UINT8_MAX;
                    retry = true;
                    retry_delay_ms = LRGB_CONNECT_RETRY_MS;
                }
            }
        } else {
            retry = true;
        }
    }
#else
    /* Render Base locally while the central connects and discovers services. */
    if (!remote_initialized) {
        if (zmk_rgb_underglow_get_state(&remote_on) < 0) {
            layer_rgb_schedule(K_MSEC(LRGB_RETRY_MS));
            return;
        }
        remote_brightness = zmk_rgb_underglow_calc_brt(0).b;
        remote_initialized = true;
    }

    uint8_t layer = remote_layer;
    uint8_t brightness = remote_brightness;
    bool underglow_on = remote_on;
#endif

    if (!underglow_on || brightness == 0) {
        if (layer_channel_active) {
            if (zmk_rgb_underglow_clear_status_channel(
                    ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER) == 0) {
                layer_channel_active = false;
            } else {
                retry = true;
            }
        }
    } else if (!layer_channel_active || layer != last_layer || brightness != last_brightness) {
        if (render_layer(layer, brightness) == 0) {
            last_layer = layer;
            last_brightness = brightness;
        } else {
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

/*
 * Profile selection and the active profile's connect/disconnect both raise this.
 * Only the Settings layer draws that state, and only on the central's own LEDs,
 * so nothing is invalidated or synced anywhere else.
 */
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

static int layer_rgb_control_convert(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    /* Only keymap-bound control commands need relative-to-absolute conversion;
     * the state sync range is built and sent by the work handler directly, and
     * the output commands are already absolute. */
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

static int layer_rgb_sync_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    if (binding->param1 >= LRGB_OUTPUT_BASE) {
        /*
         * &out is central-only, and so is the key that shows which output is
         * chosen; the peripheral is reached because this behavior is global,
         * and has nothing to do with the payload.
         */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        struct zmk_behavior_binding output = {
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(out)),
            .param1 = binding->param1 - LRGB_OUTPUT_BASE,
            .param2 = binding->param2,
        };
        int err = behavior_keymap_binding_pressed(&output, event);
        if (err < 0) {
            return err;
        }

        last_layer = UINT8_MAX;
        layer_rgb_schedule(K_NO_WAIT);
#else
        ARG_UNUSED(event);
#endif
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (binding->param1 >= LRGB_CONTROL_BASE) {
        struct zmk_behavior_binding underglow = {
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgb_ug)),
            .param1 = binding->param1 - LRGB_CONTROL_BASE,
            .param2 = binding->param2,
        };
        int err = behavior_keymap_binding_pressed(&underglow, event);
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

    ARG_UNUSED(event);
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    remote_layer = binding->param1;
    remote_brightness = binding->param2 & 0xff;
    remote_on = (binding->param2 & BIT(8)) != 0;
    remote_initialized = true;
    layer_rgb_schedule(K_NO_WAIT);
#else
    ARG_UNUSED(binding);
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

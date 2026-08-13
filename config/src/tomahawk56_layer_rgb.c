/*
 * Tomahawk56 per-key layer lighting coordinator
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_layer_rgb_sync

#include "tomahawk56_layer_rgb_renderer.h"

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

#define RENDER_RETRY_DELAY_MS 50
#define CONNECTION_SYNC_RETRY_DELAY_MS 500
#define CONNECTION_SYNC_ATTEMPT_COUNT 8
#define ACTIVITY_SETTLE_DELAY_MS 10
#define STARTUP_RENDER_DELAY_MS 350

/*
 * The global behavior transports three command ranges:
 *
 * 0x000-0x0ff: central-to-peripheral layer, brightness, and on/off state
 * 0x100-0x1ff: commands forwarded to &rgb_ug
 * 0x200-0x2ff: commands forwarded to the central's &out behavior
 */
#define RGB_COMMAND_BASE 0x100
#define OUTPUT_COMMAND_BASE 0x200
#define SYNC_BRIGHTNESS_MASK 0xff
#define SYNC_UNDERGLOW_ON_FLAG BIT(8)

#define SETTINGS_LAYER 4

struct layer_rgb_state {
    uint8_t layer;
    uint8_t brightness;
    bool underglow_on;
};

struct rendered_layer_rgb_state {
    uint8_t layer;
    uint8_t brightness;
    bool channel_active;
};

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
struct peripheral_sync_state {
    struct layer_rgb_state last_sent;
    uint8_t connection_attempts_remaining;
};
#else
struct received_layer_rgb_state {
    struct layer_rgb_state value;
    bool initialized;
};
#endif

struct layer_rgb_runtime {
    struct rendered_layer_rgb_state rendered;
    bool startup_underglow_forced_on;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct peripheral_sync_state peripheral_sync;
#else
    struct received_layer_rgb_state received;
#endif
};

static struct layer_rgb_runtime runtime = {
    .rendered =
        {
            .layer = UINT8_MAX,
            .brightness = UINT8_MAX,
        },
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    .peripheral_sync.last_sent =
        {
            .layer = UINT8_MAX,
            .brightness = UINT8_MAX,
        },
#endif
};

static void layer_rgb_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(layer_rgb_work, layer_rgb_work_handler);

/* Split behavior sends may block, so they must never stall the key-event queue. */
static void schedule_layer_rgb(k_timeout_t delay) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &layer_rgb_work, delay);
}

static void invalidate_rendered_frame(void) { runtime.rendered.layer = UINT8_MAX; }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void invalidate_peripheral_sync(void) {
    runtime.peripheral_sync.last_sent.layer = UINT8_MAX;
}
#endif

/* A connected peripheral follows central activity; a disconnected one falls back locally. */
static bool activity_requires_darkness(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE;
#else
    return !zmk_split_bt_peripheral_is_connected() &&
           zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE;
#endif
}

/* False means underglow state is not ready and the caller must retry. */
static bool read_desired_state(struct layer_rgb_state *desired) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (zmk_rgb_underglow_get_state(&desired->underglow_on) < 0) {
        return false;
    }

    desired->layer = zmk_keymap_highest_layer_active();
    desired->brightness = zmk_rgb_underglow_calc_brt(0).b;
#else
    /* Show Base locally until the central connects and discovers behaviors. */
    if (!runtime.received.initialized) {
        if (zmk_rgb_underglow_get_state(&runtime.received.value.underglow_on) < 0) {
            return false;
        }

        runtime.received.value.brightness = zmk_rgb_underglow_calc_brt(0).b;
        runtime.received.initialized = true;
    }

    *desired = runtime.received.value;
#endif
    return true;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* Return the next retry delay, or zero when the peripheral is up to date. */
static uint32_t synchronize_peripheral(const struct layer_rgb_state *desired) {
    struct layer_rgb_state *last_sent = &runtime.peripheral_sync.last_sent;
    if (desired->layer == last_sent->layer && desired->brightness == last_sent->brightness &&
        desired->underglow_on == last_sent->underglow_on) {
        return 0;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(lrgb_sync)),
        .param1 = desired->layer,
        .param2 = desired->brightness | (desired->underglow_on ? SYNC_UNDERGLOW_ON_FLAG : 0),
    };
    struct zmk_behavior_binding_event event = {
        .source = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    /* Claim before the blocking send so a concurrent invalidation survives. */
    *last_sent = *desired;

    if (zmk_split_central_invoke_behavior(0, &binding, event, true) != 0) {
        invalidate_peripheral_sync();
        return RENDER_RETRY_DELAY_MS;
    }

    if (runtime.peripheral_sync.connection_attempts_remaining > 0) {
        runtime.peripheral_sync.connection_attempts_remaining--;
        if (runtime.peripheral_sync.connection_attempts_remaining > 0) {
            /* Queue acceptance is not a delivery acknowledgement. */
            invalidate_peripheral_sync();
            return CONNECTION_SYNC_RETRY_DELAY_MS;
        }
    }

    return 0;
}

static void layer_rgb_connected(struct bt_conn *connection, uint8_t error) {
    if (error != 0) {
        return;
    }

    struct bt_conn_info connection_info;
    if (bt_conn_get_info(connection, &connection_info) != 0 ||
        connection_info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    /* GATT behavior discovery finishes after the connection callback. */
    invalidate_peripheral_sync();
    runtime.peripheral_sync.connection_attempts_remaining = CONNECTION_SYNC_ATTEMPT_COUNT;
    schedule_layer_rgb(K_MSEC(CONNECTION_SYNC_RETRY_DELAY_MS));
}

BT_CONN_CB_DEFINE(tomahawk56_layer_rgb_connection_callbacks) = {
    .connected = layer_rgb_connected,
};
#else
/* Keep the peripheral's underglow power aligned with the central's effective state. */
static int align_peripheral_underglow(bool effective_on) {
    bool local_underglow_on;
    int result = zmk_rgb_underglow_get_state(&local_underglow_on);

    if (result == 0 && local_underglow_on != effective_on) {
        result = effective_on ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off();
    }

    return result;
}
#endif

static void update_rendered_frame(const struct layer_rgb_state *desired, bool effective_on,
                                  bool *retry) {
    if (!effective_on || desired->brightness == 0) {
        if (runtime.rendered.channel_active && tomahawk56_layer_rgb_clear() != 0) {
            *retry = true;
            return;
        }

        runtime.rendered.channel_active = false;
        return;
    }

    if (runtime.rendered.channel_active && desired->layer == runtime.rendered.layer &&
        desired->brightness == runtime.rendered.brightness) {
        return;
    }

    /* Claim before rendering so a concurrent dynamic-state invalidation survives. */
    runtime.rendered.layer = desired->layer;
    runtime.rendered.brightness = desired->brightness;

    if (tomahawk56_layer_rgb_render(desired->layer, desired->brightness) != 0) {
        invalidate_rendered_frame();
        *retry = true;
        return;
    }

    runtime.rendered.channel_active = true;
}

static void layer_rgb_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    bool retry = false;
    uint32_t retry_delay_ms = RENDER_RETRY_DELAY_MS;

    /* A persisted manual-off state does not carry across a power cycle. */
    if (!runtime.startup_underglow_forced_on) {
        if (zmk_rgb_underglow_on() < 0) {
            schedule_layer_rgb(K_MSEC(RENDER_RETRY_DELAY_MS));
            return;
        }
        runtime.startup_underglow_forced_on = true;
    }

    struct layer_rgb_state desired = {0};
    if (!read_desired_state(&desired)) {
        schedule_layer_rgb(K_MSEC(RENDER_RETRY_DELAY_MS));
        return;
    }

    bool effective_on = desired.underglow_on && !activity_requires_darkness();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct layer_rgb_state peripheral_state = desired;
    peripheral_state.underglow_on = effective_on;
    uint32_t sync_retry_delay_ms = synchronize_peripheral(&peripheral_state);
    if (sync_retry_delay_ms != 0) {
        retry = true;
        retry_delay_ms = sync_retry_delay_ms;
    }
#else
    if (align_peripheral_underglow(effective_on) < 0) {
        schedule_layer_rgb(K_MSEC(RENDER_RETRY_DELAY_MS));
        return;
    }
#endif

    update_rendered_frame(&desired, effective_on, &retry);

    if (retry) {
        schedule_layer_rgb(K_MSEC(retry_delay_ms));
    }
}

static int layer_rgb_listener(const zmk_event_t *event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    invalidate_rendered_frame();
    invalidate_peripheral_sync();
#endif
    schedule_layer_rgb(K_NO_WAIT);
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb, layer_rgb_listener);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb, zmk_layer_state_changed);

/* Profile and endpoint state changes affect only the central's Settings frame. */
static int layer_rgb_settings_state_listener(const zmk_event_t *event) {
    ARG_UNUSED(event);
    if (zmk_keymap_highest_layer_active() != SETTINGS_LAYER) {
        return 0;
    }

    invalidate_rendered_frame();
    schedule_layer_rgb(K_NO_WAIT);
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_settings_state, layer_rgb_settings_state_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_settings_state, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_settings_state, zmk_endpoint_changed);
#endif

/* Let ZMK's own idle listener update underglow state before reading it. */
static int layer_rgb_activity_listener(const zmk_event_t *event) {
    const struct zmk_activity_state_changed *activity_event = as_zmk_activity_state_changed(event);
    if (activity_event == NULL) {
        return -ENOTSUP;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    invalidate_peripheral_sync();
#else
    /* Central sync governs a connected peripheral's independent idle transition. */
    if (activity_event->state != ZMK_ACTIVITY_ACTIVE && zmk_split_bt_peripheral_is_connected()) {
        return 0;
    }
#endif
    invalidate_rendered_frame();
    schedule_layer_rgb(K_MSEC(ACTIVITY_SETTLE_DELAY_MS));
    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_activity, layer_rgb_activity_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_activity, zmk_activity_state_changed);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* A disconnected idle peripheral receives no later activity event to clear its frame. */
static int layer_rgb_peripheral_status_listener(const zmk_event_t *event) {
    const struct zmk_split_peripheral_status_changed *status_event =
        as_zmk_split_peripheral_status_changed(event);
    if (status_event == NULL) {
        return -ENOTSUP;
    }

    if (!status_event->connected && zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        invalidate_rendered_frame();
        schedule_layer_rgb(K_MSEC(ACTIVITY_SETTLE_DELAY_MS));
    }

    return 0;
}

ZMK_LISTENER(tomahawk56_layer_rgb_peripheral_status, layer_rgb_peripheral_status_listener);
ZMK_SUBSCRIPTION(tomahawk56_layer_rgb_peripheral_status, zmk_split_peripheral_status_changed);
#endif

static int convert_rgb_command(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    /* Sync and output payloads are already absolute. */
    if (binding->param1 < RGB_COMMAND_BASE || binding->param1 >= OUTPUT_COMMAND_BASE) {
        return 0;
    }

    struct zmk_behavior_binding underglow_binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgb_ug)),
        .param1 = binding->param1 - RGB_COMMAND_BASE,
        .param2 = binding->param2,
    };
    int result =
        behavior_keymap_binding_convert_central_state_dependent_params(&underglow_binding, event);
    if (result == 0) {
        binding->param1 = RGB_COMMAND_BASE + underglow_binding.param1;
        binding->param2 = underglow_binding.param2;
    }

    return result;
}

static int press_forwarded_behavior(const char *behavior_device, uint32_t parameter_1,
                                    uint32_t parameter_2, struct zmk_behavior_binding_event event) {
    struct zmk_behavior_binding forwarded_binding = {
        .behavior_dev = behavior_device,
        .param1 = parameter_1,
        .param2 = parameter_2,
    };
    return behavior_keymap_binding_pressed(&forwarded_binding, event);
}

static int layer_rgb_sync_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    if (binding->param1 >= OUTPUT_COMMAND_BASE) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        int result =
            press_forwarded_behavior(DEVICE_DT_NAME(DT_NODELABEL(out)),
                                     binding->param1 - OUTPUT_COMMAND_BASE, binding->param2, event);
        if (result < 0) {
            return result;
        }

        invalidate_rendered_frame();
        schedule_layer_rgb(K_NO_WAIT);
#endif
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (binding->param1 >= RGB_COMMAND_BASE) {
        int result =
            press_forwarded_behavior(DEVICE_DT_NAME(DT_NODELABEL(rgb_ug)),
                                     binding->param1 - RGB_COMMAND_BASE, binding->param2, event);
        if (result < 0) {
            return result;
        }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        invalidate_peripheral_sync();
#else
        zmk_rgb_underglow_get_state(&runtime.received.value.underglow_on);
        runtime.received.value.brightness = zmk_rgb_underglow_calc_brt(0).b;
#endif
        schedule_layer_rgb(K_NO_WAIT);
        return ZMK_BEHAVIOR_OPAQUE;
    }

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    runtime.received.value.layer = binding->param1;
    runtime.received.value.brightness = binding->param2 & SYNC_BRIGHTNESS_MASK;
    runtime.received.value.underglow_on = (binding->param2 & SYNC_UNDERGLOW_ON_FLAG) != 0;
    runtime.received.initialized = true;
    schedule_layer_rgb(K_NO_WAIT);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

/* All payloads are edge-triggered on press. */
static int layer_rgb_sync_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api layer_rgb_sync_driver_api = {
    .binding_convert_central_state_dependent_params = convert_rgb_command,
    .binding_pressed = layer_rgb_sync_pressed,
    .binding_released = layer_rgb_sync_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &layer_rgb_sync_driver_api);

static int layer_rgb_init(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    runtime.peripheral_sync.connection_attempts_remaining = CONNECTION_SYNC_ATTEMPT_COUNT;
#endif
    schedule_layer_rgb(K_MSEC(STARTUP_RENDER_DELAY_MS));
    return 0;
}

SYS_INIT(layer_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

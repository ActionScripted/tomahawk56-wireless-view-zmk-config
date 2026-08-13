/*
 * Tomahawk56 ZMK Studio keymap reset on firmware change
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/keymap.h>

#include <tomahawk56_build_id.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define BUILD_ID_SETTINGS_KEY "tomahawk56/build_id"

/* Zero cannot be a timestamp build id, so an absent value identifies a new image. */
static uint32_t stored_build_id;

static int load_build_id(const char *name, size_t value_length, settings_read_cb read_callback,
                         void *callback_argument) {
    if (!settings_name_steq(name, "build_id", NULL)) {
        return -ENOENT;
    }

    if (value_length != sizeof(stored_build_id)) {
        LOG_WRN("Ignoring a %d byte build id", (int)value_length);
        return -EINVAL;
    }

    int read_result = read_callback(callback_argument, &stored_build_id, sizeof(stored_build_id));
    return read_result < 0 ? read_result : 0;
}

/* Settings commit runs after Studio's persisted bindings have been loaded. */
static int reset_saved_keymap_for_new_build(void) {
    if (stored_build_id == TOMAHAWK56_BUILD_ID) {
        return 0;
    }

    LOG_INF("New firmware image (build id %u, previously %u) - dropping the ZMK Studio keymap "
            "saved by the old one",
            TOMAHAWK56_BUILD_ID, stored_build_id);

    int reset_result = zmk_keymap_reset_settings();
    if (reset_result < 0) {
        /* Retain the old id so the next boot retries. */
        LOG_ERR("Failed to reset the saved keymap: %d", reset_result);
        return 0;
    }

    const uint32_t build_id = TOMAHAWK56_BUILD_ID;
    int save_result = settings_save_one(BUILD_ID_SETTINGS_KEY, &build_id, sizeof(build_id));
    if (save_result < 0) {
        /* A failed save only causes another reset on the next boot. */
        LOG_ERR("Failed to store the build id: %d", save_result);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tomahawk56, "tomahawk56", NULL, load_build_id,
                               reset_saved_keymap_for_new_build, NULL);

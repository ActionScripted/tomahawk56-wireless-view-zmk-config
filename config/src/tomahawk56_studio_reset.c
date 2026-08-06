/*
 * Tomahawk56: drop ZMK Studio's saved keymap when new firmware is flashed
 * SPDX-License-Identifier: MIT
 *
 * A Studio save writes bindings into the settings partition, and
 * `keymap_handle_set()` in zmk/app/src/keymap.c reapplies them over the
 * compiled keymap on every boot. The settings partition is not part of the
 * .uf2, so a reflash cannot dislodge them.
 *
 * Each build stamps a fresh TOMAHAWK56_BUILD_ID into the firmware (see
 * cmake/tomahawk56_build_id.cmake) and the id of the image that last ran is
 * kept in settings. When they disagree the board is booting an image it has not
 * run before, so the saved keymap is dropped. Nothing else in settings is
 * touched; `make flash-reset` wipes the whole partition.
 *
 * This runs from the settings commit callback rather than SYS_INIT because
 * `settings_load()` runs from main(), long after every SYS_INIT, and commit is
 * the one hook Zephyr runs once all stored values have been applied.
 */

#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/keymap.h>

#include <tomahawk56_build_id.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define BUILD_ID_SETTINGS_KEY "tomahawk56/build_id"

// Zero until a stored id is loaded, which no build id can be (it is a Unix
// timestamp), so an unset id reads as "this image has not run here before".
static uint32_t last_booted_build_id;

static int build_id_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (!settings_name_steq(name, "build_id", NULL)) {
        return -ENOENT;
    }

    if (len != sizeof(last_booted_build_id)) {
        LOG_WRN("Ignoring a %d byte build id", (int)len);
        return -EINVAL;
    }

    int ret = read_cb(cb_arg, &last_booted_build_id, sizeof(last_booted_build_id));
    return ret < 0 ? ret : 0;
}

static int build_id_commit(void) {
    if (last_booted_build_id == TOMAHAWK56_BUILD_ID) {
        return 0;
    }

    LOG_INF("New firmware image (build id %u, previously %u) - dropping the ZMK Studio keymap "
            "saved by the old one",
            TOMAHAWK56_BUILD_ID, last_booted_build_id);

    int ret = zmk_keymap_reset_settings();
    if (ret < 0) {
        // Leave the stored id alone so the next boot retries the reset.
        LOG_ERR("Failed to reset the saved keymap: %d", ret);
        return 0;
    }

    const uint32_t build_id = TOMAHAWK56_BUILD_ID;
    ret = settings_save_one(BUILD_ID_SETTINGS_KEY, &build_id, sizeof(build_id));
    if (ret < 0) {
        // Harmless on its own: the reset just runs again on the next boot.
        LOG_ERR("Failed to store the build id: %d", ret);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tomahawk56, "tomahawk56", NULL, build_id_set, build_id_commit,
                               NULL);

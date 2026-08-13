/*
 * Tomahawk56 per-key layer lighting renderer
 * SPDX-License-Identifier: MIT
 */

#include "tomahawk56_layer_rgb_renderer.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#endif
#include <zmk/rgb_underglow.h>

#define LED_COUNT 28
#define MAIN_ROW_COUNT 4
#define MAIN_COLUMN_COUNT 6
#define MAIN_KEY_COUNT (MAIN_ROW_COUNT * MAIN_COLUMN_COUNT)
#define THUMB_KEY_COUNT 4

#define SETTINGS_LAYER 4
#define BLUETOOTH_PROFILE_ROW 0
#define FIRST_BLUETOOTH_PROFILE_COLUMN 1
#define BLUETOOTH_PROFILE_COUNT 4
#define OUTPUT_ROW 1
#define USB_OUTPUT_COLUMN 1
#define BLUETOOTH_OUTPUT_COLUMN 2

#ifndef ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER 1
#endif

BUILD_ASSERT(MAIN_KEY_COUNT + THUMB_KEY_COUNT == LED_COUNT,
             "the LED chain is the 24 main keys followed by the 4 thumbs");
BUILD_ASSERT(FIRST_BLUETOOTH_PROFILE_COLUMN + BLUETOOTH_PROFILE_COUNT <= MAIN_COLUMN_COUNT,
             "the profile keys must fit the row without reaching BT_CLR_ALL");
/* An invalid index would make the coordinator retry the rejected frame forever. */
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

/* Defy-inspired hues; the user's underglow state supplies brightness. */
static const struct zmk_led_hsb color_palette[] = {
    [COLOR_OFF] = {0, 0, 0},           [COLOR_WHITE] = {0, 0, 100},
    [COLOR_ORANGE] = {38, 100, 100},   [COLOR_TEAL] = {174, 100, 100},
    [COLOR_YELLOW] = {55, 100, 100},   [COLOR_PURPLE] = {275, 85, 100},
    [COLOR_GREEN] = {120, 100, 100},   [COLOR_RED] = {0, 100, 100},
    [COLOR_BLUE] = {220, 100, 100},    [COLOR_LIGHT_BLUE] = {213, 66, 100},
    [COLOR_MAGENTA] = {310, 100, 100}, [COLOR_LIME] = {78, 100, 100},
};

/* Rows read left-to-right as viewed on each half. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static const uint8_t base_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
    {COLOR_OFF, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE},
};

static const uint8_t symbols_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE},
    {COLOR_OFF, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL},
    {COLOR_OFF, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW},
};

static const uint8_t functional_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE},
    {COLOR_OFF, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN},
    {COLOR_OFF, COLOR_ORANGE, COLOR_ORANGE, COLOR_TEAL, COLOR_TEAL, COLOR_LIME},
    {COLOR_OFF, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_LIME},
};

static const uint8_t magic_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_OFF},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
};

/* Red marks destructive controls, blue Bluetooth, orange unlock, and white exit. */
static const uint8_t settings_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_RED, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_BLUE, COLOR_RED},
    {COLOR_ORANGE, COLOR_TEAL, COLOR_LIGHT_BLUE, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_WHITE, COLOR_OFF, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_OFF},
    {COLOR_WHITE, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_GREEN},
};

/* The left thumb chain runs from Opt/Enter back toward Ctrl/Tab. */
static const uint8_t base_thumb_colors[THUMB_KEY_COUNT] = {
    COLOR_GREEN,
    COLOR_MAGENTA,
    COLOR_ORANGE,
    COLOR_BLUE,
};
#else
static const uint8_t base_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
    {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_OFF},
};

static const uint8_t symbols_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_ORANGE, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_OFF},
    {COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_OFF},
};

static const uint8_t functional_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE, COLOR_PURPLE},
    {COLOR_RED, COLOR_RED, COLOR_RED, COLOR_RED, COLOR_ORANGE, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_ORANGE, COLOR_OFF},
    {COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_ORANGE, COLOR_OFF},
};

static const uint8_t magic_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_LIGHT_BLUE, COLOR_LIGHT_BLUE, COLOR_LIGHT_BLUE, COLOR_OFF, COLOR_OFF},
    {COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_TEAL, COLOR_GREEN, COLOR_OFF},
    {COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_GREEN, COLOR_OFF},
};

/* The right half retains only its bootloader corner and Settings exit pair. */
static const uint8_t settings_main_colors[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_RED},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
    {COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_OFF, COLOR_WHITE},
};

/* The right thumb chain runs from Ctrl/Tab toward Opt/Enter. */
static const uint8_t base_thumb_colors[THUMB_KEY_COUNT] = {
    COLOR_BLUE,
    COLOR_ORANGE,
    COLOR_MAGENTA,
    COLOR_GREEN,
};
#endif

static const uint8_t settings_thumb_colors[THUMB_KEY_COUNT] = {
    COLOR_WHITE,
    COLOR_WHITE,
    COLOR_WHITE,
    COLOR_WHITE,
};

/* Array order is the keymap's layer numbering. */
static const uint8_t (*const main_color_maps[])[MAIN_COLUMN_COUNT] = {
    base_main_colors,  symbols_main_colors,  functional_main_colors,
    magic_main_colors, settings_main_colors,
};

static const uint8_t *const thumb_color_maps[] = {
    base_thumb_colors, base_thumb_colors,     base_thumb_colors,
    base_thumb_colors, settings_thumb_colors,
};

BUILD_ASSERT(ARRAY_SIZE(thumb_color_maps) == ARRAY_SIZE(main_color_maps),
             "every layer needs both a main and a thumb color map");
BUILD_ASSERT(SETTINGS_LAYER < ARRAY_SIZE(main_color_maps),
             "SETTINGS_LAYER must match L_SET in tomahawk56.keymap");

/* Physical LED indices; both halves snake from the outer top-row key. */
static const uint8_t main_key_led_indices[MAIN_ROW_COUNT][MAIN_COLUMN_COUNT] = {
    {5, 4, 3, 2, 1, 0},
    {6, 7, 8, 9, 10, 11},
    {17, 16, 15, 14, 13, 12},
    {18, 19, 20, 21, 22, 23},
};

static const uint16_t led_indices[LED_COUNT] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
};

static struct zmk_led_hsb rendered_colors[LED_COUNT];

static const uint8_t (*main_colors_for_layer(uint8_t active_layer)) [MAIN_COLUMN_COUNT] {
    return active_layer < ARRAY_SIZE(main_color_maps) ? main_color_maps[active_layer]
                                                      : main_color_maps[0];
}

static const uint8_t *thumb_colors_for_layer(uint8_t active_layer) {
    return active_layer < ARRAY_SIZE(thumb_color_maps) ? thumb_color_maps[active_layer]
                                                       : thumb_color_maps[0];
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void highlight_active_bluetooth_profile(void) {
    int active_profile = zmk_ble_active_profile_index();
    if (active_profile < 0 || active_profile >= BLUETOOTH_PROFILE_COUNT) {
        return;
    }

    uint8_t led_index = main_key_led_indices[BLUETOOTH_PROFILE_ROW]
                                            [FIRST_BLUETOOTH_PROFILE_COLUMN + active_profile];
    rendered_colors[led_index] =
        color_palette[zmk_ble_active_profile_is_connected() ? COLOR_GREEN : COLOR_WHITE];
}

/* Show the preferred transport even when it is temporarily unavailable. */
static void highlight_preferred_output(void) {
    uint8_t output_column = zmk_endpoint_get_preferred_transport() == ZMK_TRANSPORT_USB
                                ? USB_OUTPUT_COLUMN
                                : BLUETOOTH_OUTPUT_COLUMN;

    rendered_colors[main_key_led_indices[OUTPUT_ROW][output_column]] = color_palette[COLOR_GREEN];
}
#endif

int tomahawk56_layer_rgb_render(uint8_t active_layer, uint8_t brightness) {
    const uint8_t (*main_colors)[MAIN_COLUMN_COUNT] = main_colors_for_layer(active_layer);
    const uint8_t *thumb_colors = thumb_colors_for_layer(active_layer);

    for (uint8_t row = 0; row < MAIN_ROW_COUNT; row++) {
        for (uint8_t column = 0; column < MAIN_COLUMN_COUNT; column++) {
            uint8_t led_index = main_key_led_indices[row][column];
            rendered_colors[led_index] = color_palette[main_colors[row][column]];
        }
    }

    for (uint8_t thumb_index = 0; thumb_index < THUMB_KEY_COUNT; thumb_index++) {
        rendered_colors[MAIN_KEY_COUNT + thumb_index] = color_palette[thumb_colors[thumb_index]];
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (active_layer == SETTINGS_LAYER) {
        highlight_active_bluetooth_profile();
        highlight_preferred_output();
    }
#endif

    for (uint8_t led_index = 0; led_index < LED_COUNT; led_index++) {
        if (rendered_colors[led_index].b > 0) {
            rendered_colors[led_index].b = brightness;
        }
    }

    return zmk_rgb_underglow_status_channel_pixels(ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER,
                                                   led_indices, rendered_colors, LED_COUNT);
}

int tomahawk56_layer_rgb_clear(void) {
    return zmk_rgb_underglow_clear_status_channel(ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER);
}

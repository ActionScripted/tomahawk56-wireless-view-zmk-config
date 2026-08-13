/*
 * Tomahawk56 per-key layer lighting renderer
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

int tomahawk56_layer_rgb_render(uint8_t active_layer, uint8_t brightness);
int tomahawk56_layer_rgb_clear(void);

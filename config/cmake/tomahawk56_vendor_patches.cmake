# Patch vendor sources before their targets compile. Each replacement accepts
# pristine or already-patched input and fails if the pinned vendor code changes.

function(tomahawk56_patch_vendor contents_var restore_command description from to)
  string(FIND "${${contents_var}}" "${to}" already_patched)
  if(NOT already_patched EQUAL -1)
    return()
  endif()

  string(FIND "${${contents_var}}" "${from}" pristine_match)
  if(pristine_match EQUAL -1)
    message(FATAL_ERROR
      "The vendor ${description} matches neither the pristine nor the patched form.\n"
      "If the tree carries an older patch, discard it with\n"
      "  ${restore_command}\n"
      "Otherwise upstream changed; review the Tomahawk56 integration.")
  endif()

  string(REPLACE "${from}" "${to}" patched_contents "${${contents_var}}")
  set(${contents_var} "${patched_contents}" PARENT_SCOPE)
endfunction()

function(tomahawk56_patch_underglow description from to)
  tomahawk56_patch_vendor(ZMK_RGB_UNDERGLOW_CONTENTS
    "git -C .build/west/zmk checkout app/src/rgb_underglow.c" "RGB ${description}"
    "${from}" "${to}")
  set(ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}" PARENT_SCOPE)
endfunction()

set(ZMK_RGB_UNDERGLOW_SOURCE "${APPLICATION_SOURCE_DIR}/src/rgb_underglow.c")
if(NOT EXISTS "${ZMK_RGB_UNDERGLOW_SOURCE}")
  message(FATAL_ERROR "Cannot locate the ZMK RGB underglow source")
endif()

if(CONFIG_TOMAHAWK56_LAYER_RGB)
  file(READ "${ZMK_RGB_UNDERGLOW_SOURCE}" ZMK_RGB_UNDERGLOW_CONTENTS)
  set(ZMK_RGB_UNDERGLOW_ORIGINAL "${ZMK_RGB_UNDERGLOW_CONTENTS}")

  tomahawk56_patch_underglow("status-channel limit"
    "#define STATUS_PIXELS_MAX 8"
    "#define STATUS_PIXELS_MAX 28")

  # Draw the layer map before battery and connectivity overrides.
  # Vendor channel ids: battery=0, layer=1, connectivity=2.
  tomahawk56_patch_underglow("status renderer"
    "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    for (uint8_t channel = 0; channel < STATUS_CHANNELS_LEN; channel++) {"
    "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    static const uint8_t render_order[] = {1, 0, 2};\n\n    for (uint8_t order_index = 0; order_index < ARRAY_SIZE(render_order); order_index++) {\n        uint8_t channel = render_order[order_index];")

  # Status channels retain data while off but must not power or illuminate LEDs.
  tomahawk56_patch_underglow("off-state renderer"
    "static void zmk_rgb_underglow_tick(struct k_work *work) {\n    if (state.on) {\n        zmk_rgb_underglow_render_effect();\n    } else {\n        zmk_rgb_underglow_effect_off();\n    }\n\n    if (zmk_rgb_underglow_status_active()) {\n        zmk_rgb_underglow_effect_status_pixel();\n    }"
    "static void zmk_rgb_underglow_tick(struct k_work *work) {\n    if (state.on) {\n        zmk_rgb_underglow_render_effect();\n        if (zmk_rgb_underglow_status_active()) {\n            zmk_rgb_underglow_effect_status_pixel();\n        }\n    } else {\n        zmk_rgb_underglow_effect_off();\n    }")

  tomahawk56_patch_underglow("off-state tick guard"
    "static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {\n    if (!state.on && !zmk_rgb_underglow_status_active()) {"
    "static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {\n    if (!state.on) {")

  # Static layer frames stop the 20 Hz timer; animated effects continue ticking.
  tomahawk56_patch_underglow("tick handler"
    "    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n}"
    "    int submit_result =\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n\n    /* Keep ticking if this frame was coalesced with work already in flight. */\n    if (state.current_effect == UNDERGLOW_EFFECT_SOLID && submit_result > 0) {\n        k_timer_stop(timer);\n    }\n}")

  tomahawk56_patch_underglow("off-state status power"
    "#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (ext_power != NULL) {\n        int rc = ext_power_enable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to enable EXT_POWER: %d\", rc);\n        }\n    }\n#endif\n\n    status_channels[channel].active = true;"
    "#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (state.on && ext_power != NULL) {\n        int rc = ext_power_enable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to enable EXT_POWER: %d\", rc);\n        }\n    }\n#endif\n\n    status_channels[channel].active = true;")

  tomahawk56_patch_underglow("off-state status timer"
    "    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));\n\n    return 0;\n}\n\nint zmk_rgb_underglow_clear_status_pixel"
    "    if (state.on) {\n        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));\n    }\n\n    return 0;\n}\n\nint zmk_rgb_underglow_clear_status_pixel")

  tomahawk56_patch_underglow("off-state power down"
    "    state.on = false;\n    if (!zmk_rgb_underglow_status_active()) {\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);\n        k_timer_stop(&underglow_tick);\n\n#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n        if (ext_power != NULL) {\n            int rc = ext_power_disable(ext_power);\n            if (rc != 0) {\n                LOG_ERR(\"Unable to disable EXT_POWER: %d\", rc);\n            }\n        }\n#endif\n    }"
    "    state.on = false;\n    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);\n    k_timer_stop(&underglow_tick);\n\n#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (ext_power != NULL) {\n        int rc = ext_power_disable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to disable EXT_POWER: %d\", rc);\n        }\n    }\n#endif")

  if(NOT ZMK_RGB_UNDERGLOW_CONTENTS STREQUAL ZMK_RGB_UNDERGLOW_ORIGINAL)
    file(WRITE "${ZMK_RGB_UNDERGLOW_SOURCE}" "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  endif()
endif()

# The pairing GPIO and its connectivity work must stop at idle on USB power.
if(CONFIG_TOMAHAWK56_LAYER_RGB AND CONFIG_RGBLED_WIDGET_PAIRING_LED AND
   CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
  get_filename_component(ZMK_WORKSPACE_DIR "${APPLICATION_SOURCE_DIR}/../.." ABSOLUTE)
  set(ZMK_RGBLED_WIDGET_SOURCE "${ZMK_WORKSPACE_DIR}/zmk-rgbled-widget/src/widget.c")
  if(NOT EXISTS "${ZMK_RGBLED_WIDGET_SOURCE}")
    message(FATAL_ERROR "Cannot locate the zmk-rgbled-widget source")
  endif()

  file(READ "${ZMK_RGBLED_WIDGET_SOURCE}" ZMK_RGBLED_WIDGET_CONTENTS)
  set(ZMK_RGBLED_WIDGET_ORIGINAL "${ZMK_RGBLED_WIDGET_CONTENTS}")

  tomahawk56_patch_vendor(ZMK_RGBLED_WIDGET_CONTENTS
    "git -C .build/west/zmk-rgbled-widget checkout src/widget.c" "RGB widget idle handler"
    "    case ZMK_ACTIVITY_IDLE:\n        // ZMK does not enter sleep while USB power is present. Start a timer for the portion of\n        // the sleep timeout that remains after the idle transition so pairing indication still\n        // stops at CONFIG_ZMK_IDLE_SLEEP_TIMEOUT while the keyboard is charging.\n        k_work_reschedule(&connectivity_inactivity_work,\n                          K_MSEC(MAX(0, CONFIG_ZMK_IDLE_SLEEP_TIMEOUT - CONFIG_ZMK_IDLE_TIMEOUT)));\n        break;\n    case ZMK_ACTIVITY_SLEEP:"
    "    case ZMK_ACTIVITY_IDLE:\n    case ZMK_ACTIVITY_SLEEP:")

  if(NOT ZMK_RGBLED_WIDGET_CONTENTS STREQUAL ZMK_RGBLED_WIDGET_ORIGINAL)
    file(WRITE "${ZMK_RGBLED_WIDGET_SOURCE}" "${ZMK_RGBLED_WIDGET_CONTENTS}")
  endif()
endif()

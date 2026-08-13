# The vendor underglow fork gives battery, layer, and connectivity their own
# pixel channels, but sized for eight status LEDs and animated at 20 Hz. The
# Tomahawk56 layer map needs all 28 key LEDs and is a static frame, so patch the
# vendor source in place before it is compiled.
set(ZMK_RGB_UNDERGLOW_SOURCE "${APPLICATION_SOURCE_DIR}/src/rgb_underglow.c")
if(NOT EXISTS "${ZMK_RGB_UNDERGLOW_SOURCE}")
  message(FATAL_ERROR "Cannot locate the ZMK RGB underglow source")
endif()

# Rewrite `from` to `to` in the vendor source held by the variable named
# `contents_var`, leaving an already-patched tree alone and failing loudly if
# the vendor code moved out from under the patch. `restore` is the command
# that discards a stale on-disk patch.
function(patch_vendor contents_var restore what from to)
  string(FIND "${${contents_var}}" "${to}" already_patched)
  if(NOT already_patched EQUAL -1)
    return()
  endif()

  string(FIND "${${contents_var}}" "${from}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "The vendor ${what} matches neither the pristine nor the patched form.\n"
      "If the tree carries an older patch, discard it with\n"
      "  ${restore}\n"
      "Otherwise upstream changed; review the Tomahawk56 integration.")
  endif()

  string(REPLACE "${from}" "${to}" patched "${${contents_var}}")
  set(${contents_var} "${patched}" PARENT_SCOPE)
endfunction()

# All the underglow patches below edit the same file; keep their call sites terse.
function(patch_underglow what from to)
  patch_vendor(ZMK_RGB_UNDERGLOW_CONTENTS
    "git -C .build/west/zmk checkout app/src/rgb_underglow.c" "RGB ${what}"
    "${from}" "${to}")
  set(ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}" PARENT_SCOPE)
endfunction()

if(CONFIG_TOMAHAWK56_LAYER_RGB)
  file(READ "${ZMK_RGB_UNDERGLOW_SOURCE}" ZMK_RGB_UNDERGLOW_CONTENTS)
  set(ZMK_RGB_UNDERGLOW_ORIGINAL "${ZMK_RGB_UNDERGLOW_CONTENTS}")

  patch_underglow("status-channel limit"
    "#define STATUS_PIXELS_MAX 8"
    "#define STATUS_PIXELS_MAX 28")

  # Draw the persistent layer map first so the battery and connectivity pixels
  # stay visible on top of it. Vendor channel ids: battery=0, layer=1, conn=2.
  patch_underglow("status renderer"
    "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    for (uint8_t channel = 0; channel < STATUS_CHANNELS_LEN; channel++) {"
    "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    static const uint8_t render_order[] = {1, 0, 2};\n\n    for (uint8_t order_index = 0; order_index < ARRAY_SIZE(render_order); order_index++) {\n        uint8_t channel = render_order[order_index];")

  # Make the underglow's ordinary on/off state authoritative. Status channels
  # retain their data while off, but cannot keep the strip powered or visible.
  patch_underglow("off-state renderer"
    "static void zmk_rgb_underglow_tick(struct k_work *work) {\n    if (state.on) {\n        zmk_rgb_underglow_render_effect();\n    } else {\n        zmk_rgb_underglow_effect_off();\n    }\n\n    if (zmk_rgb_underglow_status_active()) {\n        zmk_rgb_underglow_effect_status_pixel();\n    }"
    "static void zmk_rgb_underglow_tick(struct k_work *work) {\n    if (state.on) {\n        zmk_rgb_underglow_render_effect();\n        if (zmk_rgb_underglow_status_active()) {\n            zmk_rgb_underglow_effect_status_pixel();\n        }\n    } else {\n        zmk_rgb_underglow_effect_off();\n    }")

  patch_underglow("off-state tick guard"
    "static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {\n    if (!state.on && !zmk_rgb_underglow_status_active()) {"
    "static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {\n    if (!state.on) {")

  # A solid layer map does not need the 20 Hz animation timer: render the frame
  # that was asked for, then sleep until the next layer/status/RGB event.
  # Animated effects keep ticking.
  patch_underglow("tick handler"
    "    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n}"
    "    int submit_result =\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n\n    /* Keep ticking if this frame was coalesced with work already in flight. */\n    if (state.current_effect == UNDERGLOW_EFFECT_SOLID && submit_result > 0) {\n        k_timer_stop(timer);\n    }\n}")

  patch_underglow("off-state status power"
    "#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (ext_power != NULL) {\n        int rc = ext_power_enable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to enable EXT_POWER: %d\", rc);\n        }\n    }\n#endif\n\n    status_channels[channel].active = true;"
    "#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (state.on && ext_power != NULL) {\n        int rc = ext_power_enable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to enable EXT_POWER: %d\", rc);\n        }\n    }\n#endif\n\n    status_channels[channel].active = true;")

  patch_underglow("off-state status timer"
    "    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));\n\n    return 0;\n}\n\nint zmk_rgb_underglow_clear_status_pixel"
    "    if (state.on) {\n        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));\n    }\n\n    return 0;\n}\n\nint zmk_rgb_underglow_clear_status_pixel")

  patch_underglow("off-state power down"
    "    state.on = false;\n    if (!zmk_rgb_underglow_status_active()) {\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);\n        k_timer_stop(&underglow_tick);\n\n#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n        if (ext_power != NULL) {\n            int rc = ext_power_disable(ext_power);\n            if (rc != 0) {\n                LOG_ERR(\"Unable to disable EXT_POWER: %d\", rc);\n            }\n        }\n#endif\n    }"
    "    state.on = false;\n    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);\n    k_timer_stop(&underglow_tick);\n\n#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)\n    if (ext_power != NULL) {\n        int rc = ext_power_disable(ext_power);\n        if (rc != 0) {\n            LOG_ERR(\"Unable to disable EXT_POWER: %d\", rc);\n        }\n    }\n#endif")

  if(NOT ZMK_RGB_UNDERGLOW_CONTENTS STREQUAL ZMK_RGB_UNDERGLOW_ORIGINAL)
    file(WRITE "${ZMK_RGB_UNDERGLOW_SOURCE}" "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  endif()
endif()

# The pairing indicator is a separate GPIO LED, so stop it (and its associated
# connectivity work) at idle too. RGB pixels themselves are covered by the
# renderer gate above.
if(CONFIG_TOMAHAWK56_LAYER_RGB AND CONFIG_RGBLED_WIDGET_PAIRING_LED AND
   CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
  get_filename_component(ZMK_WORKSPACE_DIR "${APPLICATION_SOURCE_DIR}/../.." ABSOLUTE)
  set(ZMK_RGBLED_WIDGET_SOURCE "${ZMK_WORKSPACE_DIR}/zmk-rgbled-widget/src/widget.c")
  if(NOT EXISTS "${ZMK_RGBLED_WIDGET_SOURCE}")
    message(FATAL_ERROR "Cannot locate the zmk-rgbled-widget source")
  endif()

  file(READ "${ZMK_RGBLED_WIDGET_SOURCE}" ZMK_RGBLED_WIDGET_CONTENTS)
  set(ZMK_RGBLED_WIDGET_ORIGINAL "${ZMK_RGBLED_WIDGET_CONTENTS}")

  patch_vendor(ZMK_RGBLED_WIDGET_CONTENTS
    "git -C .build/west/zmk-rgbled-widget checkout src/widget.c" "RGB widget idle handler"
    "    case ZMK_ACTIVITY_IDLE:\n        // ZMK does not enter sleep while USB power is present. Start a timer for the portion of\n        // the sleep timeout that remains after the idle transition so pairing indication still\n        // stops at CONFIG_ZMK_IDLE_SLEEP_TIMEOUT while the keyboard is charging.\n        k_work_reschedule(&connectivity_inactivity_work,\n                          K_MSEC(MAX(0, CONFIG_ZMK_IDLE_SLEEP_TIMEOUT - CONFIG_ZMK_IDLE_TIMEOUT)));\n        break;\n    case ZMK_ACTIVITY_SLEEP:"
    "    case ZMK_ACTIVITY_IDLE:\n    case ZMK_ACTIVITY_SLEEP:")

  if(NOT ZMK_RGBLED_WIDGET_CONTENTS STREQUAL ZMK_RGBLED_WIDGET_ORIGINAL)
    file(WRITE "${ZMK_RGBLED_WIDGET_SOURCE}" "${ZMK_RGBLED_WIDGET_CONTENTS}")
  endif()
endif()

target_sources_ifdef(CONFIG_TOMAHAWK56_LAYER_RGB app PRIVATE
  src/tomahawk56_layer_rgb.c)

# Re-stamp the build id on every build, not just on reconfigure, so the image
# the board boots after a flash never matches the one it ran before. Costs one
# recompile plus a relink per build; see cmake/tomahawk56_build_id.cmake.
if(CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH)
  set(TOMAHAWK56_BUILD_ID_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/generated")
  set(TOMAHAWK56_BUILD_ID_HEADER "${TOMAHAWK56_BUILD_ID_DIR}/tomahawk56_build_id.h")

  add_custom_target(tomahawk56_build_id ALL
    BYPRODUCTS "${TOMAHAWK56_BUILD_ID_HEADER}"
    COMMAND ${CMAKE_COMMAND}
            "-DTOMAHAWK56_BUILD_ID_HEADER=${TOMAHAWK56_BUILD_ID_HEADER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tomahawk56_build_id.cmake"
    COMMENT "Stamping the Tomahawk56 build id")

  add_dependencies(app tomahawk56_build_id)
  target_include_directories(app PRIVATE "${TOMAHAWK56_BUILD_ID_DIR}")
  target_sources(app PRIVATE src/tomahawk56_studio_reset.c)
endif()

# The vendor underglow fork provides independent battery, layer, and
# connectivity pixel channels, but originally sized each channel for only
# eight status LEDs. The Tomahawk56 layer channel needs all 28 key LEDs.
set(ZMK_RGB_UNDERGLOW_SOURCE "${APPLICATION_SOURCE_DIR}/src/rgb_underglow.c")
if(NOT EXISTS "${ZMK_RGB_UNDERGLOW_SOURCE}")
  message(FATAL_ERROR "Cannot locate the ZMK RGB underglow source")
endif()

if(CONFIG_TOMAHAWK56_LAYER_RGB AND EXISTS "${ZMK_RGB_UNDERGLOW_SOURCE}")
  file(READ "${ZMK_RGB_UNDERGLOW_SOURCE}" ZMK_RGB_UNDERGLOW_CONTENTS)
  set(ZMK_RGB_UNDERGLOW_ORIGINAL "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "#define STATUS_PIXELS_MAX 8"
         TOMAHAWK_STATUS_LIMIT_8)
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "#define STATUS_PIXELS_MAX 28"
         TOMAHAWK_STATUS_LIMIT_28)

  if(NOT TOMAHAWK_STATUS_LIMIT_8 EQUAL -1)
    string(REPLACE "#define STATUS_PIXELS_MAX 8"
                   "#define STATUS_PIXELS_MAX 28"
                   ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  elseif(TOMAHAWK_STATUS_LIMIT_28 EQUAL -1)
    message(FATAL_ERROR
      "The vendor RGB status-channel limit changed; review the Tomahawk56 layer RGB integration")
  endif()

  # Draw the persistent layer map first so battery and connectivity status
  # pixels remain visible on top of it. The vendor channel IDs are battery=0,
  # layer=1, connectivity=2.
  set(RGB_STATUS_RENDER_LOOP
      "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    for (uint8_t channel = 0; channel < STATUS_CHANNELS_LEN; channel++) {")
  set(TOMAHAWK_STATUS_RENDER_LOOP
      "static void zmk_rgb_underglow_effect_status_pixel(void) {\n    static const uint8_t render_order[] = {1, 0, 2};\n\n    for (uint8_t order_index = 0; order_index < ARRAY_SIZE(render_order); order_index++) {\n        uint8_t channel = render_order[order_index];")
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${RGB_STATUS_RENDER_LOOP}"
         TOMAHAWK_STATUS_RENDER_DEFAULT)
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${TOMAHAWK_STATUS_RENDER_LOOP}"
         TOMAHAWK_STATUS_RENDER_PATCHED)

  if(NOT TOMAHAWK_STATUS_RENDER_DEFAULT EQUAL -1)
    string(REPLACE "${RGB_STATUS_RENDER_LOOP}" "${TOMAHAWK_STATUS_RENDER_LOOP}"
                   ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  elseif(TOMAHAWK_STATUS_RENDER_PATCHED EQUAL -1)
    message(FATAL_ERROR
      "The vendor RGB status renderer changed; review the Tomahawk56 channel ordering")
  endif()

  # A solid layer map does not need ZMK's 20 Hz animation timer. Render one
  # requested frame, then sleep until a layer/status/RGB event starts it again.
  # Animated effects keep the normal periodic timer.
  set(RGB_TICK_SUBMIT
      "    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n}")
  set(TOMAHAWK_RGB_TICK_SUBMIT_OLD
      "    if (state.current_effect == UNDERGLOW_EFFECT_SOLID) {\n        k_timer_stop(timer);\n    }\n\n    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n}")
  set(TOMAHAWK_RGB_TICK_SUBMIT
      "    int submit_result =\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n\n    /* Keep ticking if this frame was coalesced with work already in flight. */\n    if (state.current_effect == UNDERGLOW_EFFECT_SOLID && submit_result > 0) {\n        k_timer_stop(timer);\n    }\n}")
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${RGB_TICK_SUBMIT}"
         TOMAHAWK_RGB_TICK_DEFAULT)
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${TOMAHAWK_RGB_TICK_SUBMIT}"
         TOMAHAWK_RGB_TICK_PATCHED)
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${TOMAHAWK_RGB_TICK_SUBMIT_OLD}"
         TOMAHAWK_RGB_TICK_OLD_PATCHED)

  if(NOT TOMAHAWK_RGB_TICK_PATCHED EQUAL -1)
    # Already patched with reliable one-shot submission.
  elseif(NOT TOMAHAWK_RGB_TICK_OLD_PATCHED EQUAL -1)
    string(REPLACE "${TOMAHAWK_RGB_TICK_SUBMIT_OLD}" "${TOMAHAWK_RGB_TICK_SUBMIT}"
                   ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  elseif(NOT TOMAHAWK_RGB_TICK_DEFAULT EQUAL -1)
    string(REPLACE "${RGB_TICK_SUBMIT}" "${TOMAHAWK_RGB_TICK_SUBMIT}"
                   ZMK_RGB_UNDERGLOW_CONTENTS "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  else()
    message(FATAL_ERROR
      "The vendor RGB tick handler changed; review the Tomahawk56 static-frame optimization")
  endif()

  if(NOT ZMK_RGB_UNDERGLOW_CONTENTS STREQUAL ZMK_RGB_UNDERGLOW_ORIGINAL)
    file(WRITE "${ZMK_RGB_UNDERGLOW_SOURCE}" "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  endif()
endif()

target_sources_ifdef(CONFIG_TOMAHAWK56_LAYER_RGB app PRIVATE
  src/tomahawk56_layer_rgb.c)

# Re-stamp the build id on every build (not just on reconfigure) so the image
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

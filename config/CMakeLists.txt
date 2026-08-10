# The vendor underglow fork gives battery, layer, and connectivity their own
# pixel channels, but sized for eight status LEDs and animated at 20 Hz. The
# Tomahawk56 layer map needs all 28 key LEDs and is a static frame, so patch the
# vendor source in place before it is compiled.
set(ZMK_RGB_UNDERGLOW_SOURCE "${APPLICATION_SOURCE_DIR}/src/rgb_underglow.c")
if(NOT EXISTS "${ZMK_RGB_UNDERGLOW_SOURCE}")
  message(FATAL_ERROR "Cannot locate the ZMK RGB underglow source")
endif()

# Rewrite `from` to `to`, leaving an already-patched tree alone and failing
# loudly if the vendor code moved out from under the patch.
function(patch_underglow what from to)
  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${to}" already_patched)
  if(NOT already_patched EQUAL -1)
    return()
  endif()

  string(FIND "${ZMK_RGB_UNDERGLOW_CONTENTS}" "${from}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "The vendor RGB ${what} matches neither the pristine nor the patched form.\n"
      "If the tree carries an older patch, discard it with\n"
      "  git -C zmk checkout app/src/rgb_underglow.c\n"
      "Otherwise upstream changed; review the Tomahawk56 layer RGB integration.")
  endif()

  string(REPLACE "${from}" "${to}" patched "${ZMK_RGB_UNDERGLOW_CONTENTS}")
  set(ZMK_RGB_UNDERGLOW_CONTENTS "${patched}" PARENT_SCOPE)
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

  # A solid layer map does not need the 20 Hz animation timer: render the frame
  # that was asked for, then sleep until the next layer/status/RGB event.
  # Animated effects keep ticking.
  patch_underglow("tick handler"
    "    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n}"
    "    int submit_result =\n        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);\n\n    /* Keep ticking if this frame was coalesced with work already in flight. */\n    if (state.current_effect == UNDERGLOW_EFFECT_SOLID && submit_result > 0) {\n        k_timer_stop(timer);\n    }\n}")

  if(NOT ZMK_RGB_UNDERGLOW_CONTENTS STREQUAL ZMK_RGB_UNDERGLOW_ORIGINAL)
    file(WRITE "${ZMK_RGB_UNDERGLOW_SOURCE}" "${ZMK_RGB_UNDERGLOW_CONTENTS}")
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

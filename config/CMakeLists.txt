include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/tomahawk56_vendor_patches.cmake")

target_sources_ifdef(CONFIG_TOMAHAWK56_LAYER_RGB app PRIVATE
  src/tomahawk56_layer_rgb.c
  src/tomahawk56_layer_rgb_renderer.c)

# Re-stamp every build so a flashed image differs from the last image booted.
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

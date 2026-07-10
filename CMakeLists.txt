cmake_minimum_required(VERSION 3.24)

# Ninja and Makefiles otherwise default to an unoptimized build. Keep symbols
# for useful crash reports while making the normal desktop build representative
# of real gameplay performance.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build type (Debug, Release, RelWithDebInfo, or MinSizeRel)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

# Without an explicit target, Xcode uses the installed SDK version as the
# minimum OS, making device builds uninstallable on slightly older iPhones.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
   NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
    "Minimum supported iOS version")
endif()

project(moppe LANGUAGES CXX)

include(CTest)
find_package(ZLIB REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall)

set(MOPPE_VERSION "1.0.0" CACHE STRING "Moppe release version")
set(MOPPE_BUILD_NUMBER "1" CACHE STRING "Moppe release build number")

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

# ---------------------------------------------------------------
# Metal shader library: one metallib per SDK (macosx / iphoneos /
# iphonesimulator) -- AIR is platform-targeted and a mac library
# fails to load on iOS and vice versa.

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(MOPPE_METAL_SDK iphonesimulator)
  else()
    set(MOPPE_METAL_SDK iphoneos)
  endif()
else()
  set(MOPPE_METAL_SDK macosx)
endif()

set(MOPPE_METAL_SOURCES
  moppe/shaders/metal/uber.metal
  moppe/shaders/metal/post.metal
  moppe/shaders/metal/terrain.metal
  moppe/shaders/metal/sky.metal
  moppe/shaders/metal/ocean.metal
)

set(MOPPE_METAL_DEPS
  moppe/shaders/metal/common.h
  moppe/render/metal/shader_types.h
)

execute_process(
  COMMAND xcrun -sdk ${MOPPE_METAL_SDK} -find metal
  RESULT_VARIABLE MOPPE_METAL_RESULT
  OUTPUT_QUIET ERROR_QUIET)

if(MOPPE_METAL_RESULT EQUAL 0)
  set(MOPPE_AIR_FILES)
  foreach(src ${MOPPE_METAL_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(air ${CMAKE_CURRENT_BINARY_DIR}/${name}.air)
    add_custom_command(
      OUTPUT ${air}
      COMMAND xcrun -sdk ${MOPPE_METAL_SDK} metal
              -c ${CMAKE_CURRENT_SOURCE_DIR}/${src}
              -o ${air}
      DEPENDS ${src} ${MOPPE_METAL_DEPS}
      COMMENT "Compiling ${src} (${MOPPE_METAL_SDK})"
      VERBATIM)
    list(APPEND MOPPE_AIR_FILES ${air})
  endforeach()

  set(MOPPE_SHADER_ASSET ${CMAKE_CURRENT_BINARY_DIR}/moppe.metallib)
  set(MOPPE_SHADER_NAME moppe.metallib)
  add_custom_command(
    OUTPUT ${MOPPE_SHADER_ASSET}
    COMMAND xcrun -sdk ${MOPPE_METAL_SDK} metallib
            ${MOPPE_AIR_FILES} -o ${MOPPE_SHADER_ASSET}
    DEPENDS ${MOPPE_AIR_FILES}
    COMMENT "Linking moppe.metallib (${MOPPE_METAL_SDK})"
    VERBATIM)
else()
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "The iOS build requires Xcode's Metal toolchain")
  endif()

  # Command Line Tools includes the Metal runtime but not the offline shader
  # compiler.  Build one self-contained source file for runtime compilation.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${MOPPE_METAL_SOURCES} ${MOPPE_METAL_DEPS})
  file(READ moppe/render/metal/shader_types.h MOPPE_METAL_SOURCE)
  file(READ moppe/shaders/metal/common.h MOPPE_METAL_COMMON)
  string(REPLACE
    "#include \"../../render/metal/shader_types.h\"" ""
    MOPPE_METAL_COMMON "${MOPPE_METAL_COMMON}")
  string(APPEND MOPPE_METAL_SOURCE "\n${MOPPE_METAL_COMMON}\n")
  foreach(src ${MOPPE_METAL_SOURCES})
    file(READ ${src} MOPPE_METAL_PART)
    string(REPLACE "#include \"common.h\"" ""
      MOPPE_METAL_PART "${MOPPE_METAL_PART}")
    string(APPEND MOPPE_METAL_SOURCE "\n${MOPPE_METAL_PART}\n")
  endforeach()
  set(MOPPE_SHADER_ASSET ${CMAKE_CURRENT_BINARY_DIR}/moppe.metal)
  set(MOPPE_SHADER_NAME moppe.metal)
  file(WRITE ${MOPPE_SHADER_ASSET} "${MOPPE_METAL_SOURCE}")
  message(STATUS
    "Metal compiler not found; shaders will compile at runtime on macOS")
endif()

add_custom_target(moppe_shaders DEPENDS ${MOPPE_SHADER_ASSET})

# ---------------------------------------------------------------
# Portable core: rendering abstraction + game-independent code.

add_library(moppe_render STATIC
  moppe/render/draw.cc
  moppe/render/text.cc
)
target_include_directories(moppe_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Runtime scalar-field expressions plus evaluator backends.  This stays
# independent of game and platform code so tools and tests use the same DAG.
add_library(moppe_terrain STATIC
  moppe/terrain/field.cc
  moppe/terrain/cpu_evaluator.cc
  moppe/terrain/geological.cc
  moppe/terrain/pipeline.cc
  moppe/terrain/image.cc
)
target_include_directories(moppe_terrain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_terrain PUBLIC ZLIB::ZLIB)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(terrain-field-demo moppe/terrain/field_demo.cc)
  target_link_libraries(terrain-field-demo PRIVATE moppe_terrain)
endif()

if(BUILD_TESTING AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(moppe-unit-tests
    tests/test_main.cc
    tests/terrain/field_test.cc
    tests/terrain/cpu_evaluator_test.cc
    tests/terrain/geological_test.cc
    tests/terrain/pipeline_test.cc
    tests/terrain/image_test.cc
  )
  target_link_libraries(moppe-unit-tests PRIVATE moppe_terrain)
  add_test(NAME moppe-unit-tests COMMAND moppe-unit-tests)
endif()

# Game logic + simulation (GL-free since the cutover).
add_library(moppe_game STATIC
  moppe/map/generate.cc
  moppe/mov/vehicle.cc
  moppe/gfx/tga.cc
  moppe/game/terrain.cc
  moppe/game/chase_camera.cc
  moppe/game/vegetation.cc
  moppe/game/stars.cc
  moppe/game/dust.cc
  moppe/game/blob_shadow.cc
  moppe/game/sprites.cc
  moppe/game/fish.cc
  moppe/game/wildlife.cc
  moppe/game/city.cc
  moppe/game/model.cc
  moppe/game/walker.cc
  moppe/game/vehicle_render.cc
  moppe/game/hud.cc
  moppe/game/inspector_ui.cc
  moppe/game/terrain_lab.cc
)
target_include_directories(moppe_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_game PUBLIC moppe_render)
target_link_libraries(moppe_game PUBLIC moppe_terrain)

if(BUILD_TESTING AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(moppe-map-tests
    tests/test_main.cc
    tests/map/pipeline_test.cc
  )
  target_link_libraries(moppe-map-tests PRIVATE moppe_game)
  add_test(NAME moppe-map-tests COMMAND moppe-map-tests)
endif()

# The heightmap CLI tool (generates a terrain and writes test.tga).
add_executable(map-test moppe/map/test.cc)
target_link_libraries(map-test PRIVATE moppe_game)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(terrain-pipeline-demo
    moppe/terrain/pipeline_demo.cc
  )
  target_link_libraries(terrain-pipeline-demo PRIVATE moppe_game)
endif()

# ---------------------------------------------------------------
# Apple: Metal backend + platform layers.

if(APPLE)
  add_library(moppe_metal STATIC
    moppe/render/metal/metal_renderer.mm
  )
  target_include_directories(moppe_metal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_metal PRIVATE -fobjc-arc)
  target_link_libraries(moppe_metal PUBLIC moppe_render
    "-framework Metal" "-framework MetalKit" "-framework QuartzCore")
  add_dependencies(moppe_metal moppe_shaders)

  add_library(moppe_apple STATIC
    moppe/platform/apple/apple_common.mm
  )
  target_include_directories(moppe_apple PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_apple PRIVATE -fobjc-arc)
  target_link_libraries(moppe_apple PUBLIC
    "-framework Foundation" "-framework CoreText"
    "-framework CoreGraphics" "-framework AVFoundation")
endif()

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(MOPPE_IOS_ASSETS
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/ios/Assets.xcassets)
  set_source_files_properties(${MOPPE_IOS_ASSETS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

  add_library(moppe_platform_ios STATIC
    moppe/platform/ios/main_ios.mm
  )
  target_include_directories(moppe_platform_ios
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_platform_ios PRIVATE -fobjc-arc)
  target_link_libraries(moppe_platform_ios PUBLIC
    moppe_metal moppe_apple "-framework UIKit")

  add_executable(moppe-ios MACOSX_BUNDLE
    moppe/game/game.cc
    ${MOPPE_IOS_ASSETS}
  )
  target_link_libraries(moppe-ios PRIVATE moppe_game moppe_platform_ios)
  set_target_properties(moppe-ios PROPERTIES
    OUTPUT_NAME "Moppe"
    XCODE_GENERATE_SCHEME TRUE
    MACOSX_BUNDLE_INFO_PLIST
      ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/ios/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Moppe"
    MACOSX_BUNDLE_GUI_IDENTIFIER "se.brockman.moppe"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MOPPE_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MOPPE_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${MOPPE_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_MARKETING_VERSION "${MOPPE_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "se.brockman.moppe"
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
  add_custom_command(TARGET moppe-ios POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MOPPE_SHADER_ASSET}
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/${MOPPE_SHADER_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/textures
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/data
    COMMENT "Bundling assets into Moppe.app (iOS)")
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_library(moppe_platform_mac STATIC
    moppe/platform/mac/main_mac.mm
  )
  target_include_directories(moppe_platform_mac
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_platform_mac PRIVATE -fobjc-arc)
  target_compile_definitions(moppe_platform_mac PRIVATE
    MOPPE_SHADER_NAME="${MOPPE_SHADER_NAME}")
  target_link_libraries(moppe_platform_mac PUBLIC
    moppe_metal moppe_apple "-framework AppKit")

  # Renderer testbed: spinning solids + HUD shapes, no game.
  add_executable(moppe-testbed moppe/game/testbed.cc)
  target_link_libraries(moppe-testbed PRIVATE moppe_platform_mac)

  # The game, as a proper .app bundle with the assets inside.
  # (Still runnable from a shell: build/moppe.app/Contents/MacOS/moppe,
  # and MOPPE_ASSETS=<repo> overrides asset resolution for dev.)
  add_executable(moppe MACOSX_BUNDLE moppe/game/game.cc)
  target_link_libraries(moppe PRIVATE moppe_game moppe_platform_mac)
  set_target_properties(moppe PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/mac/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Moppe"
    MACOSX_BUNDLE_GUI_IDENTIFIER "se.brockman.moppe"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1")
  add_custom_command(TARGET moppe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MOPPE_SHADER_ASSET}
      $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/${MOPPE_SHADER_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/textures
      $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/data
    COMMENT "Bundling assets into moppe.app")
endif()

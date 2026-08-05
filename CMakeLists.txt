cmake_minimum_required(VERSION 3.24)

set(MOPPE_IOS FALSE)
set(MOPPE_TVOS FALSE)
set(MOPPE_MOBILE_APPLE FALSE)
set(MOPPE_APPLE_SIMULATOR FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(MOPPE_IOS TRUE)
  set(MOPPE_MOBILE_APPLE TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  set(MOPPE_TVOS TRUE)
  set(MOPPE_MOBILE_APPLE TRUE)
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
  set(MOPPE_APPLE_SIMULATOR TRUE)
endif()

# The normal build is an edit build: do no optimization, and keep only the line
# tables needed to turn a crash address into a source location. Optimized
# gameplay, profiling, and distribution builds select RelWithDebInfo or Release
# explicitly.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING
    "Build type (Debug, Release, RelWithDebInfo, or MinSizeRel)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

# Moppe's Apple renderer is Metal 4-only.  Metal 4 arrived with the 26.0
# platform generation, so every Apple target names that contract explicitly
# instead of inheriting an SDK-dependent desktop default or carrying an older
# Metal compatibility path.  FORCE also upgrades existing build directories.
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING
    "Minimum Apple platform version (required by Metal 4)" FORCE)
endif()

project(moppe LANGUAGES C CXX)

# Keep editor tooling in sync with the normal Ninja and Makefile builds.
# Multi-config generators such as Xcode do not support this output.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
  add_compile_options($<$<CONFIG:Debug>:-gline-tables-only>)
endif()

# A Moppe web build keeps world generation off the browser UI thread. Every
# object in a shared-memory WebAssembly executable must be compiled with the
# atomics/bulk-memory feature set, not only the platform target which creates
# the worker.
if(EMSCRIPTEN)
  add_compile_options(-pthread)
  add_link_options(-pthread)
endif()

set(MOPPE_VERSION "1.0.0" CACHE STRING "Moppe release version")
set(MOPPE_BUILD_NUMBER "1" CACHE STRING "Moppe release build number")
option(MOPPE_BUILD_DEVELOPER_TOOLS
  "Build demos, experiments, testbeds, and command-line tools" OFF)
option(MOPPE_UNITY_BUILD
  "Compile each target as a single unity translation unit" ON)
option(MOPPE_ENABLE_TRACY "Enable Tracy runtime profiling" OFF)
set(MOPPE_TVOS_BAKED_WORLD "" CACHE PATH
  "Host-baked default finished-world cache to bundle into the tvOS app")

# Moppe's implementation files are deliberately small, but compiling every one
# separately makes heavily-templated headers (notably mp-units) dominate build
# time and memory.  A very large batch size gives each target one translation
# unit per source language.  CMake still splits C++ from Objective-C++, which is
# required by AppleClang, but a normal game build no longer launches dozens of
# redundant C++ compiler processes.
if(MOPPE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 1000)
  # A plain `cmake --build build` should remain usable while a large unity
  # source is instantiating templates.  Ninja otherwise starts one compiler
  # for every ready target and can consume the whole machine.
  set_property(GLOBAL PROPERTY JOB_POOLS moppe_compile_pool=2)
  set(CMAKE_JOB_POOL_COMPILE moppe_compile_pool)
endif()

# Keep the units vocabulary available to every target without forcing a
# large-scale conversion of existing float-based APIs.  Consumers opt in with
# target_link_libraries(<target> PRIVATE mp-units::mp-units).
set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL
  "Do not install the vendored mp-units target" FORCE)
set(MP_UNITS_API_CONTRACTS NONE CACHE STRING
  "Avoid an additional contracts dependency" FORCE)
FetchContent_Declare(mp-units
  # Pinned to master (post-v2.5.0, pre-v2.6.0) for cartesian_vector/tensor
  # and the reworked representation-concepts machinery.
  URL https://github.com/mpusz/mp-units/archive/bb86582ec2561419bc461a5ce7df01328ba08ea1.tar.gz
  URL_HASH
    SHA256=eb78dbc19c65d5954d31b78149936ca46f2eafb5e5f9c2b1e58f583fcf6705ea
  SOURCE_SUBDIR src
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(mp-units)

# nanoarrow is vendored as its upstream amalgamation so bundle storage is
# available offline and uses the same implementation on every platform.
add_library(nanoarrow STATIC
  third_party/nanoarrow/nanoarrow.c
  third_party/nanoarrow/nanoarrow_ipc.c
  third_party/nanoarrow/flatcc.c)
add_library(nanoarrow::nanoarrow ALIAS nanoarrow)
target_include_directories(nanoarrow SYSTEM PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanoarrow)
set_target_properties(nanoarrow PROPERTIES UNITY_BUILD OFF)

if(MOPPE_ENABLE_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiling" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "Record only with a connected viewer"
    FORCE)
  set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "Accept only local Tracy clients"
    FORCE)
  set(TRACY_NO_BROADCAST ON CACHE BOOL "Disable Tracy network discovery"
    FORCE)
  FetchContent_Declare(tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.13.1.tar.gz
    URL_HASH
      SHA256=d4efc50ebcb0bfcfdbba148995aeb75044c0d80f5d91223aebfaa8fa9e563d2b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(tracy)
endif()

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

# ---------------------------------------------------------------
# Atelier: a deliberately tiny, standalone Metal graphics workshop.

if(APPLE AND NOT MOPPE_MOBILE_APPLE)
  FetchContent_Declare(metal-cpp
    GIT_REPOSITORY https://github.com/apple/metal-cpp.git
    GIT_TAG 27c4382b7151d55a51692cdcb27aaa98752240de)
  FetchContent_MakeAvailable(metal-cpp)

  add_executable(atelier MACOSX_BUNDLE EXCLUDE_FROM_ALL
    atelier/atelier.cc
    atelier/atelier.hh
    atelier/atelier_mac.mm
    atelier/atelier_renderer.cc
    atelier/atelier_renderer.hh
    atelier/atelier_shaders.hh
    atelier/camera.hh
    atelier/carpet.hh
    atelier/embedding.cc
    atelier/embedding.hh
    atelier/hex_sheet.cc
    atelier/hex_sheet.hh
    atelier/matrix.hh
    atelier/metal.hh
    atelier/pipeline.hh
    atelier/prism.hh
    atelier/space.hh
    atelier/surface.hh
    atelier/tabulate.hh
    atelier/tree.cc
    atelier/tree.hh
    atelier/tree_embedding.cc
    atelier/tree_embedding.hh
    atelier/wire.hh
  )
  set_target_properties(atelier PROPERTIES UNITY_BUILD OFF)
  target_include_directories(atelier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${metal-cpp_SOURCE_DIR})
  target_compile_options(atelier PRIVATE
    -mmacosx-version-min=26.0
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_options(atelier PRIVATE -mmacosx-version-min=26.0)
  target_link_libraries(atelier PRIVATE
    mp-units::mp-units
    "-framework Cocoa"
    "-framework ImageIO"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers")

  # Lavoir: the wash-house by the water. A fresh workshop with Arrow
  # storage at the bottom and Metal at the top.
  add_executable(lavoir MACOSX_BUNDLE EXCLUDE_FROM_ALL
    lavoir/buffer.hh
    lavoir/bundle.hh
    lavoir/lavoir_mac.mm
    lavoir/lending.hh
    lavoir/metal.hh
    lavoir/pipeline.hh
    lavoir/renderer.cc
    lavoir/renderer.hh
    lavoir/shaders.hh
    lavoir/surface.hh
    lavoir/units.hh
    lavoir/wave.hh
  )
  set_target_properties(lavoir PROPERTIES UNITY_BUILD OFF)
  target_include_directories(lavoir PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${metal-cpp_SOURCE_DIR})
  target_compile_options(lavoir PRIVATE
    -mmacosx-version-min=26.0
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_options(lavoir PRIVATE -mmacosx-version-min=26.0)
  target_link_libraries(lavoir PRIVATE
    mp-units::mp-units
    nanoarrow
    "-framework Cocoa"
    "-framework ImageIO"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers")
endif()

# ---------------------------------------------------------------
# Metal shader library: one metallib per Apple SDK. AIR is
# platform-targeted, so each app must load a library built for its OS.

if(APPLE)
if(MOPPE_TVOS)
  if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(MOPPE_METAL_SDK appletvsimulator)
  else()
    set(MOPPE_METAL_SDK appletvos)
  endif()
elseif(MOPPE_IOS)
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
  moppe/shaders/metal/river.metal
  moppe/shaders/metal/dust.metal
  moppe/shaders/metal/undergrowth.metal
  moppe/shaders/metal/forest.metal
  moppe/shaders/metal/reflection_geometry.metal
  moppe/shaders/metal/water_reflection.metal
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
              -std=metal4.0
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
  if(MOPPE_MOBILE_APPLE)
    message(FATAL_ERROR
      "The iOS/tvOS build requires Xcode's Metal toolchain")
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
endif()

# ---------------------------------------------------------------
# Source composition.
#
# Directories and namespaces describe ownership. They are deliberately not
# mirrored by static libraries: every terminal program compiles the source it
# needs as one unity translation unit per language. This avoids repeatedly
# parsing the mp-units-heavy header graph at artificial library boundaries.

set(MOPPE_RENDER_SOURCES
  moppe/render/draw.cc
  moppe/render/reflection_geometry.cc
  moppe/render/text.cc
)

set(MOPPE_TERRAIN_SOURCES
  moppe/terrain/noise.cc
  moppe/terrain/readings.cc
  moppe/terrain/drainage.cc
  moppe/terrain/fractional_drainage.cc
  moppe/terrain/river.cc
  moppe/terrain/flood.cc
  moppe/terrain/merge_tree.cc
  moppe/terrain/waterline.cc
  moppe/terrain/stream_power_evolution.cc
  moppe/terrain/trail.cc
  moppe/terrain/moisture.cc
  moppe/terrain/watercourse.cc
  moppe/terrain/geological.cc
  moppe/terrain/world_recipe.cc
)

set(MOPPE_WORLD_SOURCES
  moppe/map/surface.cc
  moppe/game/generated_world.cc
  moppe/game/world_cache.cc
  moppe/game/forest_plan.cc
  moppe/game/water_capture.cc
  moppe/game/landscape_gazetteer.cc
)

set(MOPPE_SIMULATION_SOURCES
  moppe/mov/glider.cc
  moppe/mov/vehicle.cc
  moppe/game/chase_camera.cc
  moppe/game/game_session.cc
  moppe/game/stars.cc
  moppe/game/dust.cc
  moppe/game/walker.cc
)

set(MOPPE_SCENE_SOURCES
  moppe/game/graphics_settings.cc
  moppe/game/launch_options.cc
  moppe/game/frame_view.cc
  moppe/gfx/tga.cc
  moppe/game/surface_presentation.cc
  moppe/game/water_presentation.cc
  moppe/game/waterfall_surface.cc
  moppe/game/cinematic_flight.cc
  moppe/game/walker_render.cc
  moppe/game/blob_shadow.cc
  moppe/game/sprites.cc
  moppe/game/model.cc
  moppe/game/glider_render.cc
  moppe/game/vehicle_render.cc
  moppe/game/hud.cc
  moppe/game/forest.cc
)

set(MOPPE_ENGINE_SOURCES
  atelier/tree.cc
  ${MOPPE_RENDER_SOURCES}
  ${MOPPE_TERRAIN_SOURCES}
  ${MOPPE_WORLD_SOURCES}
  ${MOPPE_SIMULATION_SOURCES}
  ${MOPPE_SCENE_SOURCES}
)

set(MOPPE_APPLICATION_SOURCES
  moppe/game/game.cc
  moppe/game/seed_memory.cc
  moppe/game/terrain.cc
  moppe/game/world_loading.cc
)

set(MOPPE_APPLE_COMMON_SOURCES
  moppe/platform/apple/apple_common.mm
  moppe/platform/apple/game_controller.mm
)

if(MOPPE_APPLE_SIMULATOR)
  # Apple's simulator SDK deliberately omits the Metal 4 core Objective-C
  # API. Keep simulator projects buildable, but fail explicitly at launch
  # instead of compiling a second, legacy command-submission backend.
  set(MOPPE_METAL_RENDER_SOURCES
    moppe/render/metal/metal4_simulator_unavailable.mm)
else()
  set(MOPPE_METAL_RENDER_SOURCES
    moppe/render/metal/metal_renderer.mm)
endif()

set(MOPPE_WEBGPU_SOURCES
  moppe/render/webgpu/webgpu_renderer.cc
  moppe/render/webgpu/webgpu_renderer.hh
)

function(moppe_configure_code_target target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE
    mp-units::mp-units
    nanoarrow::nanoarrow)
  if(MOPPE_ENABLE_TRACY)
    target_link_libraries(${target} PRIVATE Tracy::TracyClient)
  endif()
endfunction()

function(moppe_configure_apple_target target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  set_target_properties(${target} PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES")
  target_link_libraries(${target} PRIVATE
    "-framework Foundation"
    "-framework CoreText"
    "-framework CoreGraphics"
    "-framework AVFoundation"
    "-framework GameController")
endfunction()

function(moppe_configure_metal_target target)
  target_link_libraries(${target} PRIVATE
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework ImageIO")
  # Apple simulator SDKs omit MetalFX along with the Metal 4 core API; those
  # targets compile the explicit unavailable renderer stub above. Physical
  # macOS, iOS, and tvOS targets all use the shared MetalFX reconstruction
  # path when the device reports support.
  if(NOT MOPPE_APPLE_SIMULATOR)
    target_link_libraries(${target} PRIVATE "-framework MetalFX")
  endif()
  add_dependencies(${target} moppe_shaders)
endfunction()

# Developer tools for inspecting and iterating on terrain generation.
if(MOPPE_BUILD_DEVELOPER_TOOLS AND NOT MOPPE_MOBILE_APPLE)
  add_executable(terrain-orogeny-benchmark EXCLUDE_FROM_ALL
    moppe/terrain/orogeny_benchmark.cc
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WORLD_SOURCES}
  )
  moppe_configure_code_target(terrain-orogeny-benchmark)

  add_executable(terrain-water-depth-experiment EXCLUDE_FROM_ALL
    moppe/terrain/water_depth_experiment.cc
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WORLD_SOURCES}
  )
  moppe_configure_code_target(terrain-water-depth-experiment)

  add_executable(terrain-trail-soak EXCLUDE_FROM_ALL
    moppe/terrain/trail_soak.cc
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WORLD_SOURCES}
  )
  moppe_configure_code_target(terrain-trail-soak)

  add_executable(terrain-cache-bake EXCLUDE_FROM_ALL
    moppe/terrain/cache_bake.cc
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WORLD_SOURCES}
  )
  moppe_configure_code_target(terrain-cache-bake)
endif()

# Tests are configured for IDEs and CTest, but excluded from the default build.
# Asking for moppe-tests compiles the engine and suite together as one unity
# translation unit per language.
if(BUILD_TESTING AND NOT MOPPE_MOBILE_APPLE)
  add_executable(moppe-tests EXCLUDE_FROM_ALL
    ${MOPPE_ENGINE_SOURCES}
    moppe/game/terrain.cc
    tests/test_main.cc
    tests/mat4_test.cc
    tests/quaternion_test.cc
    tests/units_test.cc
    tests/pacioli_test.cc
    tests/lavoir/buffer_test.cc
    tests/lavoir/wave_test.cc
    tests/spatial/bundle_test.cc
    tests/spatial/bundle_storage_test.cc
    tests/terrain/domain_test.cc
    tests/terrain/geological_test.cc
    tests/terrain/world_recipe_test.cc
    tests/terrain/readings_test.cc
    tests/terrain/drainage_test.cc
    tests/terrain/fractional_drainage_test.cc
    tests/terrain/flood_test.cc
    tests/terrain/merge_tree_test.cc
    tests/terrain/waterline_test.cc
    tests/terrain/stream_power_evolution_test.cc
    tests/terrain/trail_test.cc
    tests/terrain/moisture_test.cc
    tests/terrain/watercourse_test.cc
    tests/game/game_state_test.cc
    tests/game/frame_view_test.cc
    tests/game/terrain_test.cc
    tests/game/graphics_benchmark_test.cc
    tests/game/graphics_settings_test.cc
    tests/game/input_frame_test.cc
    tests/game/launch_options_test.cc
    tests/game/landscape_gazetteer_test.cc
    tests/game/simulation_clock_test.cc
    tests/game/generated_world_test.cc
    tests/game/waterfall_surface_test.cc
    tests/game/water_capture_test.cc
    tests/game/forest_test.cc
    tests/game/cinematic_flight_test.cc
    tests/map/surface_test.cc
    tests/map/water_surface_test.cc
    tests/map/terrain_generation_test.cc
    tests/render/reflection_geometry_test.cc
  )
  moppe_configure_code_target(moppe-tests)
  if(APPLE)
    target_sources(moppe-tests PRIVATE
      ${MOPPE_APPLE_COMMON_SOURCES})
    moppe_configure_apple_target(moppe-tests)
    moppe_configure_metal_target(moppe-tests)
    target_compile_definitions(moppe-tests PRIVATE
      MOPPE_SHADER_ASSET_PATH="${MOPPE_SHADER_ASSET}")
    target_sources(moppe-tests PRIVATE
      atelier/embedding.cc
      atelier/hex_sheet.cc
      atelier/tree_embedding.cc
      tests/atelier/bundle_test.cc
      tests/atelier/embedding_test.cc
      tests/atelier/hex_sheet_test.cc
      tests/atelier/tree_test.cc)
  endif()
  add_test(NAME moppe-tests COMMAND moppe-tests)
endif()

if(APPLE AND MOPPE_IOS)
  set(MOPPE_IOS_ASSETS
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/ios/Assets.xcassets)
  set_source_files_properties(${MOPPE_IOS_ASSETS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

  add_executable(moppe-ios MACOSX_BUNDLE
    moppe/game/main.cc
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    ${MOPPE_APPLE_COMMON_SOURCES}
    ${MOPPE_METAL_RENDER_SOURCES}
    moppe/platform/ios/main_ios.mm
    ${MOPPE_IOS_ASSETS}
  )
  moppe_configure_code_target(moppe-ios)
  moppe_configure_apple_target(moppe-ios)
  moppe_configure_metal_target(moppe-ios)
  target_link_libraries(moppe-ios PRIVATE "-framework UIKit")
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

if(APPLE AND MOPPE_TVOS)
  add_executable(moppe-tvos MACOSX_BUNDLE
    moppe/game/main.cc
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    ${MOPPE_APPLE_COMMON_SOURCES}
    ${MOPPE_METAL_RENDER_SOURCES}
    moppe/platform/tvos/main_tvos.mm
  )
  moppe_configure_code_target(moppe-tvos)
  moppe_configure_apple_target(moppe-tvos)
  moppe_configure_metal_target(moppe-tvos)
  target_compile_definitions(moppe-tvos PRIVATE
    MOPPE_DEFAULT_APPLE_TV_GRAPHICS)
  if(MOPPE_TVOS_BAKED_WORLD)
    if(NOT IS_DIRECTORY "${MOPPE_TVOS_BAKED_WORLD}")
      message(FATAL_ERROR
        "MOPPE_TVOS_BAKED_WORLD is not a directory: "
        "${MOPPE_TVOS_BAKED_WORLD}")
    endif()
    target_compile_definitions(moppe-tvos PRIVATE
      MOPPE_BUNDLED_WORLD_CACHE="world/apple-tv-default")
  endif()
  target_link_libraries(moppe-tvos PRIVATE "-framework UIKit")
  set_target_properties(moppe-tvos PROPERTIES
    OUTPUT_NAME "Moppe"
    XCODE_GENERATE_SCHEME TRUE
    MACOSX_BUNDLE_INFO_PLIST
      ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/tvos/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Moppe"
    MACOSX_BUNDLE_GUI_IDENTIFIER "se.brockman.moppe.tvos"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MOPPE_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MOPPE_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${MOPPE_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_MARKETING_VERSION "${MOPPE_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "se.brockman.moppe.tvos"
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "3")
  add_custom_command(TARGET moppe-tvos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MOPPE_SHADER_ASSET}
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/${MOPPE_SHADER_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/textures
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/data
    COMMENT "Bundling assets into Moppe.app (tvOS)")
  if(MOPPE_TVOS_BAKED_WORLD)
    add_custom_command(TARGET moppe-tvos POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/terrain
      COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/world/apple-tv-default
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${MOPPE_TVOS_BAKED_WORLD}
        $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/world/apple-tv-default
      COMMENT "Bundling host-baked Apple TV world")
  endif()
endif()

if(APPLE AND NOT MOPPE_MOBILE_APPLE)
  if(MOPPE_BUILD_DEVELOPER_TOOLS)
    # Renderer testbed: spinning solids + HUD shapes, no game.
    add_executable(moppe-testbed EXCLUDE_FROM_ALL
      moppe/game/testbed.cc
      ${MOPPE_RENDER_SOURCES}
      ${MOPPE_TERRAIN_SOURCES}
      ${MOPPE_APPLE_COMMON_SOURCES}
      ${MOPPE_METAL_RENDER_SOURCES}
      moppe/platform/mac/main_mac.mm)
    moppe_configure_code_target(moppe-testbed)
    moppe_configure_apple_target(moppe-testbed)
    moppe_configure_metal_target(moppe-testbed)
    target_compile_definitions(moppe-testbed PRIVATE
      MOPPE_SHADER_NAME="${MOPPE_SHADER_NAME}")
    target_link_libraries(moppe-testbed PRIVATE "-framework AppKit")
  endif()

  # The game, as a proper .app bundle with the assets inside.
  # (Still runnable from a shell: build/moppe.app/Contents/MacOS/moppe,
  # and MOPPE_ASSETS=<repo> overrides asset resolution for dev.)
  add_executable(moppe MACOSX_BUNDLE
    moppe/game/main.cc
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    ${MOPPE_APPLE_COMMON_SOURCES}
    ${MOPPE_METAL_RENDER_SOURCES}
    moppe/platform/mac/main_mac.mm)
  moppe_configure_code_target(moppe)
  moppe_configure_apple_target(moppe)
  moppe_configure_metal_target(moppe)
  target_compile_definitions(moppe PRIVATE
    MOPPE_SHADER_NAME="${MOPPE_SHADER_NAME}")
  target_link_libraries(moppe PRIVATE "-framework AppKit")
  set_target_properties(moppe PROPERTIES
    XCODE_GENERATE_SCHEME TRUE
    XCODE_SCHEME_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "se.brockman.moppe"
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
  # POST_BUILD bundling only runs when the executable relinks, which lets
  # shader-only edits go stale inside the bundle. Refresh them every build.
  add_custom_target(moppe_bundle_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MOPPE_SHADER_ASSET}
      $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/${MOPPE_SHADER_NAME}
    DEPENDS ${MOPPE_SHADER_ASSET} moppe
    COMMENT "Refreshing bundled shaders")
endif()

if(EMSCRIPTEN)
  function(moppe_configure_web_target target)
    moppe_configure_code_target(${target})
    target_compile_options(${target} PRIVATE
      --use-port=emdawnwebgpu)
    target_link_options(${target} PRIVATE
      --use-port=emdawnwebgpu
      -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
      -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=268435456
      -sDISABLE_EXCEPTION_CATCHING=0)
  endfunction()

  function(moppe_configure_web_executable target)
    moppe_configure_web_target(${target})
    set_target_properties(${target} PROPERTIES
      SUFFIX ".html"
      LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/web/shell.html")
    target_link_options(${target} PRIVATE
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/web/shell.html"
      "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/textures@/textures"
      -sASSERTIONS=1)
  endfunction()

  add_executable(moppe-web-testbed
    moppe/game/testbed.cc
    ${MOPPE_RENDER_SOURCES}
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WEBGPU_SOURCES}
    moppe/platform/web/main_web.cc)
  moppe_configure_web_executable(moppe-web-testbed)

  add_executable(moppe-web
    moppe/game/main.cc
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    ${MOPPE_WEBGPU_SOURCES}
    moppe/platform/web/main_web.cc)
  moppe_configure_web_executable(moppe-web)
endif()

# CMake's Xcode generator normally presents sources target-by-target, which is
# useful for building but awkward for browsing. Add one read-only source tree
# and tuck the shader plumbing into a collapsed folder.
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
  file(GLOB_RECURSE MOPPE_XCODE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/*.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/*.metal
    ${CMAKE_CURRENT_SOURCE_DIR}/moppe/*.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hh)
  add_custom_target(Source SOURCES ${MOPPE_XCODE_SOURCES})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${MOPPE_XCODE_SOURCES})

  foreach(target
      moppe_bundle_shaders
      moppe_shaders)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES FOLDER "Build Internals")
    endif()
  endforeach()
endif()

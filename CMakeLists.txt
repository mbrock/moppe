cmake_minimum_required(VERSION 3.24)

set(MOPPE_IOS FALSE)
set(MOPPE_TVOS FALSE)
set(MOPPE_MOBILE_APPLE FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(MOPPE_IOS TRUE)
  set(MOPPE_MOBILE_APPLE TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  set(MOPPE_TVOS TRUE)
  set(MOPPE_MOBILE_APPLE TRUE)
endif()

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
if(MOPPE_MOBILE_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  if(MOPPE_TVOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING
      "Minimum supported tvOS version")
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
      "Minimum supported iOS version")
  endif()
endif()

project(moppe LANGUAGES CXX)

# Keep editor tooling in sync with the normal Ninja and Makefile builds.
# Multi-config generators such as Xcode do not support this output.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(FetchContent)
find_package(ZLIB REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall)

set(MOPPE_VERSION "1.0.0" CACHE STRING "Moppe release version")
set(MOPPE_BUILD_NUMBER "1" CACHE STRING "Moppe release build number")
option(MOPPE_BUILD_DEVELOPER_TOOLS
  "Build demos, experiments, testbeds, and command-line tools" OFF)
option(MOPPE_UNITY_BUILD
  "Compile each target as a single unity translation unit" ON)
option(MOPPE_ENABLE_TRACY "Enable Tracy runtime profiling" OFF)

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

# The Atelier's combinatorial and intrinsic botany is a small portable
# library. Both the workshop renderer and the game provide their own
# extrinsic embeddings of the same organism.
add_library(atelier_botany STATIC
  atelier/tree.cc
  atelier/tree.hh
  atelier/space.hh
)
target_include_directories(atelier_botany PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atelier_botany PUBLIC mp-units::mp-units)

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
    atelier_botany
    mp-units::mp-units
    "-framework Cocoa"
    "-framework ImageIO"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers")
endif()

# ---------------------------------------------------------------
# Metal shader library: one metallib per Apple SDK. AIR is
# platform-targeted, so each app must load a library built for its OS.

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
  moppe/shaders/metal/field.metal
  moppe/shaders/metal/sky.metal
  moppe/shaders/metal/ocean.metal
  moppe/shaders/metal/river.metal
  moppe/shaders/metal/dust.metal
)

set(MOPPE_METAL_DEPS
  moppe/shaders/metal/common.h
  moppe/render/metal/shader_types.h
  moppe/terrain/metal/field_shader_types.h
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
              -std=metal3.0
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
  file(READ moppe/terrain/metal/field_shader_types.h
    MOPPE_METAL_FIELD_TYPES)
  string(APPEND MOPPE_METAL_SOURCE "\n${MOPPE_METAL_FIELD_TYPES}\n")
  file(READ moppe/shaders/metal/common.h MOPPE_METAL_COMMON)
  string(REPLACE
    "#include \"../../render/metal/shader_types.h\"" ""
    MOPPE_METAL_COMMON "${MOPPE_METAL_COMMON}")
  string(APPEND MOPPE_METAL_SOURCE "\n${MOPPE_METAL_COMMON}\n")
  foreach(src ${MOPPE_METAL_SOURCES})
    file(READ ${src} MOPPE_METAL_PART)
    string(REPLACE "#include \"common.h\"" ""
      MOPPE_METAL_PART "${MOPPE_METAL_PART}")
    string(REPLACE
      "#include \"../../terrain/metal/field_shader_types.h\"" ""
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

# `moppe/spatial` is header-only, but it is a real dependency boundary:
# Bundle and the quantity vocabulary expose mp-units values to their users.
add_library(moppe_spatial INTERFACE)
target_include_directories(moppe_spatial INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_spatial INTERFACE mp-units::mp-units)

add_library(moppe_render STATIC
  moppe/render/draw.cc
  moppe/render/text.cc
)
target_include_directories(moppe_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_render PUBLIC mp-units::mp-units)

# Runtime scalar-field expressions plus evaluator backends.  This stays
# independent of game and platform code so tools and tests use the same DAG.
add_library(moppe_terrain STATIC
  moppe/terrain/field.cc
  moppe/terrain/noise.cc
  moppe/terrain/cpu_evaluator.cc
  moppe/terrain/readings.cc
  moppe/terrain/drainage.cc
  moppe/terrain/fractional_drainage.cc
  moppe/terrain/river.cc
  moppe/terrain/flood.cc
  moppe/terrain/merge_tree.cc
  moppe/terrain/waterline.cc
  moppe/terrain/analytical_erosion.cc
  moppe/terrain/stream_power_evolution.cc
  moppe/terrain/trail.cc
  moppe/terrain/moisture.cc
  moppe/terrain/watercourse.cc
  moppe/terrain/geological.cc
  moppe/terrain/editor.cc
  moppe/terrain/program.cc
  moppe/terrain/world_recipe.cc
  moppe/terrain/image.cc
)
target_include_directories(moppe_terrain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The field DAG stays float-typed, but the recipe layer's phantom
# quantity kinds (Field<QS>) come from mp-units.
target_link_libraries(moppe_terrain PUBLIC moppe_spatial ZLIB::ZLIB)
if(MOPPE_ENABLE_TRACY)
  target_link_libraries(moppe_terrain PUBLIC Tracy::TracyClient)
endif()

if(APPLE AND NOT MOPPE_MOBILE_APPLE)
  add_library(moppe_terrain_metal STATIC
    moppe/terrain/metal/metal_evaluator.mm
  )
  target_include_directories(moppe_terrain_metal
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_terrain_metal PRIVATE -fobjc-arc)
  target_link_libraries(moppe_terrain_metal PUBLIC moppe_terrain
    "-framework Metal" "-framework Foundation")
  add_dependencies(moppe_terrain_metal moppe_shaders)

  if(MOPPE_BUILD_DEVELOPER_TOOLS)
    add_executable(terrain-metal-demo EXCLUDE_FROM_ALL
      moppe/terrain/metal/metal_field_demo.cc)
    target_link_libraries(terrain-metal-demo PRIVATE moppe_terrain_metal)
    target_compile_definitions(terrain-metal-demo PRIVATE
      MOPPE_SHADER_ASSET_PATH="${MOPPE_SHADER_ASSET}")
  endif()

endif()

if(MOPPE_BUILD_DEVELOPER_TOOLS AND NOT MOPPE_MOBILE_APPLE)
  add_executable(terrain-field-demo EXCLUDE_FROM_ALL
    moppe/terrain/field_demo.cc)
  target_link_libraries(terrain-field-demo PRIVATE moppe_terrain)
endif()

# Concrete completed-world storage, its finite surface sections, and the
# renderer-free Terrain Lab model all sit above the reusable terrain programs.
add_library(moppe_world STATIC
  moppe/map/generate.cc
  moppe/map/surface_domain.cc
  moppe/map/surface.cc
  moppe/map/surface_ecology.cc
  moppe/map/surface_materials.cc
  moppe/map/water_surface.cc
  moppe/map/terrain_evaluator.cc
  moppe/game/generated_world.cc
  moppe/game/terrain_lab_model.cc
  moppe/game/water_capture.cc
)
target_include_directories(moppe_world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_world PUBLIC moppe_terrain)
if(MOPPE_ENABLE_TRACY)
  target_link_libraries(moppe_world PUBLIC Tracy::TracyClient)
endif()

# Mutable gameplay state is deliberately separate from completed-world
# construction. Stars and Dust remain here because GameSession owns them;
# their retained meshes make the current simulation-to-render edge real.
add_library(moppe_simulation STATIC
  moppe/mov/glider.cc
  moppe/mov/vehicle.cc
  moppe/game/chase_camera.cc
  moppe/game/game_session.cc
  moppe/game/stars.cc
  moppe/game/dust.cc
  moppe/game/walker.cc
)
target_include_directories(moppe_simulation
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_simulation PUBLIC moppe_world moppe_render)
if(MOPPE_ENABLE_TRACY)
  target_link_libraries(moppe_simulation PUBLIC Tracy::TracyClient)
endif()

# Presentation reads completed worlds and sessions through FrameView. This is
# distinct from the renderer API and its platform-specific backends.
add_library(moppe_scene STATIC
  moppe/game/graphics_settings.cc
  moppe/game/frame_view.cc
  moppe/gfx/tga.cc
  moppe/game/surface_presentation.cc
  moppe/game/water_presentation.cc
  moppe/game/river_surface.cc
  moppe/game/cinematic_flight.cc
  moppe/game/walker_render.cc
  moppe/game/blob_shadow.cc
  moppe/game/sprites.cc
  moppe/game/model.cc
  moppe/game/glider_render.cc
  moppe/game/vehicle_render.cc
  moppe/game/hud.cc
  moppe/game/inspector_ui.cc
  moppe/game/forest.cc
  moppe/game/tree_stand.cc
)
target_include_directories(moppe_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_scene PUBLIC
  moppe_simulation
  moppe_world
  moppe_render
  atelier_botany)
if(MOPPE_ENABLE_TRACY)
  target_link_libraries(moppe_scene PUBLIC Tracy::TracyClient)
endif()

# The shared application layer is intentionally narrow: these two
# presentation systems make concrete platform service calls, while game.cc
# stays on each terminal executable because it defines main.
add_library(moppe_app STATIC
  moppe/game/terrain.cc
  moppe/game/terrain_lab.cc
)
target_include_directories(moppe_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moppe_app PUBLIC moppe_scene)

# Developer tools for inspecting and iterating on terrain generation.
if(MOPPE_BUILD_DEVELOPER_TOOLS AND NOT MOPPE_MOBILE_APPLE)
  add_executable(map-test EXCLUDE_FROM_ALL moppe/map/test.cc)
  target_link_libraries(map-test PRIVATE moppe_world)

  add_executable(terrain-pipeline-demo EXCLUDE_FROM_ALL
    moppe/terrain/pipeline_demo.cc
  )
  target_link_libraries(terrain-pipeline-demo PRIVATE moppe_world)

  add_executable(terrain-water-depth-experiment EXCLUDE_FROM_ALL
    moppe/terrain/water_depth_experiment.cc
  )
  target_link_libraries(terrain-water-depth-experiment PRIVATE moppe_world)
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
    "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
    "-framework ImageIO")
  if(MOPPE_ENABLE_TRACY)
    target_link_libraries(moppe_metal PUBLIC Tracy::TracyClient)
  endif()
  add_dependencies(moppe_metal moppe_shaders)

  add_library(moppe_apple STATIC
    moppe/platform/apple/apple_common.mm
    moppe/platform/apple/game_controller.mm
  )
  target_include_directories(moppe_apple PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_apple PRIVATE -fobjc-arc)
  target_link_libraries(moppe_apple PUBLIC
    mp-units::mp-units
    "-framework Foundation" "-framework CoreText"
    "-framework CoreGraphics" "-framework AVFoundation"
    "-framework GameController")

  # InspectorUi and the portable text path use Apple-common asset and glyph
  # services, but the host event loop stays above the scene target.
  target_link_libraries(moppe_scene PUBLIC moppe_apple)
endif()

# Keep the entire portable test suite behind one executable and one unity
# translation unit.  The macOS Metal evaluator test joins the same binary when
# that backend is available; it does not need its own copy of test_main.cc.
if(BUILD_TESTING AND NOT MOPPE_MOBILE_APPLE)
  add_executable(moppe-tests
    tests/test_main.cc
    tests/units_test.cc
    tests/spatial/bundle_test.cc
    tests/terrain/discretization_test.cc
    tests/terrain/field_test.cc
    tests/terrain/cpu_evaluator_test.cc
    tests/terrain/geological_test.cc
    tests/terrain/program_test.cc
    tests/terrain/world_recipe_test.cc
    tests/terrain/image_test.cc
    tests/terrain/readings_test.cc
    tests/terrain/drainage_test.cc
    tests/terrain/fractional_drainage_test.cc
    tests/terrain/flood_test.cc
    tests/terrain/merge_tree_test.cc
    tests/terrain/waterline_test.cc
    tests/terrain/analytical_erosion_test.cc
    tests/terrain/stream_power_evolution_test.cc
    tests/terrain/trail_test.cc
    tests/terrain/moisture_test.cc
    tests/terrain/watercourse_test.cc
    tests/game/game_state_test.cc
    tests/game/frame_view_test.cc
    tests/game/graphics_benchmark_test.cc
    tests/game/graphics_settings_test.cc
    tests/game/input_frame_test.cc
    tests/game/inspector_ui_test.cc
    tests/game/generated_world_test.cc
    tests/game/terrain_lab_model_test.cc
    tests/game/river_surface_test.cc
    tests/game/water_capture_test.cc
    tests/game/forest_test.cc
    tests/game/tree_stand_test.cc
    tests/game/cinematic_flight_test.cc
    tests/map/surface_test.cc
    tests/map/water_surface_test.cc
    tests/map/terrain_evaluator_test.cc
  )
  target_link_libraries(moppe-tests PRIVATE moppe_scene)
  if(TARGET moppe_terrain_metal)
    target_sources(moppe-tests PRIVATE
      tests/terrain/metal_evaluator_test.cc)
    target_link_libraries(moppe-tests PRIVATE moppe_terrain_metal)
    target_compile_definitions(moppe-tests PRIVATE
      MOPPE_SHADER_ASSET_PATH="${MOPPE_SHADER_ASSET}")
  endif()
  if(APPLE)
    target_sources(moppe-tests PRIVATE
      atelier/embedding.cc
      atelier/hex_sheet.cc
      atelier/tree_embedding.cc
      tests/atelier/bundle_test.cc
      tests/atelier/embedding_test.cc
      tests/atelier/hex_sheet_test.cc
      tests/atelier/tree_test.cc)
    set_source_files_properties(
      atelier/embedding.cc
      atelier/hex_sheet.cc
      atelier/tree_embedding.cc
      PROPERTIES
      SKIP_UNITY_BUILD_INCLUSION ON)
    # The testable scene reaches the shared Apple asset and glyph services,
    # but no longer inherits the desktop event-loop target from a broad
    # unity-built game archive.
    target_link_libraries(moppe-tests PRIVATE moppe_apple)
    target_link_libraries(moppe-tests PRIVATE atelier_botany)
  endif()
  add_test(NAME moppe-tests COMMAND moppe-tests)
endif()

if(APPLE AND MOPPE_IOS)
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
  target_link_libraries(moppe_app PUBLIC moppe_platform_ios)

  add_executable(moppe-ios MACOSX_BUNDLE
    moppe/game/game.cc
    ${MOPPE_IOS_ASSETS}
  )
  target_link_libraries(moppe-ios PRIVATE moppe_app moppe_platform_ios)
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
  add_library(moppe_platform_tvos STATIC
    moppe/platform/tvos/main_tvos.mm
  )
  target_include_directories(moppe_platform_tvos
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_platform_tvos PRIVATE -fobjc-arc)
  target_link_libraries(moppe_platform_tvos PUBLIC
    moppe_metal moppe_apple "-framework UIKit")
  target_link_libraries(moppe_app PUBLIC moppe_platform_tvos)

  add_executable(moppe-tvos MACOSX_BUNDLE
    moppe/game/game.cc
  )
  target_compile_definitions(moppe-tvos PRIVATE
    MOPPE_DEFAULT_BALANCED_GRAPHICS)
  target_link_libraries(moppe-tvos PRIVATE moppe_app moppe_platform_tvos)
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
endif()

if(APPLE AND NOT MOPPE_MOBILE_APPLE)
  add_library(moppe_platform_mac STATIC
    moppe/platform/mac/main_mac.mm
  )
  target_include_directories(moppe_platform_mac
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_platform_mac PRIVATE -fobjc-arc)
  target_compile_definitions(moppe_platform_mac PRIVATE
    MOPPE_SHADER_NAME="${MOPPE_SHADER_NAME}")
  target_link_libraries(moppe_platform_mac PUBLIC
    moppe_metal moppe_apple moppe_terrain_metal "-framework AppKit")
  target_link_libraries(moppe_app PUBLIC moppe_platform_mac)

  if(MOPPE_BUILD_DEVELOPER_TOOLS)
    # Renderer testbed: spinning solids + HUD shapes, no game.
    add_executable(moppe-testbed EXCLUDE_FROM_ALL moppe/game/testbed.cc)
    target_link_libraries(moppe-testbed PRIVATE moppe_platform_mac)
  endif()

  # The game, as a proper .app bundle with the assets inside.
  # (Still runnable from a shell: build/moppe.app/Contents/MacOS/moppe,
  # and MOPPE_ASSETS=<repo> overrides asset resolution for dev.)
  add_executable(moppe MACOSX_BUNDLE moppe/game/game.cc)
  target_link_libraries(moppe PRIVATE moppe_app moppe_platform_mac)
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

# CMake's Xcode generator normally presents the project target-by-target,
# which is useful for building but awkward for browsing. Add one read-only
# source tree and tuck implementation targets into a collapsed folder.
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
      moppe_apple
      moppe_app
      moppe_bundle_shaders
      moppe_metal
      moppe_platform_mac
      moppe_render
      moppe_scene
      moppe_shaders
      moppe_simulation
      moppe_spatial
      moppe_terrain
      moppe_terrain_metal
      moppe_world)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES FOLDER "Build Internals")
    endif()
  endforeach()
endif()

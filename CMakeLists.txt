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
# A world generates bit for bit the same on every platform: no fused
# multiply-adds the source does not ask for (an arm64 build would fuse where
# baseline x86-64 cannot), and correctly rounded transcendental functions
# from CORE-MATH below (docs/determinism.md).
add_compile_options(-ffp-contract=off)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
  add_compile_options($<$<CONFIG:Debug>:-gline-tables-only>)
endif()

set(MOPPE_VERSION "1.0.0" CACHE STRING "Moppe release version")
set(MOPPE_BUILD_NUMBER "1" CACHE STRING "Moppe release build number")
option(MOPPE_XBOX_GAME
  "On Windows, build the game for Xbox rather than the headless core probe"
  OFF)
option(MOPPE_NHAL_RENDERER
  "Build the NHAL renderer into the macOS game (needs luv-shaderc or nix)" ON)
option(MOPPE_BUILD_DEVELOPER_TOOLS
  "Build demos, experiments, testbeds, and command-line tools" OFF)
option(MOPPE_UNITY_BUILD
  "Compile each target as a single unity translation unit" ON)
option(MOPPE_ENABLE_TRACY "Enable Tracy runtime profiling" OFF)
set(MOPPE_TVOS_BAKED_WORLD "" CACHE PATH
  "Host-baked default finished-world cache to bundle into the tvOS app")
set(MOPPE_XBOX_BAKED_WORLD "" CACHE PATH
  "Host-baked finished-world cache to ship in the Xbox game package")

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

# Box3D is vendored from upstream (see third_party/box3d/VENDOR.md). Only the
# library is built; its own CMake files and samples are not vendored.
file(GLOB BOX3D_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/box3d/src/*.c)
add_library(box3d STATIC ${BOX3D_SOURCES})
add_library(box3d::box3d ALIAS box3d)
target_include_directories(box3d
  SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/box3d/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/box3d/src)
set_target_properties(box3d PROPERTIES
  C_STANDARD 17
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS YES
  UNITY_BUILD OFF)
# Upstream builds without floating-point contraction for cross-platform
# determinism.
target_compile_options(box3d PRIVATE -ffp-contract=off)
# nixbox's Xbox compiler searches the MSVC headers before Clang's own, so
# <emmintrin.h> declares the SSE intrinsics as external functions nothing
# defines. Box3D's SIMD kernels need Clang's inline definitions first.
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang"
    AND CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE MOPPE_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_include_directories(box3d SYSTEM BEFORE PRIVATE
    ${MOPPE_CLANG_RESOURCE_DIR}/include)
endif()

# CORE-MATH's correctly rounded sin, cos, tan, atan2, exp, log, log2, pow,
# and hypot in both precisions: each has one right answer, so they agree on
# every platform where the C libraries' do not (moppe/correct_math.hh).
set(CORE_MATH_FUNCTIONS sin cos tan atan2 exp log log2 pow hypot)
set(CORE_MATH_SOURCES)
foreach(function IN LISTS CORE_MATH_FUNCTIONS)
  list(APPEND CORE_MATH_SOURCES
    third_party/core-math/binary32/${function}/${function}f.c
    third_party/core-math/binary64/${function}/${function}.c)
endforeach()
add_library(core-math STATIC ${CORE_MATH_SOURCES})
set_target_properties(core-math PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS YES
  UNITY_BUILD OFF)
target_compile_options(core-math PRIVATE -w)
# Nothing reads the floating-point exception flags, which hypot and pow
# would otherwise save and restore around every call (a library call on
# arm64; third_party/core-math/VENDOR.md).
target_compile_definitions(core-math PRIVATE CORE_MATH_IGNORE_FLAGS)

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
# Source composition.
#
# Directories and namespaces describe ownership. They are deliberately not
# mirrored by static libraries: every terminal program compiles the source it
# needs as one unity translation unit per language. This avoids repeatedly
# parsing the mp-units-heavy header graph at artificial library boundaries.

set(MOPPE_RENDER_SOURCES
  moppe/render/draw.cc
  moppe/render/slug.cc
  moppe/render/text.cc
  moppe/render/truetype.cc
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
  moppe/terrain/sediment_transport.cc
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
  moppe/game/landscape_summary.cc
)

set(MOPPE_SIMULATION_SOURCES
  moppe/mov/glider.cc
  moppe/mov/vehicle.cc
  moppe/mov/trunk_field.cc
  moppe/mov/rigid_bike.cc
  moppe/mov/character.cc
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
  moppe/game/opening.cc
  moppe/game/cairn.cc
  moppe/game/avatar.cc
  moppe/game/walker_render.cc
  moppe/game/figure.cc
  moppe/game/figure_mesh.cc
  moppe/game/model_mesh.cc
  moppe/game/bike_model.cc
  moppe/game/glider_model.cc
  moppe/game/blob_shadow.cc
  moppe/game/sprites.cc
  moppe/game/glider_render.cc
  moppe/game/vehicle_render.cc
  moppe/game/hud.cc
  moppe/game/forest.cc
  moppe/game/boulders.cc
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
  moppe/platform/input.cc
  moppe/game/seed_memory.cc
  moppe/game/terrain.cc
  moppe/game/world_loading.cc
)

set(MOPPE_APPLE_COMMON_SOURCES
  moppe/platform/apple/apple_common.mm
  moppe/platform/apple/game_controller.mm
)

function(moppe_configure_code_target target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE
    mp-units::mp-units
    nanoarrow::nanoarrow
    box3d::box3d
    core-math)
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
    "-framework CoreGraphics"
    "-framework AVFoundation"
    "-framework GameController")
endfunction()

# NHAL, the renderer's hardware layer (docs/nhal.md). Its world programs are
# Luv's Lisp, lowered by luv-shaderc: from PATH, or else the version this
# repository's flake pins. The lowered shaders depend on the compiler too,
# so a new luv-shaderc relowers them.
find_program(MOPPE_LUV_SHADERC luv-shaderc NO_CACHE)
if(MOPPE_LUV_SHADERC)
  set(MOPPE_LUV_SHADERC_COMMAND ${MOPPE_LUV_SHADERC})
else()
  set(MOPPE_LUV_SHADERC "")
  set(MOPPE_LUV_SHADERC_COMMAND
    nix run "${CMAKE_CURRENT_SOURCE_DIR}#luv-shaderc" --)
endif()

set(MOPPE_NHAL_WORLD ${CMAKE_CURRENT_BINARY_DIR}/nhal-world)
set(MOPPE_NHAL_WORLD_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/moppe/nhal/renderer/shaders/world.lisp)
# The world programs by their stages.
set(MOPPE_NHAL_WORLD_DRAWN
  terrain sky uber hud resolve present slug_text forest
  bloom_bright bloom_blur grass sward gtao gtao_blur shafts boulders
  rain water leaves)
set(MOPPE_NHAL_WORLD_CASTERS terrain_shadow forest_shadow boulders_shadow)
set(MOPPE_NHAL_WORLD_COMPUTED
  forest_cull exposure grass_tiles sward_patches boulder_cull)
set(MOPPE_NHAL_WORLD_STAGES)
foreach(program IN LISTS MOPPE_NHAL_WORLD_DRAWN)
  list(APPEND MOPPE_NHAL_WORLD_STAGES ${program}.vertex ${program}.fragment)
endforeach()
foreach(program IN LISTS MOPPE_NHAL_WORLD_CASTERS)
  list(APPEND MOPPE_NHAL_WORLD_STAGES ${program}.vertex)
endforeach()
foreach(program IN LISTS MOPPE_NHAL_WORLD_COMPUTED)
  list(APPEND MOPPE_NHAL_WORLD_STAGES ${program}.compute)
endforeach()

# Lowers world.lisp to LANGUAGE (msl, hlsl, or spirv), whose stage files end
# in EXTENSION, once per build: the custom target moppe_nhal_world_LANGUAGE
# owns the command (Xcode lets one target own it). Sets OUTPUTS to every file
# it writes: each program's reflection header and stages.
function(moppe_lower_world_shaders language extension outputs)
  set(written)
  foreach(stage IN LISTS MOPPE_NHAL_WORLD_STAGES)
    string(REGEX REPLACE "[.].*" "" program ${stage})
    list(APPEND written
      ${MOPPE_NHAL_WORLD}/${program}.hh
      ${MOPPE_NHAL_WORLD}/${stage}.${extension})
  endforeach()
  list(REMOVE_DUPLICATES written)
  if(NOT TARGET moppe_nhal_world_${language})
    add_custom_command(
      OUTPUT ${written}
      COMMAND ${MOPPE_LUV_SHADERC_COMMAND} --out ${MOPPE_NHAL_WORLD}
        --target ${language} ${MOPPE_NHAL_WORLD_SOURCE}
      DEPENDS ${MOPPE_NHAL_WORLD_SOURCE} ${MOPPE_LUV_SHADERC}
      COMMENT "Lowering the NHAL renderer's Lisp shaders to ${language}"
      VERBATIM)
    add_custom_target(moppe_nhal_world_${language} DEPENDS ${written})
  endif()
  set(${outputs} ${written} PARENT_SCOPE)
endfunction()

# Adds the NHAL renderer to TARGET, with its world programs embedded in
# LANGUAGE through WORLD_SHADERS (world_shaders_<backend>.cc).
function(moppe_add_nhal_renderer target language extension world_shaders)
  moppe_lower_world_shaders(${language} ${extension} lowered)
  add_dependencies(${target} moppe_nhal_world_${language})
  target_sources(${target} PRIVATE
    moppe/nhal/renderer/nhal_renderer.cc ${world_shaders})
  target_include_directories(${target} PRIVATE ${MOPPE_NHAL_WORLD})
  set_source_files_properties(moppe/nhal/renderer/nhal_renderer.cc
    TARGET_DIRECTORY ${target} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  set_source_files_properties(${world_shaders}
    TARGET_DIRECTORY ${target} PROPERTIES
    COMPILE_OPTIONS "--embed-dir=${MOPPE_NHAL_WORLD}"
    SKIP_UNITY_BUILD_INCLUSION ON)
  # Relowered shaders recompile what includes or embeds them. Xcode's
  # browsing target lists these sources too, and an object dependency there
  # would give the lowering a second owner; the target dependency orders it.
  if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    set_source_files_properties(
      moppe/nhal/renderer/nhal_renderer.cc ${world_shaders}
      TARGET_DIRECTORY ${target} PROPERTIES OBJECT_DEPENDS "${lowered}")
  endif()
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

  # A hash after every generation stage and geological step, for comparing
  # builds and machines line by line.
  add_executable(world-fingerprint EXCLUDE_FROM_ALL
    moppe/terrain/world_fingerprint.cc
    ${MOPPE_TERRAIN_SOURCES}
    ${MOPPE_WORLD_SOURCES}
  )
  moppe_configure_code_target(world-fingerprint)
endif()

# Tests are configured for IDEs and CTest, but excluded from the default build.
# Asking for moppe-tests compiles the engine and suite together as one unity
# translation unit per language.
if(BUILD_TESTING AND NOT MOPPE_MOBILE_APPLE)
  add_executable(moppe-tests EXCLUDE_FROM_ALL
    ${MOPPE_ENGINE_SOURCES}
    moppe/game/terrain.cc
    moppe/platform/input.cc
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
    tests/terrain/river_test.cc
    tests/terrain/flood_test.cc
    tests/terrain/merge_tree_test.cc
    tests/terrain/waterline_test.cc
    tests/terrain/sediment_transport_test.cc
    tests/terrain/stream_power_evolution_test.cc
    tests/terrain/trail_test.cc
    tests/terrain/moisture_test.cc
    tests/terrain/watercourse_test.cc
    tests/game/game_state_test.cc
    tests/game/walker_test.cc
    tests/game/figure_test.cc
    tests/game/frame_view_test.cc
    tests/game/terrain_test.cc
    tests/game/graphics_benchmark_test.cc
    tests/game/graphics_settings_test.cc
    tests/mov/trunk_field_test.cc
    tests/mov/rigid_bike_test.cc
    tests/game/input_frame_test.cc
    tests/game/launch_options_test.cc
    tests/game/landscape_gazetteer_test.cc
    tests/game/simulation_clock_test.cc
    tests/game/generated_world_test.cc
    tests/game/waterfall_surface_test.cc
    tests/game/water_capture_test.cc
    tests/game/forest_test.cc
    tests/game/boulders_test.cc
    tests/game/cinematic_flight_test.cc
    tests/game/opening_test.cc
    tests/map/surface_test.cc
    tests/map/water_surface_test.cc
    tests/map/terrain_generation_test.cc
    tests/render/slug_test.cc
    tests/render/text_test.cc
  )
  moppe_configure_code_target(moppe-tests)
  target_compile_definitions(moppe-tests PRIVATE
    MOPPE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(APPLE)
    target_sources(moppe-tests PRIVATE
      ${MOPPE_APPLE_COMMON_SOURCES})
    moppe_configure_apple_target(moppe-tests)
    target_sources(moppe-tests PRIVATE
      atelier/embedding.cc
      atelier/hex_sheet.cc
      atelier/tree_embedding.cc
      tests/atelier/bundle_test.cc
      tests/atelier/embedding_test.cc
      tests/atelier/hex_sheet_test.cc
      tests/atelier/tree_test.cc)
  else()
    target_sources(moppe-tests PRIVATE tests/headless_platform.cc)
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
    moppe/nhal/metal/metal_device.mm
    moppe/platform/ios/main_ios.mm
    ${MOPPE_IOS_ASSETS}
  )
  moppe_configure_code_target(moppe-ios)
  moppe_configure_apple_target(moppe-ios)
  moppe_add_nhal_renderer(moppe-ios msl metal
    moppe/nhal/renderer/world_shaders_metal.cc)
  set_source_files_properties(moppe/nhal/metal/metal_device.mm
    moppe/platform/ios/main_ios.mm
    TARGET_DIRECTORY moppe-ios PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  target_link_libraries(moppe-ios PRIVATE
    "-framework Metal" "-framework QuartzCore" "-framework MetalKit")
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/textures
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/fonts
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-ios>/fonts
    COMMENT "Bundling assets into Moppe.app (iOS)")
endif()

if(APPLE AND MOPPE_TVOS)
  add_executable(moppe-tvos MACOSX_BUNDLE
    moppe/game/main.cc
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    ${MOPPE_APPLE_COMMON_SOURCES}
    moppe/nhal/metal/metal_device.mm
    moppe/platform/tvos/main_tvos.mm
  )
  moppe_configure_code_target(moppe-tvos)
  moppe_configure_apple_target(moppe-tvos)
  moppe_add_nhal_renderer(moppe-tvos msl metal
    moppe/nhal/renderer/world_shaders_metal.cc)
  set_source_files_properties(moppe/nhal/metal/metal_device.mm
    moppe/platform/tvos/main_tvos.mm
    TARGET_DIRECTORY moppe-tvos PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  target_link_libraries(moppe-tvos PRIVATE
    "-framework Metal" "-framework QuartzCore" "-framework MetalKit")
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/textures
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/fonts
      $<TARGET_BUNDLE_CONTENT_DIR:moppe-tvos>/fonts
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

# ---------------------------------------------------------------
# The desktop game: an SDL3 window on macOS and Linux, drawn by the NHAL
# renderer through Metal 4 or Vulkan (moppe/platform/sdl/). On Linux,
# `nix develop` provides SDL3, Vulkan, and luv-shaderc; elsewhere a missing
# SDL3 is built from its release.

if((APPLE AND NOT MOPPE_MOBILE_APPLE) OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz
      URL_HASH
        SHA256=30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()

  if(APPLE)
    set(MOPPE_SDL_DEVICE
      moppe/nhal/metal/metal_device.mm
      moppe/platform/sdl/device_metal.mm)
    set(MOPPE_SDL_SHADERS msl metal moppe/nhal/renderer/world_shaders_metal.cc)
  else()
    find_package(Vulkan REQUIRED)
    set(MOPPE_SDL_DEVICE
      moppe/nhal/vulkan/vulkan_device.cc
      moppe/platform/sdl/device_vulkan.cc)
    set(MOPPE_SDL_SHADERS spirv spv
      moppe/nhal/renderer/world_shaders_vulkan.cc)
  endif()

  # A Game (platform.hh) hosted in an SDL window: TARGET's own sources plus
  # the host, its services, the device, and the NHAL renderer.
  function(moppe_add_sdl_host target)
    target_sources(${target} PRIVATE
      moppe/platform/sdl/host.cc
      moppe/platform/sdl/services.cc
      ${MOPPE_SDL_DEVICE})
    moppe_configure_code_target(${target})
    moppe_add_nhal_renderer(${target} ${MOPPE_SDL_SHADERS})
    # SDL, Vulkan, and Objective-C stay out of the game's unity translation
    # unit; the services find assets in the source tree in development.
    set_source_files_properties(
      moppe/platform/sdl/host.cc ${MOPPE_SDL_DEVICE}
      TARGET_DIRECTORY ${target} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set_source_files_properties(moppe/platform/sdl/services.cc
      TARGET_DIRECTORY ${target} PROPERTIES
      COMPILE_DEFINITIONS "MOPPE_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
      SKIP_UNITY_BUILD_INCLUSION ON)
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
    if(APPLE)
      target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
      target_link_libraries(${target} PRIVATE
        "-framework Foundation"
        "-framework Metal"
        "-framework QuartzCore")
    else()
      target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
    endif()
  endfunction()

  # The game. On the Mac it is an app bundle with the assets inside, still
  # runnable from a shell: build/moppe.app/Contents/MacOS/moppe.
  if(APPLE)
    add_executable(moppe MACOSX_BUNDLE
      moppe/game/main.cc
      ${MOPPE_ENGINE_SOURCES}
      ${MOPPE_APPLICATION_SOURCES})
    set_target_properties(moppe PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_CURRENT_SOURCE_DIR}/moppe/platform/mac/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "Moppe"
      MACOSX_BUNDLE_GUI_IDENTIFIER "se.brockman.moppe"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${MOPPE_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${MOPPE_BUILD_NUMBER}"
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "se.brockman.moppe")
    add_custom_command(TARGET moppe POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/textures
        $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/textures
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/data
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_BUNDLE_CONTENT_DIR:moppe>/Resources/fonts
      COMMENT "Bundling assets into moppe.app")
  else()
    add_executable(moppe
      moppe/game/main.cc
      ${MOPPE_ENGINE_SOURCES}
      ${MOPPE_APPLICATION_SOURCES})
    install(TARGETS moppe RUNTIME DESTINATION bin)
    install(DIRECTORY fonts textures data DESTINATION share/moppe)
  endif()
  moppe_add_sdl_host(moppe)
  set_source_files_properties(moppe/game/main.cc
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

  if(MOPPE_BUILD_DEVELOPER_TOOLS)
    # Renderer testbed: spinning solids and HUD shapes, no game.
    add_executable(moppe-testbed EXCLUDE_FROM_ALL
      moppe/game/testbed.cc
      ${MOPPE_RENDER_SOURCES}
      ${MOPPE_TERRAIN_SOURCES})
    moppe_add_sdl_host(moppe-testbed)

    # Tree studio: a flat plane and a lineup of conifer specimens, the fast
    # instrument for iterating on tree geometry without a generated world.
    add_executable(moppe-tree-studio EXCLUDE_FROM_ALL
      moppe/game/tree_studio.cc
      ${MOPPE_RENDER_SOURCES}
      ${MOPPE_TERRAIN_SOURCES})
    moppe_add_sdl_host(moppe-tree-studio)
  endif()

  # The NHAL demo (docs/nhal.md): a valley, a spruce stand, and their sun
  # shadow on Metal 4 or Vulkan. Its shaders are read at run time.
  set(MOPPE_NHAL_SHADERS ${CMAKE_CURRENT_BINARY_DIR}/nhal-shaders)
  if(APPLE)
    set(MOPPE_NHAL_DEMO_LANGUAGE msl)
    set(MOPPE_NHAL_DEMO_EXTENSION metal)
  else()
    set(MOPPE_NHAL_DEMO_LANGUAGE spirv)
    set(MOPPE_NHAL_DEMO_EXTENSION spv)
  endif()
  set(MOPPE_NHAL_SHADER_OUTPUTS)
  foreach(stage IN ITEMS
      terrain.vertex terrain.fragment terrain_shadow.vertex
      trees.vertex trees.fragment trees_shadow.vertex
      sky.vertex sky.fragment tonemap.vertex tonemap.fragment
      forest_wind.compute resolve.vertex resolve.fragment)
    string(REGEX REPLACE "[.].*" "" program ${stage})
    list(APPEND MOPPE_NHAL_SHADER_OUTPUTS
      ${MOPPE_NHAL_SHADERS}/${program}.hh
      ${MOPPE_NHAL_SHADERS}/${stage}.${MOPPE_NHAL_DEMO_EXTENSION})
  endforeach()
  list(REMOVE_DUPLICATES MOPPE_NHAL_SHADER_OUTPUTS)
  add_custom_command(
    OUTPUT ${MOPPE_NHAL_SHADER_OUTPUTS}
    COMMAND ${MOPPE_LUV_SHADERC_COMMAND} --out ${MOPPE_NHAL_SHADERS}
      --target ${MOPPE_NHAL_DEMO_LANGUAGE}
      ${CMAKE_CURRENT_SOURCE_DIR}/moppe/nhal/demo/shaders/scene.lisp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/moppe/nhal/demo/shaders/scene.lisp
      ${MOPPE_LUV_SHADERC}
    COMMENT "Lowering the NHAL demo's Lisp shaders"
    VERBATIM)
  set_source_files_properties(${MOPPE_NHAL_SHADER_OUTPUTS} PROPERTIES
    HEADER_FILE_ONLY ON)
  if(APPLE)
    add_executable(nhal-demo EXCLUDE_FROM_ALL
      moppe/nhal/metal/metal_device.mm
      moppe/nhal/demo/scene.cc
      moppe/nhal/demo/main_mac.mm
      ${MOPPE_NHAL_SHADER_OUTPUTS})
    target_compile_options(nhal-demo PRIVATE
      $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(nhal-demo PRIVATE
      "-framework AppKit"
      "-framework Metal"
      "-framework QuartzCore"
      "-framework ImageIO"
      "-framework UniformTypeIdentifiers")
  else()
    add_executable(nhal-demo EXCLUDE_FROM_ALL
      moppe/nhal/vulkan/vulkan_device.cc
      moppe/nhal/demo/scene.cc
      moppe/nhal/demo/main_linux.cc
      ${MOPPE_NHAL_SHADER_OUTPUTS})
    target_link_libraries(nhal-demo PRIVATE Vulkan::Vulkan SDL3::SDL3)
  endif()
  target_include_directories(nhal-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${MOPPE_NHAL_SHADERS})
  target_compile_definitions(nhal-demo PRIVATE
    MOPPE_NHAL_DEMO_SHADERS="${MOPPE_NHAL_SHADERS}")
endif()

if(APPLE AND NOT MOPPE_MOBILE_APPLE)
  # The core probe from the command line (moppe/game/core_probe.hh): the
  # headless world generation and simulation measurement moppe-core-xbox
  # makes on the console, for comparison.
  add_executable(moppe-core-probe EXCLUDE_FROM_ALL
    ${MOPPE_ENGINE_SOURCES}
    ${MOPPE_APPLICATION_SOURCES}
    moppe/game/core_probe.cc
    moppe/platform/apple/apple_common.mm
    moppe/platform/mac/core_probe_mac.cc)
  moppe_configure_code_target(moppe-core-probe)
  moppe_configure_apple_target(moppe-core-probe)
endif()



# Xbox Series consoles in Developer Mode: x86_64 UWP, cross-built by nixbox.
# MOPPE_XBOX_GAME chooses the game (moppe/platform/uwp/game.nix; `nix build
# .#moppe-xbox`) over the core probe (moppe/platform/uwp/xbox.nix; `nix build
# .#moppe-core-xbox`), which runs world generation and simulation headlessly
# on the console and reports how they fare.
if(WIN32)
  # The game is drawn by the NHAL renderer through Direct3D 12. luv-shaderc
  # lowers the renderer's Lisp shaders to HLSL and DXC compiles each stage
  # to a header of signed DXIL.
  if(MOPPE_XBOX_GAME)
    if(NOT MOPPE_LUV_SHADERC)
      message(FATAL_ERROR "The Xbox game needs luv-shaderc on PATH")
    endif()
    find_program(MOPPE_DXC dxc REQUIRED)
    set(MOPPE_XBOX_SHADERS ${MOPPE_NHAL_WORLD})
    set(MOPPE_XBOX_STAGES ${MOPPE_NHAL_WORLD_STAGES})
    moppe_lower_world_shaders(hlsl hlsl MOPPE_XBOX_LOWERED)
    set(MOPPE_XBOX_DXIL)
    foreach(stage IN LISTS MOPPE_XBOX_STAGES)
      string(REPLACE "." "_" entry ${stage})
      string(REGEX REPLACE "^[^.]*[.]" "" kind ${stage})
      if(kind STREQUAL "vertex")
        set(profile vs_6_0)
      elseif(kind STREQUAL "fragment")
        set(profile ps_6_0)
      else()
        set(profile cs_6_0)
      endif()
      add_custom_command(
        OUTPUT ${MOPPE_XBOX_SHADERS}/shader_${entry}.h
        COMMAND ${MOPPE_DXC} -O3 -T ${profile} -E ${entry}
          -Fh ${MOPPE_XBOX_SHADERS}/shader_${entry}.h -Vn shader_${entry}
          ${MOPPE_XBOX_SHADERS}/${stage}.hlsl
        DEPENDS ${MOPPE_XBOX_SHADERS}/${stage}.hlsl
        VERBATIM)
      list(APPEND MOPPE_XBOX_DXIL ${MOPPE_XBOX_SHADERS}/shader_${entry}.h)
    endforeach()
    set_source_files_properties(${MOPPE_XBOX_LOWERED} ${MOPPE_XBOX_DXIL}
      PROPERTIES HEADER_FILE_ONLY ON)

    # The SDL host, on nixbox's SDL3 with its UWP backend.
    find_package(SDL3 CONFIG REQUIRED)
    add_executable(moppe-xbox WIN32
      moppe/game/main.cc
      ${MOPPE_ENGINE_SOURCES}
      ${MOPPE_APPLICATION_SOURCES}
      moppe/nhal/d3d12/d3d12_device.cc
      moppe/nhal/renderer/nhal_renderer.cc
      moppe/nhal/renderer/world_shaders_d3d12.cc
      moppe/platform/sdl/host.cc
      moppe/platform/sdl/device_d3d12.cc
      moppe/platform/uwp/platform_uwp.cc
      moppe/platform/uwp/main_uwp.cc
      ${MOPPE_XBOX_LOWERED}
      ${MOPPE_XBOX_DXIL})
    moppe_configure_code_target(moppe-xbox)
    target_include_directories(moppe-xbox PRIVATE ${MOPPE_XBOX_SHADERS})
    # main.cc's main is the game's entry, which main_uwp.cc's main, under
    # SDL's UWP entry point, calls after the console's prelude.
    set_source_files_properties(moppe/game/main.cc PROPERTIES
      COMPILE_DEFINITIONS "main=moppe_main;MOPPE_DEFAULT_XBOX_WORLD")
    set_source_files_properties(
      moppe/game/main.cc
      moppe/nhal/d3d12/d3d12_device.cc
      moppe/nhal/renderer/nhal_renderer.cc
      moppe/nhal/renderer/world_shaders_d3d12.cc
      moppe/platform/sdl/host.cc
      moppe/platform/sdl/device_d3d12.cc
      moppe/platform/uwp/platform_uwp.cc
      moppe/platform/uwp/main_uwp.cc
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set_source_files_properties(moppe/nhal/renderer/world_shaders_d3d12.cc
      PROPERTIES OBJECT_DEPENDS "${MOPPE_XBOX_DXIL}")
    set_source_files_properties(moppe/nhal/renderer/nhal_renderer.cc
      PROPERTIES OBJECT_DEPENDS "${MOPPE_XBOX_LOWERED}")
    target_link_libraries(moppe-xbox PRIVATE SDL3::SDL3 -ld3d12 -ldxgi)
    # A package may ship a finished world baked on the host
    # (tools/bake-world); the game starts in it instead of generating one.
    target_compile_definitions(moppe-xbox PRIVATE
      NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
      MOPPE_BUNDLED_WORLD_CACHE="world/default")
    install(TARGETS moppe-xbox RUNTIME DESTINATION bin)
    install(DIRECTORY fonts textures data DESTINATION share/moppe-xbox)
    if(MOPPE_XBOX_BAKED_WORLD)
      if(NOT EXISTS "${MOPPE_XBOX_BAKED_WORLD}/topology.bin")
        message(FATAL_ERROR
          "MOPPE_XBOX_BAKED_WORLD is not a finished-world cache: "
          "${MOPPE_XBOX_BAKED_WORLD}")
      endif()
      install(DIRECTORY ${MOPPE_XBOX_BAKED_WORLD}/
        DESTINATION share/moppe-xbox/world/default)
    endif()
  else()
    add_executable(moppe-core-xbox WIN32
      ${MOPPE_ENGINE_SOURCES}
      ${MOPPE_APPLICATION_SOURCES}
      moppe/game/core_probe.cc
      moppe/platform/uwp/platform_uwp.cc
      moppe/platform/uwp/core_main.cc)
    moppe_configure_code_target(moppe-core-xbox)
    # windows.h and C++/WinRT stay out of the game's unity translation unit.
    set_source_files_properties(
      moppe/platform/uwp/platform_uwp.cc
      moppe/platform/uwp/core_main.cc
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_link_libraries(moppe-core-xbox PRIVATE -ld3d11 -ldxgi)
    target_compile_definitions(moppe-core-xbox PRIVATE
      NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    install(TARGETS moppe-core-xbox RUNTIME DESTINATION bin)
    install(DIRECTORY fonts textures
      DESTINATION share/moppe-core-xbox)
  endif()
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

endif()

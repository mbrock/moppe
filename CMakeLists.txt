cmake_minimum_required(VERSION 3.24)
project(moppe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall)

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 11)
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

set(MOPPE_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/moppe.metallib)
add_custom_command(
  OUTPUT ${MOPPE_METALLIB}
  COMMAND xcrun -sdk ${MOPPE_METAL_SDK} metallib
          ${MOPPE_AIR_FILES} -o ${MOPPE_METALLIB}
  DEPENDS ${MOPPE_AIR_FILES}
  COMMENT "Linking moppe.metallib (${MOPPE_METAL_SDK})"
  VERBATIM)
add_custom_target(moppe_shaders DEPENDS ${MOPPE_METALLIB})

# ---------------------------------------------------------------
# Portable core: rendering abstraction + game-independent code.

add_library(moppe_render STATIC
  moppe/render/draw.cc
)
target_include_directories(moppe_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_library(moppe_platform_mac STATIC
    moppe/platform/mac/main_mac.mm
  )
  target_include_directories(moppe_platform_mac
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(moppe_platform_mac PRIVATE -fobjc-arc)
  target_link_libraries(moppe_platform_mac PUBLIC
    moppe_metal moppe_apple "-framework AppKit")

  # Renderer testbed: spinning solids + HUD shapes, no game.
  add_executable(moppe-testbed moppe/game/testbed.cc)
  target_link_libraries(moppe-testbed PRIVATE moppe_platform_mac)
endif()

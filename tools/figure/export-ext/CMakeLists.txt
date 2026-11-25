cmake_minimum_required(VERSION 3.25)
project(moppe-export LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE nanobind_ROOT
  RESULT_VARIABLE nanobind_status)
if(NOT nanobind_status EQUAL 0)
  message(FATAL_ERROR "nanobind CMake directory not found")
endif()
find_package(nanobind CONFIG REQUIRED)

set(MOPPE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
if(NOT DEFINED MP_UNITS_SOURCE_DIR)
  message(FATAL_ERROR "MP_UNITS_SOURCE_DIR is required")
endif()

set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)
set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)
add_subdirectory("${MP_UNITS_SOURCE_DIR}/src" mp-units EXCLUDE_FROM_ALL)

nanobind_add_module(moppe_export
  moppe_export.cc
  "${MOPPE_ROOT}/third_party/nanoarrow/nanoarrow.c"
  "${MOPPE_ROOT}/third_party/nanoarrow/nanoarrow_ipc.c"
  "${MOPPE_ROOT}/third_party/nanoarrow/flatcc.c")
target_include_directories(moppe_export PRIVATE
  "${MOPPE_ROOT}"
  "${MOPPE_ROOT}/third_party/nanoarrow")
target_link_libraries(moppe_export PRIVATE mp-units::mp-units)
set_source_files_properties(
  "${MOPPE_ROOT}/third_party/nanoarrow/nanoarrow.c"
  "${MOPPE_ROOT}/third_party/nanoarrow/nanoarrow_ipc.c"
  "${MOPPE_ROOT}/third_party/nanoarrow/flatcc.c"
  PROPERTIES COMPILE_FLAGS "-w")

install(TARGETS moppe_export DESTINATION .)

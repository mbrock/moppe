# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENCE.txt or https://cmake.org/licensing for details.

# This file has been modified to pass CMAKE_MODULE_PATH to the compiler probe.
# CMake includes that fix from 3.30 onward.
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.30)
    include(${CMAKE_ROOT}/Modules/CheckLanguage.cmake)
else()
    include_guard(GLOBAL)

    block(SCOPE_FOR POLICIES)
    cmake_policy(SET CMP0126 NEW)

    macro(check_language lang)
      if(NOT DEFINED CMAKE_${lang}_COMPILER)
        set(_desc "Looking for a ${lang} compiler")
        message(CHECK_START "${_desc}")
        file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang})

        set(extra_compiler_variables)
        if("${lang}" MATCHES "^(CUDA|HIP)$" AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
          set(extra_compiler_variables "set(CMAKE_${lang}_HOST_COMPILER \\\"\${CMAKE_${lang}_HOST_COMPILER}\\\")")
        endif()

        if("${lang}" STREQUAL "HIP")
          list(APPEND extra_compiler_variables "set(CMAKE_${lang}_PLATFORM \\\"\${CMAKE_${lang}_PLATFORM}\\\")")
        endif()

        list(TRANSFORM extra_compiler_variables PREPEND "\"")
        list(TRANSFORM extra_compiler_variables APPEND "\\n\"")
        list(JOIN extra_compiler_variables "\n  " extra_compiler_variables)

        set(_cl_content
          "cmake_minimum_required(VERSION ${CMAKE_VERSION})
    set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
    project(Check${lang} ${lang})
    file(WRITE \"\${CMAKE_CURRENT_BINARY_DIR}/result.cmake\"
      \"set(CMAKE_${lang}_COMPILER \\\"\${CMAKE_${lang}_COMPILER}\\\")\\n\"
      ${extra_compiler_variables}
      )"
        )

        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}/CMakeLists.txt"
          "${_cl_content}")
        if(CMAKE_GENERATOR_INSTANCE)
          set(_D_CMAKE_GENERATOR_INSTANCE "-DCMAKE_GENERATOR_INSTANCE:INTERNAL=${CMAKE_GENERATOR_INSTANCE}")
        else()
          set(_D_CMAKE_GENERATOR_INSTANCE "")
        endif()
        if(CMAKE_GENERATOR MATCHES "^(Xcode$|Green Hills MULTI$|Visual Studio)")
          set(_D_CMAKE_MAKE_PROGRAM "")
        else()
          set(_D_CMAKE_MAKE_PROGRAM "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
        endif()
        if(CMAKE_TOOLCHAIN_FILE)
          set(_D_CMAKE_TOOLCHAIN_FILE "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
        else()
          set(_D_CMAKE_TOOLCHAIN_FILE "")
        endif()
        if(CMAKE_${lang}_PLATFORM)
          set(_D_CMAKE_LANG_PLATFORM "-DCMAKE_${lang}_PLATFORM:STRING=${CMAKE_${lang}_PLATFORM}")
        else()
          set(_D_CMAKE_LANG_PLATFORM "")
        endif()
        execute_process(
          WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}
          COMMAND ${CMAKE_COMMAND} . -G ${CMAKE_GENERATOR}
                                     -A "${CMAKE_GENERATOR_PLATFORM}"
                                     -T "${CMAKE_GENERATOR_TOOLSET}"
                                     ${_D_CMAKE_GENERATOR_INSTANCE}
                                     ${_D_CMAKE_MAKE_PROGRAM}
                                     ${_D_CMAKE_TOOLCHAIN_FILE}
                                     ${_D_CMAKE_LANG_PLATFORM}
          OUTPUT_VARIABLE _cl_output
          ERROR_VARIABLE _cl_output
          RESULT_VARIABLE _cl_result
          )
        include(${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}/result.cmake OPTIONAL)
        if(CMAKE_${lang}_COMPILER AND "${_cl_result}" STREQUAL "0")
          message(CONFIGURE_LOG
            "${_desc} passed with the following output:\n"
            "${_cl_output}\n")
          set(_CHECK_COMPILER_STATUS CHECK_PASS)
        else()
          set(CMAKE_${lang}_COMPILER NOTFOUND)
          set(_CHECK_COMPILER_STATUS CHECK_FAIL)
          message(CONFIGURE_LOG
            "${_desc} failed with the following output:\n"
            "${_cl_output}\n")
        endif()
        message(${_CHECK_COMPILER_STATUS} "${CMAKE_${lang}_COMPILER}")
        set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER}" CACHE FILEPATH "${lang} compiler")
        mark_as_advanced(CMAKE_${lang}_COMPILER)

        if(CMAKE_${lang}_HOST_COMPILER)
          message(STATUS "Looking for a ${lang} host compiler - ${CMAKE_${lang}_HOST_COMPILER}")
          set(CMAKE_${lang}_HOST_COMPILER "${CMAKE_${lang}_HOST_COMPILER}" CACHE FILEPATH "${lang} host compiler")
          mark_as_advanced(CMAKE_${lang}_HOST_COMPILER)
        endif()

        if(CMAKE_${lang}_PLATFORM)
          set(CMAKE_${lang}_PLATFORM "${CMAKE_${lang}_PLATFORM}" CACHE STRING "${lang} platform")
          mark_as_advanced(CMAKE_${lang}_PLATFORM)
        endif()
      endif()
    endmacro()

    endblock()
endif()

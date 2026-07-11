# Configure CMake with the Homebrew LLVM toolchain.  Homebrew LLVM can lag the
# active Xcode SDK, so prefer the matching Command Line Tools SDK when present.
find_program(MOPPE_BREW_EXECUTABLE brew REQUIRED)

execute_process(
  COMMAND "${MOPPE_BREW_EXECUTABLE}" --prefix llvm@20
  RESULT_VARIABLE MOPPE_BREW_LLVM_RESULT
  OUTPUT_VARIABLE MOPPE_BREW_LLVM_PREFIX
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT MOPPE_BREW_LLVM_RESULT EQUAL 0)
  execute_process(
    COMMAND "${MOPPE_BREW_EXECUTABLE}" --prefix llvm
    RESULT_VARIABLE MOPPE_BREW_LLVM_RESULT
    OUTPUT_VARIABLE MOPPE_BREW_LLVM_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT MOPPE_BREW_LLVM_RESULT EQUAL 0)
  message(FATAL_ERROR "Install Homebrew LLVM with: brew install llvm")
endif()

set(CMAKE_CXX_COMPILER
  "${MOPPE_BREW_LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "")
set(CMAKE_OBJCXX_COMPILER
  "${MOPPE_BREW_LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "")

set(MOPPE_CLT_SDK
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
if(APPLE AND EXISTS "${MOPPE_CLT_SDK}" AND NOT CMAKE_OSX_SYSROOT)
  set(CMAKE_OSX_SYSROOT "${MOPPE_CLT_SDK}" CACHE PATH
    "macOS SDK used with Homebrew LLVM")
endif()

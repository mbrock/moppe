#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_if_different /Users/mbrock/2025/moppe/build-ios/moppe.metallib /Users/mbrock/2025/moppe/build-ios/Debug${EFFECTIVE_PLATFORM_NAME}/Moppe.app/moppe.metallib
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/textures /Users/mbrock/2025/moppe/build-ios/Debug${EFFECTIVE_PLATFORM_NAME}/Moppe.app/textures
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/data /Users/mbrock/2025/moppe/build-ios/Debug${EFFECTIVE_PLATFORM_NAME}/Moppe.app/data
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_if_different /Users/mbrock/2025/moppe/build-ios/moppe.metallib /Users/mbrock/2025/moppe/build-ios/Release${EFFECTIVE_PLATFORM_NAME}/Moppe.app/moppe.metallib
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/textures /Users/mbrock/2025/moppe/build-ios/Release${EFFECTIVE_PLATFORM_NAME}/Moppe.app/textures
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/data /Users/mbrock/2025/moppe/build-ios/Release${EFFECTIVE_PLATFORM_NAME}/Moppe.app/data
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_if_different /Users/mbrock/2025/moppe/build-ios/moppe.metallib /Users/mbrock/2025/moppe/build-ios/MinSizeRel${EFFECTIVE_PLATFORM_NAME}/Moppe.app/moppe.metallib
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/textures /Users/mbrock/2025/moppe/build-ios/MinSizeRel${EFFECTIVE_PLATFORM_NAME}/Moppe.app/textures
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/data /Users/mbrock/2025/moppe/build-ios/MinSizeRel${EFFECTIVE_PLATFORM_NAME}/Moppe.app/data
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_if_different /Users/mbrock/2025/moppe/build-ios/moppe.metallib /Users/mbrock/2025/moppe/build-ios/RelWithDebInfo${EFFECTIVE_PLATFORM_NAME}/Moppe.app/moppe.metallib
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/textures /Users/mbrock/2025/moppe/build-ios/RelWithDebInfo${EFFECTIVE_PLATFORM_NAME}/Moppe.app/textures
  /opt/homebrew/Cellar/cmake/3.28.3/bin/cmake -E copy_directory /Users/mbrock/2025/moppe/data /Users/mbrock/2025/moppe/build-ios/RelWithDebInfo${EFFECTIVE_PLATFORM_NAME}/Moppe.app/data
fi


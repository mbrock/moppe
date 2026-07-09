#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  make -f /Users/mbrock/2025/moppe/build-ios/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  make -f /Users/mbrock/2025/moppe/build-ios/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  make -f /Users/mbrock/2025/moppe/build-ios/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  make -f /Users/mbrock/2025/moppe/build-ios/CMakeScripts/ReRunCMake.make
fi


#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  xcrun -sdk iphonesimulator metal -c /Users/mbrock/2025/moppe/moppe/shaders/metal/terrain.metal -o /Users/mbrock/2025/moppe/build-ios/terrain.air
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  xcrun -sdk iphonesimulator metal -c /Users/mbrock/2025/moppe/moppe/shaders/metal/terrain.metal -o /Users/mbrock/2025/moppe/build-ios/terrain.air
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  xcrun -sdk iphonesimulator metal -c /Users/mbrock/2025/moppe/moppe/shaders/metal/terrain.metal -o /Users/mbrock/2025/moppe/build-ios/terrain.air
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/mbrock/2025/moppe/build-ios
  xcrun -sdk iphonesimulator metal -c /Users/mbrock/2025/moppe/moppe/shaders/metal/terrain.metal -o /Users/mbrock/2025/moppe/build-ios/terrain.air
fi


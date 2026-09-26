// Platform hooks for test builds on hosts without an Apple platform layer,
// such as Linux development machines.
#include "moppe/platform/platform.hh"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace moppe {
  namespace platform {
    std::string asset_path (const std::string& relative) {
      if (const char* base = std::getenv ("MOPPE_ASSETS")) {
        std::string p = std::string (base) + "/" + relative;
        if (std::filesystem::exists (p))
          return p;
      }
      return relative;
    }

    bool
    rasterize_glyph (const char*, float, float, unsigned int, GlyphBitmap&) {
      return false;
    }
  }
}

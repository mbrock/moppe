#include <moppe/game/graphics_settings.hh>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ostream>
#include <string>

namespace moppe::game {
  namespace {
    bool parse_float (const char* text,
                      float minimum,
                      float maximum,
                      float& result) {
      if (!text || *text == '\0')
        return false;
      char* end = nullptr;
      errno = 0;
      const float value = std::strtof (text, &end);
      if (errno || end == text || *end != '\0' || !std::isfinite (value) ||
          value < minimum || value > maximum)
        return false;
      result = value;
      return true;
    }

    bool env_enabled (const char* name) {
      const char* value = std::getenv (name);
      return value && std::string_view (value) != "0";
    }
  }

  GraphicsSettings high_graphics_settings () {
    return {};
  }

  GraphicsSettings low_graphics_settings () {
    GraphicsSettings settings;
    settings.scene_scale = 0.5f;
    settings.terrain_shadows = false;
    settings.vegetation = false;
    settings.grass = false;
    settings.ocean = false;
    settings.particles = false;
    settings.vehicle_effects = false;
    settings.star_effects = false;
    settings.motion_blur = false;
    settings.bloom = false;
    settings.auto_exposure = false;
    settings.lens_flare = false;
    return settings;
  }

  const GraphicsFeature* find_graphics_feature (std::string_view name) {
    const auto found = std::find_if (graphics_features.begin (),
                                     graphics_features.end (),
                                     [name] (const GraphicsFeature* feature) {
                                       return feature->name == name;
                                     });
    return found == graphics_features.end () ? nullptr : *found;
  }

  bool set_graphics_features (GraphicsSettings& settings,
                              std::string_view names,
                              bool enabled,
                              std::string& error) {
    while (!names.empty ()) {
      const std::size_t comma = names.find (',');
      const std::string_view name = names.substr (0, comma);
      if (name.empty ()) {
        error = "empty graphics feature name";
        return false;
      }
      const GraphicsFeature* feature = find_graphics_feature (name);
      if (!feature) {
        error = "unknown graphics feature: " + std::string (name);
        return false;
      }
      feature->set (settings, enabled);
      if (comma == std::string_view::npos)
        break;
      if (comma + 1 == names.size ()) {
        error = "empty graphics feature name";
        return false;
      }
      names.remove_prefix (comma + 1);
    }
    return true;
  }

  bool apply_graphics_environment (GraphicsSettings& settings,
                                   std::string& error) {
    if (const char* scale = std::getenv ("MOPPE_RENDERSCALE")) {
      if (!parse_float (scale, 0.25f, 1.0f, settings.render_scale_override)) {
        error = "MOPPE_RENDERSCALE must be between 0.25 and 1";
        return false;
      }
    }
    if (const char* grass = std::getenv ("MOPPE_GRASS")) {
      if (!parse_float (grass, 0.0f, 16.0f, settings.grass_density)) {
        error = "MOPPE_GRASS must be between 0 and 16";
        return false;
      }
    }
    if (const char* sun = std::getenv ("MOPPE_SUNHEIGHT")) {
      if (!parse_float (sun, 0.0f, 1.0f, settings.sun_height)) {
        error = "MOPPE_SUNHEIGHT must be between 0 and 1";
        return false;
      }
    }
    if (env_enabled ("MOPPE_NOSHADOW"))
      settings.terrain_shadows = false;
    if (env_enabled ("MOPPE_RIVER_RIBBONS"))
      settings.river_ribbons = true;
    if (env_enabled ("MOPPE_TERRAIN_TOPOLOGY"))
      settings.terrain_topology = true;
    return true;
  }

  void print_graphics_settings (std::ostream& output,
                                const GraphicsSettings& settings) {
    output << "moppe: graphics: scene-scale=" << settings.scene_scale
           << " render-scale-override=" << settings.render_scale_override
           << " grass-density=" << settings.grass_density
           << " sun-height=" << settings.sun_height;
    for (const GraphicsFeature* feature : graphics_features)
      output << ' ' << feature->name << '='
             << (feature->enabled (settings) ? "on" : "off") << '('
             << (feature->hot ? "hot" : "not-hot") << ')';
    output << '\n';
  }
}

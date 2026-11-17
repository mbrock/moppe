#ifndef MOPPE_GAME_PLACE_KIND_HH
#define MOPPE_GAME_PLACE_KIND_HH

// What each kind of place the road network reaches (terrain::RoadPlace)
// is, as the game tells it: one row per kind, read by the places' log,
// the pastures, the map sheet, and the chart. What is drawn for each
// stays with the drawing (map_sheet.cc, hud.cc); what is built there is
// PlacesPlan's (places.hh).

#include <moppe/color.hh>
#include <moppe/terrain/roads.hh>

#include <array>
#include <cstddef>

namespace moppe::game {
  struct PlaceKind {
    // How the places' log names one ("a landing").
    const char* what;
    // Named on the map and shown as a place on the chart. The trailhead
    // is marked as where the machines start instead.
    bool named;
    // How far round it the ground was grazed open while it was lived in,
    // metres; none round the trailhead.
    float pasture_m;
    // Its ink on the map and the chart, and once it has long been left.
    DisplayColor ink;
    DisplayColor quiet_ink;
  };

  inline constexpr std::array<PlaceKind, 4> place_kinds = { {
    // Trailhead
    { .what = "the trailhead",
      .named = false,
      .pasture_m = 0.0f,
      .ink = DisplayColor (0.80f, 0.13f, 0.11f),
      .quiet_ink = DisplayColor (0.80f, 0.13f, 0.11f) },
    // Landing
    { .what = "a landing",
      .named = true,
      .pasture_m = 650.0f,
      .ink = DisplayColor (0.10f, 0.30f, 0.62f),
      .quiet_ink = DisplayColor (0.10f, 0.30f, 0.62f) },
    // LakeOutlet
    { .what = "a lake outlet",
      .named = true,
      .pasture_m = 500.0f,
      .ink = DisplayColor (0.10f, 0.30f, 0.62f),
      .quiet_ink = DisplayColor (0.10f, 0.30f, 0.62f) },
    // Sanctuary
    { .what = "a sanctuary",
      .named = true,
      .pasture_m = 1300.0f,
      .ink = DisplayColor (0.56f, 0.12f, 0.16f),
      .quiet_ink = DisplayColor (0.36f, 0.24f, 0.26f) },
  } };

  constexpr const PlaceKind& place_kind (terrain::RoadPlace::Kind kind) {
    return place_kinds[static_cast<std::size_t> (kind)];
  }
}

#endif

#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/map/surface.hh>

#include <moppe/profile.hh>
#include <moppe/spatial/bundle_operations.hh>
#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::map {
  // ---- Geometry ----

  namespace {
    // How far the broad support plane reaches, as a step across the lattice.
    // This is the one place the metre radius meets the sample spacing.
    struct SnowSupportStencil {
      int dx;
      int dz;
    };

    SnowSupportStencil
    snow_support_stencil (const terrain::TerrainDomain& domain) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const auto cells = [] (meters_t radius, meters_t spacing) {
        return std::max (
          1,
          static_cast<int> (std::lround (
            (radius / spacing).numerical_value_in (mp_units::one))));
      };
      return {
        .dx = cells (support_radius, domain.spacing_x ()),
        .dz = cells (support_radius, domain.spacing_z ()),
      };
    }

    // The broad plane the snow rests on: a weighted average of the normals
    // one stencil step away, which is a coarser reading of the ground than
    // the lighting normal at a single cell.
    SurfaceNormal snow_support_normal (const SurfaceGeometry& geometry,
                                       const SnowSupportStencil& stencil,
                                       terrain::TerrainIndex site) {
      const terrain::TerrainDomain& domain = geometry.domain ();
      const auto sample = [&] (int dx, int dz) {
        return spatial::get<terrain::terrain_normal> (
          geometry[domain.shifted (site, dx, dz)]);
      };
      SurfaceNormal support = sample (0, 0) * 4.0f;
      support += (sample (-stencil.dx, 0) + sample (stencil.dx, 0) +
                  sample (0, -stencil.dz) + sample (0, stencil.dz)) *
                 2.0f;
      support +=
        sample (-stencil.dx, -stencil.dz) + sample (stencil.dx, -stencil.dz) +
        sample (-stencil.dx, stencil.dz) + sample (stencil.dx, stencil.dz);
      return support / support.magnitude ();
    }

    void populate_snow_support (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("surface.populate_snow_support");
      const SnowSupportStencil stencil =
        snow_support_stencil (geometry.domain ());
      // Snow answers to how level that broad plane lies, which is the plane
      // projected onto the vertical -- the same reading the habitat rule
      // takes of the ground itself.
      const auto world_up = Vec3 (0.0f, 1.0f, 0.0f) * one;
      constexpr auto level_ground = terrain::terrain_normal[one];
      spatial::for_each_site (geometry, [&] (const auto& site) {
        const SurfaceNormal support =
          snow_support_normal (geometry, stencil, site.index ());
        const auto levelness = std::clamp (
          dot (support, world_up), 0.0f * level_ground, 1.0f * level_ground);
        spatial::get<snow_support> (site) = levelness.magnitude ();
      });
    }

    void recompute_surface_normals (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("map::recompute_normals");
      const terrain::TerrainDomain& domain = geometry.domain ();
      std::ranges::fill (spatial::get<terrain::terrain_normal> (geometry),
                         Vec3 (0, 0, 0) *
                           terrain::terrain_normal[mp_units::one]);

      // One lattice step in world space. This is the only place the cell
      // spacing becomes a number, and it does so once for the whole sweep
      // rather than per corner.
      const float step_x = domain.spacing_x ().numerical_value_in (u::m);
      const float step_z = domain.spacing_z ().numerical_value_in (u::m);

      // A cell corner as a place in the world. The elevation is read at the
      // wrapped position, but the horizontal coordinate does not wrap: a face
      // spanning the seam has to stay continuous, or its normal would fold
      // back on itself there.
      const auto corner = [&] (terrain::TerrainIndex site, int dx, int dz) {
        const float along_x =
          step_x * static_cast<float> (static_cast<int> (site.column) + dx);
        const float along_z =
          step_z * static_cast<float> (static_cast<int> (site.row) + dz);
        const float height = terrain::surface_elevation_value (
          spatial::get<terrain::surface_elevation> (
            geometry[domain.shifted (site, dx, dz)]));
        return position (Vec3 (along_x, height, along_z));
      };

      // The normal of the triangle spanned by two corners and the origin.
      // Crossing two edges of a surface gives a vector along its normal whose
      // length is twice the triangle's area, so dividing by its own magnitude
      // is what leaves a direction and nothing else.
      const auto facet = [&] (terrain::TerrainIndex site,
                              const position_t& origin,
                              int dx1,
                              int dz1,
                              int dx2,
                              int dz2) -> SurfaceNormal {
        const auto spanned = cross (corner (site, dx1, dz1) - origin,
                                    corner (site, dx2, dz2) - origin);
        return spanned / spanned.magnitude () *
               terrain::terrain_normal[mp_units::one];
      };

      const auto add = [&] (terrain::TerrainIndex site,
                            int dx,
                            int dz,
                            const SurfaceNormal& face) {
        spatial::get<terrain::terrain_normal> (
          geometry[domain.shifted (site, dx, dz)]) += face;
      };

      // Each site owns the cell reaching one step further along both axes,
      // split into two triangles whose normals every touched corner
      // accumulates. A corner shared by several faces ends up holding their
      // sum, which points along the average of the surface there.
      for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
        const position_t origin = corner (site, 0, 0);
        const SurfaceNormal left = facet (site, origin, 0, 1, 1, 1);
        const SurfaceNormal right = facet (site, origin, 1, 1, 1, 0);
        add (site, 0, 0, left);
        add (site, 0, 1, left);
        add (site, 1, 1, left);
        add (site, 0, 0, right);
        add (site, 1, 0, right);
        add (site, 1, 1, right);
      }

      // Those sums carry the faces' areas as their length; only the direction
      // is wanted, so each is divided by its own magnitude.
      for (SurfaceNormal& value :
           spatial::get<terrain::terrain_normal> (geometry))
        value = value / value.magnitude ();
    }
  }

  void rebuild_geometry (SurfaceGeometry& geometry) {
    MOPPE_PROFILE_ZONE ("map::rebuild_geometry");
    recompute_surface_normals (geometry);
    populate_snow_support (geometry);
  }

  // ---- Generation ----

  namespace {
    constexpr auto coastline = 0.4f * terrain::continent_shape[one];
    constexpr meters_t initial_land_relief = 20.0f * u::m;
    constexpr meters_t initial_bathymetric_relief = 240.0f * u::m;
    constexpr auto maximum_uplift_m_per_year =
      0.001f * u::m / mp_units::astronomy::Julian_year;

    void reset_material_history (SurfaceGeometry& surface) {
      std::ranges::fill (spatial::get<terrain::sediment_thickness> (surface),
                         0.0f * terrain::sediment_thickness[u::m]);
      std::ranges::fill (spatial::get<eroded_surface_material> (surface),
                         0.0f * eroded_surface_material[u::m]);
      std::ranges::fill (spatial::get<deposited_surface_material> (surface),
                         0.0f * deposited_surface_material[u::m]);
    }

    // The solver answers in linear buffers, so this is where those meet the
    // surface's typed columns. The geological material histories deliberately
    // exclude the later trail earthwork pass.
    void
    set_evolution_result (SurfaceGeometry& surface,
                          const terrain::StreamPowerEvolutionResult& result) {
      const terrain::TerrainDomain& domain = surface.domain ();
      for (const terrain::TerrainIndex site : spatial::sites (surface)) {
        const std::size_t offset = domain.offset (site);
        const auto row = surface[site];
        spatial::get<terrain::surface_elevation> (surface[site]) =
          result.heights[offset];
        spatial::get<terrain::sediment_thickness> (row) =
          result.sediment_thickness[offset];
        spatial::get<eroded_surface_material> (row) +=
          result.eroded_thickness[offset].numerical_value_in (u::m) *
          eroded_surface_material[u::m];
        spatial::get<deposited_surface_material> (row) +=
          result.deposited_thickness[offset].numerical_value_in (u::m) *
          deposited_surface_material[u::m];
      }
    }
  }

  std::vector<meters_per_julian_year_t>
  initialize_terrain (SurfaceGeometry& surface,
                      terrain::Seed seed,
                      meters_t water_datum,
                      const terrain::GeologicalProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.initialize");
    terrain::GeologicalSections geology =
      terrain::generate_geology (surface.domain (), seed, progress);
    std::vector<meters_per_julian_year_t> uplift;
    uplift.reserve (geology.size ());
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const auto ground = geology[site];
      const auto land =
        spatial::get<terrain::continent_shape> (ground) - coastline;
      const meters_t relief =
        land < 0.0f ? initial_bathymetric_relief : initial_land_relief;
      spatial::get<terrain::surface_elevation> (surface[site]) =
        terrain::surface_elevation_point (water_datum + relief * land);
      uplift.push_back (spatial::get<terrain::uplift_weight> (ground) *
                        maximum_uplift_m_per_year);
    }
    reset_material_history (surface);
    return uplift;
  }

  terrain::StreamPowerEvolutionReport
  evolve_terrain (SurfaceGeometry& surface,
                  std::span<const meters_per_julian_year_t> uplift,
                  const terrain::StreamPowerEvolution& parameters,
                  const terrain::StreamPowerProgress& progress) {
    MOPPE_PROFILE_ZONE ("terrain.evolve");
    const auto& initial_sediment =
      spatial::get<terrain::sediment_thickness> (surface);
    terrain::StreamPowerEvolutionResult result = terrain::evolve_stream_power (
      surface, uplift, parameters, progress, {}, initial_sediment);
    set_evolution_result (surface, result);
    return result.report;
  }

  terrain::ChannelFormationResult
  form_river_channels (SurfaceGeometry& surface,
                       meters_t water_datum,
                       const terrain::ChannelFormation& parameters) {
    MOPPE_PROFILE_ZONE ("terrain.form_channels");
    // The rivers as the finished world will find them, but for the beds
    // this is about to give them.
    const terrain::FloodField flood = terrain::analyze_standing_water (
      surface, water_datum.numerical_value_in (u::m));
    const terrain::LakeCensus lakes = terrain::census_lakes (flood);
    const terrain::DrainageGraph drainage =
      terrain::analyze_wet_drainage (flood, lakes);
    const terrain::RiverNetwork rivers = terrain::extract_river_network (
      flood,
      lakes,
      drainage,
      terrain::analyze_fractional_drainage (flood, lakes),
      terrain::visible_river_minimum_area ());
    terrain::ChannelFormationResult result = terrain::form_channels (
      surface, flood, lakes, drainage, rivers, parameters);
    const terrain::TerrainDomain& domain = surface.domain ();
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const std::size_t offset = domain.offset (site);
      const float change = result.change_m[offset];
      if (change == 0.0f)
        continue;
      const auto row = surface[site];
      spatial::get<terrain::surface_elevation> (row) = result.heights[offset];
      // What a river lays down is loose; what it cuts, it cuts from the
      // loose cover first.
      auto& sediment = spatial::get<terrain::sediment_thickness> (row);
      if (change > 0.0f) {
        sediment += change * terrain::sediment_thickness[u::m];
        spatial::get<deposited_surface_material> (row) +=
          change * deposited_surface_material[u::m];
      } else {
        sediment =
          std::max (sediment + change * terrain::sediment_thickness[u::m],
                    0.0f * terrain::sediment_thickness[u::m]);
        spatial::get<eroded_surface_material> (row) +=
          -change * eroded_surface_material[u::m];
      }
    }
    return result;
  }

  terrain::TrailNetwork
  form_terrain_trails (SurfaceGeometry& surface,
                       const terrain::TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("terrain.form_trails");
    terrain::TrailFormationResult result =
      terrain::form_trails (surface, parameters);
    const terrain::TerrainDomain& domain = surface.domain ();
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const SurfaceElevation formed = result.heights[domain.offset (site)];
      auto& elevation =
        spatial::get<terrain::surface_elevation> (surface[site]);
      elevation = formed;
    }
    return std::move (result.network);
  }

  // ---- Readings ----

  namespace {
    // Quantities of one spec are ordered, so the percentile never has to
    // leave the column's own type: nth_element compares them directly and
    // the scale that comes back divides its own readings.
    template <typename Values>
    auto robust_positive_scale (const Values& values) {
      using Value = std::ranges::range_value_t<Values>;
      constexpr Value zero {};
      std::vector<Value> positive;
      positive.reserve (values.size () / 8);
      for (const Value value : values)
        if (value > zero)
          positive.push_back (value);
      if (positive.empty ())
        return 1.0f * Value::reference;
      const std::size_t rank = positive.size () * 49 / 50;
      std::nth_element (
        positive.begin (), positive.begin () + rank, positive.end ());
      return std::max (positive[rank], 1e-6f * Value::reference);
    }
  }

  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry) {
    const auto& eroded = spatial::get<eroded_surface_material> (geometry);
    const auto& deposited = spatial::get<deposited_surface_material> (geometry);
    const auto eroded_scale = robust_positive_scale (eroded);
    const auto deposited_scale = robust_positive_scale (deposited);
    GeologyMaterials values (geometry.domain ());
    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto ground = geometry[site];
      const auto reading = values[site];
      // A reading over the scale drawn from its own column is a ratio, and a
      // ratio of two like quantities has no unit to speak of.
      const auto share = [] (auto value, auto scale) {
        return std::clamp (
          (value / scale).numerical_value_in (mp_units::one), 0.0f, 1.0f);
      };
      spatial::get<erosion_exposure> (reading) =
        share (spatial::get<eroded_surface_material> (ground), eroded_scale) *
        erosion_exposure[one];
      spatial::get<deposition_cover> (reading) =
        share (spatial::get<deposited_surface_material> (ground),
               deposited_scale) *
        deposition_cover[one];
    }
    return values;
  }

  namespace {
    // A field smoothed over about RADIUS on the world torus: two passes of
    // a box along each axis, which is close to a tent of twice the radius.
    // A plant answers to a stand's ground, not a sample's, so vegetation
    // reads the ground through this rather than cell by cell.
    std::vector<float> smoothed (std::vector<float> field,
                                 const terrain::TerrainDomain& domain,
                                 meters_t radius) {
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      const auto cells = [] (meters_t reach, meters_t spacing) {
        return std::max (1,
                         static_cast<int> (std::lround (
                           (reach / spacing).numerical_value_in (one))));
      };
      const int rx = cells (radius, domain.spacing_x ());
      const int rz = cells (radius, domain.spacing_z ());
      std::vector<float> line;
      const auto pass =
        [&] (int count, int stride, int lines, int step, int reach) {
          line.resize (static_cast<std::size_t> (count));
          const float scale = 1.0f / static_cast<float> (2 * reach + 1);
          const auto wrap = [count] (int at) {
            at %= count;
            return at < 0 ? at + count : at;
          };
          for (int l = 0; l < lines; ++l) {
            float* base =
              field.data () + static_cast<std::ptrdiff_t> (l) * step;
            const auto at = [&] (int i) -> float& {
              return base[static_cast<std::ptrdiff_t> (wrap (i)) * stride];
            };
            float sum = 0.0f;
            for (int k = -reach; k <= reach; ++k)
              sum += at (k);
            for (int i = 0; i < count; ++i) {
              line[static_cast<std::size_t> (i)] = sum * scale;
              sum += at (i + reach + 1) - at (i - reach);
            }
            for (int i = 0; i < count; ++i)
              at (i) = line[static_cast<std::size_t> (i)];
          }
        };
      for (int twice = 0; twice < 2; ++twice) {
        pass (width, 1, height, width, rx);
        pass (height, width, width, 1, rz);
      }
      return field;
    }

    // Value noise of OCTAVES octaves from PATCHES a lap, 0 to 1.
    float layered_noise (terrain::TerrainDomain::LapPosition where,
                         std::uint32_t patches,
                         std::uint32_t seed,
                         std::uint32_t octaves = 4) {
      float sum = 0.0f;
      float amplitude = 1.0f;
      float norm = 0.0f;
      for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += amplitude *
               periodic_noise (
                 where.along_x, where.along_z, patches << octave, seed + octave)
                 .numerical_value_in (one);
        norm += amplitude;
        amplitude *= 0.5f;
      }
      return sum / norm;
    }

    float rises (float edge0, float edge1, float value) {
      return smoothstep (edge0, edge1, value);
    }

    float falls (float edge0, float edge1, float value) {
      return 1.0f - smoothstep (edge0, edge1, value);
    }

    // The hidden quantities at one place, each 0 to 1, as the plants read
    // them: a stand's worth of ground, not a sample's.
    struct Site {
      float wet;       // soil water: the wetness index, smoothed
      float dry;       // share of the stand that is dry
      float soaked;    // share of the stand that is soaked
      float thin;      // share of the stand with bedrock within a span
      float peat;      // share of the stand under deep wet soil
      float poor;      // thin and dry together: sand and rock
      float level;     // flat enough to hold water
      float limestone; // on the limestone sheet
      float cold;      // height, north aspect, and frost hollows
      float exposure;  // ridges and high ground, open to the wind
    };

    // A species' tolerance of a site, 0 to 1. Each curve is wide where the
    // species will grow at all; where it wins is decided by competition.
    struct Species {
      float strength; // how hard it pushes the others out where both grow
      float (*tolerance) (const Site&, float light);
      std::uint32_t lane;
    };

    // Trees. Spruce shades out everything where the soil is deep and damp
    // and the summer long enough; pine grows almost anywhere below the
    // fjäll but wins only where the others cannot, on dry sand and rock
    // and in the bog; birch grows wherever it is not too dry or cold and
    // wins at the edges; the fell birch holds the treeline.
    constexpr Species canopy[] = {
      { 1.00f,
        [] (const Site& at, float) {
          return rises (0.20f, 0.65f, 1.0f - at.thin) *
                 rises (0.08f, 0.18f, at.wet) * falls (0.45f, 0.75f, at.wet) *
                 falls (0.30f, 0.55f, at.cold) * falls (0.30f, 0.80f, at.peat) *
                 (1.0f - 0.85f * at.poor) *
                 (1.0f - 0.6f * at.limestone * at.thin);
        },
        0x5b1ceU },
      { 0.80f,
        [] (const Site& at, float) {
          const float poor = std::max (at.poor, rises (0.20f, 0.60f, at.peat));
          return falls (0.55f, 0.80f, at.cold) * (0.40f + 0.60f * poor);
        },
        0x9a1e5U },
      { 0.52f,
        [] (const Site& at, float) {
          return 0.9f * falls (0.45f, 0.75f, at.cold) *
                 rises (0.05f, 0.14f, at.wet) * falls (0.50f, 0.90f, at.peat) *
                 (1.0f - 0.6f * at.poor);
        },
        0xb12c4U },
      { 0.95f,
        [] (const Site& at, float) {
          return rises (0.25f, 0.50f, at.cold) * falls (0.65f, 0.85f, at.cold) *
                 (1.0f - 0.5f * at.exposure) * falls (0.6f, 0.95f, at.soaked);
        },
        0x3f3b1U },
    };
    // How much light each tree's canopy takes from the ground.
    constexpr float canopy_shade[] = { 0.90f, 0.40f, 0.55f, 0.45f };

    // The field layer, in the light the trees leave.
    constexpr Species field[] = {
      // Reindeer lichen: dry, thin, acid ground in good light.
      { 0.75f,
        [] (const Site& at, float light) {
          return at.poor * (1.0f - 0.85f * at.limestone) *
                 rises (0.30f, 0.65f, light);
        },
        0x11c4e5U },
      // Heather and crowberry: open, poor, acid ground, and the wind.
      { 0.85f,
        [] (const Site& at, float light) {
          return rises (0.45f, 0.75f, light) * falls (0.30f, 0.55f, at.wet) *
                 (1.0f - 0.7f * at.limestone) *
                 (0.35f + 0.65f * std::max ({ at.thin,
                                              at.exposure,
                                              rises (0.4f, 0.7f, at.cold) }));
        },
        0x4ea74U },
      // Bilberry: the half-shade of the conifers, damp acid humus.
      { 0.70f,
        [] (const Site& at, float light) {
          return rises (0.08f, 0.25f, light) * falls (0.55f, 0.85f, light) *
                 rises (0.08f, 0.18f, at.wet) * falls (0.45f, 0.70f, at.wet) *
                 (1.0f - 0.6f * at.limestone) * falls (0.82f, 1.0f, at.cold);
        },
        0xb1bb3U },
      // Sphagnum: level ground the water cannot leave. It grows its own
      // ground, so once it is there little else holds.
      { 1.25f,
        [] (const Site& at, float light) {
          return at.level *
                 std::max (rises (0.20f, 0.50f, at.soaked),
                           rises (0.25f, 0.60f, at.peat)) *
                 rises (0.20f, 0.50f, light) *
                 std::max (1.0f - at.limestone, at.peat);
        },
        0x5a6c1U },
      // Dwarf birch: cold, damp, open ground of the fjäll.
      { 0.90f,
        [] (const Site& at, float light) {
          return rises (0.40f, 0.70f, at.cold) * rises (0.12f, 0.28f, at.wet) *
                 rises (0.40f, 0.70f, light);
        },
        0xdb1c7U },
      // Sedge: the wet margins in the open.
      { 0.75f,
        [] (const Site& at, float light) {
          return rises (0.15f, 0.45f, at.soaked) * rises (0.40f, 0.70f, light);
        },
        0x5ed6eU },
      // The alvar's short turf on bare limestone, which drains through its
      // karst.
      { 0.92f,
        [] (const Site& at, float light) {
          return at.limestone * rises (0.42f, 0.62f, at.thin) *
                 rises (0.25f, 0.50f, at.dry) *
                 falls (0.15f, 0.45f, at.soaked) * rises (0.40f, 0.70f, light) *
                 falls (0.70f, 0.90f, at.cold);
        },
        0xa17a2U },
      // Grass and herbs: deep, moist, fertile soil in the light. Not stored:
      // the ground draws turf wherever nothing else holds it.
      { 0.90f,
        [] (const Site& at, float light) {
          return rises (0.25f, 0.70f, 1.0f - at.thin) *
                 rises (0.08f, 0.22f, at.wet) * falls (0.50f, 0.80f, at.wet) *
                 rises (0.30f, 0.60f, light) * falls (0.60f, 0.85f, at.cold);
        },
        0x9a55eU },
    };

    // Competition: each species' claim is its tolerance times its strength,
    // a little frayed by chance (where seed happened to land and take), and
    // the claims share the ground in proportion to their cubes, so the
    // strongest dominates without the others vanishing at its edge. What a
    // species holds is its share times how well it grows there.
    template <std::size_t N>
    std::array<float, N> compete (const std::array<float, N>& tolerance,
                                  const Species (&species)[N],
                                  const std::array<float, N>& chance) {
      std::array<float, N> claim {};
      float total = 1e-6f;
      for (std::size_t s = 0; s < N; ++s) {
        const float c = tolerance[s] * species[s].strength * chance[s];
        claim[s] = c * c * c;
        total += claim[s];
      }
      std::array<float, N> held {};
      for (std::size_t s = 0; s < N; ++s)
        held[s] = claim[s] / total * tolerance[s];
      return held;
    }
  }

  VegetationMap analyze_vegetation (const SurfaceGeometry& geometry,
                                    const terrain::MoistureMap& moisture,
                                    meters_t water_level,
                                    meters_t land_top,
                                    std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("map::analyze_vegetation");
    if (moisture.domain () != geometry.domain ())
      throw std::invalid_argument (
        "Moisture does not share the surface domain");
    using spatial::get;
    const terrain::TerrainDomain& domain = geometry.domain ();
    const std::size_t count = domain.size ();

    // How deep the world's history cut each place: what it took away less
    // what it laid back. The limestone's thickness is counted against the
    // median of that cut, so a calm world keeps as much of its cover as a
    // violent one.
    const auto& eroded = get<eroded_surface_material> (geometry);
    const auto& deposited = get<deposited_surface_material> (geometry);
    const auto& sediment = get<terrain::sediment_thickness> (geometry);
    const auto& normals = get<terrain::terrain_normal> (geometry);
    const auto& elevation = get<terrain::surface_elevation> (geometry);
    const auto& wetness = get<terrain::soil_wetness> (moisture);
    std::vector<float> cut (count), wet (count), thin (count), dry (count),
      soaked (count), peat (count), up (count), south (count), height (count);
    for (std::size_t i = 0; i < count; ++i) {
      cut[i] = eroded[i].numerical_value_in (u::m) -
               deposited[i].numerical_value_in (u::m);
      const float soil = sediment[i].numerical_value_in (u::m);
      wet[i] = wetness[i].numerical_value_in (one);
      // Under a span of soil a root reaches bedrock; over two and a half
      // metres of it the ground is peat if it is also wet.
      thin[i] = 1.0f - smoothstep (0.15f, 0.60f, soil);
      peat[i] =
        smoothstep (0.80f, 2.50f, soil) * smoothstep (0.2f, 0.4f, wet[i]);
      dry[i] = 1.0f - smoothstep (0.12f, 0.28f, wet[i]);
      soaked[i] = smoothstep (0.30f, 0.60f, wet[i]);
      const Vec3 normal = normals[i].numerical_value_in (one);
      up[i] = normal[1];
      // North is -z, so a slope facing the sun leans its normal toward +z.
      south[i] = normal[2];
      height[i] = terrain::surface_elevation_value (elevation[i]);
    }
    std::vector<float> positive;
    positive.reserve (count);
    for (const float value : cut)
      if (value > 0.0f)
        positive.push_back (value);
    float median_cut = 1.0f;
    if (!positive.empty ()) {
      const auto middle = positive.begin () + positive.size () / 2;
      std::nth_element (positive.begin (), middle, positive.end ());
      median_cut = std::max (*middle, 0.5f);
    }
    cut = smoothed (std::move (cut), domain, 10.0f * u::m);
    wet = smoothed (std::move (wet), domain, 12.0f * u::m);
    thin = smoothed (std::move (thin), domain, 25.0f * u::m);
    dry = smoothed (std::move (dry), domain, 25.0f * u::m);
    soaked = smoothed (std::move (soaked), domain, 25.0f * u::m);
    peat = smoothed (std::move (peat), domain, 25.0f * u::m);
    up = smoothed (std::move (up), domain, 25.0f * u::m);
    south = smoothed (std::move (south), domain, 25.0f * u::m);
    // The ground's height against its neighbourhood's: a ridge stands
    // above it, a hollow below, where cold air settles on still nights.
    const std::vector<float> around = smoothed (height, domain, 75.0f * u::m);

    const auto laps = [&] (float patch_m) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (std::lround (
          domain.period_x ().numerical_value_in (u::m) / patch_m)));
    };
    const std::uint32_t sheet_laps = laps (1700.0f);
    const std::uint32_t chance_laps = laps (160.0f);
    const float sea = water_level.numerical_value_in (u::m);
    const float relief =
      std::max ((land_top - water_level).numerical_value_in (u::m), 1.0f);
    const float edge = std::max (0.06f * median_cut, 0.5f);

    VegetationMap values (domain);
    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const std::size_t i = domain.offset (site);
      const auto where = domain.lap_position (site);
      const float thickness =
        median_cut *
        (0.25f + 1.0f * layered_noise (where, sheet_laps, seed ^ 0x11a7c0deU));
      const float rise = (height[i] - sea) / relief;
      const float above = height[i] - around[i];
      const Site at {
        .wet = wet[i],
        .dry = dry[i],
        .soaked = soaked[i],
        .thin = thin[i],
        .peat = peat[i],
        .poor = smoothstep (0.44f, 0.60f, 0.5f * (thin[i] + dry[i])),
        .level = smoothstep (0.95f, 0.985f, up[i]),
        .limestone = smoothstep (-edge, edge, thickness - cut[i]),
        .cold = smoothstep (0.40f,
                            0.90f,
                            rise - 0.20f * south[i] +
                              0.05f * smoothstep (0.0f, 8.0f, -above)),
        .exposure = std::clamp (0.7f * smoothstep (0.0f, 8.0f, above) +
                                  0.6f * smoothstep (0.45f, 0.9f, rise),
                                0.0f,
                                1.0f),
      };
      // Steep ground holds little of anything rooted.
      const float footing = smoothstep (0.70f, 0.85f, up[i]);
      const auto chances = [&] (const auto& species) {
        std::array<float, std::size (species)> chance {};
        for (std::size_t s = 0; s < chance.size (); ++s)
          chance[s] =
            0.75f + 0.5f * layered_noise (
                             where, chance_laps, seed ^ species[s].lane, 2);
        return chance;
      };

      std::array<float, std::size (canopy)> trees {};
      for (std::size_t s = 0; s < trees.size (); ++s)
        trees[s] = canopy[s].tolerance (at, 1.0f) * footing;
      const auto held = compete (trees, canopy, chances (canopy));
      float shade = 0.0f;
      for (std::size_t s = 0; s < held.size (); ++s)
        shade += canopy_shade[s] * held[s];
      const float light = std::clamp (1.0f - shade, 0.0f, 1.0f);

      std::array<float, std::size (field)> plants {};
      for (std::size_t s = 0; s < plants.size (); ++s)
        plants[s] = field[s].tolerance (at, light) * std::max (footing, 0.3f);
      const auto ground = compete (plants, field, chances (field));

      const auto reading = values[site];
      get<limestone_cover> (reading) = at.limestone * limestone_cover[one];
      get<spruce_abundance> (reading) = held[0] * spruce_abundance[one];
      get<pine_abundance> (reading) = held[1] * pine_abundance[one];
      get<birch_abundance> (reading) = held[2] * birch_abundance[one];
      get<mountain_birch_abundance> (reading) =
        held[3] * mountain_birch_abundance[one];
      get<lichen_abundance> (reading) = ground[0] * lichen_abundance[one];
      get<heather_abundance> (reading) = ground[1] * heather_abundance[one];
      get<bilberry_abundance> (reading) = ground[2] * bilberry_abundance[one];
      get<sphagnum_abundance> (reading) = ground[3] * sphagnum_abundance[one];
      get<dwarf_birch_abundance> (reading) =
        ground[4] * dwarf_birch_abundance[one];
      get<sedge_abundance> (reading) = ground[5] * sedge_abundance[one];
      get<alvar_abundance> (reading) = ground[6] * alvar_abundance[one];
    }
    return values;
  }

  TreeHabitatMap analyze_tree_habitat (const SurfaceGeometry& geometry,
                                       const terrain::MoistureMap& moisture,
                                       meters_t water_level,
                                       meters_t tree_tops) {
    if (moisture.domain () != geometry.domain ())
      throw std::invalid_argument (
        "Moisture does not share the surface domain");

    using spatial::get;
    using u::m;

    if (tree_tops <= water_level + 20.0f * m)
      throw std::invalid_argument (
        "Tree line must leave a terrestrial habitat band");

    TreeHabitatMap values (geometry.domain ());

    // How level the ground lies is the ground normal projected onto the
    // vertical: a normal pointing straight up gives one, a cliff face gives
    // nothing. Saying it as a dot product means no one has to know which lane
    // of the vector the vertical happens to be stored in.
    const auto world_up = Vec3 (0.0f, 1.0f, 0.0f) * one;
    constexpr auto level_ground = terrain::terrain_normal[one];

    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto ground = geometry[site];
      const auto ground_normal = get<terrain::terrain_normal> (ground);
      const auto ground_level =
        get<terrain::surface_elevation> (ground).quantity_from_zero ();

      const auto ground_horizontality = dot (ground_normal, world_up);

      const auto soil_stability =
        band (0.72f * level_ground, 0.96f * level_ground, ground_horizontality);

      const auto soil_dryness =
        band (water_level + 3.0f * m, water_level + 18.0f * m, ground_level);

      // Trees thin out as the ground approaches the tree line and stop at
      // it. The band rises from 0 to 1 across the last 35 m of climb, so one
      // minus it is the headroom still left below the line: full well down
      // the slope, nothing at the top.
      const auto tree_line_headroom =
        1 - band (tree_tops - 35.0f * m, tree_tops, ground_level);

      // What the ground holds, not how near it is to open water: a bank a
      // step from the river can be a dry cut, and a flat two hundred metres
      // away can be a bog. The tree is standing in soil, not beside a view.
      const auto wetness = get<terrain::soil_wetness> (moisture[site]);

      // Where these bands sit is a reading of the index's own distribution
      // over a generated world: the driest tenth of the ground sits at zero,
      // the median hillside near a fifth, and the wettest tenth is standing
      // water and the flats around it. So trees want more than a bare ridge
      // and less than a bog, and almost every hillside qualifies.
      const auto hydrated = band (0.05f * one, 0.20f * one, wetness);
      const auto not_sodden = 1 - band (0.60f * one, 0.88f * one, wetness);
      const auto water_response = 0.28f + 0.72f * hydrated * not_sodden;
      const auto treeishness =
        soil_dryness * tree_line_headroom * soil_stability * water_response;

      get<tree_habitat> (values[site]) = treeishness * tree_habitat[one];
    }
    return values;
  }

  // How much canopy the world wants at each place, from 0 for bare ground to
  // 1 for closed forest.
  //
  // Nothing here plants a tree. Placement samples this field later and turns
  // it into three separate things: whether a candidate site gets a tree at
  // all, how tall that tree grows, and how its foliage is tinted. So this is
  // a density the world is asking for, not a probability -- forest.cc derives
  // the probability from it with a threshold of its own.
  ForestCoverMap analyze_forest_cover (const TreeHabitatMap& habitat,
                                       const terrain::TrailUseMap& use,
                                       const VegetationMap& vegetation,
                                       std::uint32_t seed) {
    if (use.domain () != habitat.domain () ||
        vegetation.domain () != habitat.domain ())
      throw std::invalid_argument (
        "Trail use or vegetation do not share the surface domain");

    using spatial::get;

    const terrain::TerrainDomain& domain = habitat.domain ();
    ForestCoverMap values (domain);

    // Woodland is patchy at more than one scale: a few large stands per lap
    // of the world, with smaller breaks inside them. Each count is how many
    // patches fit around the world, which is also the period the field wraps
    // on -- one number now, so the two cannot drift apart.
    constexpr std::uint32_t stands_per_lap = 7;
    constexpr std::uint32_t breaks_per_lap = 23;
    constexpr auto signal = noise_signal[one];

    for (const terrain::TerrainIndex site : spatial::sites (habitat)) {
      const auto where = domain.lap_position (site);

      const noise_signal_t stands = periodic_noise (
        where.along_x, where.along_z, stands_per_lap, seed ^ 0x4b1d9e37U);
      const noise_signal_t breaks = periodic_noise (
        where.along_x, where.along_z, breaks_per_lap, seed ^ 0x91e10da5U);
      const noise_signal_t mosaic = 0.72f * stands + 0.28f * breaks;

      // Habitat raised slightly above one: good ground stays good, and
      // marginal ground gives up a little faster than it otherwise would.
      const auto habitable = cr::pow (
        get<tree_habitat> (habitat[site]).numerical_value_in (one), 1.15f);

      // The ground decides how hard the mosaic has to try. Multiplying the
      // two instead would make the noise an equal author of where the forest
      // is, and a wet corridor would go bare as often as a dry ridge. Here a
      // sheltered damp hollow closes canopy on a low roll and a dry shoulder
      // needs a high one, so the stands land where the water does and the
      // noise only says where their edges fall.
      const auto reluctant = 0.74f * signal;
      const auto eager = 0.30f * signal;
      const auto gate = reluctant + (eager - reluctant) * habitable;

      // Most of the mosaic is either forest or clearing; the band is narrow
      // so the edges between them stay edges instead of a long gradient.
      const auto seeded =
        band (gate - 0.085f * signal, gate + 0.085f * signal, mosaic);

      // A route keeps almost all canopy off a corridor about its formed
      // width; a settlement clears its ground completely.
      const auto trodden = use[site];
      constexpr auto trail = map::trail_influence[one];
      const auto route_clearance =
        1 - 0.96f * band (0.35f * trail,
                          0.65f * trail,
                          get<trail_influence> (trodden));
      const auto settled_clearance = 1 - get<home_base_influence> (trodden);

      // Every factor is a soft yes between 0 and 1, and multiplying them is
      // a soft "and": canopy needs habitable ground AND a seeded patch AND no
      // route AND no settlement. Any one of them near zero vetoes the rest,
      // which is why a place can fail four mild tests and still come out
      // bare. Adding them would say "any of these will do", which is not how
      // a forest works.
      // Habitat has already chosen where the stands are; here it only says
      // how thickly they grow, which is what placement reads for density,
      // height, and tint.
      const auto thickness = 0.45f + 0.55f * habitable;
      // How closed a canopy the trees that hold this place make: spruce
      // closes it, birch nearly, while pine and the fell birch stand open
      // and the forest plan plants them itself.
      const auto plants = vegetation[site];
      const float closing =
        get<spruce_abundance> (plants).numerical_value_in (one) +
        0.8f * get<birch_abundance> (plants).numerical_value_in (one);
      const auto wanted = thickness * seeded * route_clearance *
                          settled_clearance *
                          smoothstep (0.08f, 0.45f, closing);

      // The bands are in range by construction, but the trail and home-base
      // readings are stored data, and a clearance is one minus one of those.
      // The clamp guards the inputs this rule did not compute, not its own
      // arithmetic.
      get<forest_cover> (values[site]) =
        std::clamp (wanted.numerical_value_in (one), 0.0f, 1.0f) *
        forest_cover[one];
    }
    return values;
  }

  // ---- Cache ----

  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    return spatial::load_bundle (file, geometry);
  }

  void save_cache (const SurfaceGeometry& geometry, const std::string& path) {
    std::ofstream file (path, std::ios::binary);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
    spatial::write_bundle (file, geometry);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
  }
}

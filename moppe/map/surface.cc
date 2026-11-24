#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/map/surface.hh>

#include <moppe/parallel.hh>
#include <moppe/profile.hh>
#include <moppe/spatial/bundle_operations.hh>
#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>
#include <moppe/terrain/glacial.hh>
#include <moppe/terrain/lithology.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <type_traits>
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

      // One lattice step in world space, read once for the whole sweep
      // rather than per corner.
      const meters_t step_x = domain.spacing_x ();
      const meters_t step_z = domain.spacing_z ();

      // A cell corner as a place in the world. The elevation is read at the
      // wrapped position, but the horizontal coordinate does not wrap: a face
      // spanning the seam has to stay continuous, or its normal would fold
      // back on itself there.
      const auto corner = [&] (terrain::TerrainIndex site, int dx, int dz) {
        const meters_t along_x =
          step_x * static_cast<float> (static_cast<int> (site.column) + dx);
        const meters_t along_z =
          step_z * static_cast<float> (static_cast<int> (site.row) + dz);
        const SurfaceElevation height =
          spatial::get<terrain::surface_elevation> (
            geometry[domain.shifted (site, dx, dz)]);
        return terrain::at_elevation (plan_position (along_x, along_z), height);
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

  SurfaceNormal ground_normal_at (const SurfaceGeometry& surface,
                                  const position_t& at) {
    if (!spatial::get<terrain::terrain_normal> (surface).empty ())
      return spatial::sample<terrain::terrain_normal> (surface, at);
    const auto& d = surface.domain ();
    const auto& h = spatial::get<terrain::surface_elevation> (surface);
    return spatial::sample_field (d, at, [&] (terrain::TerrainIndex site) {
      const auto centre = h[d.offset (site)];
      const auto edge = [&] (int x, int z) {
        return displacement (
          Vec3 (meters_value (x * d.spacing_x ()),
                (h[d.offset (d.shifted (site, x, z))] - centre)
                  .numerical_value_in (u::m),
                meters_value (z * d.spacing_z ())));
      };
      const std::array edges { edge (0, 1),  edge (1, 1),   edge (1, 0),
                               edge (0, -1), edge (-1, -1), edge (-1, 0) };
      SurfaceNormal sum {};
      for (unsigned i = 0; i < edges.size (); ++i) {
        auto face = cross (edges[i], edges[(i + 1) % edges.size ()]);
        sum += face / face.magnitude () * terrain::terrain_normal[one];
      }
      return sum / sum.magnitude ();
    });
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
      0.001f * u::m / moppe::astronomy::Julian_year;

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
      const meters_t relief = land < 0.0f * terrain::continent_shape[one]
                                ? initial_bathymetric_relief
                                : initial_land_relief;
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
    // First analyze pre-glacial drainage and carve Scandinavian glacial
    // U-troughs (grading the main stem smoothly from highlands to sea while
    // leaving side tributaries perched as hanging-valley waterfalls), alpine
    // cirques, and cold-based vidda shelves.
    const terrain::SurfaceElevation sea_level =
      terrain::surface_elevation_point (water_datum);
    {
      const terrain::FloodField pre_flood =
        terrain::analyze_standing_water (surface, sea_level);
      const terrain::LakeCensus pre_lakes = terrain::census_lakes (pre_flood);
      const terrain::DrainageGraph pre_drainage =
        terrain::analyze_wet_drainage (pre_flood, pre_lakes);
      const terrain::RiverNetwork pre_rivers = terrain::extract_river_network (
        pre_flood,
        pre_lakes,
        pre_drainage,
        terrain::analyze_fractional_drainage (pre_flood, pre_lakes),
        terrain::visible_river_minimum_area ());
      const terrain::GlacialSculptingResult glacial =
        terrain::sculpt_glacial_valleys (
          surface, pre_flood, pre_lakes, pre_drainage, pre_rivers, sea_level);
      if (glacial.troughs > 0 || glacial.snowfields > 0 || glacial.horns > 0 ||
          glacial.cirques > 0) {
        const terrain::TerrainDomain& domain = surface.domain ();
        for (const terrain::TerrainIndex site : spatial::sites (surface)) {
          const std::size_t offset = domain.offset (site);
          const meters_t change =
            quantity_cast<isq::length> (glacial.change[offset]);
          const terrain::SedimentThickness outwash = glacial.outwash[offset];
          if (change == 0.0f * u::m &&
              outwash == 0.0f * terrain::sediment_thickness[u::m])
            continue;
          const auto row = surface[site];
          if (change != 0.0f * u::m) {
            spatial::get<terrain::surface_elevation> (row) =
              glacial.heights[offset];
            auto& sediment = spatial::get<terrain::sediment_thickness> (row);
            if (change < 0.0f * u::m) {
              sediment = std::max (
                sediment + quantity_cast<terrain::sediment_thickness> (change),
                0.0f * terrain::sediment_thickness[u::m]);
              spatial::get<eroded_surface_material> (row) +=
                quantity_cast<eroded_surface_material> (-change);
            }
          }
          if (outwash > 0.0f * terrain::sediment_thickness[u::m]) {
            auto& sediment = spatial::get<terrain::sediment_thickness> (row);
            if (sediment < outwash) {
              const meters_t added =
                quantity_cast<isq::length> (outwash - sediment);
              sediment = outwash;
              spatial::get<deposited_surface_material> (row) +=
                quantity_cast<deposited_surface_material> (added);
            }
          }
        }
      }
    }

    // Re-analyze hydrology on the glaciated relief and carve the post-glacial
    // navigable river beds, plunge pools, and submarine estuaries.
    const terrain::FloodField flood =
      terrain::analyze_standing_water (surface, sea_level);
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
      const meters_t change =
        quantity_cast<isq::length> (result.change[offset]);
      if (change == 0.0f * u::m)
        continue;
      const auto row = surface[site];
      spatial::get<terrain::surface_elevation> (row) = result.heights[offset];
      // What a river lays down is loose; what it cuts, it cuts from the
      // loose cover first.
      auto& sediment = spatial::get<terrain::sediment_thickness> (row);
      if (change > 0.0f * u::m) {
        sediment += quantity_cast<terrain::sediment_thickness> (change);
        spatial::get<deposited_surface_material> (row) +=
          quantity_cast<deposited_surface_material> (change);
      } else {
        sediment = std::max (
          sediment + quantity_cast<terrain::sediment_thickness> (change),
          0.0f * terrain::sediment_thickness[u::m]);
        spatial::get<eroded_surface_material> (row) +=
          quantity_cast<eroded_surface_material> (-change);
      }
    }
    return result;
  }

  terrain::RoadFormationResult
  form_terrain_roads (SurfaceGeometry& surface,
                      terrain::RoadNetwork& roads,
                      const terrain::FloodField& flood,
                      const terrain::DrainageGraph& drainage,
                      const terrain::RoadFormation& parameters) {
    MOPPE_PROFILE_ZONE ("terrain.form_roads");
    terrain::RoadFormationResult result =
      terrain::form_roads (surface.domain (),
                           spatial::get<terrain::surface_elevation> (surface),
                           roads,
                           flood,
                           drainage,
                           parameters);
    const terrain::TerrainDomain& domain = surface.domain ();
    for (const terrain::TerrainIndex site : spatial::sites (surface)) {
      const std::size_t offset = domain.offset (site);
      const meters_t change =
        quantity_cast<isq::length> (result.change[offset]);
      if (change == 0.0f * u::m)
        continue;
      const auto row = surface[site];
      spatial::get<terrain::surface_elevation> (row) = result.heights[offset];
      // A cutting bares what lay under the cover; an embankment is made
      // ground.
      auto& sediment = spatial::get<terrain::sediment_thickness> (row);
      if (change > 0.0f * u::m) {
        sediment += quantity_cast<terrain::sediment_thickness> (change);
        spatial::get<deposited_surface_material> (row) +=
          quantity_cast<deposited_surface_material> (change);
      } else {
        sediment = std::max (
          sediment + quantity_cast<terrain::sediment_thickness> (change),
          0.0f * terrain::sediment_thickness[u::m]);
        spatial::get<eroded_surface_material> (row) +=
          quantity_cast<eroded_surface_material> (-change);
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
    template <typename Body>
    void parallel_sites (const terrain::TerrainDomain& domain, Body body) {
      parallel_for (domain.size (), 8192, [&] (size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
          body (domain.index (i));
      });
    }

    // A field smoothed over about RADIUS on the world torus: two passes of
    // a box along each axis, which is close to a tent of twice the radius.
    // A plant answers to a stand's ground, not a sample's, so vegetation
    // reads the ground through this rather than cell by cell.
    template <typename Value>
    std::vector<Value> smoothed (std::vector<Value> field,
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
      std::vector<Value> output (field.size ());
      const float wx = 1.0f / static_cast<float> (2 * rx + 1);
      const float wz = 1.0f / static_cast<float> (2 * rz + 1);
      for (int twice = 0; twice < 2; ++twice) {
        parallel_for (height, 16, [&] (size_t begin, size_t end) {
          for (size_t z = begin; z < end; ++z) {
            const Value* row = field.data () + z * width;
            Value sum = Value::zero ();
            for (int k = -rx; k <= rx; ++k)
              sum += row[(k % width + width) % width];
            int left = (width - rx % width) % width, right = (rx + 1) % width;
            for (int x = 0; x < width; ++x) {
              output[z * width + x] = sum * wx;
              sum += row[right] - row[left];
              if (++left == width)
                left = 0;
              if (++right == width)
                right = 0;
            }
          }
        });
        // Adjacent columns travel together. Even at 4096 samples the
        // vertical pass reads whole cache lines and keeps each column's
        // running sum in exactly the order used by the serial filter.
        constexpr size_t tile = 64;
        parallel_for (
          (width + tile - 1) / tile, 2, [&] (size_t begin, size_t end) {
            for (size_t block = begin; block < end; ++block) {
              const size_t x = block * tile;
              const size_t lanes = std::min (tile, size_t (width) - x);
              std::array<Value, tile> sum {};
              for (int k = -rz; k <= rz; ++k) {
                const int row = (k % height + height) % height;
                for (size_t j = 0; j < lanes; ++j)
                  sum[j] += output[size_t (row) * width + x + j];
              }
              int top = (height - rz % height) % height,
                  bottom = (rz + 1) % height;
              for (int z = 0; z < height; ++z) {
                for (size_t j = 0; j < lanes; ++j) {
                  field[size_t (z) * width + x + j] = sum[j] * wz;
                  sum[j] += output[size_t (bottom) * width + x + j] -
                            output[size_t (top) * width + x + j];
                }
                if (++top == height)
                  top = 0;
                if (++bottom == height)
                  bottom = 0;
              }
            }
          });
      }
      return field;
    }

    // Value noise of OCTAVES octaves from PATCHES a lap, 0 to 1. The
    // octaves' weights are the noise's own arithmetic.
    noise_signal_t layered_noise (terrain::TerrainDomain::LapPosition where,
                                  std::uint32_t patches,
                                  std::uint32_t seed,
                                  std::uint32_t octaves = 4) {
      noise_signal_t sum = 0.0f * noise_signal[one];
      float amplitude = 1.0f;
      float norm = 0.0f;
      for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += amplitude * periodic_noise (where.along_x,
                                           where.along_z,
                                           patches << octave,
                                           seed + octave);
        norm += amplitude;
        amplitude *= 0.5f;
      }
      return sum / norm;
    }

    // What the plants read of a site is a share of it, none to all, and so
    // is each tolerance and claim made from those.
    constexpr auto part = proportion[one];
    constexpr proportion_t whole = 1.0f * part;

    // Shares multiplied are a soft "and": a share of a share, which is
    // still a share.
    template <typename Product>
    proportion_t together (Product product) {
      return quantity_cast<proportion> (product);
    }

    proportion_t
    rises (proportion_t edge0, proportion_t edge1, proportion_t value) {
      return band (edge0, edge1, value);
    }

    proportion_t
    falls (proportion_t edge0, proportion_t edge1, proportion_t value) {
      return whole - band (edge0, edge1, value);
    }

    // The hidden quantities at one place, each 0 to 1, as the plants read
    // them: a stand's worth of ground, not a sample's.
    struct Site {
      proportion_t wet;       // soil water: the wetness index, smoothed
      proportion_t dry;       // share of the stand that is dry
      proportion_t soaked;    // share of the stand that is soaked
      proportion_t thin;      // share of the stand with bedrock within a span
      proportion_t peat;      // share of the stand under deep wet soil
      proportion_t poor;      // thin and dry together: sand and rock
      proportion_t level;     // flat enough to hold water
      proportion_t limestone; // on the limestone sheet
      proportion_t cold;      // height, north aspect, and frost hollows
      proportion_t exposure;  // ridges and high ground, open to the wind
      proportion_t grazed;    // cropped by the beasts of a place still lived in
      proportion_t recovery;  // come back since last opened, 0 to the old stand
      proportion_t burnt;     // how recently fire went over it
      proportion_t spring;    // fed by a spring at the limestone's foot
      proportion_t rock;      // the basement scoured bare
    };

    // How far each tree has come back on ground that was opened: birch
    // first, from seed blown in the next spring; the fell birch, which
    // sprouts again from its stump; pine, whose seed wants the bare
    // mineral soil a fire leaves; spruce last, in the shade of the others.
    // Grazing keeps all of them off.
    proportion_t
    returned (const Site& at, proportion_t early, proportion_t late) {
      return together (rises (early, late, at.recovery) *
                       falls (0.15f * part, 0.75f * part, at.grazed));
    }

    // A species' tolerance of a site, 0 to 1. Each curve is wide where the
    // species will grow at all; where it wins is decided by competition.
    struct Species {
      // How hard it pushes the others out where both grow.
      scale_factor_t strength;
      proportion_t (*tolerance) (const Site&, proportion_t light);
      std::uint32_t lane;
    };

    // Trees. Spruce shades out everything where the soil is deep and damp
    // and the summer long enough; pine grows almost anywhere below the
    // fjäll but wins only where the others cannot, on dry sand and rock
    // and in the bog; birch grows wherever it is not too dry or cold and
    // wins at the edges; the fell birch holds the treeline.
    constexpr Species canopy[] = {
      { 1.00f * scale_factor[one],
        [] (const Site& at, proportion_t) {
          return together (rises (0.20f * part, 0.65f * part, whole - at.thin) *
                           rises (0.08f * part, 0.18f * part, at.wet) *
                           falls (0.45f * part, 0.75f * part, at.wet) *
                           falls (0.30f * part, 0.55f * part, at.cold) *
                           falls (0.30f * part, 0.80f * part, at.peat) *
                           (whole - 0.85f * at.poor) *
                           (whole - together (0.6f * at.limestone * at.thin)) *
                           returned (at, 0.30f * part, 0.75f * part));
        },
        0x5b1ceU },
      { 0.80f * scale_factor[one],
        [] (const Site& at, proportion_t) {
          const proportion_t poor =
            std::max (at.poor, rises (0.20f * part, 0.60f * part, at.peat));
          return together (falls (0.55f * part, 0.80f * part, at.cold) *
                           (0.40f * part + 0.60f * poor) *
                           returned (at, 0.06f * part, 0.30f * part) *
                           (whole + 0.5f * at.burnt));
        },
        0x9a1e5U },
      { 0.52f * scale_factor[one],
        [] (const Site& at, proportion_t) {
          return together (0.9f * falls (0.45f * part, 0.75f * part, at.cold) *
                           rises (0.05f * part, 0.14f * part, at.wet) *
                           falls (0.50f * part, 0.90f * part, at.peat) *
                           (whole - 0.6f * at.poor) *
                           returned (at, 0.01f * part, 0.08f * part));
        },
        0xb12c4U },
      { 0.95f * scale_factor[one],
        [] (const Site& at, proportion_t) {
          return together (rises (0.25f * part, 0.50f * part, at.cold) *
                           falls (0.65f * part, 0.85f * part, at.cold) *
                           (whole - 0.5f * at.exposure) *
                           falls (0.6f * part, 0.95f * part, at.soaked) *
                           returned (at, 0.02f * part, 0.12f * part));
        },
        0x3f3b1U },
    };
    // How much light each tree's canopy takes from the ground.
    constexpr proportion_t canopy_shade[] = {
      0.90f * part, 0.40f * part, 0.55f * part, 0.45f * part
    };

    // The field layer, in the light the trees leave.
    constexpr Species field[] = {
      // Reindeer lichen: dry, thin, acid ground in good light.
      { 0.75f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (at.poor * (whole - 0.85f * at.limestone) *
                           rises (0.30f * part, 0.65f * part, light) *
                           rises (0.15f * part, 0.50f * part, at.recovery));
        },
        0x11c4e5U },
      // Heather and crowberry: open, poor, acid ground, and the wind.
      { 0.85f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (
            rises (0.45f * part, 0.75f * part, light) *
            falls (0.30f * part, 0.55f * part, at.wet) *
            (whole - 0.7f * at.limestone) *
            (0.35f * part +
             0.65f * std::max ({ at.thin,
                                 at.exposure,
                                 rises (0.4f * part, 0.7f * part, at.cold),
                                 together (at.grazed * (whole - at.wet)) })) *
            rises (0.04f * part, 0.15f * part, at.recovery));
        },
        0x4ea74U },
      // Bilberry: the half-shade of the conifers, damp acid humus.
      { 0.70f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (rises (0.08f * part, 0.25f * part, light) *
                           falls (0.55f * part, 0.85f * part, light) *
                           rises (0.08f * part, 0.18f * part, at.wet) *
                           falls (0.45f * part, 0.70f * part, at.wet) *
                           (whole - 0.6f * at.limestone) *
                           falls (0.82f * part, 1.0f * part, at.cold) *
                           rises (0.08f * part, 0.35f * part, at.recovery));
        },
        0xb1bb3U },
      // Sphagnum: level ground the water cannot leave. It grows its own
      // ground, so once it is there little else holds.
      { 1.25f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (
            at.level *
            std::max (rises (0.20f * part, 0.50f * part, at.soaked),
                      rises (0.25f * part, 0.60f * part, at.peat)) *
            rises (0.20f * part, 0.50f * part, light) *
            std::max (whole - at.limestone, at.peat) *
            (whole - 0.8f * at.spring));
        },
        0x5a6c1U },
      // Dwarf birch: cold, damp, open ground of the fjäll.
      { 0.90f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (rises (0.40f * part, 0.70f * part, at.cold) *
                           rises (0.12f * part, 0.28f * part, at.wet) *
                           rises (0.40f * part, 0.70f * part, light));
        },
        0xdb1c7U },
      // Sedge: the wet margins in the open.
      { 0.75f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (rises (0.15f * part, 0.45f * part, at.soaked) *
                           rises (0.40f * part, 0.70f * part, light));
        },
        0x5ed6eU },
      // The alvar's short turf on bare limestone, which drains through its
      // karst.
      { 0.92f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (at.limestone *
                           rises (0.42f * part, 0.62f * part, at.thin) *
                           rises (0.25f * part, 0.50f * part, at.dry) *
                           falls (0.15f * part, 0.45f * part, at.soaked) *
                           rises (0.40f * part, 0.70f * part, light) *
                           falls (0.70f * part, 0.90f * part, at.cold));
        },
        0xa17a2U },
      // Fireweed: the ground something has just opened -- a burn, a felled
      // verge, a pasture let go -- in the light, until the birch closes over
      // it; the beasts eat it where they still graze.
      { 1.10f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (falls (0.12f * part, 0.35f * part, at.recovery) *
                           rises (0.35f * part, 0.65f * part, light) *
                           rises (0.06f * part, 0.16f * part, at.wet) *
                           falls (0.55f * part, 0.85f * part, at.wet) *
                           falls (0.60f * part, 0.85f * part, at.cold) *
                           (whole - 0.85f * at.grazed) *
                           (0.6f * part + 0.4f * at.burnt));
        },
        0xf13e7dU },
      // The spring fen: hard water seeping out of the limestone's foot
      // keeps the ground soaked and rich all year, which suits brown moss,
      // sedge, and marsh marigold and starves the sphagnum.
      { 1.30f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (rises (0.12f * part, 0.45f * part, at.spring) *
                           rises (0.20f * part, 0.50f * part, light) *
                           falls (0.75f * part, 0.95f * part, at.cold));
        },
        0x5f1e9eU },
      // Grass and herbs: deep, moist, fertile soil in the light, and the
      // grazed turf of a pasture. Not stored: the ground draws turf
      // wherever nothing else holds it.
      { 0.90f * scale_factor[one],
        [] (const Site& at, proportion_t light) {
          return together (rises (0.25f * part, 0.70f * part, whole - at.thin) *
                           rises (0.08f * part, 0.22f * part, at.wet) *
                           falls (0.50f * part, 0.80f * part, at.wet) *
                           rises (0.30f * part, 0.60f * part, light) *
                           falls (0.60f * part, 0.85f * part, at.cold) *
                           (whole + 0.4f * at.grazed));
        },
        0x9a55eU },
    };

    // Competition: each species' claim is its tolerance times its strength,
    // a little frayed by chance (where seed happened to land and take), and
    // the claims share the ground in proportion to their cubes, so the
    // strongest dominates without the others vanishing at its edge. What a
    // species holds is its share times how well it grows there.
    template <std::size_t N>
    std::array<proportion_t, N>
    compete (const std::array<proportion_t, N>& tolerance,
             const Species (&species)[N],
             const std::array<scale_factor_t, N>& chance) {
      // A claim is a cube: it is weighed only against the others' claims.
      const auto claim_of = [&] (std::size_t s) {
        const auto c = tolerance[s] * species[s].strength * chance[s];
        return c * c * c;
      };
      using Claim = decltype (claim_of (0));
      std::array<Claim, N> claim {};
      Claim total = 1e-6f * Claim::reference;
      for (std::size_t s = 0; s < N; ++s) {
        claim[s] = claim_of (s);
        total += claim[s];
      }
      std::array<proportion_t, N> held {};
      for (std::size_t s = 0; s < N; ++s)
        held[s] = together (claim[s] / total * tolerance[s]);
      return held;
    }
  }

  DisturbanceMap undisturbed (const terrain::TerrainDomain& domain) {
    DisturbanceMap values (domain);
    std::ranges::fill (spatial::get<stand_recovery> (values),
                       1.0f * stand_recovery[one]);
    return values;
  }

  VegetationMap
  analyze_vegetation (const SurfaceGeometry& geometry,
                      std::span<const terrain::SoilWetness> wetness,
                      meters_t water_level,
                      meters_t land_top,
                      std::uint32_t seed,
                      const DisturbanceMap& disturbance) {
    MOPPE_PROFILE_ZONE ("map::analyze_vegetation");
    if (wetness.size () != geometry.domain ().size () ||
        disturbance.domain () != geometry.domain ())
      throw std::invalid_argument (
        "Wetness or disturbance does not share the surface domain");
    using spatial::get;
    using terrain::ElevationChange;
    const terrain::TerrainDomain& domain = geometry.domain ();
    const std::size_t count = domain.size ();
    constexpr auto signal = noise_signal[one];

    const auto& sediment = get<terrain::sediment_thickness> (geometry);
    const auto& normals = get<terrain::terrain_normal> (geometry);
    const auto& elevation = get<terrain::surface_elevation> (geometry);
    std::vector<ElevationChange> height (count);
    std::vector<proportion_t> wet (count), thin (count), dry (count),
      soaked (count), peat (count), bare (count);
    // The ground normal's upward and southward lanes, each a share of its
    // unit length; a north face's southward share is negative.
    std::vector<proportion_t> up (count), south (count);
    for (std::size_t i = 0; i < count; ++i) {
      const meters_t soil = sediment[i];
      wet[i] = wetness[i];
      // Under a span of soil a root reaches bedrock; over two and a half
      // metres of it the ground is peat if it is also wet.
      thin[i] = whole - band (0.15f * u::m, 0.60f * u::m, soil);
      bare[i] = whole - band (0.08f * u::m, 0.45f * u::m, soil);
      peat[i] = together (band (0.80f * u::m, 2.50f * u::m, soil) *
                          band (0.2f * part, 0.4f * part, wet[i]));
      dry[i] = whole - band (0.12f * part, 0.28f * part, wet[i]);
      soaked[i] = band (0.30f * part, 0.60f * part, wet[i]);
      // The stored normal's lanes, read once.
      const Vec3 normal = normals[i].numerical_value_in (one);
      up[i] = normal[1] * part;
      // North is -z, so a slope facing the sun leans its normal toward +z.
      south[i] = normal[2] * part;
      height[i] = elevation[i].delta_from_zero ();
    }
    wet = smoothed (std::move (wet), domain, 12.0f * u::m);
    thin = smoothed (std::move (thin), domain, 25.0f * u::m);
    dry = smoothed (std::move (dry), domain, 25.0f * u::m);
    soaked = smoothed (std::move (soaked), domain, 25.0f * u::m);
    peat = smoothed (std::move (peat), domain, 25.0f * u::m);
    up = smoothed (std::move (up), domain, 25.0f * u::m);
    south = smoothed (std::move (south), domain, 25.0f * u::m);
    // The ground's height against its neighbourhood's: a ridge stands
    // above it, a hollow below, where cold air settles on still nights.
    const std::vector<ElevationChange> around =
      smoothed (height, domain, 75.0f * u::m);
    // A knoll's height over the ground a stone's throw round it: the ice
    // ground its way over these and scoured them to the rock.
    const std::vector<ElevationChange> nearby =
      smoothed (height, domain, 18.0f * u::m);
    bare = smoothed (std::move (bare), domain, 5.0f * u::m);

    const auto laps = [&] (meters_t patch) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (
          std::lround ((domain.period_x () / patch).numerical_value_in (one))));
    };
    const std::uint32_t chance_laps = laps (160.0f * u::m);
    const ElevationChange sea =
      quantity_cast<terrain::surface_elevation> (water_level);
    const meters_t relief = std::max (land_top - water_level, 1.0f * u::m);

    // Sample the same 3D stratigraphic lithology that governed fluvial
    // incision: surviving limestone caprock plateaus feed karst springs along
    // their escarpment feet, while exposed crystalline granite domes and
    // fissure-valley walls supply bare bedrock whalebacks and crags.
    const terrain::Lithology lithology =
      terrain::make_lithology (terrain::Seed { seed },
                               domain.period_x (),
                               domain.period_z (),
                               terrain::surface_elevation_point (water_level));
    std::vector<proportion_t> limestone (count);
    parallel_sites (geometry.domain (), [&] (terrain::TerrainIndex site) {
      const std::size_t i = domain.offset (site);
      const terrain::SurfaceElevation bedrock =
        elevation[i] - meters_t (sediment[i]);
      const terrain::LithologySample strata =
        lithology.sample (domain.lap_position (site), bedrock, sediment[i]);
      limestone[i] = strata.limestone;
    });
    // The limestone is a sponge: the rain sinks through its joints and
    // runs along its foot, over the basement it cannot get into, until
    // the foot comes out on a hillside and the water with it. So a spring
    // rises where the ground underfoot is below the limestone cap and the
    // slope above it, a few tens of metres up, is limestone; its water keeps
    // the ground below soaked for a stone's throw down the slope, and the
    // line of them breaks into separate seeps, each with its own tongue of fen.
    std::vector<proportion_t> spring (count), rock (count);
    {
      const std::vector<ElevationChange> slope_height =
        smoothed (height, domain, 12.0f * u::m);
      const meters_t sx = domain.spacing_x ();
      const meters_t sz = domain.spacing_z ();
      const std::uint32_t seep_laps = laps (45.0f * u::m);
      constexpr meters_t reach[] = {
        8.0f * u::m, 18.0f * u::m, 32.0f * u::m, 50.0f * u::m
      };
      constexpr proportion_t fade[] = {
        1.0f * part, 0.95f * part, 0.85f * part, 0.65f * part
      };
      parallel_sites (geometry.domain (), [&] (terrain::TerrainIndex site) {
        const std::size_t i = domain.offset (site);
        const auto at = [&] (int columns, int rows) {
          return domain.offset (domain.shifted (site, columns, rows));
        };
        // The slope's rise over its run along each axis.
        const auto rise_over = [] (ElevationChange rise, meters_t run) {
          return quantity_cast<grade> (quantity_cast<isq::height> (rise) / run);
        };
        const grade_t gx = rise_over (
          slope_height[at (1, 0)] - slope_height[at (-1, 0)], 2.0f * sx);
        const grade_t gz = rise_over (
          slope_height[at (0, 1)] - slope_height[at (0, -1)], 2.0f * sz);
        const grade_t steepness = sqrt (gx * gx + gz * gz);
        proportion_t upslope = 0.0f * part;
        if (steepness > 1e-4f * grade[one])
          for (std::size_t k = 0; k < std::size (reach); ++k) {
            const int columns = static_cast<int> (std::lround (
              (gx / steepness * reach[k] / sx).numerical_value_in (one)));
            const int rows = static_cast<int> (std::lround (
              (gz / steepness * reach[k] / sz).numerical_value_in (one)));
            upslope = std::max (
              upslope, together (fade[k] * limestone[at (columns, rows)]));
          }
        const noise_signal_t seeps = layered_noise (
          domain.lap_position (site), seep_laps, seed ^ 0x5ee95U);
        spring[i] =
          together ((whole - limestone[i]) * upslope * upslope *
                    band (0.03f * grade[one], 0.10f * grade[one], steepness) *
                    band (0.45f * signal, 0.62f * signal, seeps) *
                    band (1.0f * u::m, 4.0f * u::m, height[i] - sea));
        // Bare rock: crystalline basement under thin soil on knolls, hard
        // granite domes, and steep fissure-valley gorge walls.
        const terrain::SurfaceElevation bedrock =
          elevation[i] - meters_t (sediment[i]);
        const terrain::LithologySample strata =
          lithology.sample (domain.lap_position (site), bedrock, sediment[i]);
        const proportion_t knoll =
          band (-0.5f * u::m, 2.5f * u::m, height[i] - nearby[i]) +
          0.5f * band (2.0f * u::m, 10.0f * u::m, height[i] - around[i]) +
          0.45f * strata.granite_dome +
          0.55f *
            together (quantity_cast<proportion> (strata.fissure) *
                      band (0.28f * grade[one], 0.55f * grade[one], steepness));
        rock[i] = together (strata.basement * bare[i] *
                            std::min (knoll, whole) * (whole - soaked[i]));
      });
      spring = smoothed (std::move (spring), domain, 6.0f * u::m);
      // The ice left the rock in whalebacks, not in the lines of the
      // spurs: what it scoured is read over a few tens of metres and broken
      // up by a noise of about that size.
      rock = smoothed (std::move (rock), domain, 9.0f * u::m);
      parallel_sites (geometry.domain (), [&] (terrain::TerrainIndex site) {
        const std::size_t i = domain.offset (site);
        const noise_signal_t whaleback = layered_noise (
          domain.lap_position (site), laps (60.0f * u::m), seed ^ 0x9a817eU, 3);
        rock[i] = band (
          0.22f * part,
          0.48f * part,
          rock[i] + quantity_cast<proportion> (quantity_cast<dimensionless> (
                      0.7f * (whaleback - 0.5f * signal))));
      });
    }

    VegetationMap values (domain);
    parallel_sites (geometry.domain (), [&] (terrain::TerrainIndex site) {
      const std::size_t i = domain.offset (site);
      const auto where = domain.lap_position (site);
      // How far up the land's relief the place stands; under the sea,
      // below none of it.
      const proportion_t rise =
        quantity_cast<proportion> (meters_t (height[i] - sea) / relief);
      const ElevationChange above = height[i] - around[i];
      const proportion_t fed =
        std::clamp (2.0f * spring[i], 0.0f * part, whole);
      const Site at {
        .wet = std::min (wet[i] + 0.35f * fed, whole),
        .dry = together (dry[i] * (whole - fed)),
        .soaked = std::max (soaked[i], 0.8f * fed),
        .thin = thin[i],
        .peat = peat[i],
        .poor = band (0.44f * part, 0.60f * part, 0.5f * (thin[i] + dry[i])),
        .level = band (0.95f * part, 0.985f * part, up[i]),
        .limestone = limestone[i],
        .cold = band (0.40f * part,
                      0.90f * part,
                      rise - 0.20f * south[i] +
                        0.05f * band (0.0f * u::m, 8.0f * u::m, -above)),
        .exposure = std::clamp (0.7f * band (0.0f * u::m, 8.0f * u::m, above) +
                                  0.6f * band (0.45f * part, 0.9f * part, rise),
                                0.0f * part,
                                whole),
        .grazed = get<grazing_pressure> (disturbance[site]),
        .recovery = get<stand_recovery> (disturbance[site]),
        .burnt = get<burn_recency> (disturbance[site]),
        .spring = fed,
        .rock = rock[i],
      };
      // Steep ground holds little of anything rooted.
      const proportion_t footing = band (0.70f * part, 0.85f * part, up[i]);
      // How a stand's seed happened to land and take: a multiplier on each
      // species' strength, drawn from noise.
      const auto chances = [&] (const auto& species) {
        std::array<scale_factor_t,
                   std::extent_v<std::remove_reference_t<decltype (species)>>>
          chance {};
        for (std::size_t s = 0; s < chance.size (); ++s)
          chance[s] =
            0.75f * scale_factor[one] +
            quantity_cast<scale_factor> (quantity_cast<dimensionless> (
              0.5f *
              layered_noise (where, chance_laps, seed ^ species[s].lane, 2)));
        return chance;
      };

      std::array<proportion_t, std::size (canopy)> trees {};
      for (std::size_t s = 0; s < trees.size (); ++s)
        // Only pine finds the cracks in bare rock.
        trees[s] = together (canopy[s].tolerance (at, whole) * footing *
                             (whole - (s == 1 ? 0.6f : 0.92f) * at.rock));
      const auto held = compete (trees, canopy, chances (canopy));
      proportion_t shade = 0.0f * part;
      for (std::size_t s = 0; s < held.size (); ++s)
        shade += together (canopy_shade[s] * held[s]);
      const proportion_t light = std::clamp (whole - shade, 0.0f * part, whole);

      std::array<proportion_t, std::size (field)> plants {};
      for (std::size_t s = 0; s < plants.size (); ++s)
        // Lichen takes the thin skin of soil in the rock's hollows.
        plants[s] = together (field[s].tolerance (at, light) *
                              std::max (footing, 0.3f * part) *
                              (whole - (s == 0 ? 0.25f : 0.85f) * at.rock));
      const auto ground = compete (plants, field, chances (field));

      const auto reading = values[site];
      get<limestone_cover> (reading) =
        quantity_cast<limestone_cover> (at.limestone);
      get<bedrock_exposure> (reading) =
        quantity_cast<bedrock_exposure> (at.rock);
      get<spruce_abundance> (reading) =
        quantity_cast<spruce_abundance> (held[0]);
      get<pine_abundance> (reading) = quantity_cast<pine_abundance> (held[1]);
      get<birch_abundance> (reading) = quantity_cast<birch_abundance> (held[2]);
      get<mountain_birch_abundance> (reading) =
        quantity_cast<mountain_birch_abundance> (held[3]);
      get<lichen_abundance> (reading) =
        quantity_cast<lichen_abundance> (ground[0]);
      get<heather_abundance> (reading) =
        quantity_cast<heather_abundance> (ground[1]);
      get<bilberry_abundance> (reading) =
        quantity_cast<bilberry_abundance> (ground[2]);
      get<sphagnum_abundance> (reading) =
        quantity_cast<sphagnum_abundance> (ground[3]);
      get<dwarf_birch_abundance> (reading) =
        quantity_cast<dwarf_birch_abundance> (ground[4]);
      get<sedge_abundance> (reading) =
        quantity_cast<sedge_abundance> (ground[5]);
      get<alvar_abundance> (reading) =
        quantity_cast<alvar_abundance> (ground[6]);
      get<fireweed_abundance> (reading) =
        quantity_cast<fireweed_abundance> (ground[7]);
      get<spring_fen_abundance> (reading) =
        quantity_cast<spring_fen_abundance> (ground[8]);
    });
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

    parallel_sites (geometry.domain (), [&] (terrain::TerrainIndex site) {
      const auto ground = geometry[site];
      const auto ground_normal = get<terrain::terrain_normal> (ground);
      const auto ground_level =
        get<terrain::surface_elevation> (ground).delta_from_zero ();

      const auto ground_horizontality = dot (ground_normal, world_up);

      const proportion_t soil_stability =
        band (0.72f * level_ground, 0.96f * level_ground, ground_horizontality);

      const proportion_t soil_dryness =
        band (water_level + 3.0f * m, water_level + 18.0f * m, ground_level);

      // Trees thin out as the ground approaches the tree line and stop at
      // it. The band rises from 0 to 1 across the last 35 m of climb, so one
      // minus it is the headroom still left below the line: full well down
      // the slope, nothing at the top.
      const proportion_t tree_line_headroom =
        whole - band (tree_tops - 35.0f * m, tree_tops, ground_level);

      // What the ground holds, not how near it is to open water: a bank a
      // step from the river can be a dry cut, and a flat two hundred metres
      // away can be a bog. The tree is standing in soil, not beside a view.
      const auto wetness = get<terrain::soil_wetness> (moisture[site]);

      // Where these bands sit is a reading of the index's own distribution
      // over a generated world: the driest tenth of the ground sits at zero,
      // the median hillside near a fifth, and the wettest tenth is standing
      // water and the flats around it. So trees want more than a bare ridge
      // and less than a bog, and almost every hillside qualifies.
      const proportion_t hydrated = band (0.05f * part, 0.20f * part, wetness);
      const proportion_t not_sodden =
        whole - band (0.60f * part, 0.88f * part, wetness);
      const proportion_t water_response =
        0.28f * part + together (0.72f * hydrated * not_sodden);
      const proportion_t treeishness = together (
        soil_dryness * tree_line_headroom * soil_stability * water_response);

      get<tree_habitat> (values[site]) =
        quantity_cast<tree_habitat> (treeishness);
    });
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

    parallel_sites (habitat.domain (), [&] (terrain::TerrainIndex site) {
      const auto where = domain.lap_position (site);

      const noise_signal_t stands = periodic_noise (
        where.along_x, where.along_z, stands_per_lap, seed ^ 0x4b1d9e37U);
      const noise_signal_t breaks = periodic_noise (
        where.along_x, where.along_z, breaks_per_lap, seed ^ 0x91e10da5U);
      const noise_signal_t mosaic = 0.72f * stands + 0.28f * breaks;

      // Habitat raised slightly above one: good ground stays good, and
      // marginal ground gives up a little faster than it otherwise would.
      const proportion_t habitable =
        cr::pow (get<tree_habitat> (habitat[site]).numerical_value_in (one),
                 1.15f) *
        part;

      // The ground decides how hard the mosaic has to try. Multiplying the
      // two instead would make the noise an equal author of where the forest
      // is, and a wet corridor would go bare as often as a dry ridge. Here a
      // sheltered damp hollow closes canopy on a low roll and a dry shoulder
      // needs a high one, so the stands land where the water does and the
      // noise only says where their edges fall.
      const noise_signal_t reluctant = 0.74f * signal;
      const noise_signal_t eager = 0.30f * signal;
      const noise_signal_t gate =
        reluctant + quantity_cast<noise_signal> (quantity_cast<dimensionless> (
                      (eager - reluctant) * habitable));

      // Most of the mosaic is either forest or clearing; the band is narrow
      // so the edges between them stay edges instead of a long gradient.
      const proportion_t seeded =
        band (gate - 0.085f * signal, gate + 0.085f * signal, mosaic);

      // A route keeps almost all canopy off a corridor about its formed
      // width; a settlement clears its ground completely.
      const auto trodden = use[site];
      constexpr auto trail = map::trail_influence[one];
      const proportion_t route_clearance =
        whole - 0.96f * band (0.35f * trail,
                              0.65f * trail,
                              get<trail_influence> (trodden));
      const proportion_t settled_clearance =
        whole - get<home_base_influence> (trodden);

      // Every factor is a soft yes between 0 and 1, and multiplying them is
      // a soft "and": canopy needs habitable ground AND a seeded patch AND no
      // route AND no settlement. Any one of them near zero vetoes the rest,
      // which is why a place can fail four mild tests and still come out
      // bare. Adding them would say "any of these will do", which is not how
      // a forest works.
      // Habitat has already chosen where the stands are; here it only says
      // how thickly they grow, which is what placement reads for density,
      // height, and tint.
      const proportion_t thickness = 0.45f * part + 0.55f * habitable;
      // How closed a canopy the trees that hold this place make: spruce
      // closes it, birch nearly, while pine and the fell birch stand open
      // and the forest plan plants them itself.
      const auto plants = vegetation[site];
      const proportion_t closing =
        get<spruce_abundance> (plants) + 0.8f * get<birch_abundance> (plants);
      const proportion_t wanted =
        together (thickness * seeded * route_clearance * settled_clearance *
                  band (0.08f * part, 0.45f * part, closing));

      // The bands are in range by construction, but the trail and home-base
      // readings are stored data, and a clearance is one minus one of those.
      // The clamp guards the inputs this rule did not compute, not its own
      // arithmetic.
      get<forest_cover> (values[site]) =
        quantity_cast<forest_cover> (std::clamp (wanted, 0.0f * part, whole));
    });
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

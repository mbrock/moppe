#include <moppe/terrain/local_evolution.hh>

#include <moppe/terrain/flood.hh>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace moppe::terrain {
  namespace {
    using Buffer = id<MTLBuffer>;
    constexpr unsigned group_size = 128;
    constexpr auto year = moppe::astronomy::Julian_year;
    constexpr auto square_metre = u::m * u::m;
    constexpr const char* source =
#include "fastflow.inc"
      ;

    struct Params {
      unsigned width, height, level, step;
      float dx, dz, dt, uplift_dt, remaining_uplift_dt, sea;
      float incision, reference_area, exponent, channel_area;
      float concentration, max_deposit, diffusion, critical, multiplier;
    };
    struct Material {
      float cover, cut, fill, rise;
    };
    struct Budget {
      float exported;
      unsigned drained;
    };
    struct FissureGpu {
      float line[4];
      float shape[4];
      float warp_a[4];
      float warp_b[4];
      float mod[4];
    };
    struct alignas (16) LithologyGpuBlock {
      unsigned enabled = 0;
      float unconformity_base_m = 0.0f;
      float shale_thickness_m = 26.0f;
      float transition_m = 3.5f;
      float bedding_wavelength_m = 14.0f;
      float bedding_contrast = 0.22f;
      float limestone_erodibility = 0.26f;
      float shale_erodibility = 2.50f;
      float gneiss_erodibility = 0.78f;
      float granite_erodibility = 0.38f;
      float cover_scale_m = 2.0f;
      float pad0 = 0.0f;
      float fold_waves[6][4] {};
      FissureGpu fissures[3] {};
    };
    static_assert (sizeof (LithologyGpuBlock) == 384);

    LithologyGpuBlock pack_lithology (const Lithology& lith) {
      LithologyGpuBlock g {};
      if (!lith.enabled)
        return g;
      g.enabled = 1u;
      g.unconformity_base_m = surface_elevation_value (lith.unconformity_base);
      g.shale_thickness_m = meters_value (lith.shale_thickness);
      g.transition_m = meters_value (lith.transition_half_width);
      g.bedding_wavelength_m = meters_value (lith.bedding_wavelength);
      g.bedding_contrast = lith.bedding_contrast.numerical_value_in (one);
      g.limestone_erodibility =
        lith.limestone_erodibility.numerical_value_in (one);
      g.shale_erodibility = lith.shale_erodibility.numerical_value_in (one);
      g.gneiss_erodibility = lith.gneiss_erodibility.numerical_value_in (one);
      g.granite_erodibility = lith.granite_erodibility.numerical_value_in (one);
      g.cover_scale_m = meters_value (lith.cover_scale);
      for (size_t k = 0; k < lith.fold_waves.size (); ++k) {
        const FoldWave& w = lith.fold_waves[k];
        g.fold_waves[k][0] = float (w.cycles_x);
        g.fold_waves[k][1] = float (w.cycles_z);
        g.fold_waves[k][2] = meters_value (w.amplitude);
        g.fold_waves[k][3] = radians_value (w.phase);
      }
      for (size_t k = 0; k < lith.fissures.size (); ++k) {
        const FissureFamily& f = lith.fissures[k];
        auto& dst = g.fissures[k];
        dst.line[0] = float (f.cycles_x);
        dst.line[1] = float (f.cycles_z);
        dst.line[2] = radians_value (f.phase);
        dst.line[3] = meters_value (f.spacing);
        dst.shape[0] = meters_value (f.half_width);
        dst.shape[1] = f.boost.numerical_value_in (one);
        dst.shape[2] = radians_value (f.warp_amplitude_a);
        dst.shape[3] = radians_value (f.warp_amplitude_b);
        dst.warp_a[0] = float (f.warp_a_x);
        dst.warp_a[1] = float (f.warp_a_z);
        dst.warp_a[2] = radians_value (f.warp_a_phase);
        dst.warp_a[3] = f.modulation_depth.numerical_value_in (one);
        dst.warp_b[0] = float (f.warp_b_x);
        dst.warp_b[1] = float (f.warp_b_z);
        dst.warp_b[2] = radians_value (f.warp_b_phase);
        dst.warp_b[3] = radians_value (f.modulation_phase);
        dst.mod[0] = float (f.modulation_x);
        dst.mod[1] = float (f.modulation_z);
        dst.mod[2] = f.uplift_suppression.numerical_value_in (one);
        dst.mod[3] = 0.0f;
      }
      return g;
    }
    constexpr unsigned bucket_size = 4 * sizeof (unsigned);

    class Compute {
    public:
      Compute () {
        m_device = MTLCreateSystemDefaultDevice ();
        m_queue = [m_device newCommandQueue];
        if (!m_queue)
          throw std::runtime_error ("no Metal compute queue");
        NSError* error = nil;
        m_library =
          [m_device newLibraryWithSource:[NSString stringWithUTF8String:source]
                                 options:nil
                                   error:&error];
        if (!m_library)
          throw std::runtime_error (error.localizedDescription.UTF8String);
      }

      Buffer buffer (size_t bytes, bool shared = false) {
        auto b =
          [m_device newBufferWithLength:bytes
                                options:shared ? MTLResourceStorageModeShared
                                               : MTLResourceStorageModePrivate];
        if (!b)
          throw std::runtime_error ("Metal terrain allocation failed");
        return b;
      }

      void begin () {
        m_command = [m_queue commandBuffer];
        m_encoder = [m_command computeCommandEncoder];
        if (!m_encoder)
          throw std::runtime_error ("no Metal compute encoder");
      }

      struct Arg {
        Buffer data;
        size_t offset;
        Arg (Buffer b, size_t off = 0) : data (b), offset (off) {}
      };

      void run (const char* name,
                const Params& p,
                std::initializer_list<Arg> args,
                size_t count,
                Buffer indirect = nil,
                unsigned slot = 0) {
        auto& pipeline = m_pipelines[name];
        if (!pipeline) {
          NSError* error = nil;
          auto function = [m_library
            newFunctionWithName:[NSString stringWithUTF8String:name]];
          pipeline = [m_device newComputePipelineStateWithFunction:function
                                                             error:&error];
          if (!pipeline)
            throw std::runtime_error (error.localizedDescription.UTF8String);
        }
        [m_encoder setComputePipelineState:pipeline];
        [m_encoder setBytes:&p length:sizeof p atIndex:0];
        unsigned index = 1;
        for (const Arg& arg : args)
          [m_encoder setBuffer:arg.data offset:arg.offset atIndex:index++];
        const MTLSize group = MTLSizeMake (group_size, 1, 1);
        if (indirect)
          [m_encoder dispatchThreadgroupsWithIndirectBuffer:indirect
                                       indirectBufferOffset:slot * bucket_size +
                                                            sizeof (unsigned)
                                      threadsPerThreadgroup:group];
        else
          [m_encoder dispatchThreads:MTLSizeMake (count, 1, 1)
               threadsPerThreadgroup:group];
        [m_encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
      }

      void submit () {
        [m_encoder endEncoding];
        [m_command commit];
        m_pending.push_back (m_command);
      }
      void finish () {
        for (auto command : m_pending) {
          [command waitUntilCompleted];
          if (command.status != MTLCommandBufferStatusCompleted)
            throw std::runtime_error (
              command.error.localizedDescription.UTF8String);
        }
        m_pending.clear ();
      }

    private:
      id<MTLDevice> m_device;
      id<MTLCommandQueue> m_queue;
      id<MTLLibrary> m_library;
      id<MTLCommandBuffer> m_command;
      id<MTLComputeCommandEncoder> m_encoder;
      std::map<std::string, id<MTLComputePipelineState>> m_pipelines;
      std::vector<id<MTLCommandBuffer>> m_pending;
    };

    struct Level {
      TerrainDomain domain;
      std::vector<SurfaceElevation> base;
      std::vector<float> uplift;
      std::vector<Material> material;
      std::vector<float> change;

      explicit Level (TerrainDomain d)
          : domain (d), base (d.size ()), uplift (d.size ()),
            material (d.size ()), change (d.size ()) {}
    };

    Level restrict_level (const Level& fine) {
      const auto& d = fine.domain;
      Level coarse ({ d.width () / 2,
                      d.height () / 2,
                      2 * d.spacing_x (),
                      2 * d.spacing_z () });
      for (size_t i = 0; i < coarse.domain.size (); ++i) {
        size_t j = 2 * (i % coarse.domain.width ()) +
                   2 * (i / coarse.domain.width ()) * d.width ();
        float h = 0;
        for (auto k : { j, j + 1, j + d.width (), j + d.width () + 1 }) {
          h += .25f * surface_elevation_value (fine.base[k]);
          coarse.uplift[i] += .25f * fine.uplift[k];
          coarse.material[i].cover += .25f * fine.material[k].cover;
        }
        coarse.base[i] = surface_elevation_point (h * u::m);
      }
      return coarse;
    }

    // Conservative, cell-centred bilinear transfer of the evolved delta.
    // Retain each fine cell's original geology, including its subgrid relief.
    void refine (const Level& coarse, Level& fine) {
      const int w = int (coarse.domain.width ()),
                h = int (coarse.domain.height ());
      for (int z = 0; z < h * 2; ++z)
        for (int x = 0; x < w * 2; ++x) {
          size_t i = size_t (z) * 2 * w + x;
          int xx = x / 2, zz = z / 2;
          int nx = (xx + (x % 2 ? 1 : w - 1)) % w;
          int nz = (zz + (z % 2 ? 1 : h - 1)) % h;
          Material m {};
          auto add = [&] (int j, float weight) {
            fine.change[i] += weight * coarse.change[j];
            auto c = coarse.material[j];
            m.cover += weight * c.cover;
            m.cut += weight * c.cut;
            m.fill += weight * c.fill;
            m.rise += weight * c.rise;
          };
          add (zz * w + xx, .5625f);
          add (zz * w + nx, .1875f);
          add (nz * w + xx, .1875f);
          add (nz * w + nx, .0625f);
          fine.material[i] = m;
        }
    }

    LocalEvolutionBudget evolve_level (Compute& gpu,
                                       Level& land,
                                       const StreamPowerEvolution& model,
                                       unsigned first_step,
                                       unsigned steps) {
      const auto& d = land.domain;
      const size_t n = d.size (), bytes = n * sizeof (float);
      unsigned levels = std::bit_width (unsigned (n));
      auto col = [&] { return gpu.buffer (bytes); };
      Buffer h = gpu.buffer (bytes, true), temp = gpu.buffer (bytes, true);
      Buffer uplift = gpu.buffer (bytes, true), ocean = gpu.buffer (n, true);
      Buffer material = gpu.buffer (n * sizeof (Material), true);
      Buffer invalid = gpu.buffer (sizeof (unsigned), true);
      *static_cast<unsigned*> (invalid.contents) = 0;
      Buffer budget = gpu.buffer (n * sizeof (Budget), true);
      const LithologyGpuBlock packed_lith = pack_lithology (model.lithology);
      Buffer lith = gpu.buffer (sizeof (LithologyGpuBlock), true);
      std::memcpy (lith.contents, &packed_lith, sizeof (LithologyGpuBlock));
      auto* height = static_cast<float*> (h.contents);
      {
        std::vector<SurfaceElevation> initial (n);
        for (size_t i = 0; i < n; ++i) {
          height[i] = surface_elevation_value (land.base[i]) + land.change[i];
          initial[i] = surface_elevation_point (height[i] * u::m);
        }
        auto outlets = ocean_component (d, initial, model.sea_level);
        if (outlets.empty ())
          outlets.push_back (
            std::min_element (initial.begin (), initial.end ()) -
            initial.begin ());
        auto* mask = static_cast<unsigned char*> (ocean.contents);
        std::memset (mask, 0, n);
        for (auto i : outlets)
          mask[i] = 1;
      }
      std::memcpy (uplift.contents, land.uplift.data (), bytes);
      std::memcpy (
        material.contents, land.material.data (), n * sizeof (Material));
      std::memset (budget.contents, 0, n * sizeof (Budget));

      Buffer parent = col (), basin = col (), component = col (),
             minima = col ();
      Buffer border = col (), best = col (), merge = col ();
      Buffer first = col (), sibling = col ();
      Buffer active = minima, next = border, nodes = gpu.buffer (3 * bytes);
      Buffer mirror = gpu.buffer (3 * bytes);
      Buffer start = gpu.buffer (levels * 4), cursor = gpu.buffer (levels * 4);
      Buffer buckets = gpu.buffer (levels * bucket_size);
      // Disjoint stages share the root, saddle, and reach workspaces.
      Buffer solved = best, outgoing = merge, area = component, up = basin;
      Buffer ends = nodes, load = mirror;
      const float dt = model.time_step.numerical_value_in (year);
      Params p {
        unsigned (d.width ()),
        unsigned (d.height ()),
        0,
        0,
        meters_value (d.spacing_x ()),
        meters_value (d.spacing_z ()),
        dt,
        0,
        0,
        surface_elevation_value (model.sea_level),
        model.reference_incision_rate.numerical_value_in (u::m / year),
        model.reference_area.numerical_value_in (square_metre),
        model.area_exponent.numerical_value_in (one),
        model.channel_initiation_area.numerical_value_in (square_metre),
        model.fluvial_transport.concentration_at_unit_slope.numerical_value_in (
          one) *
          model.fluvial_transport.runoff_rate.numerical_value_in (u::m / year),
        0,
        0,
        model.critical_hillslope_gradient.numerical_value_in (one),
        model.maximum_hillslope_diffusivity_multiplier.numerical_value_in (one)
      };
      for (unsigned s = first_step; s < first_step + steps; ++s) {
        p.step = s;
        p.dt = std::min (dt, model.duration.numerical_value_in (year) - s * dt);
        const float uplift_rem =
          model.uplift_duration.numerical_value_in (year) - s * dt;
        p.uplift_dt = std::clamp (uplift_rem, 0.f, p.dt);
        p.remaining_uplift_dt = std::max (uplift_rem, 0.f);
        p.max_deposit =
          p.dt * model.maximum_deposition_rate.numerical_value_in (u::m / year);
        gpu.begin ();
        auto run = [&] (const char* name,
                        std::initializer_list<Compute::Arg> args,
                        size_t count) { gpu.run (name, p, args, count); };
        auto sparse = [&] (const char* name,
                           std::initializer_list<Compute::Arg> args,
                           unsigned slot) {
          gpu.run (name, p, args, 0, buckets, slot);
        };
        run ("clear_buckets", { buckets }, 3);
        run ("receivers",
             { h, ocean, parent, component, minima, buckets, budget },
             n);
        Buffer src = parent, dst = first;
        for (unsigned bits = 0; bits < levels; bits += 3) {
          run ("roots", { src, dst }, n);
          src = dst;
          dst = dst == first ? sibling : first;
        }
        run ("borders", { src, ocean, basin, border, buckets }, n);
        run ("grids", { buckets }, 2);
        for (unsigned r = 0; r < levels; ++r) {
          sparse ("reset_choices", { minima, best, buckets }, 0);
          sparse ("saddles", { h, basin, component, best, border, buckets }, 1);
          sparse ("carve",
                  { minima, best, basin, component, merge, parent, buckets },
                  0);
          Buffer links = merge, jumps = first;
          for (unsigned bits = 0; bits < levels; bits += 3) {
            sparse ("jump_components", { minima, links, jumps, buckets }, 0);
            std::swap (links, jumps);
          }
          sparse ("relabel", { minima, links, component, buckets }, 0);
          run ("next_merge", { buckets }, 1);
        }
        run ("donors", { parent, first, sibling }, n);
        run ("clear_buckets", { buckets }, 2);
        run (
          "start_rake", { first, sibling, nodes, mirror, active, buckets }, n);
        run ("grids", { buckets }, 1);
        for (unsigned r = 0; r < 3 * levels + 2; ++r) {
          unsigned a = r % 2, b = 1 - a;
          Compute::Arg from (buckets, a * bucket_size),
            to (buckets, b * bucket_size);
          run ("clear_buckets", { to }, 1);
          sparse ("rake", { nodes, mirror, active, next, from, to }, a);
          run ("grids", { to }, 1);
          std::swap (nodes, mirror);
          std::swap (active, next);
        }
        run ("catchments", { first, nodes, area }, n);
        run ("clear_buckets", { buckets }, levels);
        run ("chains",
             { parent, area, first, sibling, up, buckets, ocean, invalid },
             n);
        run ("offsets", { buckets, start, cursor }, levels);
        run ("grids", { buckets }, levels);
        run ("bin_chains", { parent, area, first, up, cursor, ends }, n);
        for (unsigned k = levels; k > 0; --k) {
          p.level = k - 1;
          sparse ("incise",
                  { parent,
                    area,
                    up,
                    ends,
                    start,
                    buckets,
                    h,
                    uplift,
                    solved,
                    load,
                    minima,
                    material,
                    lith },
                  p.level);
        }
        for (p.level = 0; p.level < levels; ++p.level)
          sparse ("transport",
                  { parent,
                    first,
                    sibling,
                    ends,
                    start,
                    buckets,
                    load,
                    outgoing,
                    h,
                    uplift,
                    material,
                    budget,
                    area,
                    minima,
                    lith },
                  p.level);
        float diffusion =
          p.dt * model.diffusivity.numerical_value_in (square_metre / year);
        unsigned sweeps =
          unsigned (std::ceil (2 * diffusion * p.multiplier *
                               (1 / (p.dx * p.dx) + 1 / (p.dz * p.dz))));
        if (sweeps)
          p.diffusion = diffusion / sweeps;
        for (unsigned k = 0; k < sweeps; ++k) {
          run ("creep", { h, temp, ocean, material }, n);
          std::swap (h, temp);
        }
        gpu.submit ();
      }
      gpu.finish ();
      if (*static_cast<unsigned*> (invalid.contents))
        throw std::runtime_error ("incomplete Metal drainage forest");
      height = static_cast<float*> (h.contents);
      auto* m = static_cast<Material*> (material.contents);
      auto* b = static_cast<Budget*> (budget.contents);
      LocalEvolutionBudget result {};
      double uplift_sum = 0, export_sum = 0, volume_error = 0;
      uint64_t drained = 0;
      for (size_t i = 0; i < n; ++i) {
        float change = height[i] - surface_elevation_value (land.base[i]);
        double rise = double (m[i].rise) - land.material[i].rise;
        uplift_sum += rise;
        export_sum += b[i].exported;
        volume_error += double (change) - land.change[i] - rise + b[i].exported;
        land.change[i] = change;
        land.material[i] = m[i];
        drained += b[i].drained;
        if (!std::isfinite (change) || !std::isfinite (m[i].cover))
          throw std::runtime_error ("non-finite Metal terrain");
      }
      result.tectonic_uplift = uplift_sum * u::m * d.cell_area ();
      result.exported_sediment = export_sum * u::m * d.cell_area ();
      result.balance_error = volume_error * u::m * d.cell_area ();
      result.drainage_area_error =
        (float (drained) - float (n)) * d.cell_area ();
      if (drained != n)
        throw std::runtime_error ("Metal catchments do not cover the domain: " +
                                  std::to_string (drained) + " of " +
                                  std::to_string (n));
      return result;
    }
  }

  LocalEvolutionBudget
  evolve_metal (map::SurfaceGeometry& surface,
                std::span<const meters_per_julian_year_t> uplift,
                const StreamPowerEvolution& model,
                bool multigrid) {
    @autoreleasepool {
      const auto& d = surface.domain ();
      auto& heights = spatial::get<surface_elevation> (surface);
      detail::validate_stream_power_evolution (d, heights, uplift, model);
      if (model.duration == 0 * year)
        return {};
      if (d.width () < 3 || d.height () < 3 || d.size () > (1u << 26))
        throw std::invalid_argument (
          "unsupported Metal evolution domain or time");
      Compute gpu;
      std::vector<Level> pyramid;
      pyramid.emplace_back (d);
      auto& cover = spatial::get<sediment_thickness> (surface);
      for (size_t i = 0; i < d.size (); ++i) {
        pyramid[0].base[i] = heights[i];
        pyramid[0].uplift[i] = uplift[i].numerical_value_in (u::m / year);
        if (!isfinite (cover[i]) || cover[i] < 0 * sediment_thickness[u::m])
          throw std::invalid_argument ("invalid initial sediment thickness");
        pyramid[0].material[i].cover = cover[i].numerical_value_in (u::m);
      }
      const double requested =
        std::ceil ((julian_years_f64_t (model.duration) / model.time_step)
                     .numerical_value_in (one));
      if (requested > std::numeric_limits<int>::max ())
        throw std::invalid_argument ("too many Metal evolution steps");
      unsigned total = static_cast<unsigned> (requested);
      const bool bare = std::ranges::all_of (
        cover, [] (auto depth) { return depth == SedimentThickness::zero (); });
      while (multigrid && bare && pyramid.back ().domain.width () >= 1024 &&
             pyramid.back ().domain.height () >= 1024 &&
             pyramid.back ().domain.width () % 2 == 0 &&
             pyramid.back ().domain.height () % 2 == 0 &&
             pyramid.size () < total)
        pyramid.push_back (restrict_level (pyramid.back ()));
      unsigned elapsed = 0;
      LocalEvolutionBudget result {};
      for (size_t l = pyramid.size (); l-- > 0;) {
        unsigned remaining = total - elapsed;
        unsigned steps =
          l ? std::min (remaining - unsigned (l), (remaining + 1) / 2)
            : remaining;
        auto budget = evolve_level (gpu, pyramid[l], model, elapsed, steps);
        elapsed += steps;
        result.tectonic_uplift += budget.tectonic_uplift;
        result.exported_sediment += budget.exported_sediment;
        result.balance_error += budget.balance_error;
        result.drainage_area_error = budget.drainage_area_error;
        if (l)
          refine (pyramid[l], pyramid[l - 1]);
      }
      auto& cut = spatial::get<map::eroded_surface_material> (surface);
      auto& fill = spatial::get<map::deposited_surface_material> (surface);
      for (size_t i = 0; i < d.size (); ++i) {
        auto m = pyramid[0].material[i];
        heights[i] += pyramid[0].change[i] * u::m;
        cover[i] = m.cover * sediment_thickness[u::m];
        cut[i] += m.cut * map::eroded_surface_material[u::m];
        fill[i] += m.fill * map::deposited_surface_material[u::m];
      }
      return result;
    }
  }
}

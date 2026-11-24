// Drawing the world each frame, layer by layer: the terrain and what grows
// and stands on it, the actors and their shadows, the water and the wakes,
// effects, and overlays such as the fingerposts and the scan.

#include <moppe/game/animal_render.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/fleet_render.hh>
#include <moppe/game/game.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>

namespace moppe {
  namespace game {
    namespace {
      // The ground as the scan reads it, in bare metres.
      GroundHeight scan_ground (const WorldGround& ground) {
        return [ground] (float x, float z) {
          return terrain::surface_elevation_value (
            ground.elevation (plan_position (x, z)));
        };
      }
    }

    void MoppeGame::draw_home_base_marker (render::DrawList& dl) const {
      dl.append (m_home_base_marker);
    }

    void MoppeGame::start_scan (duration_t ago) {
      m_scan.start (session ().walker ().position (),
                    logic ().m_total_time - ago,
                    m_mushrooms,
                    m_trunk_field,
                    scan_ground (world_ground ()));
    }

    void MoppeGame::draw_scan (render::Renderer& r, const FrameView& frame) {
      const FrameCamera& camera = frame.camera;
      // The drawn view's basis, from its matrix, as the post lighting
      // takes it.
      const Mat4& m = camera.view;
      const ScanView view {
        .eye = camera.position,
        .right = unit_direction (Vec3 (m.at (0, 0), m.at (1, 0), m.at (2, 0))),
        .up = unit_direction (Vec3 (m.at (0, 1), m.at (1, 1), m.at (2, 1))),
        .forward =
          unit_direction (Vec3 (-m.at (0, 2), -m.at (1, 2), -m.at (2, 2))),
        .tan_half_height =
          std::tan (0.5f * camera.field_of_view.numerical_value_in (u::rad)),
        .width_pts = static_cast<float> (r.width_pts ()),
        .height_pts = static_cast<float> (r.height_pts ()),
      };
      m_scan.draw (r,
                   m_heads_up.hud.font (),
                   logic ().m_total_time,
                   view,
                   session ().walker ().position (),
                   scan_ground (world_ground ()));
    }

    render::FrameParams MoppeGame::frame_params_for (const FrameView& frame) {
      const FrameLighting& lighting = frame.lighting;
      return {
        .view = frame.camera.view,
        .proj = frame.camera.projection,
        .camera_pos = frame.camera.position,
        .cam_right = frame.camera.right,
        .cam_up = frame.camera.up,
        .cam_forward = frame.camera.frame_forward,
        .clear_color = lighting.clear_color,
        .fog_scale = lighting.fog_scale,
        .sun_dir = lighting.sun_direction,
        .sun_diffuse = lighting.sun_diffuse,
        .sun_specular = lighting.sun_specular,
        .ambient = lighting.ambient,
        .exposure_bias = lighting.exposure_bias,
        .time = lighting.time,
        .cloud_cover = quantity_cast<proportion> (lighting.cloudiness),
        .mist = lighting.mist,
        .rain = lighting.rain,
        .sun_visibility = lighting.sun_visibility,
        .sky_sun_dir = lighting.sky_sun,
        .sky_moon_dir = lighting.sky_moon,
        .sky_pole = lighting.sky_pole,
        .sky_turn = lighting.sky_turn,
        .moonlight = lighting.moonlight,
        .lamp_pos = lighting.lamp_position,
        .lamp_reach = lighting.lamp_reach,
        .lamp_color = lighting.lamp_color,
        .scene_scale = frame.graphics.scene_scale,
        .render_scale_override = frame.graphics.render_scale_override,
        .scene_megapixel_budget = frame.graphics.scene_megapixel_budget,
        .frame_rate_target = frame.graphics.target_frame_rate,
        .bloom = frame.graphics.bloom,
        .auto_exposure = frame.graphics.auto_exposure,
        .lens_flare = frame.graphics.lens_flare,
        .profile = true,
      };
    }

    // The ground the animals stand on: the land, or a floor built on it.
    terrain::ElevationField MoppeGame::animal_ground () const {
      return [this] (plan_position_t at) {
        const terrain::SurfaceElevation ground = world_ground ().elevation (at);
        if (const std::optional<terrain::SurfaceElevation> floor =
              floor_at (m_places.layout, at))
          return std::max (*floor, ground);
        return ground;
      };
    }

    plan_extent_t MoppeGame::places_period () const {
      return m_places.layout.period;
    }

    void MoppeGame::cast_actor_shadows (render::Renderer& r,
                                        const FrameView& frame) {
      m_caster_dl.clear ();
      r.begin_shadow_casters ();
      // The bridges shade the river and the banks under them.
      m_bridges.draw (r, frame.camera.position);
      if (frame.scene != FrameSceneMode::Gameplay) {
        r.end_shadow_casters ();
        return;
      }
      const FrameActors& actors = frame.actors;
      cast_fleet (r, m_caster_dl, actors.fleet);
      m_places.landscape.cast_shadows (r, frame.camera.position);
      m_places.bus_view.cast_shadows (
        r, m_places.bus, bus_time (logic ()), frame.camera.position);
      render_animals (r,
                      session ().animals ().animals (),
                      animal_ground (),
                      frame.camera.position,
                      frame.camera.frame_forward,
                      frame.lighting.time,
                      70.0f * u::m,
                      places_period (),
                      0);
      m_signs.cast_shadow (r);
      if (m_company.companion)
        cast_fleet (r, m_caster_dl, m_company.companion->fleet);
      r.end_shadow_casters ();
    }

    void MoppeGame::draw_world_layers (render::Renderer& r,
                                       const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      const Vec3& camera = position_value (frame.camera.position);
      if (m_graphics.terrain_shadows)
        cast_actor_shadows (r, frame);
      if (m_graphics.terrain_shadows)
        m_terrain.render_local_shadow (r,
                                       frame.camera.position,
                                       frame.camera.frame_forward,
                                       frame.lighting.sun_direction,
                                       frame.lighting.sky_pole);
      // At this extreme altitude, drawing the far-plane dome after terrain
      // exposes depth precision at the horizon. Paint it first in the lab;
      // terrain then covers it deterministically. Gameplay retains the
      // cheaper depth-culled order after all opaque geometry.
      if (visibility.sky_before_terrain) {
        render::SkyParams sky;
        sky.time = frame.lighting.time;
        sky.sun_height = frame.lighting.sun_height;
        sky.cloudiness = quantity_cast<proportion> (frame.lighting.cloudiness);
        sky.sun_dir = frame.lighting.sun_direction;
        sky.fog_color = frame.lighting.fog_color;
        r.draw_sky (sky);
      }

      // Keep all opaque world geometry together before sky and blended
      // sward so hardware HSR and early-Z reject occluded terrain, sky,
      // and sward fragments behind boulders, forest crowns, and actors.
      m_terrain.render (r,
                        frame.camera.position,
                        frame.camera.projection * frame.camera.view,
                        frame.terrain_distance);
      m_bridges.draw (r, frame.camera.position);
      m_boulders.draw (r);
      m_mushrooms.follow (logic ().m_basket);
      m_mushrooms.draw (r, position (camera));
      m_forest.draw (r);
    }

    void MoppeGame::draw_actor_layers (render::Renderer& r,
                                       const FrameView& frame) {
      // The world draw list, in the GL build's draw order.
      m_world_dl.clear ();
      const FrameActors& actors = frame.actors;

      // Soft blob shadows under the movers.
      draw_home_base_marker (m_world_dl);
      m_signs.draw (r,
                    m_heads_up.hud.font (),
                    m_place_names,
                    [this] (const plan_position_t& at) {
                      return generated_world ().road_surface ().ground (
                        at, world_ground ().elevation (at));
                    });
      if (actors.walker)
        m_blob.draw (m_world_dl,
                     surface (),
                     actors.walker->position +
                       quantity_cast<isq::position_vector> (
                         displacement (Vec3 (0, 0.5f, 0))),
                     0.8f * u::m);
      if (actors.glider)
        m_blob.draw (
          m_world_dl, surface (), actors.glider->position, 3.4f * u::m);

      // In helmet cam you ARE the rider: don't draw yourself. The other
      // player rides along in the jeep, or on the boat's fore seat.
      const bool helmet = actors.helmet_camera;
      FleetRiders riders {};
      if (const std::optional<Machine> driven =
            driven_machine (actors.active_mode)) {
        Riders& who = riders[static_cast<std::size_t> (*driven)];
        who.from_the_seat = helmet;
        if (!helmet)
          who.driver = m_company.outfit;
      }
      if (const std::optional<Machine> seat =
            m_company.companion ? passenger_in (m_company.companion->mode)
                                : std::nullopt)
        riders[static_cast<std::size_t> (*seat)].passenger =
          m_company.their_outfit ();
      draw_fleet ({ .r = r,
                    .dl = m_world_dl,
                    .time = frame.lighting.time,
                    .rod = actors.helm_rod,
                    .blob = &m_blob,
                    .surface = &surface () },
                  actors.fleet,
                  riders,
                  false);
      if (actors.sling)
        render_sling (m_world_dl, *actors.sling);

      const Basket& basket = logic ().m_basket;
      m_basket_contents.clear ();
      for (std::size_t i = basket.used; i < basket.picked.size (); ++i)
        m_basket_contents.push_back (m_mushrooms.site (basket.picked[i]));
      draw_camp (m_world_dl);
      m_places.draw (r, m_world_dl, frame, logic ());
      // Who rides the buses: this player, and the other, whose buses
      // keep the same time (Companionship::keep_time).
      std::vector<BusRider> bus_riders;
      if (actors.active_mode == Doing::on_the_bus ())
        bus_riders.push_back ({ .bus = logic ().m_bus,
                                .seat = logic ().m_bus_seat,
                                .outfit = m_company.outfit,
                                .seen = !helmet });
      if (m_company.companion &&
          m_company.companion->mode == Doing::on_the_bus ())
        bus_riders.push_back ({ .bus = m_company.companion->bus,
                                .seat = m_company.companion->bus_seat,
                                .outfit = m_company.their_outfit () });
      m_places.bus_view.draw (r,
                              m_places.bus,
                              bus_time (logic ()),
                              frame.camera.position,
                              0xb05000,
                              bus_riders);
      render_animals (r,
                      session ().animals ().animals (),
                      animal_ground (),
                      frame.camera.position,
                      frame.camera.frame_forward,
                      frame.lighting.time,
                      450.0f * u::m,
                      places_period (),
                      0x200000);
      render_birds (r,
                    session ().hens ().birds (),
                    frame.camera.position,
                    frame.camera.frame_forward,
                    frame.lighting.time,
                    90.0f * u::m,
                    m_places.layout.period,
                    0x300000);
      draw_nest_eggs (m_world_dl,
                      m_places.nests,
                      logic ().m_nests,
                      frame.camera.position,
                      m_places.layout.period);
      if (actors.walker && lying_down ())
        draw_sleeper (m_world_dl, helmet, frame.lighting.time);
      else if (actors.walker && !helmet)
        render_walker (m_world_dl,
                       *actors.walker,
                       frame.lighting.time,
                       m_basket_contents,
                       m_company.outfit);
      else if (actors.walker)
        render_basket (
          m_world_dl, *actors.walker, frame.lighting.time, m_basket_contents);
      // The mushroom just picked rises from the moss into the basket.
      if (actors.walker && !basket.picked.empty () && !basket.last_refused) {
        const float age = seconds_value (
          seconds_between (logic ().m_total_time, basket.last_reach_time));
        if (age >= 0.0f && age < 0.45f) {
          const float t = age / 0.45f;
          MushroomSite rising = m_mushrooms.site (basket.picked.back ());
          const Vec3& walker_at = position_value (actors.walker->position);
          position_t rising_base = m_mushrooms.base_near (
            basket.picked.back (), actors.walker->position);
          const Vec3 to =
            walker_at +
            normalized (direction_value (actors.walker->heading)) * 0.2f +
            Vec3 (0, 0.55f, 0);
          rising_base = position (position_value (rising_base) +
                                  (to - position_value (rising_base)) * t +
                                  Vec3 (0, 0.35f * std::sin (PI * t), 0));
          rising.scale *= 1.0f - 0.5f * t;
          rising.yaw += 3.0f * t * heading[u::rad];
          m_world_dl.state (render::DrawState ());
          m_world_dl.lit (true);
          m_world_dl.fogged (true);
          m_world_dl.begin (render::Prim::Triangles);
          draw_mushroom (m_world_dl, rising, rising_base);
          m_world_dl.end ();
        }
      }
      if (actors.glider && !helmet)
        render_glider (r,
                       m_world_dl,
                       *actors.glider,
                       frame.lighting.time,
                       0x2000,
                       m_company.outfit);
      m_company.draw (r,
                      m_world_dl,
                      frame.lighting.time,
                      logic (),
                      m_mushrooms,
                      m_blob,
                      surface (),
                      world_ground ());

      r.draw_list (m_world_dl, 0x0001);
    }

    void MoppeGame::draw_sky_and_undergrowth (render::Renderer& r,
                                              const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      // Sky after all opaque world and actor geometry so depth testing and
      // TBDR HSR skip clouds behind terrain, trees, boulders, and buildings.
      if (visibility.sky_after_terrain) {
        render::SkyParams sky;
        sky.time = frame.lighting.time;
        sky.sun_height = frame.lighting.sun_height;
        sky.cloudiness = quantity_cast<proportion> (frame.lighting.cloudiness);
        sky.sun_dir = frame.lighting.sun_direction;
        sky.fog_color = frame.lighting.fog_color;
        r.draw_sky (sky);
      }

      // Undergrowth (blended sward canopy + grass blades) after all opaque
      // blockers so sward neither flushes TBDR HSR for trees/actors nor
      // shades pixels behind them.
      if (!visibility.undergrowth)
        return;
      meters_t interaction_radius = 0.0f * u::m;
      if (frame.scene == FrameSceneMode::Gameplay) {
        switch (frame.actors.active_mode.way) {
        case Doing::Way::foot:
          interaction_radius = 0.55f * u::m;
          break;
        case Doing::Way::glider:
        case Doing::Way::passenger:
        case Doing::Way::bus:
          break;
        case Doing::Way::driving:
          interaction_radius =
            machine_kind (frame.actors.active_mode.machine).undergrowth_parting;
          break;
        }
      }
      render::UndergrowthParams undergrowth {
        .time = frame.lighting.time,
        .cloud_cover = quantity_cast<proportion> (frame.lighting.cloudiness),
        .reach = 64.0f * u::m,
        .interaction_position = frame.hud.subject_position,
        .interaction_radius = interaction_radius
      };
      // No grass under the floor of the farmhouse and the barn
      // nearest the camera, which can be walked into: out to the
      // plinth's edge, and over the barn's ramp; and round the barn
      // its yard is trodden short.
      const Vec3& eye = position_value (frame.camera.position);
      meters_t nearest[2] = { 150.0f * u::m, 150.0f * u::m };
      for (const Farm& farm : m_places.layout.farms)
        for (const Building& b : farm.buildings) {
          if (farm.ruined || b.kind == Building::Kind::mill)
            continue;
          Vec3 at = position_value (b.at);
          at[0] += terrain::whole_periods (
            eye[0] - at[0], plan_value (m_places.layout.period).x ());
          at[2] += terrain::whole_periods (
            eye[2] - at[2], plan_value (m_places.layout.period).z ());
          const meters_t d = std::hypot (at[0] - eye[0], at[2] - eye[2]) * u::m;
          const bool barn = b.kind == Building::Kind::barn;
          if (d >= nearest[barn])
            continue;
          nearest[barn] = d;
          Vec3 f = direction_value (b.forward);
          f[1] = 0.0f;
          f = normalized (f);
          // Along the floor's length: its forward, turned a right angle.
          const direction_t along = unit_direction (Vec3 (f[2], 0.0f, -f[0]));
          if (barn) {
            undergrowth.yard_centre = position (at + f * 0.95f);
            undergrowth.yard_along = along;
            undergrowth.yard_half_x = 6.15f * u::m;
            undergrowth.yard_half_z = 5.1f * u::m;
            undergrowth.yard_margin = (farm.kept ? 9.0f : 0.0f) * u::m;
          } else {
            undergrowth.bare_centre = position (at);
            undergrowth.bare_along = along;
            undergrowth.bare_half_x = 3.75f * u::m;
            undergrowth.bare_half_z = 2.75f * u::m;
          }
        }
      r.draw_undergrowth (undergrowth);
    }

    namespace {
      struct PostBasis {
        direction_t forward;
        Vec3 right_span;
        Vec3 up_span;
      };

      PostBasis post_basis (const FrameCamera& camera) {
        const Mat4& view = camera.view;
        const Vec3 right (view.at (0, 0), view.at (1, 0), view.at (2, 0));
        const Vec3 up (view.at (0, 1), view.at (1, 1), view.at (2, 1));
        const Vec3 forward (-view.at (0, 2), -view.at (1, 2), -view.at (2, 2));
        const float half_tangent = tan (camera.field_of_view * 0.5f);
        return {
          .forward = unit_direction (forward),
          .right_span =
            right * (half_tangent * camera.aspect.numerical_value_in (one)),
          .up_span = up * half_tangent,
        };
      }
    }

    void MoppeGame::apply_scene_occlusion (render::Renderer& r,
                                           const FrameView& frame) {
      // Ambient occlusion runs on the opaque scene before translucent
      // water, glass, wakes, glows, and rain blend over it, so background
      // depth creases never darken translucent surfaces in front of them.
      if (frame.visibility.underwater)
        return;
      const PostBasis basis = post_basis (frame.camera);
      r.apply_gtao ({
        .camera_pos = frame.camera.position,
        .forward = basis.forward,
        .right_span = basis.right_span,
        .up_span = basis.up_span,
      });
    }

    void MoppeGame::draw_actor_glows (render::Renderer& r,
                                      const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      if (frame.scene != FrameSceneMode::Gameplay)
        return;
      const FrameActors& actors = frame.actors;
      const bool helmet = actors.helmet_camera;

      if (visibility.vehicle_effects && logic ().m_snow.revision ()) {
        m_world_dl.clear ();
        session ().dust ().draw_powder (m_world_dl, frame.environment);
        if (!m_world_dl.empty ())
          r.draw_list (m_world_dl);
      }

      // Additive glow after the solid list, water, and occlusion, so it
      // blends un-darkened over everything already drawn: exhaust and
      // jump-jet flames, fires, star halos, and scan overlays.
      if (visibility.vehicle_effects &&
          !(helmet && actors.active_mode == Doing::driving (Machine::bike)))
        render_vehicle_flames (r,
                               actors.fleet.bike,
                               frame.camera.position,
                               frame.lighting.time,
                               0x1000);
      m_places.landscape.draw_beacon_fires (r,
                                            frame.camera.position,
                                            logic ().m_beacon_burn,
                                            frame.lighting.daylight,
                                            frame.lighting.time);
      if (hearth_laid (logic ().m_camp))
        draw_fire (r,
                   logic ().m_camp.hearth,
                   frame.camera.position,
                   fire_burn (logic ().m_camp, logic ().m_total_time),
                   frame.lighting.time);
      if (m_company.companion) {
        if (visibility.vehicle_effects)
          render_vehicle_flames (r,
                                 m_company.companion->fleet.bike,
                                 frame.camera.position,
                                 frame.lighting.time,
                                 0x3000);
        if (m_company.companion->hearth_laid)
          draw_fire (r,
                     m_company.companion->hearth,
                     frame.camera.position,
                     m_company.companion->fire_burn,
                     frame.lighting.time);
      }
      if (visibility.star_effects)
        session ().stars ().render (r, frame.environment);
      draw_scan (r, frame);
    }

    void MoppeGame::draw_water_surfaces (render::Renderer& r,
                                         const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      const Vec3& camera = position_value (frame.camera.position);

      // The lab keeps the game's painted water while the map is the game's
      // own; a rebuilt map invalidates the water sheets, so they disappear
      // until the lab's own analysis draws ribbons.
      if (visibility.ocean) {
        render::OceanParams ocean;
        ocean.time = frame.lighting.time;
        ocean.fog_color = frame.lighting.fog_color;
        ocean.fog_scale = frame.lighting.fog_scale;
        const Vec3& world_extent = extent_value (world ().map_size);
        const Vec3 center (0.5f * world_extent[0], 0, 0.5f * world_extent[2]);
        // The camera from the world's middle, seen from above.
        ocean.world_offset = displacement (
          Vec3 (camera[0] - center[0], 0.0f, camera[2] - center[2]));
        r.draw_ocean (ocean);
      }

      // Running channels are part of the same clipped water-level field as
      // lakes and the sea. Only vertical nickpoint geometry is separate;
      // ordinary reaches never draw an overlapping ribbon here.
      m_waterfall_surface.draw (r, position (camera));
    }

    void MoppeGame::draw_boat_wakes (render::Renderer& r,
                                     const FrameView& frame) {
      if (!frame.visibility.ocean)
        return;
      const auto sheets = generated_world ().water_view ();
      const seconds_t time = frame.lighting.time;
      const auto level = [&] (const position_t& at) {
        return water_surface_at (
          world (), surface (), sheets, at, seconds_value (time));
      };
      const CompanionSnapshot* theirs =
        m_company.companion ? &*m_company.companion : nullptr;
      m_wakes.draw (
        r,
        frame.actors.fleet.boat,
        frame.actors.fleet.jet_ski,
        theirs ? &theirs->fleet.boat : nullptr,
        theirs ? &theirs->fleet.jet_ski : nullptr,
        level,
        [&] (const position_t& at) {
          return terrain::surface_elevation_value (
                   world_ground ().elevation (plan (at))) *
                 u::m;
        },
        time,
        m_sky.sky ().daylight);
    }

    void MoppeGame::draw_effect_layers (render::Renderer& r,
                                        const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;

      // Sun shafts march the camera-local shadow map, so the beams carry
      // the same tree and terrain shapes as the ground shadows. Underwater
      // frames get their own grade instead. Both shade the jittered scene,
      // so they come before its reconstruction.
      if (!visibility.underwater &&
          direction_value (frame.lighting.sun_direction)[1] > 0.02f) {
        const PostBasis basis = post_basis (frame.camera);
        r.apply_light_shafts ({
          .camera_pos = frame.camera.position,
          .forward = basis.forward,
          .right_span = basis.right_span,
          .up_span = basis.up_span,
          .sun_dir = frame.lighting.sun_direction,
          .sun_color = frame.lighting.sun_diffuse,
          .strength = 0.55f * one,
        });
      }

      // Grade near the waterline as well as below it: the shader decides
      // per pixel whether the near plane is wet, before reconstruction
      // accumulates the boundary with the rest of the jittered scene.
      if (visibility.ocean && frame.camera_water_level &&
          position_value (frame.camera.position)[1] <
            terrain::surface_elevation_value (*frame.camera_water_level) + 1.0f)
        r.apply_underwater ({
          .water_level =
            terrain::surface_elevation_value (*frame.camera_water_level) * u::m,
        });

      // Reconstruction consumes the scene's colour, depth, and motion;
      // motion blur operates on its full-size result.
      r.reconstruct_scene ();
      if (visibility.motion_blur)
        r.apply_motion_blur (frame.motion_blur_amount.numerical_value_in (one));
    }
  }
}

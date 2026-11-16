// Drawing the world each frame, layer by layer: the terrain and what grows
// and stands on it, the actors and their shadows, the water and the wakes,
// effects, and overlays such as the fingerposts and the scan.

#include <moppe/game/game.hh>
#include <moppe/terrain/torus.hh>

namespace moppe {
  namespace game {
    void MoppeGame::draw_home_base_marker (render::DrawList& dl) const {
      dl.append (m_home_base_marker);
    }

    void MoppeGame::draw_fingerposts (render::Renderer& r) {
      if (!m_fingerposts_made && m_hud.font ()) {
        m_fingerposts_made = true;
        // The signs letter in the face's heavier cut, which carries
        // further.
        if (!m_sign_font)
          m_sign_font = render::Font::load (r, "fonts/Geomini-SemiBold.ttf");
        FingerpostSigns signs =
          make_fingerposts (r,
                            m_sign_font->ok () ? *m_sign_font : *m_hud.font (),
                            m_fingerposts,
                            m_place_names,
                            [this] (float x, float z) {
                              return generated_world ().road_surface ().ground (
                                x, z, ground_height (Vec3 (x, 0.0f, z)));
                            });
        m_fingerpost_mesh =
          signs.mesh.empty () ? nullptr : r.create_mesh (signs.mesh);
        m_fingerpost_lettering = signs.lettering.empty ()
                                   ? nullptr
                                   : r.create_world_text (signs.lettering);
      }
      if (m_fingerpost_mesh)
        r.draw_mesh (*m_fingerpost_mesh, Mat4 {}, 0);
      if (m_fingerpost_lettering)
        r.draw_world_text (*m_fingerpost_lettering);
    }

    void MoppeGame::start_scan (double ago) {
      const Vec3 feet = session ().walker ().position ();
      std::vector<ScanFind> finds = scan_mushrooms (m_mushrooms, feet);
      const std::vector<mov::Trunk> trunks =
        m_trunk_field.gather (feet, scan_reach + 6.0f);
      const std::vector<ScanFind> trees = scan_hammocks (
        hammock_sites_near (trunks, feet, scan_reach, ground_reader ()));
      finds.insert (finds.end (), trees.begin (), trees.end ());
      std::cerr << "moppe: scan from " << feet[0] << " " << feet[1] << " "
                << feet[2] << " found " << finds.size () << "\n";
      m_scan.start (feet, logic ().m_total_time - ago, std::move (finds));
    }

    void MoppeGame::draw_scan (render::Renderer& r, const FrameView& frame) {
      const double now = logic ().m_total_time;
      if (!m_scan.showing (now) || !m_hud.font ())
        return;
      if (!m_scan_marks.set)
        m_scan_marks = make_scan_marks (r);
      m_scan_dl.clear ();
      draw_scan_glow (
        m_scan_dl, m_scan, now, frame.camera.position, ground_reader ());
      r.draw_list (m_scan_dl, 0);
      m_scan_text.clear ();
      const FrameCamera& camera = frame.camera;
      // The drawn view's basis, from its matrix, as the post lighting
      // takes it.
      const Mat4& m = camera.view;
      const ScanView view {
        .eye = camera.position,
        .right = Vec3 (m.at (0, 0), m.at (1, 0), m.at (2, 0)),
        .up = Vec3 (m.at (0, 1), m.at (1, 1), m.at (2, 1)),
        .forward = Vec3 (-m.at (0, 2), -m.at (1, 2), -m.at (2, 2)),
        .tan_half_height =
          std::tan (0.5f * camera.field_of_view.numerical_value_in (u::rad)),
        .width_pts = static_cast<float> (r.width_pts ()),
        .height_pts = static_cast<float> (r.height_pts ()),
      };
      draw_scan_labels (m_scan_text,
                        *m_hud.font (),
                        m_scan_marks,
                        m_scan,
                        now,
                        session ().walker ().position (),
                        view);
      r.draw_world_labels (m_scan_text);
    }

    render::FrameParams MoppeGame::frame_params_for (const FrameView& frame) {
      render::FrameParams params;
      params.view = frame.camera.view;
      params.proj = frame.camera.projection;
      params.camera_pos = frame.camera.position;
      params.cam_right = frame.camera.right;
      params.cam_up = frame.camera.up;
      params.cam_forward = frame.camera.frame_forward;
      params.clear_color = frame.lighting.clear_color;
      params.fog_scale = attenuation_value (frame.lighting.fog_scale);
      params.sun_dir = frame.lighting.sun_direction;
      params.sun_diffuse = frame.lighting.sun_diffuse;
      params.sun_specular = frame.lighting.sun_specular;
      params.ambient = frame.lighting.ambient;
      params.exposure_bias = frame.lighting.exposure_bias;
      params.time = frame.lighting.time;
      params.cloud_cover = frame.lighting.cloudiness.numerical_value_in (one);
      params.mist = frame.lighting.mist;
      params.rain = frame.lighting.rain;
      params.sun_visibility = frame.lighting.sun_visibility;
      params.sky_sun_dir = frame.lighting.sky_sun;
      params.sky_moon_dir = frame.lighting.sky_moon;
      params.sky_pole = frame.lighting.sky_pole;
      params.sky_turn = frame.lighting.sky_turn;
      params.moonlight = frame.lighting.moonlight;
      params.lamp_pos = frame.lighting.lamp_position;
      params.lamp_reach = frame.lighting.lamp_reach;
      params.lamp_color = frame.lighting.lamp_color;
      params.scene_scale = frame.graphics.scene_scale;
      params.render_scale_override = frame.graphics.render_scale_override;
      params.scene_megapixel_budget = frame.graphics.scene_megapixel_budget;
      params.frame_rate_target = frame.graphics.target_frame_rate;
      params.bloom = frame.graphics.bloom;
      params.auto_exposure = frame.graphics.auto_exposure;
      params.lens_flare = frame.graphics.lens_flare;
      params.profile = true;
      return params;
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
      render_vehicle (r, m_caster_dl, actors.bike, false, 0);
      render_mountain_bike (
        r, m_caster_dl, actors.mountain_bike, std::nullopt, 0);
      render_long_john (r, m_caster_dl, actors.long_john, std::nullopt, 0);
      render_jeep (r, m_caster_dl, actors.jeep, {}, 0);
      render_caddy (r, m_caster_dl, actors.caddy, {}, 0);
      render_tractor (r, m_caster_dl, actors.tractor, std::nullopt, 0);
      render_helicopter (r, actors.helicopter, 0);
      render_boat (r, m_caster_dl, actors.boat, std::nullopt, 0);
      render_jet_ski (r, m_caster_dl, actors.jet_ski, std::nullopt, 0);
      m_places_landscape.cast_shadows (r, frame.camera.position);
      render_animals (
        r,
        session ().animals ().animals (),
        [this] (float x, float z) {
          const float ground = ground_height (Vec3 (x, 0.0f, z));
          if (const std::optional<float> floor = floor_at (m_places, x, z))
            return std::max (*floor, ground);
          return ground;
        },
        frame.camera.position,
        frame.camera.frame_forward,
        frame.lighting.time,
        70.0f,
        m_places.period_x,
        m_places.period_z,
        0);
      if (m_fingerpost_mesh)
        r.draw_mesh (*m_fingerpost_mesh, Mat4 {}, 0);
      if (m_companion) {
        const CompanionSnapshot& theirs = *m_companion;
        render_vehicle (r, m_caster_dl, theirs.bike, false, 0);
        render_mountain_bike (
          r, m_caster_dl, theirs.mountain_bike, std::nullopt, 0);
        render_long_john (r, m_caster_dl, theirs.long_john, std::nullopt, 0);
        render_jeep (r, m_caster_dl, theirs.jeep, {}, 0);
        render_caddy (r, m_caster_dl, theirs.caddy, {}, 0);
        render_tractor (r, m_caster_dl, theirs.tractor, std::nullopt, 0);
        render_helicopter (r, theirs.helicopter, 0);
        render_boat (r, m_caster_dl, theirs.boat, std::nullopt, 0);
        render_jet_ski (r, m_caster_dl, theirs.jet_ski, std::nullopt, 0);
      }
      r.end_shadow_casters ();
    }

    void MoppeGame::draw_world_layers (render::Renderer& r,
                                       const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      const Vec3& camera = frame.camera.position;
      if (m_graphics.terrain_shadows)
        cast_actor_shadows (r, frame);
      if (m_graphics.terrain_shadows)
        m_terrain.render_local_shadow (r,
                                       position (camera),
                                       frame.camera.frame_forward,
                                       frame.lighting.sun_direction,
                                       frame.lighting.sky_pole);
      const auto draw_world_sky = [&] {
        render::SkyParams sky;
        sky.time = frame.lighting.time;
        sky.sun_height = frame.lighting.sun_height;
        sky.cloudiness = frame.lighting.cloudiness.numerical_value_in (one);
        sky.sun_dir = frame.lighting.sun_direction;
        sky.fog_color = frame.lighting.fog_color;
        r.draw_sky (sky);
      };

      // At this extreme altitude, drawing the far-plane dome after terrain
      // exposes depth precision at the horizon. Paint it first in the lab;
      // terrain then covers it deterministically. Gameplay retains the
      // cheaper depth-culled order below.
      if (visibility.sky_before_terrain)
        draw_world_sky ();

      // Terrain first, chunk-culled to the haze horizon.
      m_terrain.render (r,
                        camera,
                        frame.camera.projection * frame.camera.view,
                        frame.terrain_distance);
      m_bridges.draw (r, camera);

      // Sky AFTER the terrain: depth testing kills the expensive
      // cloud shader wherever terrain covers it.
      if (visibility.sky_after_terrain)
        draw_world_sky ();

      // The floor grows itself from the same canopy and moisture fields the
      // trees were planted from, so it arrives already agreeing with them.
      // Gameplay movers part the generated field locally; cinematics keep
      // their authored floor undisturbed even though actors may exist.
      if (visibility.undergrowth) {
        float interaction_radius = 0.0f;
        if (frame.scene == FrameSceneMode::Gameplay) {
          switch (frame.actors.active_mode) {
          case M_BIKE:
            interaction_radius = 1.15f;
            break;
          case M_FOOT:
            interaction_radius = 0.55f;
            break;
          case M_JEEP:
            interaction_radius = 1.7f;
            break;
          case M_TRACTOR:
            interaction_radius = 1.8f;
            break;
          case M_CADDY:
            interaction_radius = 1.6f;
            break;
          case M_MOUNTAIN_BIKE:
            interaction_radius = 0.6f;
            break;
          case M_LONG_JOHN:
            interaction_radius = 0.8f;
            break;
          case M_HELICOPTER:
            // The downwash flattens the grass under it.
            interaction_radius = 4.0f;
            break;
          case M_GLIDER:
          case M_PASSENGER:
          case M_BOAT:
          case M_JET_SKI:
            break;
          }
        }
        render::UndergrowthParams undergrowth {
          .time = frame.lighting.time,
          .cloud_cover = frame.lighting.cloudiness.numerical_value_in (one),
          .reach = 64.0f,
          .interaction_position = frame.hud.subject_position,
          .interaction_radius = interaction_radius
        };
        // No grass under the floor of the farmhouse and the barn
        // nearest the camera, which can be walked into: out to the
        // plinth's edge, and over the barn's ramp; and round the barn
        // its yard is trodden short.
        const Vec3 eye = frame.camera.position;
        float nearest[2] = { 150.0f, 150.0f };
        for (const Farm& farm : m_places.farms)
          for (const Building& b : farm.buildings) {
            if (farm.ruined || b.kind == Building::Kind::mill)
              continue;
            Vec3 at = b.at;
            at[0] += terrain::whole_periods (eye[0] - at[0], m_places.period_x);
            at[2] += terrain::whole_periods (eye[2] - at[2], m_places.period_z);
            const float d = std::hypot (at[0] - eye[0], at[2] - eye[2]);
            const bool barn = b.kind == Building::Kind::barn;
            if (d >= nearest[barn])
              continue;
            nearest[barn] = d;
            Vec3 f = b.forward;
            f[1] = 0.0f;
            f = normalized (f);
            if (barn) {
              undergrowth.yard_centre = at + f * 0.95f;
              undergrowth.yard_along = Vec3 (f[2], 0.0f, -f[0]);
              undergrowth.yard_half_x = 6.15f;
              undergrowth.yard_half_z = 5.1f;
              undergrowth.yard_margin = farm.kept ? 9.0f : 0.0f;
            } else {
              undergrowth.bare_centre = at;
              undergrowth.bare_along = Vec3 (f[2], 0.0f, -f[0]);
              undergrowth.bare_half_x = 3.75f;
              undergrowth.bare_half_z = 2.75f;
            }
          }
        r.draw_undergrowth (undergrowth);
      }

      // Opaque individuals depth-test normally. The distant stand quotient
      // follows the ground medium so its non-depth-writing canopy roof
      // cannot be painted over by the sward's own far density layer.
      m_boulders.draw (r);
      m_mushrooms.follow (logic ().m_basket);
      m_mushrooms.draw (r, camera);
      m_forest.draw (r);
    }

    void MoppeGame::draw_actor_layers (render::Renderer& r,
                                       const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;

      // The world draw list, in the GL build's draw order.
      m_world_dl.clear ();
      const FrameActors& actors = frame.actors;

      // Soft blob shadows under the movers.
      draw_home_base_marker (m_world_dl);
      draw_fingerposts (r);
      m_blob.draw (m_world_dl, surface (), actors.bike.position, 2.2f);
      if (actors.walker)
        m_blob.draw (m_world_dl,
                     surface (),
                     actors.walker->position + Vec3 (0, 0.5f, 0),
                     0.8f);
      if (actors.glider)
        m_blob.draw (m_world_dl, surface (), actors.glider->position, 3.4f);

      // In helmet cam you ARE the rider: don't draw yourself.
      const bool helmet = actors.helmet_camera;
      if (!(helmet && actors.active_mode == M_BIKE))
        render_vehicle (r,
                        m_world_dl,
                        actors.bike,
                        actors.active_mode == M_BIKE,
                        0x1000,
                        m_outfit);
      m_blob.draw (m_world_dl, surface (), actors.jeep.position, 3.4f);
      render_jeep (r,
                   m_world_dl,
                   actors.jeep,
                   { .driver = actors.active_mode == M_JEEP && !helmet,
                     .passenger = m_companion &&
                                  m_companion->mode == M_PASSENGER &&
                                  !m_companion->in_boat,
                     .driver_outfit = m_outfit,
                     .passenger_outfit = their_outfit () },
                   0x5000);
      m_blob.draw (m_world_dl, surface (), actors.helicopter.position, 4.5f);
      render_helicopter (r, actors.helicopter, 0x8000);
      m_blob.draw (m_world_dl, surface (), actors.tractor.position, 2.8f);
      m_blob.draw (
        m_world_dl, surface (), actors.tractor.trailer_position, 2.4f);
      render_tractor (r,
                      m_world_dl,
                      actors.tractor,
                      actors.active_mode == M_TRACTOR && !helmet
                        ? std::optional<Outfit> (m_outfit)
                        : std::nullopt,
                      0xA000);
      m_blob.draw (m_world_dl, surface (), actors.caddy.position, 3.6f);
      render_caddy (r,
                    m_world_dl,
                    actors.caddy,
                    { .driver = actors.active_mode == M_CADDY && !helmet,
                      .driver_outfit = m_outfit },
                    0xC000);
      if (actors.sling)
        render_sling (m_world_dl, *actors.sling);
      render_boat (r,
                   m_world_dl,
                   actors.boat,
                   actors.active_mode == M_BOAT && !helmet
                     ? std::optional<Outfit> (m_outfit)
                     : std::nullopt,
                   0xF000,
                   actors.helm_rod,
                   frame.lighting.time,
                   m_companion && m_companion->mode == M_PASSENGER &&
                       m_companion->in_boat
                     ? std::optional<Outfit> (their_outfit ())
                     : std::nullopt);
      m_blob.draw (m_world_dl, surface (), actors.mountain_bike.position, 1.3f);
      // Through the rider's eyes, the bars and the fork below them.
      render_mountain_bike (r,
                            m_world_dl,
                            actors.mountain_bike,
                            actors.active_mode == M_MOUNTAIN_BIKE && !helmet
                              ? std::optional<Outfit> (m_outfit)
                              : std::nullopt,
                            0xD000);
      m_blob.draw (m_world_dl, surface (), actors.long_john.position, 2.0f);
      render_long_john (r,
                        m_world_dl,
                        actors.long_john,
                        actors.active_mode == M_LONG_JOHN && !helmet
                          ? std::optional<Outfit> (m_outfit)
                          : std::nullopt,
                        0xD100);
      render_jet_ski (r,
                      m_world_dl,
                      actors.jet_ski,
                      actors.active_mode == M_JET_SKI && !helmet
                        ? std::optional<Outfit> (m_outfit)
                        : std::nullopt,
                      0xD200);

      const Basket& basket = logic ().m_basket;
      m_basket_contents.clear ();
      for (std::size_t i = basket.used; i < basket.picked.size (); ++i)
        m_basket_contents.push_back (m_mushrooms.site (basket.picked[i]));
      draw_camp (m_world_dl);
      draw_places (r, frame);
      render_animals (
        r,
        session ().animals ().animals (),
        [this] (float x, float z) {
          const float ground = ground_height (Vec3 (x, 0.0f, z));
          if (const std::optional<float> floor = floor_at (m_places, x, z))
            return std::max (*floor, ground);
          return ground;
        },
        frame.camera.position,
        frame.camera.frame_forward,
        frame.lighting.time,
        450.0f,
        m_places.period_x,
        m_places.period_z,
        0x200000);
      render_birds (r,
                    session ().hens ().birds (),
                    frame.camera.position,
                    frame.camera.frame_forward,
                    frame.lighting.time,
                    90.0f,
                    m_places.period_x,
                    m_places.period_z,
                    0x300000);
      draw_nest_eggs (m_world_dl,
                      m_nest_sites,
                      logic ().m_nests,
                      frame.camera.position,
                      m_places.period_x,
                      m_places.period_z);
      if (actors.walker && lying_down ())
        draw_sleeper (m_world_dl, helmet, frame.lighting.time);
      else if (actors.walker && !helmet)
        render_walker (m_world_dl,
                       *actors.walker,
                       frame.lighting.time,
                       m_basket_contents,
                       m_outfit);
      else if (actors.walker)
        render_basket (
          m_world_dl, *actors.walker, frame.lighting.time, m_basket_contents);
      // The mushroom just picked rises from the moss into the basket.
      if (actors.walker && !basket.picked.empty () && !basket.last_refused) {
        const float age =
          static_cast<float> (logic ().m_total_time - basket.last_reach_time);
        if (age >= 0.0f && age < 0.45f) {
          const float t = age / 0.45f;
          MushroomSite rising = m_mushrooms.site (basket.picked.back ());
          rising.base = m_mushrooms.base_near (basket.picked.back (),
                                               actors.walker->position);
          const Vec3 to = actors.walker->position +
                          normalized (actors.walker->heading) * 0.2f +
                          Vec3 (0, 0.55f, 0);
          rising.base = rising.base + (to - rising.base) * t +
                        Vec3 (0, 0.35f * std::sin (PI * t), 0);
          rising.scale *= 1.0f - 0.5f * t;
          rising.yaw += 3.0f * t;
          m_world_dl.state (render::DrawState ());
          m_world_dl.lit (true);
          m_world_dl.fogged (true);
          m_world_dl.begin (render::Prim::Triangles);
          draw_mushroom (m_world_dl, rising);
          m_world_dl.end ();
        }
      }
      if (actors.glider && !helmet)
        render_glider (
          r, m_world_dl, *actors.glider, frame.lighting.time, 0x2000, m_outfit);
      draw_companion (r, m_world_dl, frame.lighting.time);

      r.draw_list (m_world_dl, 0x0001);

      // Additive glow after the solid list, so it blends over everything
      // already drawn: exhaust and jump-jet flames, then star halos.
      if (visibility.vehicle_effects &&
          !(helmet && actors.active_mode == M_BIKE))
        render_vehicle_flames (
          r, actors.bike, frame.camera.position, frame.lighting.time, 0x1000);
      m_places_landscape.draw_beacon_fires (r,
                                            frame.camera.position,
                                            logic ().m_beacon_burn,
                                            frame.lighting.daylight,
                                            frame.lighting.time);
      if (logic ().m_camp.fire_time > -50.0)
        draw_fire (r,
                   logic ().m_camp.hearth,
                   frame.camera.position,
                   fire_burn (logic ().m_camp, logic ().m_total_time),
                   frame.lighting.time);
      if (m_companion) {
        if (visibility.vehicle_effects)
          render_vehicle_flames (r,
                                 m_companion->bike,
                                 frame.camera.position,
                                 frame.lighting.time,
                                 0x3000);
        if (m_companion->hearth_laid)
          draw_fire (r,
                     m_companion->hearth,
                     frame.camera.position,
                     m_companion->fire_burn,
                     frame.lighting.time);
      }
      if (visibility.star_effects)
        session ().stars ().render (r, frame.environment);
      draw_scan (r, frame);
    }

    void MoppeGame::draw_water_surfaces (render::Renderer& r,
                                         const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;
      const Vec3& camera = frame.camera.position;

      // The lab keeps the game's painted water while the map is the game's
      // own; a rebuilt map invalidates the water sheets, so they disappear
      // until the lab's own analysis draws ribbons.
      if (visibility.ocean) {
        render::OceanParams ocean;
        ocean.time = frame.lighting.time;
        ocean.fog_color = frame.lighting.fog_color;
        ocean.fog_scale = attenuation_value (frame.lighting.fog_scale);
        const Vec3& world_extent = extent_value (world ().map_size);
        const Vec3 center (0.5f * world_extent[0], 0, 0.5f * world_extent[2]);
        ocean.world_offset[0] = camera[0] - center[0];
        ocean.world_offset[2] = camera[2] - center[2];
        r.draw_ocean (ocean);
      }

      // Running channels are part of the same clipped water-level field as
      // lakes and the sea. Only vertical nickpoint geometry is separate;
      // ordinary reaches never draw an overlapping ribbon here.
      m_waterfall_surface.draw (r, camera);
    }

    void MoppeGame::draw_boat_wakes (render::Renderer& r,
                                     const FrameView& frame) {
      if (!frame.visibility.ocean)
        return;
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const float time = frame.lighting.time;
      const auto level = [&] (const Vec3& at) {
        return water_surface_at (world (), surface (), &sheets, at, time)
          .numerical_value_in (u::m);
      };
      const auto depth = [&] (const Vec3& at) {
        return level (at) - ground_height (at);
      };
      // The jet ski leaves a wake as a boat of its place and heading.
      const auto as_boat = [] (const JetSkiPose& ski) {
        return BoatPose { .position = ski.position,
                          .forward = ski.forward,
                          .up = ski.up };
      };
      const auto trail = [&] (BoatWake& wake, const BoatPose& boat) {
        const Vec3& at = boat.position;
        wake.follow (boat, level (at), depth (at), time);
        wake.draw (m_wake_dl, time, m_sky.daylight, level);
      };
      m_wake_dl.clear ();
      trail (m_boat_wake, frame.actors.boat);
      trail (m_jet_ski_wake, as_boat (frame.actors.jet_ski));
      if (m_companion) {
        trail (m_their_wake, m_companion->boat);
        trail (m_their_jet_ski_wake, as_boat (m_companion->jet_ski));
      } else {
        m_their_wake.clear ();
        m_their_jet_ski_wake.clear ();
      }
      r.draw_list (m_wake_dl, 0x0002);
    }

    void MoppeGame::draw_effect_layers (render::Renderer& r,
                                        const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;

      // Screen-space post lighting shares the shaken camera basis,
      // extracted straight from the final view matrix with the frustum
      // half-extents folded into the right/up spans. Occlusion first so
      // the added beams are not darkened; sun shafts march the
      // camera-local shadow map, so the beams carry the same tree and
      // terrain shapes as the ground shadows. Underwater frames get
      // their own grade instead. Both shade the jittered scene, so they
      // come before its reconstruction.
      if (!visibility.underwater) {
        const FrameCamera& camera = frame.camera;
        const Mat4& view = camera.view;
        const Vec3 right (view.at (0, 0), view.at (1, 0), view.at (2, 0));
        const Vec3 up (view.at (0, 1), view.at (1, 1), view.at (2, 1));
        const Vec3 forward (-view.at (0, 2), -view.at (1, 2), -view.at (2, 2));
        const float half_tangent = tan (camera.field_of_view * 0.5f);
        const Vec3 right_span = right * (half_tangent * camera.aspect);
        const Vec3 up_span = up * half_tangent;
        r.apply_gtao ({
          .camera_pos = position (camera.position),
          .forward = forward,
          .right_span = right_span,
          .up_span = up_span,
        });
        if (frame.lighting.sun_direction[1] > 0.02f)
          r.apply_light_shafts ({
            .camera_pos = position (camera.position),
            .forward = forward,
            .right_span = right_span,
            .up_span = up_span,
            .sun_dir = frame.lighting.sun_direction,
            .sun_color = frame.lighting.sun_diffuse,
            .strength = 0.55f * one,
          });
      }

      // Grade near the waterline as well as below it: the shader decides
      // per pixel whether the near plane is wet, before reconstruction
      // accumulates the boundary with the rest of the jittered scene.
      if (visibility.ocean && frame.camera_water_level &&
          frame.camera.position[1] < *frame.camera_water_level + 1.0f)
        r.apply_underwater ({
          .water_level = *frame.camera_water_level * u::m,
        });

      // Reconstruction consumes the scene's colour, depth, and motion;
      // motion blur operates on its full-size result.
      r.reconstruct_scene ();
      if (visibility.motion_blur)
        r.apply_motion_blur (frame.motion_blur_amount);
    }

    void MoppeGame::draw_overlays (render::Renderer& r,
                                   const FrameView& frame) {
      const FrameVisibility& visibility = frame.visibility;

      // HUD, kept inside the safe area (notch / home indicator).
      m_hud_dl.clear ();
      m_hud_text.clear ();
      const platform::Insets safe_insets = platform::safe_insets ();
      m_hud_dl.translate (safe_insets.left, safe_insets.top, 0);
      const int hud_width =
        r.width_pts () - (int)(safe_insets.left + safe_insets.right);
      const int hud_height =
        r.height_pts () - (int)(safe_insets.top + safe_insets.bottom);
      if (visibility.cinematic_hud) {
        draw_opening_titles (hud_width, hud_height);
        m_hud.draw_ride_prompt (m_hud_text,
                                frame.overlay.cinematic_prompt_alpha,
                                hud_width,
                                hud_height);
      } else if (visibility.game_hud) {
        HudState hud_state = hud_state_for (frame.hud);
        add_basket_readings (hud_state);
        add_camp_readings (hud_state);
        add_fishing_readings (hud_state);
        add_company_readings (hud_state);
        add_waymarks (hud_state, frame, hud_width, hud_height);
        add_chart (hud_state, frame);
        m_hud.draw (m_hud_dl, m_hud_text, hud_state, hud_width, hud_height);
        if (m_hud.diagnostics ())
          draw_trail_map (m_hud_dl,
                          hud_width,
                          hud_height,
                          frame.hud.subject_position,
                          frame.hud.subject_heading);
        if (m_map.open ())
          draw_map (r, hud_width, hud_height);
        if (m_quit_asked)
          m_hud.draw_quit_question (
            m_hud_dl, m_hud_text, hud_width, hud_height);
      }
      m_hud.publish_offers ();
      m_hud_text.translate (safe_insets.left, safe_insets.top);

      // Even a clean inspection capture needs this empty HUD pass: it is
      // also the final post-chain composite into the drawable.
      r.draw_hud_text (m_hud_text);
      r.draw_hud (m_hud_dl);
    }
  }
}

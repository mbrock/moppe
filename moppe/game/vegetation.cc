#include <moppe/game/vegetation.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

namespace moppe {
namespace game {
  namespace {
    // Deterministic per-plant randomness: the structure and detail
    // bake passes both replay a plant's layout from its seed, so a
    // leaf always sits on the canopy blob it was grown on.
    struct Rand {
      std::mt19937 g;
      explicit Rand (uint32_t seed) : g (seed) {}
      float next () { return (g () >> 8) * (1.0f / 16777216.0f); }
      float range (float a, float b) { return a + (b - a) * next (); }
    };

    // A plant handed to the recorders (the public face of the
    // private Plant).
    struct Spot {
      Vector3D pos;
      Vector3D ground_n;
      float s, tint;
      uint32_t seed;
      int palette;
    };

    struct Blob { Vector3D c; float r, squash; };

    // Sunlit grass tones, shared by the baked meadow tufts and the
    // per-frame near-field grass so they read as one field.  `dry`
    // pushes toward straw, `j` is per-blade jitter.
    inline Vector3D blade_base (float dry, float j) {
      return Vector3D (0.22f + 0.20f * dry + j,
		       0.38f - 0.03f * dry + 2 * j,
		       0.09f + 0.06f * dry);
    }
    inline Vector3D blade_tip (float dry, float j) {
      return Vector3D (0.48f + 0.25f * dry + 2 * j,
		       0.66f - 0.08f * dry + 2 * j,
		       0.16f + 0.10f * dry);
    }

    inline float grass_patch (float x, float z) {
      const float broad = std::sin (x * 0.036f
	+ 1.7f * std::sin (z * 0.019f));
      const float detail = std::sin (z * 0.071f - x * 0.043f);
      return std::clamp (0.52f + 0.28f * broad + 0.20f * detail,
			 0.0f, 1.0f);
    }

    // A narrow segmented strip rather than the old single triangle. The
    // quadratic centerline keeps the root upright and bends progressively
    // toward the tip; UV.y and the grass flag drive blade shading in Metal.
    void record_blade (render::DrawList& dl, const Vector3D& root,
		       float height, float bend_angle, float bend,
		       float half_width, float dry, float jitter,
		       float sway, int segments) {
      const Vector3D side (std::cos (bend_angle + PI * 0.5f), 0,
			   std::sin (bend_angle + PI * 0.5f));
      const Vector3D direction (std::cos (bend_angle), 0,
				std::sin (bend_angle));
      const Vector3D face (-side.z, 0.18f, side.x);
      const Vector3D base = blade_base (dry, jitter);
      const Vector3D tip = blade_tip (dry, jitter);

      auto center = [&] (float t) {
	return root + Vector3D (0, height * t, 0)
	  + direction * (bend * t * t);
      };
      auto emit = [&] (float t, float side_sign) {
	const float width = half_width * (1.0f - 0.86f * t);
	dl.normal (face);
	dl.uv (0.5f + 0.5f * side_sign, t);
	dl.color (base * (1.0f - t) + tip * t);
	dl.wind (sway * t * t);
	dl.vertex (center (t) + side * (width * side_sign));
      };

      for (int segment = 0; segment < segments; ++segment) {
	const float t0 = segment / (float) segments;
	const float t1 = (segment + 1) / (float) segments;
	emit (t0, -1); emit (t0, 1); emit (t1, -1);
	emit (t1, -1); emit (t0, 1); emit (t1, 1);
      }
    }

    const float FLOWER_PETALS[5][3] = {
      { 0.95f, 0.95f, 0.88f },   // daisy white
      { 0.98f, 0.80f, 0.18f },   // buttercup yellow
      { 0.88f, 0.16f, 0.10f },   // poppy red
      { 0.58f, 0.36f, 0.85f },   // harebell violet
      { 0.95f, 0.55f, 0.70f },   // clover pink
    };

    // -- shared layouts ---------------------------------------------
    //
    // Blob positions are in tree-local units, scaled by the plant's
    // s at record time.  Each consumes a fixed prefix of the seed's
    // sequence so both bake passes agree.

    int broadleaf_canopy (const Spot& p, Blob* out) {
      Rand r (p.seed);
      int n = 0;
      Blob core = { Vector3D (0, 3.55f, 0), 1.45f, 0.80f };
      out[n++] = core;
      for (int k = 0; k < 3; ++k) {
	const float a = 2.09f * k + r.range (0.0f, 1.2f);
	const float d = r.range (0.65f, 1.05f);
	Blob b = { Vector3D (std::cos (a) * d, r.range (3.0f, 4.2f),
			     std::sin (a) * d),
		   r.range (0.75f, 1.15f), 0.72f };
	out[n++] = b;
      }
      return n;
    }

    int birch_canopy (const Spot& p, Blob* out) {
      Rand r (p.seed);
      int n = 0;
      Blob core = { Vector3D (0, 4.15f, 0), 0.95f, 0.85f };
      out[n++] = core;
      for (int k = 0; k < 2; ++k) {
	const float a = 3.1f * k + r.range (0.0f, 1.5f);
	const float d = r.range (0.35f, 0.65f);
	Blob b = { Vector3D (std::cos (a) * d, r.range (3.4f, 4.5f),
			     std::sin (a) * d),
		   r.range (0.55f, 0.80f), 0.80f };
	out[n++] = b;
      }
      return n;
    }

    int bush_blobs (const Spot& p, Blob* out) {
      Rand r (p.seed);
      int n = 0;
      Blob core = { Vector3D (0, 0.55f, 0), 0.85f, 0.72f };
      out[n++] = core;
      const int extra = 1 + (int) (r.next () * 2.0f);
      for (int k = 0; k < extra; ++k) {
	const float a = r.range (0, PI2);
	const float d = r.range (0.45f, 0.80f);
	Blob b = { Vector3D (std::cos (a) * d, r.range (0.30f, 0.55f),
			     std::sin (a) * d),
		   r.range (0.45f, 0.70f), 0.75f };
	out[n++] = b;
      }
      return n;
    }

    // -- structure pieces ---------------------------------------------

    // Upright cone: base radius `base` on the ground, apex `height`
    // up.
    void trunk (render::DrawList& dl, float base, float height,
		int slices) {
      dl.push ();
      dl.rotate_deg (-90, 1, 0, 0);
      dl.cone (base, height, slices, 1);
      dl.pop ();
    }

    void blob (render::DrawList& dl, const Blob& b,
	       int slices, int stacks, float sway) {
      dl.wind (sway);
      dl.push ();
      dl.translate (b.c);
      dl.scale (b.r, b.r * b.squash, b.r);
      dl.sphere (1.0f, slices, stacks);
      dl.pop ();
      dl.wind (0);
    }

    void record_broadleaf (render::DrawList& dl, const Spot& p,
			   bool lean) {
      Blob blobs[4];
      const int n = broadleaf_canopy (p, blobs);
      Rand r (p.seed ^ 0x51ed270bu);
      const bool autumn = (p.seed % 19) == 0;

      dl.push ();
      dl.translate (p.pos);
      dl.scale (p.s, p.s, p.s);

      dl.color (0.32f + 0.08f * p.tint, 0.22f + 0.05f * p.tint, 0.12f);
      dl.push ();
      dl.rotate_deg (r.range (-4, 4), 1, 0, 0);
      dl.rotate_deg (r.range (-4, 4), 0, 0, 1);
      trunk (dl, 0.26f, 3.8f, 7);
      dl.pop ();

      const int blobn = lean ? std::min (n, 3) : n;
      for (int i = 0; i < blobn; ++i) {
	const float j = r.range (-0.03f, 0.03f);
	if (autumn)
	  dl.color (0.62f + 3 * j, 0.30f + j, 0.06f);
	else
	  dl.color (0.09f + 0.10f * p.tint + j,
		    0.30f + 0.14f * p.tint + j, 0.09f);
	blob (dl, blobs[i], (i == 0 && !lean) ? 5 : 4,
	      (i == 0 && !lean) ? 4 : 3,
	      0.06f + 0.02f * blobs[i].c.y);
      }
      dl.pop ();
    }

    void record_conifer (render::DrawList& dl, const Spot& p) {
      dl.push ();
      dl.translate (p.pos);
      dl.scale (p.s, p.s, p.s);

      dl.color (0.30f, 0.21f, 0.12f);
      trunk (dl, 0.17f, 1.6f, 6);

      // Three stacked skirts, lighter and swishier toward the top.
      for (int i = 0; i < 3; ++i) {
	const float g = 0.17f + 0.05f * i + 0.06f * p.tint;
	dl.color (0.045f + 0.02f * i, g, 0.075f + 0.015f * i);
	dl.wind (0.03f + 0.035f * i);
	dl.push ();
	dl.translate (0, 0.85f + 1.05f * i, 0);
	dl.rotate_deg (-90, 1, 0, 0);
	dl.cone (1.35f - 0.34f * i, 2.1f - 0.25f * i, 8, 1);
	dl.pop ();
      }
      dl.wind (0);
      dl.pop ();
    }

    void record_birch (render::DrawList& dl, const Spot& p,
		       bool lean) {
      Blob blobs[3];
      const int n = birch_canopy (p, blobs);
      Rand r (p.seed ^ 0x0077aa11u);

      dl.push ();
      dl.translate (p.pos);
      dl.scale (p.s, p.s, p.s);

      dl.push ();
      dl.rotate_deg (r.range (-6, 6), 1, 0, 0);
      dl.rotate_deg (r.range (-6, 6), 0, 0, 1);
      dl.color (0.86f, 0.86f, 0.81f);
      trunk (dl, 0.13f, 4.4f, 6);
      if (!lean) {
	dl.color (0.16f, 0.14f, 0.11f);
	for (int i = 0; i < 3; ++i) {
	  const float h = 0.8f + 1.0f * i + 0.4f * r.next ();
	  const float w = 0.27f * (1.0f - h / 4.6f) + 0.03f;
	  dl.push ();
	  dl.translate (0, h, 0);
	  dl.scale (w, 0.07f, w);
	  dl.cube (1.0f);
	  dl.pop ();
	}
      }
      dl.pop ();

      for (int i = 0; i < n; ++i) {
	const float j = r.range (-0.03f, 0.03f);
	dl.color (0.27f + 0.10f * p.tint + j,
		  0.43f + 0.10f * p.tint + j, 0.13f);
	blob (dl, blobs[i], 4, 3, 0.08f + 0.02f * blobs[i].c.y);
      }
      dl.pop ();
    }

    void record_bush (render::DrawList& dl, const Spot& p) {
      Blob blobs[3];
      const int n = bush_blobs (p, blobs);
      Rand r (p.seed ^ 0x0099aabbu);

      dl.push ();
      dl.translate (p.pos);
      dl.scale (p.s, p.s, p.s);
      for (int i = 0; i < n; ++i) {
	const float j = r.range (-0.03f, 0.03f);
	dl.color (0.09f + 0.11f * p.tint + j,
		  0.27f + 0.15f * p.tint + j, 0.07f);
	blob (dl, blobs[i], 4, 3, 0.05f);
      }
      dl.pop ();
    }

    // A soft translucent disc grounds the plant: cheap baked
    // ambient occlusion where the real shadow map never reaches.
    void record_disc (render::DrawList& dl, const Spot& p,
		      float radius, float alpha) {
      const Vector3D n = p.ground_n;
      const Vector3D h = std::fabs (n.y) > 0.9f
	? Vector3D (1, 0, 0) : Vector3D (0, 1, 0);
      const Vector3D t = n.cross (h).normalized ();
      const Vector3D b = n.cross (t);
      const Vector3D c = p.pos + n * 0.10f;

      dl.begin (render::Prim::TriangleFan);
      dl.color (0, 0, 0, alpha);
      dl.vertex (c);
      dl.color (0, 0, 0, 0.0f);
      for (int k = 0; k <= 8; ++k) {
	const float a = PI2 * k / 8;
	dl.vertex (c + t * (std::cos (a) * radius)
		   + b * (std::sin (a) * radius));
      }
      dl.end ();
    }

    // -- detail pieces --------------------------------------------

    void leaf_frame (const Vector3D& n, Vector3D& t, Vector3D& b) {
      const Vector3D h = std::fabs (n.y) > 0.9f
	? Vector3D (1, 0, 0) : Vector3D (0, 1, 0);
      t = n.cross (h).normalized ();
      b = n.cross (t);
    }

    // Loose leaf quads scattered on the canopy surface break up the
    // blob silhouette and flutter in the shader's wind.
    void record_leaves (render::DrawList& dl, const Spot& p,
			const Blob* blobs, int nblobs,
			int count, bool autumn, float lightness) {
      Rand r (p.seed ^ 0xa5a5f00du);
      dl.begin (render::Prim::Quads);
      for (int i = 0; i < count; ++i) {
	const Blob& b = blobs[(int) (r.next () * nblobs) % nblobs];
	const float az = r.range (0, PI2);
	const float sy = r.range (-0.3f, 1.0f);
	const float cy = std::sqrt (std::max (0.0f, 1 - sy * sy));
	const Vector3D dir (std::cos (az) * cy, sy,
			    std::sin (az) * cy);
	const Vector3D local = b.c
	  + Vector3D (dir.x * b.r, dir.y * b.r * b.squash,
		      dir.z * b.r);
	const Vector3D w = p.pos + local * p.s;
	const float size = r.range (0.15f, 0.26f) * p.s;

	Vector3D t, bt;
	leaf_frame (dir, t, bt);
	const float sa = r.range (0, PI2);
	const Vector3D e1 = t * std::cos (sa) + bt * std::sin (sa);
	const Vector3D e2 = t * -std::sin (sa) + bt * std::cos (sa);

	const float j = r.range (-0.03f, 0.03f);
	if (autumn)
	  dl.color (0.70f + 3 * j, 0.36f + j, 0.07f);
	else
	  dl.color (0.12f + 0.10f * p.tint + j,
		    (0.38f + 0.14f * p.tint + j) * lightness,
		    0.10f);
	dl.normal (dir);
	dl.wind (r.range (0.20f, 0.40f));
	dl.vertex (w - e1 * size - e2 * (size * 0.6f));
	dl.vertex (w + e1 * size - e2 * (size * 0.6f));
	dl.vertex (w + e1 * size + e2 * (size * 0.6f));
	dl.vertex (w - e1 * size + e2 * (size * 0.6f));
      }
      dl.end ();
      dl.wind (0);
    }

    // A flower head: two upright crossed quads plus a lifted cap so
    // it reads from every camera angle.
    void record_bloom (render::DrawList& dl, const Vector3D& c,
		       float size, const float* pc, float sway,
		       Rand& r) {
      const float j = r.range (0.85f, 1.10f);
      dl.color (pc[0] * j, pc[1] * j, pc[2] * j);
      dl.normal (Vector3D (0, 1, 0));
      dl.wind (sway);
      dl.begin (render::Prim::Quads);
      const float a0 = r.range (0, PI);
      for (int k = 0; k < 2; ++k) {
	const float a = a0 + k * (PI / 2);
	const Vector3D t (std::cos (a) * size, 0,
			  std::sin (a) * size);
	const Vector3D up (0, size, 0);
	dl.vertex (c - t - up);
	dl.vertex (c + t - up);
	dl.vertex (c + t + up);
	dl.vertex (c - t + up);
      }
      {
	const float a = r.range (0, PI);
	const Vector3D t1 (std::cos (a) * size, 0,
			   std::sin (a) * size);
	const Vector3D t2 (-std::sin (a) * size, 0,
			   std::cos (a) * size);
	const Vector3D lift (0, size * 0.6f, 0);
	dl.vertex (c - t1 - t2 + lift);
	dl.vertex (c + t1 - t2 + lift);
	dl.vertex (c + t1 + t2 + lift);
	dl.vertex (c - t1 + t2 + lift);
      }
      dl.end ();
      dl.wind (0);
    }

    void record_flower (render::DrawList& dl, const Spot& p) {
      Rand r (p.seed);
      const float h = r.range (0.24f, 0.50f) * p.s;
      const float la = r.range (0, PI2);
      const float lm = r.range (0.0f, 0.18f) * h;
      const Vector3D top = p.pos
	+ Vector3D (std::cos (la) * lm, h, std::sin (la) * lm);
      const float sway = r.range (0.25f, 0.40f);

      dl.color (0.14f, 0.30f, 0.08f);
      dl.normal (Vector3D (0, 1, 0));
      dl.begin (render::Prim::Triangles);
      const float pa = r.range (0, PI2);
      const Vector3D perp (std::cos (pa) * 0.014f, 0,
			   std::sin (pa) * 0.014f);
      dl.wind (0);
      dl.vertex (p.pos - perp);
      dl.vertex (p.pos + perp);
      dl.wind (sway);
      dl.vertex (top);
      dl.end ();

      // The head shares the stem tip's sway so they bend together.
      record_bloom (dl, top + Vector3D (0, 0.015f, 0),
		    r.range (0.05f, 0.09f) * p.s,
		    FLOWER_PETALS[p.palette % 5], sway, r);
    }

    void record_tuft (render::DrawList& dl, const Spot& p) {
      Rand r (p.seed);
      const float dry = p.tint;
      dl.normal (Vector3D (0, 1, 0));
      dl.grass (true);
      dl.begin (render::Prim::Triangles);
      const int blades = 5 + (int) (r.next () * 3.0f);
      for (int k = 0; k < blades; ++k) {
	const float oa = r.range (0, PI2);
	const float od = r.range (0.0f, 0.34f) * p.s;
	const Vector3D root = p.pos
	  + Vector3D (std::cos (oa) * od, 0, std::sin (oa) * od);
	const float h = r.range (0.26f, 0.58f) * p.s;
	const float ba = r.range (0, PI2);
	const float bm = r.range (0.06f, 0.34f) * h;
	const float w = r.range (0.020f, 0.042f);
	const float j = r.range (-0.02f, 0.02f);
	record_blade (dl, root, h, ba, bm, w, dry, j,
		      std::min (1.0f, h * 1.6f)
		        * r.range (0.45f, 0.85f), 3);
      }
      dl.end ();
      dl.wind (0);
      dl.grass (false);
    }

    void record_reeds (render::DrawList& dl, const Spot& p) {
      Rand r (p.seed);
      const int stalks = 4 + (int) (r.next () * 4.0f);
      for (int k = 0; k < stalks; ++k) {
	const float oa = r.range (0, PI2);
	const float od = r.range (0.05f, 0.50f) * p.s;
	const Vector3D root = p.pos
	  + Vector3D (std::cos (oa) * od, 0, std::sin (oa) * od);
	const float h = r.range (0.9f, 1.7f) * p.s;
	const float ba = r.range (0, PI2);
	const float bm = r.range (0.02f, 0.14f) * h;
	const Vector3D tip = root
	  + Vector3D (std::cos (ba) * bm, h, std::sin (ba) * bm);
	const float sway = r.range (0.35f, 0.60f);

	dl.normal (Vector3D (0, 1, 0));
	dl.begin (render::Prim::Triangles);
	const Vector3D perp (std::cos (oa) * 0.03f, 0,
			     std::sin (oa) * 0.03f);
	dl.wind (0);
	dl.color (0.13f, 0.24f, 0.09f);
	dl.vertex (root - perp);
	dl.vertex (root + perp);
	dl.wind (sway);
	dl.color (0.34f, 0.42f, 0.16f);
	dl.vertex (tip);
	dl.end ();

	if (r.next () < 0.45f) {
	  // the cattail: a stubby brown cross near the head
	  const Vector3D hc = root + (tip - root) * 0.82f;
	  dl.color (0.33f, 0.20f, 0.10f);
	  dl.wind (sway * 0.85f);
	  dl.begin (render::Prim::Quads);
	  for (int q = 0; q < 2; ++q) {
	    const float a = oa + q * (PI / 2);
	    const Vector3D t (std::cos (a) * 0.035f, 0,
			      std::sin (a) * 0.035f);
	    const Vector3D up (0, 0.12f, 0);
	    dl.vertex (hc - t - up);
	    dl.vertex (hc + t - up);
	    dl.vertex (hc + t + up);
	    dl.vertex (hc - t + up);
	  }
	  dl.end ();
	}
      }
      dl.wind (0);
    }

    void record_bush_detail (render::DrawList& dl, const Spot& p,
			     bool lean) {
      Blob blobs[3];
      const int n = bush_blobs (p, blobs);
      Rand r (p.seed ^ 0x0b00b1e5u);
      if (p.palette > 0) {
	// A flowering bush: blooms dotted over the upper surface.
	const float* pc = FLOWER_PETALS[(p.palette - 1) % 5];
	const int blooms = lean ? 5 : 9;
	for (int i = 0; i < blooms; ++i) {
	  const Blob& b = blobs[(int) (r.next () * n) % n];
	  const float az = r.range (0, PI2);
	  const float sy = r.range (0.15f, 1.0f);
	  const float cy = std::sqrt (std::max (0.0f, 1 - sy * sy));
	  const Vector3D dir (std::cos (az) * cy, sy,
			      std::sin (az) * cy);
	  const Vector3D local = b.c
	    + Vector3D (dir.x * b.r, dir.y * b.r * b.squash,
			dir.z * b.r);
	  const Vector3D w = p.pos + local * p.s
	    + Vector3D (0, 0.02f, 0);
	  record_bloom (dl, w, r.range (0.035f, 0.06f) * p.s, pc,
			0.10f, r);
	}
      } else {
	record_leaves (dl, p, blobs, n, lean ? 3 : 6, false, 0.9f);
      }
    }
  }

  // -- placement -------------------------------------------------

  Vegetation::Population
  Vegetation::population_for (const WorldParams& world)
  {
    if (world.pico_mode)
      return Population (6000, 4000, 30000, 5200, 700);
    if (world.city_mode)
      return Population (500, 300, 30000, 5200, 700);
    return Population (3200, 2000, 30000, 5200, 700);
  }

  void
  Vegetation::append_plant (Species species, const Vector3D& position,
			    const Vector3D& normal, float scale,
			    float tint, uint32_t seed, uint8_t palette)
  {
    Plant plant;
    plant.species = species;
    plant.palette = palette;
    plant.position = position;
    if (m_periodic) {
      plant.position.x = terrain::wrap_coordinate
	(plant.position.x, m_map_size.x);
      plant.position.z = terrain::wrap_coordinate
	(plant.position.z, m_map_size.z);
    }
    plant.ground_normal = normal;
    plant.scale = scale;
    plant.tint = tint;
    plant.seed = seed;
    m_plants.push_back (plant);
  }

  void
  Vegetation::generate (const map::HeightMap& map,
			const WorldParams& world,
			const Population& population)
  {
    const int trees = std::max (0, population.trees);
    const int bushes = std::max (0, population.bushes);
    const int max_tufts = std::max (0, population.max_tufts);
    const int max_flowers = std::max (0, population.max_flowers);
    const int reed_count = std::max (0, population.reeds);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const auto random_coordinate = [&u, &rng, this] (float extent) {
      const float unit = u (rng);
      return extent * (m_periodic ? unit : 0.02f + 0.96f * unit);
    };

    const Vector3D size = map.size ();
    m_map_size = world.map_size;
    m_lean = world.pico_mode;
    m_periodic = map.periodic ();
    m_map = &map;
    m_water = world.water_level;
    m_height = world.map_size.y;
    m_plants.clear ();
    m_plants.reserve (trees + bushes + max_tufts
		      + max_flowers + reed_count);

    const float H = world.map_size.y;
    const float water = world.water_level;

    // Grove centers first: most trees cluster into woods with
    // clearings between them, instead of an even sprinkle.
    const int ngrove = std::max (8, trees / 26);
    std::vector<Vector3D> groves;
    for (int i = 0; i < ngrove * 30
	   && (int) groves.size () < ngrove; ++i) {
      const float x = random_coordinate (size.x);
      const float z = random_coordinate (size.z);
      const float y = map.interpolated_height (x, z);
      if (y < water + 6 || y > 0.30f * H) continue;
      if (map.interpolated_normal (x, z).y < 0.80f) continue;
      groves.push_back (Vector3D (x, y, z));
    }

    // Trees like gentle grassy ground below the rock line; only
    // conifers climb the last stretch toward it.  Nothing grows in
    // the ocean.
    int placed = 0;
    for (int i = 0; i < trees * 20 && placed < trees; ++i) {
      float x, z;
      if (u (rng) < 0.65f && !groves.empty ()) {
	const Vector3D& g = groves[(int) (u (rng) * groves.size ())];
	const float a = PI2 * u (rng);
	const float d = 42.0f * std::pow (u (rng), 0.7f);
	x = g.x + std::cos (a) * d;
	z = g.z + std::sin (a) * d;
	if (!map.in_bounds (x, z)) continue;
      } else {
	x = random_coordinate (size.x);
	z = random_coordinate (size.z);
      }
      const float y = map.interpolated_height (x, z);
      if (y < water + 5) continue;
      const Vector3D n = map.interpolated_normal (x, z);
      const float yn = y / H;

      Species species;
      if (yn > 0.40f) continue;
      else if (yn > 0.30f) {
	if (n.y < 0.74f) continue;
	species = Species::Conifer;
      } else {
	if (n.y < 0.80f) continue;
	const float pcon = 0.10f + 0.75f
	  * std::min (1.0f, std::max (0.0f, (yn - 0.08f) / 0.22f));
	species = u (rng) < pcon ? Species::Conifer
	  : (u (rng) < 0.18f ? Species::Birch : Species::Broadleaf);
      }

      float scale = species == Species::Conifer
	? 0.9f + 1.1f * u (rng) : 0.9f + 1.2f * u (rng);
      if (u (rng) < 0.07f)
	scale *= 1.6f;   // the occasional old giant
      const float tint = u (rng);
      const uint32_t seed = rng ();
      append_plant (species, Vector3D (x, y, z), n, scale,
		    tint, seed);
      ++placed;
    }

    // Bushes climb a little higher and tolerate more slope; some
    // flower.
    placed = 0;
    for (int i = 0; i < bushes * 20 && placed < bushes; ++i) {
      const float x = random_coordinate (size.x);
      const float z = random_coordinate (size.z);
      const float y = map.interpolated_height (x, z);
      if (y > 0.45f * H) continue;
      if (y < water + 5) continue;
      const Vector3D n = map.interpolated_normal (x, z);
      if (n.y < 0.72f) continue;

      const uint8_t palette = u (rng) < 0.30f
	? (uint8_t) (1 + (int) (u (rng) * 5)) : 0;
      const float scale = 0.6f + 0.8f * u (rng);
      const float tint = u (rng);
      const uint32_t seed = rng ();
      append_plant (Species::Bush, Vector3D (x, y, z), n,
		    scale, tint, seed, palette);
      ++placed;
    }

    // Grass grows in meadows around sampled centers (plus a loose
    // scatter), so the ground reads as fields rather than static.
    const int tufts = std::min (max_tufts, trees * 9);
    const int nmeadow = std::max (16, tufts / 90);
    std::vector<Vector3D> meadows;
    std::vector<uint8_t> meadow_palette;
    for (int i = 0; i < nmeadow * 30
	   && (int) meadows.size () < nmeadow; ++i) {
      const float x = random_coordinate (size.x);
      const float z = random_coordinate (size.z);
      const float y = map.interpolated_height (x, z);
      if (y < water + 4 || y > 0.36f * H) continue;
      if (map.interpolated_normal (x, z).y < 0.78f) continue;
      meadows.push_back (Vector3D (x, y, z));
      meadow_palette.push_back ((uint8_t) (u (rng) * 5));
    }

    placed = 0;
    for (int i = 0; i < tufts * 8 && placed < tufts
	   && !meadows.empty (); ++i) {
      float x, z;
      if (u (rng) < 0.68f) {
	const Vector3D& m =
	  meadows[(int) (u (rng) * meadows.size ())];
	const float a = PI2 * u (rng);
	const float d = 65.0f * std::pow (u (rng), 1.5f);
	x = m.x + std::cos (a) * d;
	z = m.z + std::sin (a) * d;
      } else {
	x = random_coordinate (size.x);
	z = random_coordinate (size.z);
      }
      if (!map.in_bounds (x, z)) continue;
      const float y = map.interpolated_height (x, z);
      if (y < water + 2.5f || y > 0.40f * H) continue;
      const Vector3D n = map.interpolated_normal (x, z);
      if (n.y < 0.74f) continue;

      const float scale = 0.75f + 0.70f * u (rng);
      // higher and steeper reads drier
      const float tint =
	std::min (1.0f, std::max (0.0f, (y / H - 0.10f) / 0.22f))
	* 0.7f + 0.3f * u (rng);
      const uint32_t seed = rng ();
      append_plant (Species::Tuft, Vector3D (x, y, z), n,
		    scale, tint, seed);
      ++placed;
    }

    // Flowers bloom in every other meadow, each with its own
    // palette (occasionally two).
    const int flowers = std::min (max_flowers, trees * 2);
    placed = 0;
    for (int i = 0; i < flowers * 8 && placed < flowers
	   && meadows.size () >= 2; ++i) {
      const int mi =
	((int) (u (rng) * (meadows.size () / 2))) * 2
	% (int) meadows.size ();
      const Vector3D& m = meadows[mi];
      const float a = PI2 * u (rng);
      const float d = 42.0f * std::pow (u (rng), 1.5f);
      const float x = m.x + std::cos (a) * d;
      const float z = m.z + std::sin (a) * d;
      if (!map.in_bounds (x, z)) continue;
      const float y = map.interpolated_height (x, z);
      if (y < water + 3 || y > 0.36f * H) continue;
      const Vector3D n = map.interpolated_normal (x, z);
      if (n.y < 0.76f) continue;

      uint8_t palette = meadow_palette[mi];
      if (u (rng) < 0.25f)
	palette = (uint8_t) ((palette + 1 + (int) (u (rng) * 4)) % 5);
      const float scale = 0.8f + 0.6f * u (rng);
      const float tint = u (rng);
      const uint32_t seed = rng ();
      append_plant (Species::Flower, Vector3D (x, y, z), n,
		    scale, tint, seed, palette);
      ++placed;
    }

    // Reed clumps stand in the shallows band along the waterline.
    placed = 0;
    for (int i = 0; i < reed_count * 30 && placed < reed_count; ++i) {
      const float x = random_coordinate (size.x);
      const float z = random_coordinate (size.z);
      const float y = map.interpolated_height (x, z);
      if (y < water + 0.15f || y > water + 2.4f) continue;
      const Vector3D n = map.interpolated_normal (x, z);
      if (n.y < 0.70f) continue;

      const float scale = 0.8f + 0.6f * u (rng);
      const float tint = u (rng);
      const uint32_t seed = rng ();
      append_plant (Species::Reed, Vector3D (x, y, z), n,
		    scale, tint, seed);
      ++placed;
    }
  }

  int
  Vegetation::cell_of (const Vector3D& position, int grid) const
  {
    int cx = (int) (position.x / (m_map_size.x / grid));
    int cz = (int) (position.z / (m_map_size.z / grid));
    cx = std::max (0, std::min (grid - 1, cx));
    cz = std::max (0, std::min (grid - 1, cz));
    return cz * grid + cx;
  }

  void
  Vegetation::record_structure (render::DrawList& dl,
				const Plant& plant) const
  {
    const Spot spot = { plant.position, plant.ground_normal,
			plant.scale, plant.tint, plant.seed, plant.palette };
    switch (plant.species) {
    case Species::Broadleaf: record_broadleaf (dl, spot, m_lean); break;
    case Species::Conifer:   record_conifer (dl, spot); break;
    case Species::Birch:     record_birch (dl, spot, m_lean); break;
    case Species::Bush:      record_bush (dl, spot); break;
    default: break;
    }
  }

  void
  Vegetation::record_shadow (render::DrawList& dl,
			     const Plant& plant) const
  {
    const Spot spot = { plant.position, plant.ground_normal,
			plant.scale, plant.tint, plant.seed, plant.palette };
    switch (plant.species) {
    case Species::Broadleaf:
      record_disc (dl, spot, 1.5f * plant.scale, 0.30f); break;
    case Species::Conifer:
      record_disc (dl, spot, 1.2f * plant.scale, 0.30f); break;
    case Species::Birch:
      record_disc (dl, spot, 1.0f * plant.scale, 0.24f); break;
    case Species::Bush:
      record_disc (dl, spot, 0.85f * plant.scale, 0.22f); break;
    default: break;
    }
  }

  void
  Vegetation::record_detail (render::DrawList& dl,
			     const Plant& plant) const
  {
    const Spot spot = { plant.position, plant.ground_normal,
			plant.scale, plant.tint, plant.seed, plant.palette };
    switch (plant.species) {
    case Species::Tuft:   record_tuft (dl, spot); break;
    case Species::Flower: record_flower (dl, spot); break;
    case Species::Reed:   record_reeds (dl, spot); break;
    case Species::Bush:   record_bush_detail (dl, spot, m_lean); break;
    case Species::Broadleaf: {
      Blob blobs[4];
      const int count = broadleaf_canopy (spot, blobs);
      record_leaves (dl, spot, blobs, count, m_lean ? 8 : 16,
		     (plant.seed % 19) == 0, 1.0f);
      break;
    }
    case Species::Birch: {
      Blob blobs[3];
      const int count = birch_canopy (spot, blobs);
      record_leaves (dl, spot, blobs, count, m_lean ? 6 : 12,
		     false, 1.25f);
      break;
    }
    default: break;
    }
  }

  void
  Vegetation::load (render::Renderer& r)
  {
    typedef std::vector<const Plant*> Bucket;
    std::vector<Bucket> structure (STRUCTURE_GRID * STRUCTURE_GRID);
    std::vector<Bucket> detail (DETAIL_GRID * DETAIL_GRID);
    for (size_t i = 0; i < m_plants.size (); ++i) {
      const Plant& plant = m_plants[i];
      structure[cell_of (plant.position, STRUCTURE_GRID)].push_back (&plant);
      detail[cell_of (plant.position, DETAIL_GRID)].push_back (&plant);
    }

    // Structure sectors: all opaque growth first, then every
    // contact shadow in one translucent run, so a sector costs two
    // pipeline states no matter how many plants it holds.
    for (int sector = 0;
	 sector < STRUCTURE_GRID * STRUCTURE_GRID; ++sector) {
      render::DrawList dl;
      const Bucket& plants = structure[sector];
      for (size_t i = 0; i < plants.size (); ++i)
	record_structure (dl, *plants[i]);

      render::DrawState shadow_state;
      shadow_state.blend = true;
      shadow_state.depth_write = false;
      shadow_state.cull = false;
      dl.state (shadow_state);
      dl.lit (false);
      for (size_t i = 0; i < plants.size (); ++i)
	record_shadow (dl, *plants[i]);

      m_meshes[sector] = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }

    // Detail cells: leaves, grass, flowers, reeds.  Two-sided so
    // blades and leaf quads read from behind.
    for (int cell = 0; cell < DETAIL_GRID * DETAIL_GRID; ++cell) {
      render::DrawList dl;
      render::DrawState ds;
      ds.cull = false;
      dl.state (ds);
      const Bucket& plants = detail[cell];
      for (size_t i = 0; i < plants.size (); ++i)
	record_detail (dl, *plants[i]);
      m_detail[cell] = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }
  }

  void
  Vegetation::render_grid (render::Renderer& r,
			   const render::MeshPtr* meshes, int grid,
			   float reach, const Vector3D& camera) const
  {
    const float width = m_map_size.x / grid;
    const float depth = m_map_size.z / grid;
    const float half_diagonal = 0.5f
      * std::sqrt (width * width + depth * depth);
    const float center_reach = reach + half_diagonal;

    for (int z = 0; z < grid; ++z)
      for (int x = 0; x < grid; ++x) {
	const render::MeshPtr& mesh = meshes[z * grid + x];
	if (!mesh)
	  continue;
	const float center_x = (x + 0.5f) * width;
	const float center_z = (z + 0.5f) * depth;
	const int min_tile_x = m_periodic ? static_cast<int> (std::ceil
	  ((camera.x - center_reach - center_x) / m_map_size.x)) : 0;
	const int max_tile_x = m_periodic ? static_cast<int> (std::floor
	  ((camera.x + center_reach - center_x) / m_map_size.x)) : 0;
	const int min_tile_z = m_periodic ? static_cast<int> (std::ceil
	  ((camera.z - center_reach - center_z) / m_map_size.z)) : 0;
	const int max_tile_z = m_periodic ? static_cast<int> (std::floor
	  ((camera.z + center_reach - center_z) / m_map_size.z)) : 0;
	for (int tile_z = min_tile_z; tile_z <= max_tile_z; ++tile_z)
	  for (int tile_x = min_tile_x; tile_x <= max_tile_x; ++tile_x) {
	    const Vector3D offset (tile_x * m_map_size.x, 0,
				   tile_z * m_map_size.z);
	    const float dx = camera.x - center_x - offset.x;
	    const float dz = camera.z - center_z - offset.z;
	    if (dx * dx + dz * dz >= center_reach * center_reach)
	      continue;
	    r.draw_mesh (*mesh, Mat4::translation (offset));
	  }
      }
  }

  void
  Vegetation::render (render::Renderer& r, const FrameEnv& env)
  {
    if (m_map_size.x <= 0 || m_map_size.z <= 0)
      return;

    // Only sectors within fog-visibility range get drawn.  The GL
    // build fogged vegetation at fog_scale * 1.35; the unified haze
    // replaces the fog, but the factor lives on in the reach.
    const float density = env.fog_scale * 1.35f;
    const float fog_reach = density > 0
      ? 1.9f / density : std::max (m_map_size.x, m_map_size.z);
    render_grid (r, m_meshes, STRUCTURE_GRID, fog_reach,
		 env.camera_pos);

    // Fine detail only registers near the camera; past that the
    // structural canopies carry the silhouette.
    render_grid (r, m_detail, DETAIL_GRID,
		 std::min (fog_reach, 560.0f), env.camera_pos);

    if (m_map) {
      // Blade count goes with (radius / spacing)^2, so MOPPE_GRASS
      // scales cost quadratically: 1 is the laptop-friendly default,
      // ~1.7 restores the original 90 m / 0.32 m field, 0 mows the
      // lawn entirely.
      static const float lush = [] {
	const char* g = ::getenv ("MOPPE_GRASS");
	return g ? std::strtof (g, nullptr) : 1.0f;
      } ();
      if (lush > 0.0f) {
	const float s = std::sqrt (lush);
	render::GrassParams grass;
	grass.radius = 65.0f * s;
	grass.spacing = 0.40f / s;
	grass.blades_per_cell = 4;
	r.draw_grass (grass);
      }
    }
  }

  void
  Vegetation::record_near_grass (const FrameEnv& env)
  {
    m_near.clear ();
    render::DrawState ds;
    ds.cull = false;
    m_near.state (ds);
    m_near.normal (Vector3D (0, 1, 0));

    // Carry individual grass into the mid-field on the target Mac instead of
    // dropping directly from nearby blades to a flat terrain texture. Outer
    // clumps use fewer, slightly broader blades to control streaming cost.
    const float cell = 1.7f;
    const float radius = 90.0f;
    const float detail_radius = 40.0f;
    const int span = (int) (radius / cell) + 1;
    const int cx = (int) std::floor (env.camera_pos.x / cell);
    const int cz = (int) std::floor (env.camera_pos.z / cell);

    for (int gz = cz - span; gz <= cz + span; ++gz)
      for (int gx = cx - span; gx <= cx + span; ++gx) {
	// One tuft per hash-elected grid cell, fixed in the world,
	// so nothing swims as the camera moves.
	uint32_t h = (uint32_t) gx * 73856093u
	  ^ (uint32_t) gz * 19349663u;
	h = (h ^ (h >> 13)) * 2654435761u;
	h ^= h >> 16;
	const float fx = ((h >> 8) & 0x3ffu) * (1.0f / 1023.0f);
	const float fz = ((h >> 18) & 0x3ffu) * (1.0f / 1023.0f);
	const float x = (gx + fx) * cell;
	const float z = (gz + fz) * cell;
	const float patch = grass_patch (x, z);
	const float occupancy = 0.65f + 0.33f * patch;
	if ((h & 0xffu) * (1.0f / 255.0f) > occupancy)
	  continue;
	if (!m_map->in_bounds (x, z))
	  continue;

	const float dx = x - env.camera_pos.x;
	const float dz2 = z - env.camera_pos.z;
	const float d2 = dx * dx + dz2 * dz2;
	if (d2 > radius * radius)
	  continue;

	const float y = m_map->interpolated_height (x, z);
	if (y < m_water + 2.0f || y > 0.42f * m_height)
	  continue;
	if (m_map->interpolated_normal (x, z).y < 0.70f)
	  continue;

	// Blades shrink toward the rim so the field has no edge.
	const float fade =
	  std::min (1.0f, 2.4f * (1.0f - d2 / (radius * radius)));
	const float dry = std::min (1.0f, std::max (0.0f,
	  (y / m_height - 0.10f) / 0.22f)) * 0.7f
	  + 0.2f * ((h >> 24) * (1.0f / 255.0f))
	  + 0.1f * patch;

	// A few elected cells grow a wildflower with the grass, so
	// blooms are part of the near field everywhere, not only in
	// the baked meadows.
    if (((h >> 5) & 0x1fu) == 0
	&& d2 < detail_radius * detail_radius) {
	  Spot fl = { Vector3D (x, y, z), Vector3D (0, 1, 0),
		      0.9f, (h >> 24) * (1.0f / 255.0f),
		      h, (int) ((h >> 10) % 5) };
	  record_flower (m_near, fl);
	}

	Rand r (h);
	const bool detailed = d2 < detail_radius * detail_radius;
	const int blades = detailed
	  ? 5 + (int) (r.next () * 4.0f)
	  : 2 + (int) (r.next () * 3.0f);
	m_near.grass (true);
	m_near.begin (render::Prim::Triangles);
	for (int k = 0; k < blades; ++k) {
	  const float oa = r.range (0, PI2);
	  const float od = r.range (0.0f, detailed ? 0.75f : 0.60f);
	  const Vector3D root (x + std::cos (oa) * od, y,
			       z + std::sin (oa) * od);
	  const float patch_height = 0.72f + 0.55f * patch;
	  const float bh = r.range (detailed ? 0.28f : 0.38f,
			    detailed ? 0.62f : 0.78f)
	    * patch_height * fade;
	  const float ba = r.range (0, PI2);
	  const float bm = r.range (0.06f, 0.34f) * bh;
	  const float w = r.range (detailed ? 0.018f : 0.025f,
			   detailed ? 0.038f : 0.048f);
	  const float j = r.range (-0.02f, 0.02f);
	  record_blade (m_near, root, bh, ba, bm, w, dry, j,
			std::min (1.0f, bh * 1.6f)
			  * r.range (0.45f, 0.85f), detailed ? 3 : 2);
	}
	m_near.end ();
	m_near.grass (false);
      }
    m_near.wind (0);
    m_near.grass (false);
  }
}
}

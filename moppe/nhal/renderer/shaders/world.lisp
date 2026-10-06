;;; The NHAL renderer's shaders: moppe's world drawn through NHAL, written
;;; in Luv's language and lowered by luv-shaderc to MSL and HLSL.
;;;
;;; Every world program reads the frame block at buffer binding 0.  The
;;; game's matrices are Metal's clip convention (y up, depth 0..1, reversed
;;; Z), while the language writes clip positions y-down, so CLIP turns one
;;; into the other.  Scene programs write colour and screen motion (last
;;; frame's place minus this frame's, in texture coordinates, unjittered)
;;; for the temporal resolve.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *frame*
    '((view-proj :mat4)            ; world to clip, unjittered
      (previous-view-proj :mat4)   ; last frame's, for motion
      (camera-position :vec4)      ; w: seconds
      (sun-direction :vec4)        ; toward the sun; w: cloud cover
      (sun-diffuse :vec4)
      (sun-specular :vec4)
      (ambient :vec4)
      (fog-color :vec4)            ; w: fog scale
      (relief :vec4)               ; sea level, land relief, previous time,
                                   ; valley mist
      (view-right :vec4)           ; right * tan(fov x / 2), for view rays
      (view-up :vec4)              ; up * tan(fov y / 2)
      (view-forward :vec4)
      (temporal :vec4)             ; scene width, height, jitter x, y (NDC)
      (temporal-blend :vec4)       ; new frame's weight, 0, output size
      (sun-view :mat4)             ; world to the sun's clip, for the map
      (shadow :vec4))))            ; strength, texel size, 0, 0

;;; -- shared ---------------------------------------------------------------

(define-shader-function ndc-uv (ndc)
  (vec2 (+ (* (swizzle ndc :x) 0.5) 0.5) (- 0.5 (* (swizzle ndc :y) 0.5))))

(define-shader-function clip-uv (clip)
  (ndc-uv (vec2 (/ (swizzle clip :x) (swizzle clip :w))
                (/ (swizzle clip :y) (swizzle clip :w)))))

;;; A Metal-convention clip position, jittered for temporal upscaling, in
;;; the language's y-down convention.
(define-shader-function clip (position jitter)
  (vec4 (+ (swizzle position :x) (* (swizzle jitter :x) (swizzle position :w)))
        (* -1.0 (+ (swizzle position :y)
                   (* (swizzle jitter :y) (swizzle position :w))))
        (swizzle position :z)
        (swizzle position :w)))

(define-shader-function srgb (c)
  (vec3 (expt (swizzle c :x) 2.2) (expt (swizzle c :y) 2.2)
        (expt (swizzle c :z) 2.2)))

(define-shader-function distance-fog (distance scale)
  (clamp (- 1.0 (exp (* -1.0 (expt (* distance scale) 1.5)))) 0.0 1.0))

;;; High ground rises above the valley haze.
(define-shader-function relief-haze (fog altitude sea-level land-relief)
  (* fog (mix 1.0 0.58 (smoothstep 0.36 0.82
                                   (/ (- altitude sea-level)
                                      (max land-relief 1.0))))))

(define-shader-function hemisphere-light (ambient normal)
  (* ambient (mix (srgb (vec3 0.92 0.74 0.58)) (srgb (vec3 0.74 0.92 1.12))
                  (+ 0.5 (* 0.5 (swizzle (normalize normal) :y))))))

;;; Haze warmed toward the sun and faintly blue away from it.
(define-shader-function warmed-fog (fog-color view sun)
  (let* ((daylight (smoothstep -0.08 0.18 (swizzle sun :y)))
         (golden (* daylight (- 1.0 (smoothstep 0.15 0.65 (swizzle sun :y)))))
         (toward (max (dot view sun) 0.0))
         (horizon (expt (- 1.0 (abs (swizzle view :y))) 3.0))
         (sun-color (mix (srgb (vec3 1.0 0.96 0.84)) (srgb (vec3 1.0 0.58 0.28))
                         golden))
         (away (max (* -1.0 (dot view sun)) 0.0))
         (base (* fog-color (mix (vec3 1.0 1.0 1.0) (vec3 0.95 0.98 1.05)
                                 (* 0.45 (* away daylight))))))
    (+ base (* sun-color
               (* daylight
                  (+ (* (expt toward 5.0) (+ 0.05 (* 0.14 horizon)))
                     (* (expt toward 24.0) (+ 0.04 (* 0.10 golden)))))))))

;;; The haze over a point at a distance, coloured for the view direction.
;;; Haze toward the warmed fog, then the authored valley mist: it lies in
;;; the low ground, below about two fifths of the land's relief, and thickens
;;; with the distance a ray travels through it.
(define-shader-function hazed (color world camera fog-color sun relief)
  (let* ((rel (- world camera))
         (distance (sqrt (dot rel rel)))
         (fog (relief-haze (distance-fog distance (swizzle fog-color :w))
                           (swizzle world :y) (swizzle relief :x)
                           (swizzle relief :y)))
         (fogged (mix color
                      (warmed-fog (swizzle fog-color :xyz)
                                  (/ rel (max distance 0.0001)) sun)
                      (smoothstep 0.0 0.9 fog)))
         (low (- 1.0 (smoothstep 0.10 0.42
                                 (/ (- (* 0.5 (+ (swizzle world :y)
                                                 (swizzle camera :y)))
                                       (swizzle relief :x))
                                    (max (swizzle relief :y) 1.0)))))
         (mist (* (swizzle relief :w) low
                  (- 1.0 (exp (/ (* -1.0 distance) 32.0))))))
    (mix fogged (* (swizzle fog-color :xyz) 1.06) (* 0.92 mist))))

;;; Where a world point falls in the sun's shadow map: (u, v, depth).  The
;;; light matrix is Metal's clip convention, y up.
(define-shader-function sun-map-coordinate (sun-view world)
  (let* ((l (* sun-view (vec4 world 1.0))))
    (vec3 (+ (* (swizzle l :x) 0.5) 0.5) (- 0.5 (* (swizzle l :y) 0.5))
          (swizzle l :z))))

;;; 1 inside the map, 0 beyond it, where the sun is unshadowed.
(define-shader-function inside-sun-map (at)
  (* (step 0.0 (swizzle at :x)) (step (swizzle at :x) 1.0)
     (step 0.0 (swizzle at :y)) (step (swizzle at :y) 1.0)
     (step 0.0 (swizzle at :z)) (step (swizzle at :z) 1.0)))

(define-shader-abstraction shadow-tap (map compare at dx dy texel bias)
  `(sample-compare ,map ,compare
                   (+ (swizzle ,at :xy) (* (vec2 ,dx ,dy) ,texel))
                   (- (swizzle ,at :z) ,bias)))

;;; -- terrain --------------------------------------------------------------

;;; Heights (R32F) and normals (RG16 snorm: x and z) follow the terrain grid;
;;; the world is periodic, so lattice reads wrap.  A chunk is a grid of
;;; vertices at a step of source samples; near the camera the step is a
;;; quarter sample.  Past its morph start a vertex slides onto the exact
;;; surface of the next coarser level, so levels meet without cracks.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *terrain*
    '((scale :vec4)                ; grid step x, height scale, step z, tex
      (fields :vec4)               ; have materials, have stand, 1 / period
      (size :vec4))))              ; samples per side x, z

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *chunk*
    '((placement :vec4)            ; origin x, origin z, step, vertices/row
      (morph :vec4)                ; start, end, parent step, 0
      (offset :vec4))))            ; periodic image offset x, 0, z, 0

(define-shader-function wrap (cell size)
  (- cell (* size (floor (/ cell size)))))

(define-shader-abstraction height-at (heights cell size)
  `(swizzle (texel-load ,heights (uvec2 (wrap ,cell ,size))) :x))

(define-shader-function unpack-normal (nxz)
  (vec3 (swizzle nxz :x)
        (sqrt (max (- 1.0 (dot nxz nxz)) 0.0))
        (swizzle nxz :y)))

(define-shader-abstraction normal-at (normals sampler cell size)
  `(unpack-normal (swizzle (sample-level ,normals ,sampler
                                         (/ (+ ,cell (vec2 0.5 0.5)) ,size)
                                         0.0)
                           :xy)))

;;; Bilinear height inside a source cell, for the sub-sample near field.
(define-shader-abstraction height-between (heights grid size)
  `(mix (mix (height-at ,heights (floor ,grid) ,size)
             (height-at ,heights (+ (floor ,grid) (vec2 1.0 0.0)) ,size)
             (swizzle (fract ,grid) :x))
        (mix (height-at ,heights (+ (floor ,grid) (vec2 0.0 1.0)) ,size)
             (height-at ,heights (+ (floor ,grid) (vec2 1.0 1.0)) ,size)
             (swizzle (fract ,grid) :x))
        (swizzle (fract ,grid) :y)))

;;; A value on the triangle surface a coarser level draws: its strips split
;;; each cell along the bottom-left to top-right diagonal.
(define-shader-function on-lattice (v00 v10 v01 v11 f)
  (if (<= (+ (swizzle f :x) (swizzle f :y)) 1.0)
      (+ v00 (* (swizzle f :x) (- v10 v00)) (* (swizzle f :y) (- v01 v00)))
      (+ v11 (* (- 1.0 (swizzle f :y)) (- v10 v11))
         (* (- 1.0 (swizzle f :x)) (- v01 v11)))))

(define-shader terrain-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (field-uv :vec2 :location 2)
               (here :vec4 :location 3)
               (then :vec4 :location 4))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (terrain :uniform-block :binding 1 :members #.*terrain*)
                 (chunk :uniform-block :binding 2 :members #.*chunk*)
                 (heights :texture-2d :binding 0)
                 (normals :texture-2d :binding 1)
                 (linear-repeat :sampler :binding 1)))
  (let* ((row-length (uint (swizzle placement :w)))
         (stride (swizzle placement :z))
         (grid (+ (swizzle placement :xy)
                  (* (vec2 (float (mod vertex-index row-length))
                           (float (/ vertex-index row-length)))
                     stride)))
         (extent (swizzle size :xy))
         (own-height (if (< stride 1.0)
                         (height-between heights grid extent)
                         (height-at heights grid extent)))
         (own-normal (normal-at normals linear-repeat grid extent))
         ;; The parent level's lattice cell and this vertex's place in it.
         (parent (swizzle morph :z))
         (cell (* (floor (/ grid parent)) parent))
         (f (clamp (/ (- grid cell) parent) (vec2 0.0 0.0) (vec2 1.0 1.0)))
         (h00 (height-at heights cell extent))
         (h10 (height-at heights (+ cell (vec2 parent 0.0)) extent))
         (h01 (height-at heights (+ cell (vec2 0.0 parent)) extent))
         (h11 (height-at heights (+ cell (vec2 parent parent)) extent))
         (n00 (normal-at normals linear-repeat cell extent))
         (n10 (normal-at normals linear-repeat (+ cell (vec2 parent 0.0))
                         extent))
         (n01 (normal-at normals linear-repeat (+ cell (vec2 0.0 parent))
                         extent))
         (n11 (normal-at normals linear-repeat (+ cell (vec2 parent parent))
                         extent))
         (ground-xz (+ (* grid (vec2 (swizzle scale :x) (swizzle scale :z)))
                       (swizzle offset :xz)))
         (from-camera (- ground-xz (swizzle camera-position :xz)))
         (morphing (* (step stride (- parent 0.001))
                      (step (swizzle morph :x) (- (swizzle morph :y) 0.001))))
         (blend (* morphing
                   (smoothstep (swizzle morph :x) (swizzle morph :y)
                               (sqrt (dot from-camera from-camera)))))
         (height (mix own-height (on-lattice h00 h10 h01 h11 f) blend))
         (normal (mix own-normal (on-lattice n00 n10 n01 n11 f) blend))
         (position (vec3 (swizzle ground-xz :x)
                         (* height (swizzle scale :y))
                         (swizzle ground-xz :y)))
         (point (vec4 position 1.0))
         (current (* view-proj point)))
    (set-output clip-position (clip current (swizzle temporal :zw)))
    (set-output world position)
    (set-output surface-normal normal)
    (set-output field-uv (/ grid extent))
    (set-output here current)
    (set-output then (* previous-view-proj point))))

;;; -- the stand ------------------------------------------------------------
;;;
;;; Two rasters over the forest's period, splatted from the actual trees: the
;;; canopy's optical closure (how much sky the crowns hide, read over about
;;; ten metres) and the fallen birch leaves.  Without them the habitat's
;;; forest cover stands in for closure and nothing has fallen.

(define-shader-abstraction read-stand (closure litter sampler xz have
                                       inverse-period fallback)
  `(mix (vec2 ,fallback 0.0)
        (vec2 (swizzle (sample-level ,closure ,sampler
                                     (* ,xz ,inverse-period) 0.0)
                       :x)
              (swizzle (sample-level ,litter ,sampler
                                     (* ,xz ,inverse-period) 0.0)
                       :x))
        ,have))

;;; -- the ground's material ----------------------------------------------
;;;
;;; A few flat, honest colours, each a surface the world's history made:
;;; turf whose hue follows moisture, heath on the high ground, litter under
;;; closed canopy, bare soil where erosion strips it, gravel in channels,
;;; banded rock on faces too steep for soil, and packed dirt on the trails.

(define-shader-function hash12 (p)
  (fract (* (sin (dot p (vec2 127.1 311.7))) 43758.5453)))

(define-shader-function value-noise (p)
  (let* ((i (floor p))
         (f (fract p))
         (u (* f (* f (- (vec2 3.0 3.0) (* f 2.0)))))
         (a (hash12 i))
         (b (hash12 (+ i (vec2 1.0 0.0))))
         (c (hash12 (+ i (vec2 0.0 1.0))))
         (d (hash12 (+ i (vec2 1.0 1.0)))))
    (mix (mix a b (swizzle u :x)) (mix c d (swizzle u :x)) (swizzle u :y))))

;;; A material boundary cut crisply from a coarse field, antialiased by its
;;; own screen-space rate of change.
(define-shader-function band (value threshold)
  (let* ((width (max (+ (abs (derivative-x value)) (abs (derivative-y value)))
                     0.015)))
    (smoothstep (- threshold width) (+ threshold width) value)))

(define-shader-function heath-amount (xz rise)
  (let* ((patch (+ (* 0.6 (value-noise (+ (/ xz 9.0) (vec2 2.7 2.7))))
                   (* 0.4 (value-noise (+ (/ xz 31.0) (vec2 8.1 8.1))))))
         (drift (value-noise (+ (/ xz 140.0) (vec2 5.3 5.3))))
         (green (- 1.0 (smoothstep 0.24 0.30 patch))))
    (* (smoothstep 0.48 0.64 (+ rise (* 0.16 (- drift 0.5))))
       (- 1.0 (* 0.75 green)))))

(define-shader-function heath-tint (xz moisture)
  (let* ((patch (+ (* 0.6 (value-noise (+ (/ xz 9.0) (vec2 2.7 2.7))))
                   (* 0.4 (value-noise (+ (/ xz 31.0) (vec2 8.1 8.1)))))))
    (mix (mix (vec3 0.45 0.42 0.29) (vec3 0.40 0.25 0.18)
              (smoothstep 0.46 0.53 patch))
         (vec3 0.33 0.25 0.25)
         (smoothstep 0.64 0.71 (+ patch (* 0.20 (- moisture 0.5)))))))

;;; Display-space albedo; the caller decodes it.  LAND is moisture,
;;; erosion, deposition, forest cover; FLOOR is shore proximity / 8 m, snow
;;; support, trail, home base.
(define-shader-function ground-albedo (world up distance land floor rise
                                       stand)
  (let* ((moisture (swizzle land :x))
         (xz (swizzle world :xz))
         (patch (value-noise (/ xz 41.0)))
         (near (- 1.0 (smoothstep 150.0 700.0 distance)))
         (fleck (mix 0.5 (value-noise (+ (/ xz 6.1) (vec2 11.7 11.7))) near))
         (grit (mix 0.5 (value-noise (+ (/ xz 1.7) (vec2 3.1 3.1)))
                    (- 1.0 (smoothstep 30.0 120.0 distance))))
         (edge (+ (* 0.6 (- fleck 0.5)) (* 0.25 (- grit 0.5))))
         (lush (clamp (+ (smoothstep 0.08 0.62 moisture)
                         (* 0.55 (- patch 0.5)))
                      0.0 1.0))
         (turf (* (mix (vec3 0.46 0.47 0.20) (vec3 0.25 0.42 0.13) lush)
                  (+ 0.92 (* 0.16 fleck))))
         (heath (mix turf (* (heath-tint xz moisture) (+ 0.90 (* 0.20 fleck)))
                     (heath-amount xz rise)))
         (forest-floor (mix heath
                            (mix (vec3 0.24 0.26 0.13) (vec3 0.30 0.25 0.15)
                                 fleck)
                            (band (+ (swizzle stand :x) (* 0.3 edge)) 0.68)))
         ;; Fallen birch leaves carpet the ground beneath turned groves.
         (leaves (mix forest-floor
                      (* (mix (vec3 0.70 0.52 0.21) (vec3 0.58 0.34 0.15)
                              grit)
                         (+ 0.88 (* 0.24 fleck)))
                      (band (+ (swizzle stand :y) (* 0.30 edge)) 0.24)))
         (shore (* (swizzle floor :x) 8.0))
         (shedding (- 1.0 (smoothstep 0.80 0.92 up)))
         (soil (mix leaves
                    (* (vec3 0.38 0.33 0.27) (+ 0.92 (* 0.16 grit)))
                    (band (+ (* (swizzle land :y) shedding) (* 0.35 edge))
                          0.45)))
         (channel (- 1.0 (smoothstep 2.0 6.0 shore)))
         (gravel (mix soil
                      (* (vec3 0.47 0.44 0.39) (+ 0.88 (* 0.24 grit)))
                      (band (+ (* (swizzle land :z) channel) (* 0.35 edge))
                            0.50)))
         (cliff (- 1.0 (band (+ up (* 0.10 edge)) 0.72)))
         (strata (value-noise (vec2 (+ (* (swizzle world :y) 0.45)
                                       (* 3.0 patch))
                                    (* 0.5 fleck))))
         (rock (mix (vec3 0.33 0.33 0.33) (vec3 0.53 0.52 0.49)
                    (smoothstep 0.3 0.7 strata)))
         (rocky (mix gravel (* rock (+ 0.94 (* 0.12 grit))) cliff))
         (trodden (mix rocky
                       (* (vec3 0.44 0.37 0.29) (+ 0.92 (* 0.16 grit)))
                       (band (+ (swizzle floor :z) (* 0.06 edge)) 0.87)))
         (swash (* (- 1.0 (smoothstep 0.4 3.0 shore))
                   (smoothstep 0.42 0.62 up))))
    (mix trodden (* trodden (vec3 0.62 0.60 0.56)) swash)))

(define-shader terrain-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (field-uv :vec2 :location 2)
              (here :vec4 :location 3)
              (then :vec4 :location 4))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (terrain :uniform-block :binding 1 :members #.*terrain*)
                 (normals :texture-2d :binding 1)
                 (landscape :texture-2d :binding 6)
                 (ground :texture-2d :binding 7)
                 (shadow-map :depth-texture-2d :binding 8)
                 (closure :texture-2d :binding 9)
                 (litter :texture-2d :binding 10)
                 (linear-repeat :sampler :binding 1)
                 (shadow-compare :sampler :binding 3)))
  (let* ((extent (swizzle size :xy))
         ;; Lit and classified from the full-resolution normal, never the
         ;; lattice's, which changes with the level of detail.
         (normal (normalize
                  (unpack-normal
                   (swizzle (sample normals linear-repeat
                                    (+ field-uv (/ (vec2 0.5 0.5) extent)))
                            :xy))))
         (have-fields (swizzle fields :x))
         (field-at (+ field-uv (/ (vec2 0.5 0.5) extent)))
         (land (mix (vec4 0.5 0.0 0.0 0.0)
                    (sample landscape linear-repeat field-at) have-fields))
         (floor-fields (mix (vec4 1.0 (swizzle normal :y) 0.0 0.0)
                            (sample ground linear-repeat field-at)
                            have-fields))
         (eye (swizzle camera-position :xyz))
         (distance (sqrt (dot (- world eye) (- world eye))))
         (rise (/ (- (swizzle world :y) (swizzle relief :x))
                  (max (swizzle relief :y) 1.0)))
         (stand (read-stand closure litter linear-repeat (swizzle world :xz)
                            (swizzle fields :y) (swizzle fields :zw)
                            (swizzle land :w)))
         (albedo (srgb (ground-albedo world (swizzle normal :y) distance
                                      land floor-fields rise stand)))
         (sun (swizzle sun-direction :xyz))
         (lambert (clamp (/ (+ (dot normal sun) 0.08) 1.08) 0.0 1.0))
         ;; The sun's shadow: five comparison taps, a slope-scaled bias
         ;; against acne on raking ground, faded out into the haze.
         (at (sun-map-coordinate sun-view world))
         (bias (+ 0.0006 (* 0.0025 (- 1.0 (max (dot normal sun) 0.0)))))
         (texel (swizzle shadow :y))
         (taps (+ (* 0.4 (shadow-tap shadow-map shadow-compare at 0.0 0.0
                                     texel bias))
                  (* 0.15 (+ (shadow-tap shadow-map shadow-compare at -1.5 -1.5
                                         texel bias)
                             (shadow-tap shadow-map shadow-compare at 1.5 -1.5
                                         texel bias)
                             (shadow-tap shadow-map shadow-compare at -1.5 1.5
                                         texel bias)
                             (shadow-tap shadow-map shadow-compare at 1.5 1.5
                                         texel bias)))))
         (fog (relief-haze (distance-fog distance (swizzle fog-color :w))
                           (swizzle world :y) (swizzle relief :x)
                           (swizzle relief :y)))
         (direct (mix 1.0 (expt taps 1.3)
                      (* (swizzle shadow :x)
                         (clamp (* 2.5 (- 1.0 fog)) 0.0 1.0)
                         (inside-sun-map at))))
         (fill (mix (vec3 0.80 0.92 1.14) (vec3 1.0 1.0 1.0) direct))
         (lit (* albedo (+ (* (hemisphere-light (swizzle ambient :xyz) normal)
                              fill)
                           (* (swizzle sun-diffuse :xyz)
                              (* 0.9 lambert direct))))))
    (set-output color (vec4 (hazed lit world eye fog-color sun relief) 1.0))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program terrain
  :vertex terrain-vertex
  :fragment terrain-fragment)

;;; -- sky -------------------------------------------------------------------

(define-shader-function fullscreen-corner (index)
  (vec2 (if (= index 1.0) 3.0 -1.0)
        (if (= index 2.0) 3.0 -1.0)))

(define-shader sky-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (ndc :vec2 :location 0)))
  (let* ((corner (fullscreen-corner (float vertex-index))))
    (set-output clip-position
                (vec4 (swizzle corner :x) (* -1.0 (swizzle corner :y))
                      0.0 1.0))
    (set-output ndc corner)))

;;; A provisional sky: the haze at the horizon deepening to blue overhead,
;;; and the sun.  Behind everything, at the reversed-Z far plane.
;;; As in sky.metal: a three-stop atmosphere with Mie lobes about the sun,
;;; high cirrus veils, a cumulus deck of value-noise fBm, the sun's disc,
;;; corona, and bloom, and stars at night.

(define-shader-function sky-hash (n)
  (fract (* (sin n) 43758.5453)))

(define-shader-function sky-noise (p)
  (let* ((i (floor p))
         (f0 (fract p))
         (f (* f0 (* f0 (- (vec3 3.0 3.0 3.0) (* f0 2.0)))))
         (n (+ (swizzle i :x) (* (swizzle i :y) 57.0) (* (swizzle i :z) 113.0)))
         (fx (swizzle f :x))
         (fy (swizzle f :y)))
    (mix (mix (mix (sky-hash n) (sky-hash (+ n 1.0)) fx)
              (mix (sky-hash (+ n 57.0)) (sky-hash (+ n 58.0)) fx) fy)
         (mix (mix (sky-hash (+ n 113.0)) (sky-hash (+ n 114.0)) fx)
              (mix (sky-hash (+ n 170.0)) (sky-hash (+ n 171.0)) fx) fy)
         (swizzle f :z))))

;;; The fBm's domain turns between octaves to break the lattice's grain.
(define-shader-function sky-octave (p)
  (* 2.03 (vec3 (+ (* 0.8 (swizzle p :x)) (* 0.6 (swizzle p :z)))
                (+ (swizzle p :y) 7.31)
                (+ (* -0.6 (swizzle p :x)) (* 0.8 (swizzle p :z))))))

(define-shader-function sky-fbm (p0)
  (let* ((p1 (sky-octave p0))
         (p2 (sky-octave p1))
         (p3 (sky-octave p2)))
    (+ (* 0.5 (sky-noise p0)) (* 0.25 (sky-noise p1))
       (* 0.125 (sky-noise p2)) (* 0.0625 (sky-noise p3)))))

(define-shader-function sky-atmosphere (ray sun)
  (let* ((daylight (smoothstep -0.08 0.18 (swizzle sun :y)))
         (golden (* daylight (- 1.0 (smoothstep 0.15 0.65 (swizzle sun :y)))))
         (thickness (- 1.0 (abs (swizzle ray :y))))
         (zenith (mix (srgb (vec3 0.004 0.009 0.05)) (srgb (vec3 0.06 0.20 0.55))
                      daylight))
         (middle (mix (srgb (vec3 0.015 0.022 0.06)) (srgb (vec3 0.25 0.46 0.78))
                      daylight))
         (horizon (mix (srgb (vec3 0.035 0.045 0.09))
                       (srgb (vec3 0.55 0.68 0.84)) daylight))
         (t0 (expt thickness 0.72))
         (gradient (if (< t0 0.62) (mix zenith middle (/ t0 0.62))
                       (mix middle horizon (/ (- t0 0.62) 0.38))))
         (band (expt thickness 4.0))
         (warmed (mix gradient (srgb (vec3 0.94 0.52 0.26))
                      (* 0.16 golden band)))
         (toward (max (dot ray sun) 0.0))
         (scatter (mix (srgb (vec3 1.0 0.96 0.82)) (srgb (vec3 1.0 0.55 0.24))
                       golden))
         (above (+ warmed
                   (* scatter
                      (* daylight
                         (+ (* (expt toward 4.0) (+ 0.07 (* 0.12 golden)))
                            (* (expt toward 32.0) (+ 0.10 (* 0.22 golden))
                               (+ 0.4 (* 0.6 band))))))))
         (below (mix (* (srgb (vec3 0.08 0.09 0.13)) (+ 0.2 (* 0.8 daylight)))
                     horizon (expt thickness 8.0))))
    (if (>= (swizzle ray :y) 0.0) above below)))

(define-shader-function cloud-shape (p coverage time)
  (let* ((base (sky-fbm (* p 0.3)))
         (detail (sky-fbm (+ (* p 1.2) (vec3 (* time 0.05) 0.0 (* time 0.03)))))
         (edge (mix 0.72 0.40 (clamp coverage 0.0 1.0))))
    (smoothstep edge (+ edge 0.13) (+ base (* detail 0.2)))))

(define-shader-function cloud-light (density ray sun)
  (let* ((daylight (smoothstep -0.08 0.18 (swizzle sun :y)))
         (golden (* daylight (- 1.0 (smoothstep 0.15 0.65 (swizzle sun :y)))))
         (sunset (* golden golden))
         (lit (mix (srgb (vec3 1.04 1.03 1.00)) (srgb (vec3 1.05 0.76 0.50))
                   sunset))
         (shade (mix (srgb (vec3 0.55 0.63 0.76)) (srgb (vec3 0.48 0.44 0.58))
                     sunset))
         (core (expt (clamp density 0.0 1.0) 0.75))
         (toward (expt (max (dot ray sun) 0.0) 16.0))
         (cloud (+ (mix lit shade (* 0.85 core))
                   (* lit (* toward (- 1.0 core) (+ 0.5 (* 0.7 golden))
                             daylight)))))
    (mix (* cloud 0.12) cloud daylight)))

(define-shader sky-fragment
    (:stage :fragment
     :inputs ((ndc :vec2 :location 0))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)))
  (let* ((ray (normalize (+ (swizzle view-forward :xyz)
                            (* (swizzle view-right :xyz) (swizzle ndc :x))
                            (* (swizzle view-up :xyz) (swizzle ndc :y)))))
         (sun (normalize (swizzle sun-direction :xyz)))
         (time (swizzle camera-position :w))
         (cloudiness (swizzle sun-direction :w))
         (rise (swizzle ray :y))
         (lifted (max rise 0.05))
         (daylight (smoothstep -0.08 0.18 (swizzle sun :y)))
         (golden (* daylight (- 1.0 (smoothstep 0.15 0.65 (swizzle sun :y)))))
         (atmosphere (sky-atmosphere ray sun))
         ;; High cirrus: soft wind-sheared veils far above the deck.
         (cirrus-at (+ (* ray (/ 900.0 lifted))
                       (vec3 (* time 0.6) 0.0 (* time 0.25))))
         (streak (* (smoothstep 0.48 0.85
                                (sky-fbm (* cirrus-at
                                            (vec3 0.0035 0.008 0.0065))))
                    (smoothstep 0.08 0.25 rise)
                    (+ 0.10 (* 0.08 cloudiness))))
         (cirrus (mix (srgb (vec3 0.9 0.95 1.05)) (srgb (vec3 1.0 0.9 0.8))
                      (expt (max (dot ray sun) 0.0) 4.0)))
         (veiled (mix atmosphere cirrus
                      (* streak (+ 0.25 (* 0.75 daylight)))))
         ;; The cumulus deck, projected onto a dome.
         (deck-at (+ (* ray (/ 200.0 lifted))
                     (vec3 (* time 2.0) 0.0 (* time 1.0))))
         (clouds (* (cloud-shape (* deck-at 0.01) cloudiness time)
                    (smoothstep 0.05 0.1 rise)))
         (clouded (mix veiled (cloud-light clouds ray sun) (* clouds 0.9)))
         ;; The horizon meets the exact fog colour the ground fades to.
         (fogged (mix clouded (swizzle fog-color :xyz)
                      (expt (- 1.0 (clamp rise 0.0 1.0)) 6.0)))
         (toward (max (dot ray sun) 0.0))
         (sun-color (mix (srgb (vec3 1.0 0.96 0.84)) (srgb (vec3 1.0 0.58 0.28))
                         golden))
         (occlude (- 1.0 (* (clamp clouds 0.0 1.0) 0.92)))
         (sunlit (+ fogged
                    (* sun-color
                       (+ (* occlude (+ (* (expt toward 2600.0) 1.35 3.0)
                                        (* (expt toward 160.0) 0.30 1.7)))
                          (* (expt toward 14.0) 0.075 daylight)))))
         ;; Stars at night.
         ;; A heavy overcast closes into a soft grey, its brightness falling
         ;; toward the horizon, and hides the sun.
         (overcast (smoothstep 0.70 1.0 cloudiness))
         (grey (* (swizzle fog-color :xyz)
                  (+ 0.92 (* 0.14 (clamp rise 0.0 1.0)))))
         (closed (mix sunlit grey (* overcast 0.9)))
         (misted (mix closed (* (swizzle fog-color :xyz) 1.06)
                      (* (swizzle relief :w)
                         (- 1.0 (smoothstep -0.02 0.22 rise)))))
         (stars (* (expt (sky-noise (* ray 100.0)) 20.0)
                   (max 0.0 (* (- 1.0 daylight) 0.3
                               (smoothstep 0.0 0.4 rise)))
                   (- 1.0 (step 0.2 daylight))))
         (star-color (mix (srgb (vec3 0.8 0.9 1.0)) (srgb (vec3 1.0 0.9 0.8))
                          (sky-noise (* ray 10.0))))
         (far (vec4 ray 0.0)))
    (set-output color (vec4 (+ misted (* star-color (* stars (- 1.0 overcast))))
                            1.0))
    (set-output motion (- (clip-uv (* previous-view-proj far))
                          (clip-uv (* view-proj far))))))

(define-shader-program sky
  :vertex sky-vertex
  :fragment sky-fragment)

;;; -- draw lists and meshes ------------------------------------------------

;;; A streamed vertex is ten words: position, normal, uv as floats, then
;;; RGBA8 colour and four flag bytes (lit, fogged, wind, flutter).
(define-shader-abstraction vertex-word (vertices first offset)
  `(buffer-element ,vertices (+ ,first (uint ,offset))))

(define-shader-abstraction vertex-float (vertices first offset)
  `(bit-cast :float (vertex-word ,vertices ,first ,offset)))

(define-shader-function byte-of (word shift)
  (/ (float (logand (shift-right word shift) (uint 255.0))) 255.0))

;;; Plants lean with the gust and their boughs shake.
(define-shader-function in-wind (world bend flutter time)
  (let* ((phase (+ (* (swizzle world :x) 0.043) (* (swizzle world :z) 0.051)))
         (gust (+ (sin (+ (* time 1.13) phase))
                  (* 0.45 (sin (+ (* time 2.63) (* phase 1.7) 1.3)))))
         (bough (sin (+ (* time 3.9) (* phase 2.3) 0.7)))
         (flick (sin (+ (* time 8.4) (* phase 13.0) (* (swizzle world :y) 1.9))))
         (lean (* 0.26 bend))
         (shake (* 0.11 (* flutter (+ 0.55 (* 0.45 (abs gust)))))))
    (+ world
       (vec3 (+ (* 0.79 (* gust lean))
                (* (+ (* 0.62 bough) (* 0.44 flick)) shake))
             (* -1.0 (+ (* 0.15 (* (abs gust) lean))
                        (* 0.10 (* (abs bough) shake))))
             (+ (* 0.53 (* gust lean))
                (* (- (* 0.47 bough) (* 0.38 flick)) shake))))))

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *draw*
    '((model :mat4)
      (previous-model :mat4)
      (normal-x :vec4)             ; the model's normal matrix, by columns
      (normal-y :vec4)
      (normal-z :vec4))))

(define-shader uber-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (uv :vec2 :location 2)
               (tint :vec4 :location 3)
               (shading :vec4 :location 4)   ; lit, fogged, foliage, 0
               (here :vec4 :location 5)
               (then :vec4 :location 6))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (vertices :storage-buffer :binding 1 :element :uint)
                 (draw :uniform-block :binding 2 :members #.*draw*)))
  (let* ((first (* vertex-index (uint 10.0)))
         (local (vec4 (vertex-float vertices first 0.0)
                      (vertex-float vertices first 1.0)
                      (vertex-float vertices first 2.0)
                      1.0))
         (normal (vec3 (vertex-float vertices first 3.0)
                       (vertex-float vertices first 4.0)
                       (vertex-float vertices first 5.0)))
         (packed (vertex-word vertices first 8.0))
         (flags (vertex-word vertices first 9.0))
         (bend (byte-of flags (uint 16.0)))
         (flutter (byte-of flags (uint 24.0)))
         (time (swizzle camera-position :w))
         (placed (swizzle (* model local) :xyz))
         (position (in-wind placed bend flutter time))
         (before (in-wind (swizzle (* previous-model local) :xyz) bend flutter
                          (swizzle relief :z)))
         (current (* view-proj (vec4 position 1.0))))
    (set-output clip-position (clip current (swizzle temporal :zw)))
    (set-output world position)
    (set-output surface-normal
                (+ (* (swizzle normal-x :xyz) (swizzle normal :x))
                   (* (swizzle normal-y :xyz) (swizzle normal :y))
                   (* (swizzle normal-z :xyz) (swizzle normal :z))))
    (set-output uv (vec2 (vertex-float vertices first 6.0)
                         (vertex-float vertices first 7.0)))
    (set-output tint (vec4 (srgb (vec3 (byte-of packed (uint 0.0))
                                       (byte-of packed (uint 8.0))
                                       (byte-of packed (uint 16.0))))
                           (byte-of packed (uint 24.0))))
    (set-output shading (vec4 (byte-of flags (uint 0.0))
                              (byte-of flags (uint 8.0))
                              flutter 0.0))
    (set-output here current)
    (set-output then (* previous-view-proj (vec4 before 1.0)))))

(define-shader-function dither (point)
  (fract (* (sin (dot point (vec3 12.9898 78.233 37.719))) 43758.5453)))

(define-shader uber-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (uv :vec2 :location 2)
              (tint :vec4 :location 3)
              (shading :vec4 :location 4)
              (here :vec4 :location 5)
              (then :vec4 :location 6))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (picture :texture-2d :binding 0)
                 (linear-repeat :sampler :binding 1)))
  (let* ((base (* tint (sample picture linear-repeat uv)))
         (foliage (swizzle shading :z))
         ;; Translucent foliage becomes a stipple of opaque fragments, so
         ;; it needs no sorting and keeps its depth.
         (stippled (* (step 0.001 foliage) (step 0.999 (- 1.0 (swizzle base :w)))))
         (threshold (dither (floor (* world 24.0))))
         (n (normalize surface-normal))
         (sun (swizzle sun-direction :xyz))
         (camera (swizzle camera-position :xyz))
         (eye (normalize (- camera world)))
         (lambert (clamp (/ (+ (dot n sun) 0.1) 1.1) 0.0 1.0))
         (half-way (normalize (+ sun eye)))
         (shine (* (smoothstep 0.82 0.98 (swizzle base :w)) (- 1.0 foliage)))
         (rim (* (expt (- 1.0 (max (dot n eye) 0.0)) 3.0)
                 (+ 0.35 (* 0.65 (max (swizzle n :y) 0.0)))))
         (albedo (swizzle base :xyz))
         (lit (+ (* albedo (+ (hemisphere-light (swizzle ambient :xyz) n)
                              (* (swizzle sun-diffuse :xyz) lambert)))
                 (* (mix albedo (vec3 1.0 1.0 1.0) 0.2)
                    (* (swizzle sun-specular :xyz)
                       (* 0.22 (* (expt (max (dot n half-way) 0.0) 64.0)
                                  shine))))
                 (* albedo (* (vec3 0.025 0.04 0.065)
                              (* rim (- 1.0 (* 0.75 foliage)))))))
         (shaded (mix albedo lit (swizzle shading :x)))
         (fogged (mix shaded
                      (hazed shaded world camera fog-color sun relief)
                      (swizzle shading :y))))
    (when (> stippled 0.5)
      (when (<= (swizzle base :w) threshold)
        (discard)))
    (set-output color (vec4 fogged (max (swizzle base :w) stippled)))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program uber
  :vertex uber-vertex
  :fragment uber-fragment)

;;; -- the heads-up display --------------------------------------------------

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *hud*
    '((projection :mat4)           ; points to clip, y down
      (output :vec4))))            ; x: 1 if the drawable is linear;
                                   ; y: pixels per point

(define-shader hud-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (uv :vec2 :location 0)
               (tint :vec4 :location 1))
     :resources ((hud :uniform-block :binding 0 :members #.*hud*)
                 (vertices :storage-buffer :binding 1 :element :uint)))
  (let* ((first (* vertex-index (uint 10.0)))
         (packed (vertex-word vertices first 8.0))
         (position (* projection
                      (vec4 (vertex-float vertices first 0.0)
                            (vertex-float vertices first 1.0)
                            (vertex-float vertices first 2.0)
                            1.0))))
    (set-output clip-position (clip position (vec2 0.0 0.0)))
    (set-output uv (vec2 (vertex-float vertices first 6.0)
                         (vertex-float vertices first 7.0)))
    (set-output tint (vec4 (byte-of packed (uint 0.0))
                           (byte-of packed (uint 8.0))
                           (byte-of packed (uint 16.0))
                           (byte-of packed (uint 24.0))))))

(define-shader hud-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0)
              (tint :vec4 :location 1))
     :outputs ((color :vec4 :location 0))
     :resources ((hud :uniform-block :binding 0 :members #.*hud*)
                 (picture :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)))
  (let* ((c (* tint (sample picture linear-clamp uv))))
    (set-output color (vec4 (mix (swizzle c :xyz) (srgb (swizzle c :xyz))
                                 (swizzle output :x))
                            (swizzle c :w)))))

(define-shader-program hud
  :vertex hud-vertex
  :fragment hud-fragment)

;;; -- the trunk forest -----------------------------------------------------
;;;
;;; As in forest_trunks.metal: each individual is one tapered trunk and a
;;; few faceted crown masses -- stacked cones for a spruce, leaf clumps on
;;; ascending branches for a birch -- generated by vertex pulling from its
;;; record against its (species, detail tier) class's shared index buffer.
;;; Here the GPU chooses the candidates: FOREST-CULL tests every individual
;;; against a view and appends it to its class's list, counting the class's
;;; indexed indirect draw.  The camera's view sorts trees into twelve
;;; classes; the sun's shadow view into two, a species each at the coarsest
;;; tier.
;;;
;;; A record is four vec4 rows: root xyz and height; ground normal and
;;; crown radius; cover, moisture, stand closure, autumn; and the seed and
;;; species as uint bits.  A candidate is two words: the record's index and
;;; its projected height in pixels as float bits, which names its tier.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *forest*
    '((view :mat4)                 ; world to the culling view's clip
      (eye :vec4)                  ; the view's centre, toward which trees wrap
      (world :vec4)                ; period x, period z, count, class capacity
      (cull :vec4)                 ; |row x|, |row y|, |row w|, scene height
      (mode :vec4)))               ; x: 1 for the sun's shadow view
  (defparameter *forest-class*
    '((class :vec4))))             ; first candidate, 0, 0, 0

(define-shader-function cross3 (a b)
  (- (* (swizzle a :yzx) (swizzle b :zxy)) (* (swizzle a :zxy) (swizzle b :yzx))))

(define-shader-function tree-mix (value)
  (let* ((a (* (logxor value (shift-right value (uint 16))) (uint #x7feb352d)))
         (b (* (logxor a (shift-right a (uint 15))) (uint #x846ca68b))))
    (logxor b (shift-right b (uint 16)))))

(define-shader-function tree-hash (seed lane)
  (/ (float (logand (tree-mix (logxor seed (* lane (uint #x9e3779b9))))
                    (uint #x00ffffff)))
     16777216.0))

;;; The detail tier of an individual this many pixels tall, 0 to 5.
(define-shader-function tree-tier (pixels)
  (+ (step 20.0 pixels) (step 30.0 pixels) (step 40.0 pixels)
     (step 60.0 pixels) (step 90.0 pixels)))

;;; The world is periodic: each individual wraps toward the view.
(define-shader-function tree-root (root period eye)
  (+ root (vec3 (* (swizzle period :x)
                   (floor (+ (/ (- (swizzle eye :x) (swizzle root :x))
                                (max (swizzle period :x) 0.001))
                             0.5)))
                0.0
                (* (swizzle period :y)
                   (floor (+ (/ (- (swizzle eye :z) (swizzle root :z))
                                (max (swizzle period :y) 0.001))
                             0.5))))))

(define-shader forest-cull-compute
    (:stage :compute
     :workgroup-size (64 1 1)
     :inputs ((invocation :uvec3 :built-in :global-invocation-id))
     :resources ((forest :uniform-block :binding 1 :members #.*forest*)
                 (trees :storage-buffer :binding 2 :element :vec4)
                 (candidates :storage-buffer :binding 3 :element :uint
                             :access :read-write)
                 (arguments :storage-buffer :binding 4 :element :uint
                            :access :read-write)))
  (let* ((index (swizzle invocation :x)))
    (when (< index (uint (swizzle world :z)))
      (let* ((row (* index (uint 4)))
             (root-height (buffer-element trees row))
             (crown (swizzle (buffer-element trees (+ row (uint 1))) :w))
             (conifer (= (bit-cast :uint
                                   (swizzle (buffer-element trees
                                                            (+ row (uint 3)))
                                            :y))
                         (uint 1)))
             (shadow (> (swizzle mode :x) 0.5))
             (height (swizzle root-height :w))
             (centre-of (swizzle eye :xyz))
             (root (tree-root (swizzle root-height :xyz) (swizzle world :xy)
                              centre-of))
             (centre (+ root (vec3 0.0 (* 0.52 height) 0.0)))
             (radius (+ (* 0.55 height) (* 1.4 crown)))
             (clip (* view (vec4 centre 1.0)))
             (w (swizzle clip :w))
             (inside (and (> w (* -1.0 radius))
                          (< (abs (swizzle clip :x))
                             (+ w (* radius (swizzle cull :x))))
                          (< (abs (swizzle clip :y))
                             (+ w (* radius (swizzle cull :y))))))
             (offset (- centre centre-of))
             (distance (max (sqrt (dot offset offset)) 0.6))
             (scale (/ (* (swizzle cull :y) (swizzle cull :w)) distance))
             (crown-pixels (* crown scale)))
        (when (and inside (or shadow (>= crown-pixels 4.0)))
          (let* ((pixels (* 0.5 height scale))
                 (species (if conifer (uint 1) (uint 0)))
                 (class (if shadow species
                            (+ (uint (tree-tier pixels))
                               (* species (uint 6)))))
                 (slot (atomic-add arguments (+ (* class (uint 5)) (uint 1))
                                   (uint 1)))
                 (at (* (+ (* class (uint (swizzle world :w))) slot)
                        (uint 2))))
            (set-buffer-element candidates at index)
            (set-buffer-element candidates (+ at (uint 1))
                                (bit-cast :uint (if shadow 0.0 pixels)))))))))

(define-shader-program forest-cull
  :compute forest-cull-compute)

;;; One individual's frame and proportions, as forest_trunks.metal's
;;; TrunkTree: nearly vertical with a small lean of its own; closure
;;; decides where the crown starts, open-grown trees keeping their branches
;;; low and a closed stand standing as a hall of clear trunks.
(define-shader-struct tree-shape
  (root :vec3) (up :vec3) (right :vec3) (forward :vec3)
  (height :float) (crown-base :float) (crown-radius :float)
  (trunk-radius :float) (seed-turn :float) (closure :float)
  (seed :uint) (conifer :bool)
  (sides :uint) (crown-sides :uint) (masses :uint) (branches :bool))

(define-shader-function grow-tree (root-height up-radius ecology identity root
                                   tier crown-scale)
  (let* ((seed (bit-cast :uint (swizzle identity :x)))
         (conifer (= (bit-cast :uint (swizzle identity :y)) (uint 1)))
         (lean-turn (* 6.2831853 (tree-hash seed (uint 3))))
         (lean (* 0.065 (tree-hash seed (uint 4))))
         (up (normalize (+ (vec3 0.0 1.0 0.0)
                           (* 0.08 (vec3 (swizzle up-radius :x) 0.0
                                         (swizzle up-radius :z)))
                           (* lean (vec3 (cos lean-turn) 0.0
                                         (sin lean-turn))))))
         (right (normalize (cross3 up (vec3 0.0 0.0 1.0))))
         (height (swizzle root-height :w))
         (closure (smoothstep 0.12 0.42 (swizzle ecology :z)))
         (base-share (+ (mix (if conifer 0.04 0.40) (if conifer 0.20 0.60)
                             closure)
                        (* 0.08 (- (tree-hash seed (uint 5)) 0.5))))
         (sides (if (>= tier 5.0) (uint 10) (if (>= tier 2.0) (uint 7)
                                                 (uint 5)))))
    (make-tree-shape
     :root root :up up :right right :forward (cross3 right up)
     :height height
     :crown-base (* height base-share)
     :crown-radius (* (swizzle up-radius :w) crown-scale)
     :trunk-radius (* height (if conifer 0.0078 0.0085)
                      (+ 0.85 (* 0.3 (tree-hash seed (uint 6)))))
     :seed-turn (* 6.2831853 (tree-hash seed (uint 7)))
     :closure closure
     :seed seed :conifer conifer
     :sides sides
     :crown-sides (if (= sides (uint 10)) (uint 9) sides)
     :masses (if conifer
                 (if (>= tier 4.0) (uint 5) (if (>= tier 1.0) (uint 4)
                                                (uint 3)))
                 (if (>= tier 4.0) (uint 10) (if (>= tier 1.0) (uint 7)
                                                 (uint 4))))
     :branches (and (not conifer) (>= tier 3.0)))))

(define-shader-struct tree-vertex
  (position :vec3) (normal :vec3) (bark :vec2) (crown-height :float)
  (foliage :bool) (branch :bool) (mass :uint))

;;; One vertex of an individual, in the numbering its class's index buffer
;;; joins into triangles: the trunk's four rings, then each crown mass.
(define-shader-function tree-vertex-at (tree index)
  (let* ((pi2 6.2831853)
         (root (tree-shape-root tree))
         (up (tree-shape-up tree))
         (right (tree-shape-right tree))
         (forward (tree-shape-forward tree))
         (height (tree-shape-height tree))
         (crown-base (tree-shape-crown-base tree))
         (crown-radius (tree-shape-crown-radius tree))
         (trunk-radius (tree-shape-trunk-radius tree))
         (seed-turn (tree-shape-seed-turn tree))
         (seed (tree-shape-seed tree))
         (conifer (tree-shape-conifer tree))
         (sides (tree-shape-sides tree))
         (crown-sides (tree-shape-crown-sides tree))
         (masses (tree-shape-masses tree))
         (trunk-vertices (* sides (uint 4)))
         (is-trunk (< index trunk-vertices))
         ;; Rings at the flared root, breast height, the crown's base, and
         ;; the tip.
         (ring (/ index sides))
         (trunk-turn (+ seed-turn (/ (* pi2 (float (mod index sides)))
                                     (float sides))))
         ;; The trunk grows out of the soil rather than standing on it: its
         ;; first ring sits below the ground and flares into uneven root
         ;; buttresses, narrowing to the bole within a metre.
         (trunk-along (if (= ring (uint 0)) -0.35
                          (if (= ring (uint 1)) 0.9
                              (if (= ring (uint 2)) crown-base
                                  (* height (if conifer 0.94 0.86))))))
         (trunk-width (if (= ring (uint 0))
                          (+ 1.75 (* 0.65 (tree-hash seed
                                                     (+ (uint 300)
                                                        (mod index sides)))))
                          (if (= ring (uint 1)) 1.05
                              (if (= ring (uint 2)) 0.72 0.14))))
         (trunk-out (+ (* right (cos trunk-turn)) (* forward (sin trunk-turn))))
         (trunk-point (+ root (* up trunk-along)
                         (* trunk-out (* trunk-radius trunk-width))))
         ;; The crown masses.
         (local (if is-trunk (uint 0) (- index trunk-vertices)))
         (per-mass (if conifer (+ crown-sides (uint 2))
                       (if (tree-shape-branches tree) (uint 14) (uint 8))))
         (mass (/ local per-mass))
         (corner (mod local per-mass))
         (fmass (float mass))
         (span (- height crown-base))
         (twist (+ seed-turn (* 1.7 fmass)))
         (tiers (float masses))
         ;; A spruce: overlapping tiers narrowing upward, a broad skirt in
         ;; the open and a narrow spire in a closed stand.
         (f0 (/ fmass tiers))
         (f1 (/ (+ fmass 1.0) tiers))
         (jitter (/ (* 0.18 (- (tree-hash seed (+ (uint 20) mass)) 0.5)) tiers))
         (cone-base (+ crown-base (* span (+ (* 0.90 f0) jitter))))
         (cone-top (if (= (+ mass (uint 1)) masses) height
                       (+ crown-base (* span (min (+ (* 0.90 f1) (/ 0.95 tiers))
                                                  1.0)))))
         (cone-radius (* crown-radius
                         (mix 1.0 0.72 (tree-shape-closure tree))
                         (- 1.0 (* 0.80 f0))
                         (+ 0.82 (* 0.36 (tree-hash seed (+ (uint 30) mass))))))
         (cone-turn (+ twist (/ (* pi2 (float corner)) (float crown-sides))))
         (on-ring (< corner crown-sides))
         (cone-along (if on-ring
                         (- cone-base (* (if (= (logand corner (uint 1))
                                                (uint 1))
                                             0.06 0.0)
                                         span))
                         (if (= corner crown-sides) cone-top
                             (+ cone-base (* 0.12 span)))))
         (cone-point (+ root (* up cone-along)
                        (if on-ring
                            (* (+ (* right (cos cone-turn))
                                  (* forward (sin cone-turn)))
                               cone-radius)
                            (vec3 0.0 0.0 0.0))))
         ;; A birch clump, spiralling up the stem by the golden angle in a
         ;; narrow oval envelope, so sky shows between them.
         (rise (clamp (/ (+ fmass 0.5
                            (* 0.6 (- (tree-hash seed (+ (uint 80) mass)) 0.5)))
                         tiers)
                      0.0 1.0))
         (envelope (expt (sin (* 3.1415927 (+ 0.12 (* 0.80 rise)))) 0.7))
         (clump-turn (+ seed-turn (* 2.39996 fmass)
                        (* 0.8 (- (tree-hash seed (+ (uint 90) mass)) 0.5))))
         (reach (* crown-radius envelope
                   (+ 0.30 (* 0.45 (tree-hash seed (+ (uint 100) mass))))))
         (clump-along (+ crown-base (* span (+ 0.06 (* 0.86 rise)))))
         (centre (+ root (* up clump-along)
                    (* (+ (* right (cos clump-turn))
                          (* forward (sin clump-turn)))
                       reach)))
         (attach (+ root (* up (max (- clump-along (* 0.9 reach))
                                    (* 0.92 crown-base)))))
         (clump-radius (* crown-radius
                          (+ 0.50 (* 0.34 (tree-hash seed (+ (uint 110) mass))))
                          (- 1.0 (* 0.35 rise)) (sqrt (/ 10.0 tiers))))
         (clump-up (normalize
                    (+ up (* right (* 0.5 (- (tree-hash seed (+ (uint 130) mass))
                                             0.5)))
                       (* forward (* 0.5 (- (tree-hash seed (+ (uint 140) mass))
                                            0.5))))))
         (across (normalize (cross3 clump-up forward)))
         (depth (cross3 across clump-up))
         (k (- corner (uint 2)))
         (leaf-lane (+ (* (uint 6) mass) k))
         (leaf-turn (+ twist (* 1.0471976 (float k))
                       (* 0.5 (- (tree-hash seed (+ (uint 120) leaf-lane)) 0.5))))
         (leaf-reach (* clump-radius
                        (+ 0.74 (* 0.42 (tree-hash seed
                                                   (+ (uint 180) leaf-lane))))))
         (leaf-lift (+ (if (= (logand k (uint 1)) (uint 1)) 0.26 -0.34)
                       (* 0.16 (- (tree-hash seed (+ (uint 240) leaf-lane))
                                  0.5))))
         (leaf-point
           (if (= corner (uint 0)) (+ centre (* clump-up (* 0.66 clump-radius)))
               (if (= corner (uint 1))
                   (- centre (* clump-up (* 0.92 clump-radius)))
                   (+ centre (* clump-up (* leaf-lift clump-radius))
                      (* (+ (* across (cos leaf-turn)) (* depth (sin leaf-turn)))
                         leaf-reach)))))
         ;; A clump's branch: a thin prism from the stem into the clump.
         (is-branch (and (not is-trunk) (not conifer) (>= corner (uint 8))))
         (bk (- corner (uint 8)))
         (branch-end (mix attach centre 0.85))
         (axis (normalize (- branch-end attach)))
         (branch-side (normalize (cross3 axis (+ forward (* 0.3 right)))))
         (branch-other (cross3 axis branch-side))
         (branch-turn (* 2.0943951 (float (mod bk (uint 3)))))
         (around (+ (* branch-side (cos branch-turn))
                    (* branch-other (sin branch-turn))))
         (first-ring (< bk (uint 3)))
         (branch-radius (* trunk-radius (if first-ring 0.34 0.10)))
         (branch-point (+ (if first-ring attach branch-end)
                          (* around branch-radius)))
         (point (if is-trunk trunk-point
                    (if is-branch branch-point
                        (if conifer cone-point leaf-point))))
         (foliage (and (not is-trunk) (not is-branch))))
    (make-tree-vertex
     :position point
     :normal (if is-trunk trunk-out
                 (if is-branch around
                     (normalize (- point (+ root (* up (+ crown-base
                                                           (* 0.5 span))))))))
     :bark (if is-trunk
               (vec2 (* trunk-turn trunk-radius) trunk-along)
               (vec2 (* branch-turn branch-radius) (dot (- point root) up)))
     :crown-height (if foliage
                       (clamp (/ (- (dot (- point root) up) crown-base)
                                 (max span 0.01))
                              0.0 1.0)
                       0.0)
     :foliage foliage
     :branch is-branch
     :mass mass)))

;;; Bark, branch, needles, or leaves that turn clump by clump in autumn,
;;; in display space.
(define-shader-function tree-palette (tree vertex moisture autumn)
  (let* ((seed (tree-shape-seed tree))
         (conifer (tree-shape-conifer tree))
         (mass (tree-vertex-mass vertex))
         (fmass (float mass))
         (hue (- (tree-hash seed (uint 50)) 0.5))
         (shade (+ 0.92 (/ (* 0.12 fmass)
                           (max (- (float (tree-shape-masses tree)) 1.0) 1.0))
                   (* -0.06 (tree-hash seed (uint 51)))
                   (if conifer 0.0
                       (* 0.16 (- (tree-hash seed (+ (uint 200) mass)) 0.5)))))
         (needle (vec3 (+ 0.20 (* 0.05 hue)) (+ 0.36 (* 0.06 moisture)) 0.22))
         (green (vec3 (+ 0.42 (* 0.08 hue)) (+ 0.60 (* 0.05 moisture)) 0.22))
         (turned (clamp (/ (- (if conifer 0.0 autumn)
                              (* 0.35 (tree-hash seed (+ (uint 70) mass))))
                           0.65)
                        0.0 1.0))
         (amber (expt (tree-hash seed (uint 52)) 3.0))
         (gold (mix (vec3 0.95 0.73 0.20) (vec3 0.92 0.48 0.13) amber)))
    (if (tree-vertex-foliage vertex)
        (* (if conifer needle (mix green gold turned)) shade)
        (if (tree-vertex-branch vertex) (vec3 0.34 0.31 0.29)
            (if conifer (vec3 0.38 0.30 0.24) (vec3 0.80 0.78 0.72))))))

;;; Wind sways the crown about its base; the trunk only bends near the top.
(define-shader-function wind-offset (world bend flutter time)
  (let* ((ph (+ (* (swizzle world :x) 0.043) (* (swizzle world :z) 0.051)))
         (gust (+ (sin (+ (* time 1.13) ph))
                  (* 0.45 (sin (+ (* time 2.63) (* ph 1.7) 1.3)))))
         (bough (sin (+ (* time 3.90) (* ph 2.3) 0.7)))
         (flick (sin (+ (* time 8.40) (* ph 13.0) (* (swizzle world :y) 1.9))))
         (driven (+ 0.55 (* 0.45 (abs gust))))
         (lean (* 0.26 bend))
         (shake (* 0.11 (* flutter driven))))
    (vec3 (+ (* 0.79 gust lean) (* (+ (* 0.62 bough) (* 0.44 flick)) shake))
          (* -1.0 (+ (* 0.15 (abs gust) lean) (* 0.10 (abs bough) shake)))
          (+ (* 0.53 gust lean) (* (- (* 0.47 bough) (* 0.38 flick)) shake)))))

(define-shader-function tree-sway (tree vertex time)
  (let* ((point (tree-vertex-position vertex))
         (rise (clamp (/ (dot (- point (tree-shape-root tree))
                              (tree-shape-up tree))
                         (max (tree-shape-height tree) 0.01))
                      0.0 1.0)))
    (wind-offset point (* 0.30 (* rise rise))
                 (if (tree-vertex-foliage vertex) (* 0.25 rise) 0.0)
                 time)))

(define-shader forest-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world-position :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (albedo :vec3 :location 2)
               (bark :vec2 :location 3)
               (crown-height :float :location 4)
               (kind :vec2 :location 5 :interpolation :flat)
               (here :vec4 :location 6)
               (then :vec4 :location 7))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (forest :uniform-block :binding 1 :members #.*forest*)
                 (draw :uniform-block :binding 2 :members #.*forest-class*)
                 (trees :storage-buffer :binding 3 :element :vec4)
                 (candidates :storage-buffer :binding 4 :element :uint)))
  (let* ((at (* (+ (uint (swizzle class :x)) instance-index) (uint 2)))
         (row (* (buffer-element candidates at) (uint 4)))
         (pixels (bit-cast :float (buffer-element candidates (+ at (uint 1)))))
         (root-height (buffer-element trees row))
         (ecology (buffer-element trees (+ row (uint 2))))
         (tree (grow-tree root-height (buffer-element trees (+ row (uint 1)))
                          ecology (buffer-element trees (+ row (uint 3)))
                          (tree-root (swizzle root-height :xyz)
                                     (swizzle world :xy)
                                     (swizzle camera-position :xyz))
                          (tree-tier pixels) 1.0))
         (vertex (tree-vertex-at tree vertex-index))
         (point (tree-vertex-position vertex))
         (current (+ point (tree-sway tree vertex
                                      (swizzle camera-position :w))))
         (previous (+ point (tree-sway tree vertex (swizzle relief :z))))
         (here-clip (* view-proj (vec4 current 1.0))))
    (set-output clip-position (clip here-clip (swizzle temporal :zw)))
    (set-output world-position current)
    (set-output surface-normal (tree-vertex-normal vertex))
    (set-output albedo (srgb (tree-palette tree vertex (swizzle ecology :y)
                                           (swizzle ecology :w))))
    (set-output bark (tree-vertex-bark vertex))
    (set-output crown-height (tree-vertex-crown-height vertex))
    (set-output kind (vec2 (if (tree-vertex-foliage vertex) 1.0 0.0)
                           (if (tree-shape-conifer tree) 1.0 0.0)))
    (set-output here here-clip)
    (set-output then (* previous-view-proj (vec4 previous 1.0)))))

;;; Bark: value noise stretched along the trunk -- deep vertical furrows on
;;; a spruce, dark horizontal lenticels on pale birch.
(define-shader-function bark-shade (bark conifer)
  (let* ((furrows (value-noise (vec2 (* (swizzle bark :x) 9.0)
                                     (* (swizzle bark :y) 0.9))))
         (plates (value-noise (vec2 (* (swizzle bark :x) 3.0)
                                    (* (swizzle bark :y) 0.35))))
         (marks (value-noise (vec2 (* (swizzle bark :x) 3.5)
                                   (* (swizzle bark :y) 5.0))))
         (lenticel (smoothstep 0.70 0.82 marks))
         (scar (smoothstep 0.80 0.92
                           (value-noise (vec2 (* (swizzle bark :x) 1.2)
                                              (* (swizzle bark :y) 0.6)))))
         (birch (* (- 1.0 (* 0.80 (max lenticel scar)))
                   (+ 0.92 (* 0.08 (value-noise (* bark 12.0))))))
         (spruce (* (+ 0.62 (* 0.45 furrows)) (+ 0.85 (* 0.3 plates)))))
    (mix birch spruce conifer)))

(define-shader forest-fragment
    (:stage :fragment
     :inputs ((world-position :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (albedo :vec3 :location 2)
              (bark :vec2 :location 3)
              (crown-height :float :location 4)
              (kind :vec2 :location 5 :interpolation :flat)
              (here :vec4 :location 6)
              (then :vec4 :location 7))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (shadow-map :depth-texture-2d :binding 8)
                 (shadow-compare :sampler :binding 3)))
  (let* ((foliage (swizzle kind :x))
         (eye (swizzle camera-position :xyz))
         (to-eye (- eye world-position))
         (view (/ to-eye (max (sqrt (dot to-eye to-eye)) 0.001)))
         (light (swizzle sun-direction :xyz))
         ;; Crowns are faceted: one normal per triangle, from the surface's
         ;; own screen derivatives, turned toward the eye.
         (facet (normalize (cross3 (derivative-x world-position)
                                   (derivative-y world-position))))
         (facing (if (< (dot facet view) 0.0) (* -1.0 facet) facet))
         (n (if (> foliage 0.5) facing (normalize surface-normal)))
         ;; Where the trunk meets the ground the light fails and the bark
         ;; changes: moss climbs a spruce's buttresses, and a birch's pale
         ;; bark gives way to its dark, fissured base.
         (base (- 1.0 (smoothstep -0.1 1.4 (swizzle bark :y))))
         (rooted (mix (mix (srgb (vec3 0.24 0.22 0.20))
                           (srgb (vec3 0.27 0.34 0.15)) (swizzle kind :y))
                      albedo (- 1.0 (* 0.75 base))))
         (surface (if (> foliage 0.5) albedo
                      (* rooted (bark-shade bark (swizzle kind :y))
                         (mix 1.0 0.55 (* base base)))))
         ;; Crowns keep metres of light-depth margin so a crown does not
         ;; shadow itself solid; trunks stay precise.
         (at (sun-map-coordinate sun-view world-position))
         (margin (/ (if (> foliage 0.5) 7.0 1.5) 1240.0))
         (texel (swizzle shadow :y))
         (lit (* 0.25 (+ (shadow-tap shadow-map shadow-compare at -0.5 -0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at 0.5 -0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at -0.5 0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at 0.5 0.5
                                     texel margin))))
         (visibility (mix 1.0 (mix 0.18 1.0 lit)
                          (* (swizzle shadow :x) (inside-sun-map at))))
         ;; Foliage wraps the light a little: a crown is porous.
         (sun (if (> foliage 0.5)
                  (clamp (/ (+ (dot n light) 0.35) 1.35) 0.0 1.0)
                  (clamp (dot n light) 0.0 1.0)))
         (occlusion (if (> foliage 0.5) (mix 0.72 1.0 crown-height) 0.80))
         (sun-light (swizzle sun-diffuse :xyz))
         (shaded (* surface (+ (* sun-light (* sun visibility 0.95))
                               (* (hemisphere-light (swizzle ambient :xyz) n)
                                  occlusion))))
         ;; Against the sun, needles and leaves glow with transmitted light.
         (glow (* surface sun-light
                  (* foliage visibility 0.35
                     (expt (clamp (dot (* -1.0 view) light) 0.0 1.0) 4.0)))))
    (set-output color (vec4 (hazed (+ shaded glow) world-position eye fog-color
                                   light relief)
                            1.0))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program forest
  :vertex forest-vertex
  :fragment forest-fragment)

;;; -- the sun's shadow -----------------------------------------------------
;;;
;;; A 2048-texel depth map over the 160 metres around the rider, rendered
;;; each frame in the sun's orthographic view (conventional depth, nearer
;;; is smaller): the terrain at native detail and the trees, each species at
;;; its coarsest tier with a smaller, porous crown.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *caster*
    '((light-view :mat4)           ; world to the sun's clip
      (focus :vec4))))             ; the map's centre; w: seconds

(define-shader terrain-shadow-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position))
     :resources ((caster :uniform-block :binding 0 :members #.*caster*)
                 (terrain :uniform-block :binding 1 :members #.*terrain*)
                 (chunk :uniform-block :binding 2 :members #.*chunk*)
                 (heights :texture-2d :binding 0)))
  (let* ((row-length (uint (swizzle placement :w)))
         (grid (+ (swizzle placement :xy)
                  (* (vec2 (float (mod vertex-index row-length))
                           (float (/ vertex-index row-length)))
                     (swizzle placement :z))))
         (ground-xz (+ (* grid (vec2 (swizzle scale :x) (swizzle scale :z)))
                       (swizzle offset :xz)))
         (height (height-at heights grid (swizzle size :xy)))
         (point (vec4 (swizzle ground-xz :x) (* height (swizzle scale :y))
                      (swizzle ground-xz :y) 1.0)))
    (set-output clip-position (clip (* light-view point) (vec2 0.0 0.0)))))

(define-shader-program terrain-shadow
  :vertex terrain-shadow-vertex)

(define-shader forest-shadow-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position))
     :resources ((caster :uniform-block :binding 0 :members #.*caster*)
                 (forest :uniform-block :binding 1 :members #.*forest*)
                 (draw :uniform-block :binding 2 :members #.*forest-class*)
                 (trees :storage-buffer :binding 3 :element :vec4)
                 (candidates :storage-buffer :binding 4 :element :uint)))
  (let* ((at (* (+ (uint (swizzle class :x)) instance-index) (uint 2)))
         (row (* (buffer-element candidates at) (uint 4)))
         (root-height (buffer-element trees row))
         (tree (grow-tree root-height (buffer-element trees (+ row (uint 1)))
                          (buffer-element trees (+ row (uint 2)))
                          (buffer-element trees (+ row (uint 3)))
                          (tree-root (swizzle root-height :xyz)
                                     (swizzle world :xy) (swizzle focus :xyz))
                          0.0 0.62))
         (vertex (tree-vertex-at tree vertex-index))
         (point (+ (tree-vertex-position vertex)
                   (tree-sway tree vertex (swizzle focus :w)))))
    (set-output clip-position
                (clip (* light-view (vec4 point 1.0)) (vec2 0.0 0.0)))))

(define-shader-program forest-shadow
  :vertex forest-shadow-vertex)

;;; -- grass ------------------------------------------------------------------
;;;
;;; As undergrowth.metal's grass, without its mesh stages.  Nothing is
;;; stored: a window of 0.6-metre ground tiles anchored to the world lattice
;;; surrounds the camera, and GRASS-TILES keeps those in view that the grass
;;; medium says carry blades -- light and water, no trail worn across, ground
;;; a root could hold -- appending each with its blade count and counting it
;;; into one indexed indirect draw.  Each surviving tile is an instance of
;;; 32 four-section blades, which GRASS-VERTEX grows from hashes and the
;;; terrain's own fields, so the sward cannot drift from the ground it
;;; stands on.  A blade's count and shape follow its projected width: the
;;; field thins blade by blade into the ground's own turf colour.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *grass*
    '((window :vec4)               ; first cell x, z, tiles per side, tile m
      (lattice :vec4)              ; 1/step x, 1/step z, height scale, samples
      (habitat :vec4)              ; window reach, density, 0, focal pixels
      (interaction :vec4)          ; the mover's position, footprint radius
      (stand-field :vec4))))       ; have stand, 1 / period x, z, 0

(define-shader-function plant-hash (cell lane)
  (/ (float (logand (tree-mix (logxor (* (swizzle cell :x) (uint #x9e3779b9))
                                      (* (swizzle cell :y) (uint #x85ebca6b))
                                      (* lane (uint #xc2b2ae35))))
                    (uint #x00ffffff)))
     16777216.0))

;;; A world cell's lattice identity; two's-complement wrap keeps negative
;;; cells well defined.
(define-shader-function cell-identity (cell)
  (uvec2 (uint (int (swizzle cell :x))) (uint (int (swizzle cell :y)))))

(define-shader-function field-uv (xz lattice)
  (/ (* xz (swizzle lattice :xy)) (swizzle lattice :w)))

;;; The ground's height under a world point, bilinear between samples.
(define-shader-abstraction sample-ground (heights xz lattice)
  `(* (height-between ,heights
                      (* ,xz (swizzle ,lattice :xy))
                      (vec2 (swizzle ,lattice :w) (swizzle ,lattice :w)))
      (swizzle ,lattice :z)))

(define-shader-struct grass-medium
  (leaf-area :float) (moisture :float) (forest-cover :float)
  (riparian :float) (clump :float) (canopy-height :float)
  (blade-tint :vec3))

;;; How much sward one patch carries, as moppe_grass_medium: closed canopy
;;; thins it, soil water sets how lush it is, a trail clears it, a slope
;;; past what roots hold sheds it, and the high fells give way to heath.
(define-shader-function read-grass-medium (xz land floor up rise density
                                           stand)
  (let* ((moisture (clamp (swizzle land :x) 0.0 1.0))
         (canopy (clamp (swizzle stand :x) 0.0 1.0))
         ;; Fallen leaves smother some of the sward beneath a turned grove.
         (smothered (- 1.0 (* 0.45 (clamp (swizzle stand :y) 0.0 1.0))))
         (clump (+ (* 0.55 (value-noise (* xz 0.085)))
                   (* 0.45 (value-noise (+ (* xz 0.021) (vec2 17.3 4.1))))))
         (light (max 0.07 (expt (- 1.0 canopy) 3.2)))
         (damp (+ 0.75 (* 0.25 (smoothstep 0.02 0.48 moisture))))
         (standable (smoothstep 0.52 0.78 up))
         (cleared (* (- 1.0 (smoothstep 0.80 0.86 (swizzle floor :z)))
                     (- 1.0 (clamp (* (swizzle floor :w) 1.6) 0.0 1.0))))
         (variation (+ 0.88 (* 0.24 (smoothstep 0.18 0.72 clump))))
         (snow (* (smoothstep 0.55 0.68 rise)
                  (smoothstep 0.58 0.78 (swizzle floor :y))))
         (alpine (- 1.0 (smoothstep 0.50 0.67 rise)))
         (rooted (* smothered light damp standable cleared variation alpine
                    (- 1.0 snow)))
         (tint (* (vec3 0.185 0.315 0.112)
                  (vec3 (- 1.12 (* 0.24 moisture)) (+ 0.84 (* 0.30 moisture))
                        (+ 0.82 (* 0.22 moisture))))))
    (make-grass-medium
     :leaf-area (clamp (* rooted density) 0.0 1.0)
     :moisture moisture
     :forest-cover canopy
     :riparian 0.0
     :clump clump
     :canopy-height (* 0.42 (+ 0.72 (* 0.22 moisture)) (+ 0.88 (* 0.12 clump))
                       (mix 0.36 1.0 (smoothstep 0.06 0.46
                                                 (clamp (* rooted density)
                                                        0.0 1.0))))
     ;; Under a turned grove the grass is drying toward straw too.
     :blade-tint (mix (mix tint (* (srgb (heath-tint xz moisture)) 0.75)
                           (heath-amount xz rise))
                      (srgb (vec3 0.50 0.46 0.24))
                      (* 0.6 (clamp (swizzle stand :y) 0.0 1.0))))))

;;; Meadow flowers arrive in single-species colonies, never as confetti:
;;; an 11-metre lattice, warped so no border runs straight, decides where a
;;; drift lies and which species it is, and a finer noise shapes its edge.
;;; Petal tints are display-space.
(define-shader-struct flower-drift
  (presence :float) (tint :vec3) (head :float) (stem :float))

(define-shader-function read-flower-drift (xz moisture forest-cover leaf-area)
  (let* ((wander (value-noise (* xz 0.117)))
         (warped (+ xz (* (vec2 7.9 -6.1) (- wander 0.5))))
         (id (cell-identity (floor (/ warped 11.0))))
         (choice (plant-hash id (uint 29)))
         (rich (plant-hash id (uint 31)))
         (field (value-noise (* warped 0.22)))
         (colony (smoothstep (mix 0.70 0.36 rich) (mix 0.85 0.54 rich) field))
         (open-sky (- 1.0 (smoothstep 0.10 0.45 forest-cover)))
         (damp-band (* (smoothstep 0.06 0.20 moisture)
                       (- 1.0 (smoothstep 0.82 0.99 moisture))))
         (sward (smoothstep 0.12 0.40 leaf-area)))
    (make-flower-drift
     :presence (* colony open-sky damp-band sward)
     ;; Oxeye daisy, buttercup, harebell, red campion.
     :tint (if (< choice 0.30) (vec3 0.93 0.93 0.86)
               (if (< choice 0.56) (vec3 0.97 0.78 0.14)
                   (if (< choice 0.80) (vec3 0.44 0.46 0.88)
                       (vec3 0.88 0.46 0.62))))
     :head (if (< choice 0.30) 0.026
               (if (< choice 0.56) 0.016 (if (< choice 0.80) 0.019 0.020)))
     :stem (if (< choice 0.30) 1.15
               (if (< choice 0.56) 0.95 (if (< choice 0.80) 1.02 1.08))))))

;;; The chromaticity a drift keeps once its heads are too small to resolve.
(define-shader-function flower-wash (tint)
  (let* ((luma (dot tint (vec3 0.299 0.587 0.114))))
    (mix (vec3 luma luma luma) tint 0.55)))

(define-shader-function feature-pixels (metres focal distance)
  (/ (* metres focal) (max distance 0.5)))

(define-shader-function flower-resolved (pixels)
  (smoothstep 0.45 1.5 pixels))

(define-shader-function blade-pixels (focal distance)
  (/ (* 0.018 focal) (max distance 0.5)))

(define-shader-function blade-resolved (pixels)
  (smoothstep 0.16 0.95 pixels))

;;; Each world tile owns one phase for thinning its ordered shoots.
(define-shader-function lod-phase (cell)
  (plant-hash cell (uint #x51a7)))

(define-shader-function lod-shoots (wanted cell)
  (min (uint (* -1.0 (floor (* -1.0 (max (+ (- wanted (lod-phase cell)) 0.52)
                                         0.0)))))
       (uint 32)))

(define-shader-function lod-presence (wanted shoot cell)
  (let* ((threshold (+ (float shoot) (lod-phase cell))))
    (* (smoothstep (- threshold 0.52) (+ threshold 0.52) wanted)
       (smoothstep 0.0 0.52 wanted))))

(define-shader grass-tiles-compute
    (:stage :compute
     :workgroup-size (64 1 1)
     :inputs ((invocation :uvec3 :built-in :global-invocation-id))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (tiles :storage-buffer :binding 2 :element :uint
                        :access :read-write)
                 (arguments :storage-buffer :binding 3 :element :uint
                            :access :read-write)
                 (heights :texture-2d :binding 0)
                 (normals :texture-2d :binding 1)
                 (landscape :texture-2d :binding 6)
                 (ground :texture-2d :binding 7)
                 (closure :texture-2d :binding 9)
                 (litter :texture-2d :binding 10)
                 (linear-repeat :sampler :binding 1)))
  (let* ((index (swizzle invocation :x))
         (side (uint (swizzle window :z))))
    (when (< index (* side side))
      (let* ((tile (swizzle window :w))
             (cell (+ (swizzle window :xy)
                      (vec2 (float (mod index side)) (float (/ index side)))))
             (centre (* (+ cell (vec2 0.5 0.5)) tile))
             (eye (swizzle camera-position :xyz))
             (across (- centre (swizzle eye :xz)))
             (horizontal (sqrt (dot across across)))
             (reach (swizzle habitat :x))
             (height (sample-ground heights centre lattice))
             (clip (* view-proj (vec4 (swizzle centre :x) (+ height 0.5)
                                      (swizzle centre :y) 1.0)))
             (w (swizzle clip :w))
             (margin (+ (* 1.25 w) (* 2.0 tile)))
             (inside (and (< horizontal (+ reach (* 0.75 tile)))
                          (> w (* -1.0 tile))
                          (< (abs (swizzle clip :x)) margin)
                          (< (abs (swizzle clip :y)) margin)))
             (uv (field-uv centre lattice))
             (normal (unpack-normal
                      (swizzle (sample-level normals linear-repeat uv 0.0)
                               :xy)))
             (medium (read-grass-medium
                      centre (sample-level landscape linear-repeat uv 0.0)
                      (sample-level ground linear-repeat uv 0.0)
                      (swizzle normal :y)
                      (/ (- height (swizzle relief :x))
                         (max (swizzle relief :y) 1.0))
                      (swizzle habitat :y)
                      (read-stand closure litter linear-repeat centre
                                  (swizzle stand-field :x)
                                  (swizzle stand-field :yz)
                                  (swizzle (sample-level landscape
                                                         linear-repeat uv 0.0)
                                           :w))))
             (offset (- (vec3 (swizzle centre :x) height (swizzle centre :y))
                        eye))
             (distance (sqrt (dot offset offset)))
             (pixels (blade-pixels (swizzle habitat :w) distance))
             (fade (- 1.0 (smoothstep (* 0.84 reach) (* 0.99 reach)
                                      horizontal)))
             (leaf (grass-medium-leaf-area medium))
             ;; A drift tile stays alive for its heads after its blades
             ;; retire: each family prices itself by its own feature.
             (drift (read-flower-drift centre (grass-medium-moisture medium)
                                       (grass-medium-forest-cover medium)
                                       leaf))
             (heads (* leaf (flower-resolved
                             (feature-pixels (* 2.0 (flower-drift-head drift))
                                             (swizzle habitat :w) distance))
                       (smoothstep 0.02 0.10 (flower-drift-presence drift))))
             (wanted (* (max (* leaf (blade-resolved pixels)) heads)
                        fade 32.0))
             (identity (cell-identity cell))
             (shoots (lod-shoots wanted identity)))
        (when (and inside (> shoots (uint 0)))
          (let* ((slot (atomic-add arguments (uint 1) (uint 1)))
                 (at (* slot (uint 4))))
            (set-buffer-element tiles at (bit-cast :uint (swizzle cell :x)))
            (set-buffer-element tiles (+ at (uint 1))
                                (bit-cast :uint (swizzle cell :y)))
            (set-buffer-element tiles (+ at (uint 2)) (bit-cast :uint wanted))
            (set-buffer-element tiles (+ at (uint 3)) shoots)))))))

(define-shader-program grass-tiles
  :compute grass-tiles-compute)

;;; moppe_wind for a blade: the gust and bough at its root, the flick per
;;; section.
(define-shader-function blade-sway (spine root bend flutter time)
  (let* ((root-phase (+ (* (swizzle root :x) 0.043)
                        (* (swizzle root :z) 0.051)))
         (gust (+ (sin (+ (* time 1.13) root-phase))
                  (* 0.45 (sin (+ (* time 2.63) (* root-phase 1.7) 1.3)))))
         (bough (sin (+ (* time 3.90) (* root-phase 2.3) 0.7)))
         (phase (+ (* (swizzle spine :x) 0.043) (* (swizzle spine :z) 0.051)))
         (flick (sin (+ (* time 8.40) (* phase 13.0)
                        (* (swizzle spine :y) 1.9))))
         (driven (+ 0.55 (* 0.45 (abs gust))))
         (lean (* 0.26 bend))
         (shake (* 0.11 flutter driven)))
    (vec3 (+ (* 0.79 gust lean) (* (+ (* 0.62 bough) (* 0.44 flick)) shake))
          (- (* -0.15 (abs gust) lean) (* 0.10 (abs bough) shake))
          (+ (* 0.53 gust lean) (* (- (* 0.47 bough) (* 0.38 flick)) shake)))))

(define-shader grass-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world-position :vec3 :location 0)
               (blade-normal :vec3 :location 1)
               (albedo :vec3 :location 2)
               (exposure :float :location 3)
               (shade :float :location 4 :interpolation :flat)
               (here :vec4 :location 5)
               (then :vec4 :location 6)
               (petals :float :location 7)
               (blade :float :location 8 :interpolation :flat))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (tiles :storage-buffer :binding 2 :element :uint)
                 (heights :texture-2d :binding 0)
                 (normals :texture-2d :binding 1)
                 (landscape :texture-2d :binding 6)
                 (ground :texture-2d :binding 7)
                 (closure :texture-2d :binding 9)
                 (litter :texture-2d :binding 10)
                 (linear-repeat :sampler :binding 1)))
  (let* ((record (* instance-index (uint 4)))
         (cell (vec2 (bit-cast :float (buffer-element tiles record))
                     (bit-cast :float (buffer-element tiles
                                                      (+ record (uint 1))))))
         (wanted (bit-cast :float (buffer-element tiles (+ record (uint 2)))))
         (shoots (buffer-element tiles (+ record (uint 3))))
         (shoot (/ vertex-index (uint 8)))
         (corner (mod vertex-index (uint 8)))
         (section (float (/ corner (uint 2))))
         (right-edge (if (= (mod corner (uint 2)) (uint 1)) 1.0 -1.0))
         (tile (swizzle window :w))
         (id (cell-identity cell))
         (identity (uvec2 (+ (* (swizzle id :x) (uint 73856093)) shoot)
                          (+ (* (swizzle id :y) (uint 19349663))
                             (* shoot (uint 83492791)))))
         ;; Where the blade stands, jittered inside its own tile.
         (root-xz (+ (* cell tile)
                     (* (vec2 (+ 0.03 (* 0.94 (plant-hash identity (uint 1))))
                              (+ 0.03 (* 0.94 (plant-hash identity (uint 2)))))
                        tile)))
         (uv (field-uv root-xz lattice))
         (ground-normal (normalize
                         (unpack-normal
                          (swizzle (sample-level normals linear-repeat uv 0.0)
                                   :xy))))
         (height (sample-ground heights root-xz lattice))
         (medium (read-grass-medium
                  root-xz (sample-level landscape linear-repeat uv 0.0)
                  (sample-level ground linear-repeat uv 0.0)
                  (swizzle ground-normal :y)
                  (/ (- height (swizzle relief :x))
                     (max (swizzle relief :y) 1.0))
                  (swizzle habitat :y)
                  (read-stand closure litter linear-repeat root-xz
                              (swizzle stand-field :x)
                              (swizzle stand-field :yz)
                              (swizzle (sample-level landscape linear-repeat
                                                     uv 0.0)
                                       :w))))
         (wet (grass-medium-moisture medium))
         (canopy (grass-medium-forest-cover medium))
         (root (vec3 (swizzle root-xz :x) height (swizzle root-xz :y)))
         (eye (swizzle camera-position :xyz))
         (offset (- root eye))
         (distance (sqrt (dot offset offset)))
         (focal (swizzle habitat :w))
         (pixels (blade-pixels focal distance))
         (resolved (blade-resolved pixels))
         ;; Grass is the ordinary answer; a flower takes the shoot where a
         ;; drift claims the ground.
         (drift (read-flower-drift root-xz wet canopy
                                   (grass-medium-leaf-area medium)))
         (flower (< (plant-hash identity (uint 3))
                    (* 0.55 (flower-drift-presence drift))))
         (head-pixels (feature-pixels (* 2.0 (flower-drift-head drift))
                                      focal distance))
         (micro (smoothstep 1.5 4.0 pixels))
         (reach (swizzle habitat :x))
         (across-eye (- root-xz (swizzle eye :xz)))
         (fade (- 1.0 (smoothstep (* 0.84 reach) (* 0.99 reach)
                                  (sqrt (dot across-eye across-eye)))))
         ;; A blade straddles its threshold by growing into or out of the
         ;; ground, never by fading.
         (presence (lod-presence (* (grass-medium-leaf-area medium)
                                    (if flower (flower-resolved head-pixels)
                                        resolved)
                                    fade 32.0)
                                 shoot (cell-identity cell)))
         (draw (plant-hash identity (uint 4)))
         (scale (* (sqrt presence)
                   (+ 0.60 (* 0.35 wet) (* 0.08 (- 1.0 canopy)))
                   (+ 0.65 (* 0.65 draw draw))
                   (if flower 1.0
                       (- 1.0 (* 0.32 (smoothstep 0.40 0.95 canopy))))))
         (up (normalize (mix ground-normal (vec3 0.0 1.0 0.0)
                             (if flower 0.85 0.72))))
         (turn (+ (* 6.2831853 (plant-hash identity (uint 5)))
                  (* 0.55 (- (plant-hash identity (uint 6)) 0.5))))
         (sideways (normalize (+ (cross3 up (vec3 0.0 0.0 1.0))
                                 (vec3 0.001 0.0 0.0))))
         (along (normalize (cross3 sideways up)))
         (out (normalize (+ (* sideways (cos turn)) (* along (sin turn)))))
         (spread (+ 0.80 (* 0.45 (plant-hash identity (uint 11)))))
         (blade-tint (grass-medium-blade-tint medium))
         ;; A flower stem stands relative to the sward; once the sward has
         ;; retired it crouches with it, and it never goes thinner than
         ;; the head it carries can be seen.
         (blade-reach (* scale (if flower 0.07 (* 0.16 spread))))
         (climb (if flower
                    (* scale (flower-drift-stem drift)
                       (+ 0.80 (* 0.30 (plant-hash identity (uint 12))))
                       (mix 0.30 1.0 resolved))
                    (* scale 0.65 spread)))
         (stem-width (* scale 0.010))
         (width (if flower
                    (* stem-width
                       (clamp (/ 0.7 (max (feature-pixels (* 2.0 stem-width)
                                                          focal distance)
                                          0.05))
                              1.0 3.5))
                    (* scale 0.018)))
         (lift (if flower 1.0 1.22))
         (arch (if flower 0.0 0.20))
         ;; The head keeps its species colour while it spans pixels a
         ;; jittered sample can revisit; past that it widens, dims, and
         ;; collapses toward its drift's wash.
         (footprint (clamp (/ 1.35 (max head-pixels 0.05)) 1.0 2.4))
         (head (* (flower-drift-head drift)
                  (+ 0.80 (* 0.35 (plant-hash identity (uint 13))))
                  (smoothstep 0.05 0.50 presence) footprint))
         (head-tint (/ (* (mix (mix blade-tint
                                    (flower-wash (flower-drift-tint drift))
                                    0.85)
                               (flower-drift-tint drift)
                               (smoothstep 1.1 3.2 head-pixels))
                          (+ 0.92 (* 0.16 (plant-hash identity (uint 29)))))
                       footprint))
         ;; Blade-to-blade variation stays subordinate to the habitat, and
         ;; settles to the field's colour as the blade's pixels run out.
         (olive (- (plant-hash identity (uint 17)) 0.5))
         (own (* (if flower (* blade-tint 0.88) blade-tint)
                 (vec3 (+ 1.0 (* 0.18 olive)) 1.0 (- 1.0 (* 0.16 olive)))
                 (+ 0.96 (* 0.11 (plant-hash identity (uint 19))))))
         (ensemble (mix blade-tint
                        (* (flower-wash (flower-drift-tint drift)) 0.60)
                        (min 0.32 (* 0.32 (flower-drift-presence drift)))))
         (tint (if flower own (mix ensemble own resolved)))
         ;; The mover parts the field: upper sections lean away and lie
         ;; down toward the centre of its footprint.
         (from-mover (- root-xz (swizzle interaction :xz)))
         (mover-distance (sqrt (dot from-mover from-mover)))
         (radius (swizzle interaction :w))
         (response (if (> radius 0.0)
                       (- 1.0 (smoothstep (* 0.18 radius) radius
                                          mover-distance))
                       0.0))
         (away (/ from-mover (max mover-distance 0.08)))
         ;; The spine leaves the root steeply, then falls away to a point.
         ;; A flower runs its stem over the first two sections; the last
         ;; two are the lower and upper edges of its head.
         (in-head (and flower (>= section 2.0)))
         (t0 (if flower (min section 1.0) (/ section 3.0)))
         (rise (- (* lift t0) (* arch t0 t0)))
         (upper (* (smoothstep 0.0 0.58 t0) response))
         (stalk (- (+ root (* out (* blade-reach t0)) (* up (* climb rise))
                      (* (vec3 (swizzle away :x) 0.0 (swizzle away :y))
                         (* 0.42 upper)))
                   (* up (* climb rise 0.72 response))))
         ;; The head is a disc tilted between the stem's up and the camera,
         ;; each rolled to its own angle so a drift is not a fall of
         ;; identical confetti.
         (to-camera (normalize (- eye stalk)))
         (facing (normalize (+ up (* to-camera 1.25))))
         (roll (* 6.2831853 (plant-hash identity (uint 41))))
         (axis-r (normalize (cross3 facing out)))
         (axis-u (cross3 axis-r facing))
         (disc-r (+ (* axis-r (cos roll)) (* axis-u (sin roll))))
         (disc-u (- (* axis-u (cos roll)) (* axis-r (sin roll))))
         (lower-edge (= section 2.0))
         (spine (if in-head
                    (+ stalk (* facing 0.012) (* up (* 0.30 head))
                       (* disc-u (* head (if lower-edge -0.62 0.66))))
                    stalk))
         (half-width (if in-head (* head (if lower-edge 0.98 0.80))
                         (if (and (not flower) (>= t0 0.999)) 0.0
                             (* width (+ 0.72 (* 0.28 (sin (* 3.1415927
                                                             t0))))))))
         (side (normalize (cross3 out up)))
         (face (if in-head facing
                   (normalize (+ up (* out (* 0.55 rise))
                                 (* side (* 0.12
                                            (sin (+ (* 6.2831853
                                                       (plant-hash identity
                                                                   (uint 23)))
                                                    (* 2.2 t0)))))))))
         (edge (if in-head disc-r (normalize (cross3 face out))))
         (petal (if in-head 1.0 0.0))
         (bend (+ 0.18 (* 0.50 t0)))
         (flutter (* (+ 0.06 (* 0.18 t0)) micro (- 1.0 petal)))
         (rest (+ spine (* edge (* half-width right-edge))))
         (current (+ rest (blade-sway spine root bend flutter
                                      (swizzle camera-position :w))))
         (previous (+ rest (blade-sway spine root bend flutter
                                       (swizzle relief :z))))
         ;; Exposure stands in for how much sky this part of the blade
         ;; sees; far blades settle to the field's mean.
         (ramp (+ 0.12 (* 0.88 t0)))
         (light (if in-head 1.0
                    (mix 0.72 ramp (if flower 1.0
                                       (smoothstep 0.35 0.95 pixels)))))
         (section-tint (if in-head (* head-tint (if lower-edge 0.84 1.05))
                           tint))
         (alive (and (< shoot shoots) (> presence 0.001)))
         (here-clip (* view-proj (vec4 current 1.0))))
    (set-output clip-position
                (if alive (clip here-clip (swizzle temporal :zw))
                    (vec4 0.0 0.0 2.0 1.0)))
    (set-output world-position current)
    (set-output blade-normal face)
    (set-output albedo (srgb (* section-tint (+ 0.58 (* 0.66 light)))))
    (set-output exposure light)
    (set-output shade canopy)
    (set-output petals petal)
    (set-output blade (if flower 0.0 1.0))
    (set-output here here-clip)
    (set-output then (* previous-view-proj (vec4 previous 1.0)))))

(define-shader grass-fragment
    (:stage :fragment
     :inputs ((world-position :vec3 :location 0)
              (blade-normal :vec3 :location 1)
              (albedo :vec3 :location 2)
              (exposure :float :location 3)
              (shade :float :location 4 :interpolation :flat)
              (here :vec4 :location 5)
              (then :vec4 :location 6)
              (petals :float :location 7)
              (blade :float :location 8 :interpolation :flat))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (shadow-map :depth-texture-2d :binding 8)
                 (shadow-compare :sampler :binding 3)))
  (let* ((eye (swizzle camera-position :xyz))
         (to-fragment (- world-position eye))
         (distance (sqrt (dot to-fragment to-fragment)))
         (view (/ to-fragment (max distance 0.0001)))
         ;; A leaf has no back: it faces whoever looks at it.
         (n0 (normalize blade-normal))
         (n (if (> (dot n0 view) 0.0) (* -1.0 n0) n0))
         (l (swizzle sun-direction :xyz))
         (pixels (blade-pixels (swizzle habitat :w) distance))
         ;; Only a grass blade settles to the ensemble; a flower's parts
         ;; keep their own shading.
         (resolved (mix 1.0 (blade-resolved pixels) blade))
         (resolvable (smoothstep 1.5 4.0 pixels))
         (petal (step 0.5 petals))
         ;; The sun's shadow, four taps.
         (at (sun-map-coordinate sun-view world-position))
         (texel (swizzle shadow :y))
         (bias 0.0015)
         (taps (* 0.25 (+ (shadow-tap shadow-map shadow-compare at -0.5 -0.5
                                      texel bias)
                          (shadow-tap shadow-map shadow-compare at 0.5 -0.5
                                      texel bias)
                          (shadow-tap shadow-map shadow-compare at -0.5 0.5
                                      texel bias)
                          (shadow-tap shadow-map shadow-compare at 0.5 0.5
                                      texel bias))))
         (fog (relief-haze (distance-fog distance (swizzle fog-color :w))
                           (swizzle world-position :y) (swizzle relief :x)
                           (swizzle relief :y)))
         (cast (mix 1.0 taps (* (swizzle shadow :x)
                                (clamp (* 2.5 (- 1.0 fog)) 0.0 1.0)
                                (inside-sun-map at))))
         ;; Once a blade is too thin to revisit, its twist stops choosing a
         ;; full-contrast lighting sample: the terms settle to the leaf
         ;; distribution's symmetric means.
         (lambert (mix (+ 0.30 (* 0.46 (abs (swizzle l :y))))
                       (clamp (/ (+ (dot n l) 0.10) 1.10) 0.0 1.0)
                       resolved))
         ;; One leaf thick: against the sun it glows, Beer-Lambert pinning
         ;; the glow to the thin lit fringe.
         (back (mix 0.30 (expt (max (dot (* -1.0 n) l) 0.0) 1.8) resolvable))
         (transmitted (mix (* (/ 0.14 3.2) cast)
                           (* back (exp (* -2.0 (- 1.0 exposure))) cast)
                           resolved))
         (toward (clamp (dot view l) 0.0 1.0))
         (ambient-light (swizzle ambient :xyz))
         (sky (hemisphere-light ambient-light n))
         (ensemble-sky (* 0.5 (+ sky (hemisphere-light ambient-light
                                                       (* -1.0 n)))))
         (sun-light (swizzle sun-diffuse :xyz))
         (fill (mix (vec3 0.80 0.92 1.14) (vec3 1.0 1.0 1.0) cast))
         (scatter (* sun-light (vec3 0.92 1.05 0.78)
                     (mix 0.12 0.20 (clamp (swizzle l :y) 0.0 1.0))
                     (clamp shade 0.0 1.0) (clamp (- 1.0 cast) 0.0 1.0)))
         (lit (* albedo (+ (* fill (mix ensemble-sky sky resolved))
                           (* sun-light (* lambert cast
                                           (- 1.0 (* 0.45 transmitted))))
                           scatter)))
         ;; Chlorophyll passes green-yellow; a backlit petal glows warm in
         ;; roughly its own colour, a little less fiercely.
         (glow (* (vec3 (sqrt (swizzle albedo :x)) (sqrt (swizzle albedo :y))
                        (sqrt (swizzle albedo :z)))
                  sun-light (mix (vec3 0.92 1.0 0.24) (vec3 1.0 0.90 0.74)
                                 petal)
                  (* transmitted (+ 0.20 (* 0.80 toward toward toward))
                     (mix 3.2 2.1 petal))))
         ;; A waxy blade glints along its axis (Kajiya-Kay), the streak
         ;; widening and steadying as the blade's pixels run out.
         (wobble (* 0.25 resolvable))
         (axis (normalize (vec3 (* wobble (swizzle n :x)) 1.0
                                (* wobble (swizzle n :z)))))
         (half (normalize (- l view)))
         (axial (dot axis half))
         (glint (* (expt (sqrt (clamp (- 1.0 (* axial axial)) 0.0 1.0))
                         (mix 10.0 64.0 resolvable))
                   (+ 0.10 (* 0.90 toward toward))))
         (sheen (* (swizzle sun-specular :xyz)
                   (* glint cast (smoothstep 0.40 2.0 pixels)
                      (mix 0.40 1.0 resolvable)
                      (+ 0.30 (* 0.70 exposure)) 2.4
                      (- 1.0 (* 0.78 petal))))))
    (set-output color (vec4 (hazed (+ lit glow sheen) world-position eye
                                   fog-color l relief)
                            1.0))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program grass
  :vertex grass-vertex
  :fragment grass-fragment)

;;; -- the sward canopy -------------------------------------------------------
;;;
;;; As undergrowth.metal's mesoscale canopy: one sampled surface over the
;;; grass medium, not a coarser population of plants, rising where single
;;; blades cease to be repeatable and sinking back once the whole sward is
;;; subpixel.  SWARD-PATCHES keeps the 16-metre patches in view; each is an
;;; instance of a 4x4 grid lifted to the sward's height, whose fragments
;;; march a short ray down through the canopy volume and integrate its
;;; extinction front to back.  The terrain stays untouched below.  The
;;; grass block's habitat z is the ground texture scale, and its interaction
;;; lanes carry the projection's x and y scale for the patch cull.

(define-shader-function grass-gust (xz time)
  (let* ((phase (+ (* (swizzle xz :x) 0.043) (* (swizzle xz :y) 0.051))))
    (+ (sin (+ (* time 1.13) phase))
       (* 0.45 (sin (+ (* time 2.63) (* phase 1.7) 1.3))))))

(define-shader-function ensemble-axis (xz ground-normal time)
  (normalize (+ (mix (normalize ground-normal) (vec3 0.0 1.0 0.0) 0.72)
                (* (vec3 0.79 0.0 0.53) (* 0.14 (grass-gust xz time))))))

;;; The middle rung: what single blades no longer carry, while the sward's
;;; whole height still spans a pixel.
(define-shader-function canopy-fraction (pixels focal distance)
  (* (- 1.0 (blade-resolved pixels))
     (smoothstep 0.28 1.15 (feature-pixels 0.42 focal distance))))

(define-shader sward-patches-compute
    (:stage :compute
     :workgroup-size (64 1 1)
     :inputs ((invocation :uvec3 :built-in :global-invocation-id))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (tiles :storage-buffer :binding 2 :element :uint
                        :access :read-write)
                 (arguments :storage-buffer :binding 3 :element :uint
                            :access :read-write)
                 (heights :texture-2d :binding 0)))
  (let* ((index (swizzle invocation :x))
         (side (uint (swizzle window :z))))
    (when (< index (* side side))
      (let* ((patch (swizzle window :w))
             (cell (+ (swizzle window :xy)
                      (vec2 (float (mod index side)) (float (/ index side)))))
             (centre (* (+ cell (vec2 0.5 0.5)) patch))
             (radius (+ (* 0.72 patch) 0.6))
             (across (- centre (swizzle camera-position :xz)))
             (height (sample-ground heights centre lattice))
             (clip (* view-proj (vec4 (swizzle centre :x) (+ height 0.3)
                                      (swizzle centre :y) 1.0)))
             (w (swizzle clip :w))
             (visible (and (< (sqrt (dot across across))
                              (+ (swizzle habitat :x) radius))
                           (> w (* -1.0 radius))
                           (< (abs (swizzle clip :x))
                              (+ w (* radius (swizzle interaction :x))))
                           (< (abs (swizzle clip :y))
                              (+ w (* radius (swizzle interaction :y)))))))
        (when visible
          (let* ((slot (atomic-add arguments (uint 1) (uint 1)))
                 (at (* slot (uint 2))))
            (set-buffer-element tiles at (bit-cast :uint (swizzle cell :x)))
            (set-buffer-element tiles (+ at (uint 1))
                                (bit-cast :uint (swizzle cell :y)))))))))

(define-shader-program sward-patches
  :compute sward-patches-compute)

(define-shader sward-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world-position :vec3 :location 0)
               (ground-position :vec3 :location 1)
               (ground-normal :vec3 :location 2)
               (sway-axis :vec3 :location 3)
               (tint :vec3 :location 4)
               (field-xz :vec2 :location 5)
               (medium :vec4 :location 6)
               (density-fraction :float :location 7)
               (here :vec4 :location 8)
               (then :vec4 :location 9))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (tiles :storage-buffer :binding 2 :element :uint)
                 (heights :texture-2d :binding 0)
                 (normals :texture-2d :binding 1)
                 (landscape :texture-2d :binding 6)
                 (ground :texture-2d :binding 7)
                 (closure :texture-2d :binding 9)
                 (litter :texture-2d :binding 10)
                 (linear-repeat :sampler :binding 1)))
  (let* ((at (* instance-index (uint 2)))
         (cell (vec2 (bit-cast :float (buffer-element tiles at))
                     (bit-cast :float (buffer-element tiles
                                                      (+ at (uint 1))))))
         (corner (vec2 (float (mod vertex-index (uint 5)))
                       (float (/ vertex-index (uint 5)))))
         (xz (* (+ cell (/ corner 4.0)) (swizzle window :w)))
         (uv (field-uv xz lattice))
         (normal (normalize
                  (unpack-normal
                   (swizzle (sample-level normals linear-repeat uv 0.0) :xy))))
         (height (sample-ground heights xz lattice))
         (grass-here (read-grass-medium
                      xz (sample-level landscape linear-repeat uv 0.0)
                      (sample-level ground linear-repeat uv 0.0)
                      (swizzle normal :y)
                      (/ (- height (swizzle relief :x))
                         (max (swizzle relief :y) 1.0))
                      (swizzle habitat :y)
                      (read-stand closure litter linear-repeat xz
                                  (swizzle stand-field :x)
                                  (swizzle stand-field :yz)
                                  (swizzle (sample-level landscape
                                                         linear-repeat uv 0.0)
                                           :w))))
         (drift (read-flower-drift xz (grass-medium-moisture grass-here)
                                   (grass-medium-forest-cover grass-here)
                                   (grass-medium-leaf-area grass-here)))
         (eye (swizzle camera-position :xyz))
         (ground-point (vec3 (swizzle xz :x) height (swizzle xz :y)))
         (offset (- ground-point eye))
         (distance (sqrt (dot offset offset)))
         (focal (swizzle habitat :w))
         (across (- xz (swizzle eye :xz)))
         (reach (swizzle habitat :x))
         (weight (* (canopy-fraction (blade-pixels focal distance) focal
                                     distance)
                    (- 1.0 (smoothstep (* 0.90 reach) (* 0.995 reach)
                                       (sqrt (dot across across))))))
         (lift (grass-medium-canopy-height grass-here))
         (axis (ensemble-axis xz normal (swizzle camera-position :w)))
         (previous-axis (ensemble-axis xz normal (swizzle relief :z)))
         (current (+ ground-point (vec3 (* (swizzle axis :x) lift 0.24) lift
                                        (* (swizzle axis :z) lift 0.24))))
         (previous (+ ground-point
                      (vec3 (* (swizzle previous-axis :x) lift 0.24) lift
                            (* (swizzle previous-axis :z) lift 0.24))))
         (here-clip (* view-proj (vec4 current 1.0))))
    (set-output clip-position (clip here-clip (swizzle temporal :zw)))
    (set-output world-position current)
    (set-output ground-position ground-point)
    (set-output ground-normal normal)
    (set-output sway-axis axis)
    (set-output tint (mix (grass-medium-blade-tint grass-here)
                          (* (flower-wash (flower-drift-tint drift)) 0.60)
                          (min 0.32 (* 0.32 (flower-drift-presence drift)))))
    (set-output field-xz xz)
    (set-output medium (vec4 (grass-medium-leaf-area grass-here)
                             (grass-medium-clump grass-here)
                             (grass-medium-forest-cover grass-here) lift))
    (set-output density-fraction weight)
    (set-output here here-clip)
    (set-output then (* previous-view-proj (vec4 previous 1.0)))))

;;; Value noise with its gradient, quintic-smoothed.
(define-shader-function value-noise-gradient (p)
  (let* ((i (floor p))
         (f (fract p))
         (u (* f f f (+ (* f (- (* f 6.0) (vec2 15.0 15.0)))
                        (vec2 10.0 10.0))))
         (du (* f f (+ (* f (- f (vec2 2.0 2.0))) (vec2 1.0 1.0)) 30.0))
         (a (hash12 i))
         (b (hash12 (+ i (vec2 1.0 0.0))))
         (c (hash12 (+ i (vec2 0.0 1.0))))
         (d (hash12 (+ i (vec2 1.0 1.0))))
         (k1 (- b a))
         (k2 (- c a))
         (k3 (+ (- a b c) d)))
    (vec3 (+ a (* k1 (swizzle u :x)) (* k2 (swizzle u :y))
             (* k3 (swizzle u :x) (swizzle u :y)))
          (* (swizzle du :x) (+ k1 (* k3 (swizzle u :y))))
          (* (swizzle du :y) (+ k2 (* k3 (swizzle u :x)))))))

;;; The mean projected leaf area along a ray through a clump, its grazing
;;; path capped by the clump's finite width.
(define-shader-function sward-path (axis ray clump)
  (let* ((mu (abs (dot (normalize axis) (normalize ray))))
         (area (mix 0.22 0.68 (sqrt (clamp (- 1.0 (* mu mu)) 0.0 1.0))))
         (limited (/ area (mix 2.55 3.15 (clamp clump 0.0 1.0)))))
    (/ area (sqrt (+ (* mu mu) (* limited limited))))))

(define-shader-function sward-extinction (leaf clump)
  (* 3.65 (clamp leaf 0.0 1.0) (mix 0.88 1.12 (clamp clump 0.0 1.0))))

(define-shader-function mean-transmission (depth)
  (if (< depth 0.001) 1.0 (/ (- 1.0 (exp (* -1.0 depth))) depth)))

;;; The light a symmetric, two-sided leaf-normal distribution returns: four
;;; lobes of one tilt around the axis, weighted by how squarely each faces
;;; the viewer, never a sampled normal.
(define-shader-function sward-radiance (tint axis sun view sun-light
                                        sun-shine ambient cast visibility
                                        sun-mean)
  (let* ((base (srgb (* tint (+ 0.58 (* 0.66 0.72)))))
         (up (normalize axis))
         (helper (if (< (abs (swizzle up :y)) 0.92) (vec3 0.0 1.0 0.0)
                     (vec3 1.0 0.0 0.0)))
         (tangent (normalize (cross3 helper up)))
         (bitangent (normalize (cross3 up tangent)))
         (up-view (* 0.872506 (dot up view)))
         (t-view (* 0.488603 (dot tangent view)))
         (b-view (* 0.488603 (dot bitangent view)))
         (view-weight (+ (vec4 0.12 0.12 0.12 0.12)
                         (abs (vec4 (+ up-view t-view) (+ up-view b-view)
                                    (- up-view t-view) (- up-view b-view)))))
         (up-sun (* 0.872506 (dot up sun)))
         (t-sun (* 0.488603 (dot tangent sun)))
         (b-sun (* 0.488603 (dot bitangent sun)))
         (facing (abs (vec4 (+ up-sun t-sun) (+ up-sun b-sun)
                            (- up-sun t-sun) (- up-sun b-sun))))
         (mean-sun (/ (dot view-weight facing)
                      (max (dot view-weight (vec4 1.0 1.0 1.0 1.0)) 0.001)))
         (sky (* 0.5 (+ (hemisphere-light ambient up)
                        (hemisphere-light ambient (* -1.0 up)))))
         (toward (clamp (dot view sun) 0.0 1.0))
         (reflected (* base sun-light (* mean-sun sun-mean)))
         (transmitted (* (vec3 (sqrt (swizzle base :x)) (sqrt (swizzle base :y))
                               (sqrt (swizzle base :z)))
                         sun-light (vec3 0.92 1.0 0.24)
                         (* 0.14 sun-mean
                            (+ 0.20 (* 0.80 toward toward toward)))))
         (half (normalize (- sun view)))
         (along (dot up half))
         (axial (sqrt (clamp (- 1.0 (* along along)) 0.0 1.0)))
         (sheen (* (expt axial 10.0) (+ 0.12 (* 0.88 toward toward))))
         (fill (mix (vec3 0.80 0.92 1.14) (vec3 1.0 1.0 1.0) cast)))
    (+ (* fill (* base sky))
       (* (+ reflected transmitted
             (* sun-shine (* 0.055 sheen sun-mean)))
          visibility))))

;;; One sample of the canopy column: density (denser toward the basal mat,
;;; with an upper-leaf shoulder), its lateral gradient, and its height.
(define-shader-function sward-sample (start view along ground-point normal
                                      column axis lift clump)
  (let* ((point (+ start (* view along)))
         (h (clamp (/ (dot (- point ground-point) normal) column) 0.0 1.0))
         (rest (- (swizzle point :xz) (* (swizzle axis :xz) (* lift h 0.24))))
         (basal (+ 0.62 (* 0.88 (- 1.0 h))))
         (upper (* 0.34 (smoothstep 0.48 0.82 h)))
         (warp (vec2 (* 3.7 h) (* -2.9 h)))
         (fine (value-noise-gradient (+ (* rest 1.35) warp)))
         (broad (value-noise (- (+ (* rest 0.31) (vec2 11.3 7.1))
                                (* warp 0.42))))
         (density (max (* (+ basal upper) (mix 0.72 1.28 (swizzle fine :x))
                          (mix 0.86 1.14 broad) (mix 0.94 1.06 clump))
                       0.02))
         (gradient (* (swizzle fine :yz) (* 1.35 (+ basal upper) 0.56))))
    (vec4 density gradient h)))

;;; What one sample adds before extinction: the canopy's radiance, shaded
;;; by height and by its clump's own facing.
(define-shader-function sward-source (radiance sample axis sun)
  (let* ((density (swizzle sample :x))
         (clump-normal (normalize (- axis (* (vec3 (swizzle sample :y) 0.0
                                                   (swizzle sample :z))
                                             0.38))))
         (reference (+ 0.28 (* 0.72 (clamp (/ (+ (dot axis sun) 0.12) 1.12)
                                           0.0 1.0))))
         (local (+ 0.28 (* 0.72 (clamp (/ (+ (dot clump-normal sun) 0.12)
                                          1.12)
                                       0.0 1.0)))))
    (* radiance (mix 0.46 1.0 (swizzle sample :w))
       (mix 1.08 0.86 (clamp (/ density 2.2) 0.0 1.0))
       (clamp (/ local reference) 0.68 1.32))))

;;; The canopy's mottle shares the terrain's: an octave near pixel scale
;;; across the whole traversal range.
(define-shader-function sward-grain (xz footprint)
  (let* ((fine-visible (- 1.0 (smoothstep 30.0 110.0 footprint)))
         (mid-visible (* (smoothstep 40.0 120.0 footprint)
                         (- 1.0 (smoothstep 400.0 900.0 footprint))))
         (broad-visible (smoothstep 250.0 700.0 footprint)))
    (* (+ 1.0 (* 0.44 (- (value-noise (+ (* xz 0.16) (vec2 31.7 8.3))) 0.5)))
       (+ 1.0 (* 0.36 (- (value-noise (* xz 1.9)) 0.5) fine-visible))
       (+ 1.0 (* 0.40 (- (value-noise (+ (* xz 0.31) (vec2 7.1 43.9))) 0.5)
             mid-visible))
       (+ 1.0 (* 0.32 (- (value-noise (+ (* xz 0.037) (vec2 11.3 71.7))) 0.5)
             broad-visible)))))

(define-shader sward-fragment
    (:stage :fragment
     :inputs ((world-position :vec3 :location 0)
              (ground-position :vec3 :location 1)
              (ground-normal :vec3 :location 2)
              (sway-axis :vec3 :location 3)
              (tint :vec3 :location 4)
              (field-xz :vec2 :location 5)
              (medium :vec4 :location 6)
              (density-fraction :float :location 7)
              (here :vec4 :location 8)
              (then :vec4 :location 9))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (grass :uniform-block :binding 1 :members #.*grass*)
                 (grass-texture :texture-2d :binding 2)
                 (shadow-map :depth-texture-2d :binding 8)
                 (linear-repeat :sampler :binding 1)
                 (shadow-compare :sampler :binding 3)))
  (let* ((leaf (swizzle medium :x))
         (clump (swizzle medium :y))
         (forest (swizzle medium :z))
         (lift (swizzle medium :w))
         (eye (swizzle camera-position :xyz))
         (to-fragment (- world-position eye))
         (distance (sqrt (dot to-fragment to-fragment)))
         (view (/ to-fragment (max distance 0.0001)))
         (l (swizzle sun-direction :xyz))
         (axis (normalize sway-axis))
         (fog (relief-haze (distance-fog distance (swizzle fog-color :w))
                           (swizzle world-position :y) (swizzle relief :x)
                           (swizzle relief :y)))
         (at (sun-map-coordinate sun-view world-position))
         (texel (swizzle shadow :y))
         (taps (* 0.25 (+ (shadow-tap shadow-map shadow-compare at -0.5 -0.5
                                      texel 0.0015)
                          (shadow-tap shadow-map shadow-compare at 0.5 -0.5
                                      texel 0.0015)
                          (shadow-tap shadow-map shadow-compare at -0.5 0.5
                                      texel 0.0015)
                          (shadow-tap shadow-map shadow-compare at 0.5 0.5
                                      texel 0.0015))))
         (cast (mix 1.0 taps (* (swizzle shadow :x)
                                (clamp (* 2.5 (- 1.0 fog)) 0.0 1.0)
                                (inside-sun-map at))))
         (visibility (* cast (mix 1.0 0.68 forest)))
         (sun-light (swizzle sun-diffuse :xyz))
         (scatter (* sun-light (vec3 0.92 1.05 0.78)
                     (mix 0.12 0.20 (clamp (swizzle l :y) 0.0 1.0))
                     (clamp forest 0.0 1.0) (clamp (- 1.0 cast) 0.0 1.0)))
         ;; Beer-Lambert coverage of the upper leaves this rung owns.
         (fraction (clamp density-fraction 0.0 1.0))
         (extinction (sward-extinction leaf clump))
         (depth (* extinction fraction (sward-path axis view clump)))
         (coverage-limit (- 1.0 (exp (* -1.0 depth))))
         (sun-mean (mix 0.92 1.0 (mean-transmission
                                  (* extinction (sward-path axis l clump)))))
         (radiance (sward-radiance tint axis l view sun-light
                                   (swizzle sun-specular :xyz)
                                   (+ (* (swizzle ambient :xyz)
                                         (mix 1.0 0.82 forest))
                                      scatter)
                                   cast visibility sun-mean))
         ;; March from the envelope toward the ground plane, no further
         ;; than the medium's lateral correlation length.
         (n (normalize ground-normal))
         (column (max (dot (- world-position ground-position) n) 0.02))
         (path (min (/ column (max (* -1.0 (dot view n)) 0.025))
                    (mix 2.2 3.1 (clamp clump 0.0 1.0))))
         (total (* depth (mix 0.82 1.18 (value-noise (+ (* field-xz 0.57)
                                                         (vec2 19.7 3.1))))))
         (s0 (sward-sample world-position view (* path 0.125) ground-position
                           n column axis lift clump))
         (s1 (sward-sample world-position view (* path 0.375) ground-position
                           n column axis lift clump))
         (s2 (sward-sample world-position view (* path 0.625) ground-position
                           n column axis lift clump))
         (s3 (sward-sample world-position view (* path 0.875) ground-position
                           n column axis lift clump))
         (sum (+ (swizzle s0 :x) (swizzle s1 :x) (swizzle s2 :x)
                 (swizzle s3 :x)))
         (c0 (- 1.0 (exp (* -1.0 (/ (* total (swizzle s0 :x)) sum)))))
         (c1 (- 1.0 (exp (* -1.0 (/ (* total (swizzle s1 :x)) sum)))))
         (c2 (- 1.0 (exp (* -1.0 (/ (* total (swizzle s2 :x)) sum)))))
         (c3 (- 1.0 (exp (* -1.0 (/ (* total (swizzle s3 :x)) sum)))))
         (r1 (- 1.0 c0))
         (r2 (* r1 (- 1.0 c1)))
         (r3 (* r2 (- 1.0 c2)))
         (accumulated (+ (* (sward-source radiance s0 axis l) c0)
                         (* (sward-source radiance s1 axis l) (* r1 c1))
                         (* (sward-source radiance s2 axis l) (* r2 c2))
                         (* (sward-source radiance s3 axis l) (* r3 c3))))
         (coverage (- 1.0 (* r3 (- 1.0 c3))))
         ;; The ground texture's own grain, near and far, as detail.
         (uv (* field-xz (swizzle habitat :z)))
         (detail (mix (swizzle (sample grass-texture linear-repeat uv) :rgb)
                      (swizzle (sample grass-texture linear-repeat
                                       (+ (* uv 0.19) (vec2 0.13 0.71)))
                               :rgb)
                      (smoothstep 40.0 350.0 distance)))
         (texture-gain (clamp (/ (dot detail (vec3 0.299 0.587 0.114)) 0.0902)
                              0.62 1.55))
         (shaded (* (/ accumulated (max coverage 0.001)) texture-gain
                    (sward-grain field-xz distance))))
    (when (or (< lift 0.006) (< density-fraction 0.001) (< leaf 0.001)
              (< coverage-limit 0.002))
      (discard))
    (set-output color (vec4 (hazed shaded world-position eye fog-color l
                                   relief)
                            coverage))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program sward
  :vertex sward-vertex
  :fragment sward-fragment)

;;; -- boulders ---------------------------------------------------------------
;;;
;;; As boulders.metal: faceted, flat-shaded lumps of rock, each an
;;; icosahedron shaped from its seed -- turned, jittered, and cleaved by a
;;; few planes so broad flat faces break its roundness -- squashed broader
;;; than tall and settled into the ground.  BOULDER-CULL sorts the visible
;;; ones into the coarse (12 corners) and fine (every face split in four)
;;; classes; the shadow view takes them all coarse.  A record is three vec4
;;; rows: centre and radius; ground normal and moisture; the seed's bits.
;;; The icosahedron's corners and its faces' corner triples arrive as small
;;; storage buffers.

(define-shader boulder-cull-compute
    (:stage :compute
     :workgroup-size (64 1 1)
     :inputs ((invocation :uvec3 :built-in :global-invocation-id))
     :resources ((forest :uniform-block :binding 1 :members #.*forest*)
                 (rocks :storage-buffer :binding 2 :element :vec4)
                 (candidates :storage-buffer :binding 3 :element :uint
                             :access :read-write)
                 (arguments :storage-buffer :binding 4 :element :uint
                            :access :read-write)))
  (let* ((index (swizzle invocation :x)))
    (when (< index (uint (swizzle world :z)))
      (let* ((rock (buffer-element rocks (* index (uint 3))))
             (radius (swizzle rock :w))
             (centre-of (swizzle eye :xyz))
             (centre (tree-root (swizzle rock :xyz) (swizzle world :xy)
                                centre-of))
             (bound (* 1.45 radius))
             (clip (* view (vec4 centre 1.0)))
             (w (swizzle clip :w))
             (inside (and (> w (* -1.0 bound))
                          (< (abs (swizzle clip :x))
                             (+ w (* bound (swizzle cull :x))))
                          (< (abs (swizzle clip :y))
                             (+ w (* bound (swizzle cull :y))))))
             (offset (- centre centre-of))
             (pixels (/ (* radius 0.5 (swizzle cull :y) (swizzle cull :w))
                        (max (sqrt (dot offset offset)) 0.6)))
             (shadow (> (swizzle mode :x) 0.5)))
        (when (and inside (or shadow (>= pixels 0.75)))
          (let* ((class (if (and (not shadow) (> pixels 14.0)) (uint 1)
                            (uint 0)))
                 (slot (atomic-add arguments (+ (* class (uint 5)) (uint 1))
                                   (uint 1)))
                 (at (* (+ (* class (uint (swizzle world :w))) slot)
                        (uint 2))))
            (set-buffer-element candidates at index)
            (set-buffer-element candidates (+ at (uint 1))
                                (bit-cast :uint pixels))))))))

(define-shader-program boulder-cull
  :compute boulder-cull-compute)

(define-shader-struct boulder-shape
  (centre :vec3) (up :vec3) (right :vec3) (forward :vec3) (radius :float)
  (stretch :vec3) (turn-x :vec3) (turn-y :vec3) (turn-z :vec3)
  (cleave-0 :vec4) (cleave-1 :vec4) (cleave-2 :vec4) (cleave-3 :vec4)
  (seed :uint))

(define-shader-function rock-direction (seed lane)
  (let* ((z (- (* 2.0 (tree-hash seed lane)) 1.0))
         (a (* 6.2831853 (tree-hash seed (+ lane (uint 1)))))
         (s (sqrt (max (- 1.0 (* z z)) 0.0))))
    (vec3 (* s (cos a)) z (* s (sin a)))))

(define-shader-function cleave-plane (seed k)
  (let* ((n (rock-direction seed (+ (uint 10) (* (uint 2) k))))
         (flat (normalize (vec3 (swizzle n :x) (* (abs (swizzle n :y)) 0.6)
                                (swizzle n :z)))))
    (vec4 flat (+ 0.50 (* 0.22 (tree-hash seed (+ (uint 20) k)))))))

;;; The body: half-way between plumb and the ground normal, turned about its
;;; own axis, and split along a tilted crown and three flanks.
(define-shader-function shape-boulder (rock ground identity centre)
  (let* ((seed (bit-cast :uint (swizzle identity :x)))
         (up (normalize (+ (vec3 0.0 1.0 0.0) (swizzle ground :xyz))))
         (yaw (* 6.2831853 (tree-hash seed (uint 1))))
         (heading (vec3 (cos yaw) 0.0 (sin yaw)))
         (right (normalize (- heading (* up (dot heading up)))))
         (elongation (+ 0.85 (* 0.40 (tree-hash seed (uint 2)))))
         (axis (rock-direction seed (uint 4)))
         (angle (* 6.2831853 (tree-hash seed (uint 6))))
         (c (cos angle))
         (s (sin angle))
         (k (- 1.0 c))
         (x (swizzle axis :x))
         (y (swizzle axis :y))
         (z (swizzle axis :z))
         (crown (rock-direction seed (uint 8))))
    (make-boulder-shape
     :centre centre :up up :right right :forward (cross3 right up)
     :radius (swizzle rock :w)
     :stretch (vec3 elongation
                    (* 0.7 (+ 0.88 (* 0.24 (tree-hash seed (uint 3)))))
                    (/ 1.0 (sqrt elongation)))
     :turn-x (vec3 (+ (* k x x) c) (+ (* k x y) (* s z)) (- (* k x z) (* s y)))
     :turn-y (vec3 (- (* k x y) (* s z)) (+ (* k y y) c) (+ (* k y z) (* s x)))
     :turn-z (vec3 (+ (* k x z) (* s y)) (- (* k y z) (* s x)) (+ (* k z z) c))
     :cleave-0 (vec4 (normalize (+ (vec3 0.0 1.0 0.0) (* crown 0.45)))
                     (+ 0.55 (* 0.20 (tree-hash seed (uint 19)))))
     :cleave-1 (cleave-plane seed (uint 1))
     :cleave-2 (cleave-plane seed (uint 2))
     :cleave-3 (cleave-plane seed (uint 3))
     :seed seed)))

(define-shader-function cleave-by (p plane)
  (let* ((reach (- (dot p (swizzle plane :xyz)) (swizzle plane :w))))
    (if (> reach 0.0) (- p (* (swizzle plane :xyz) reach)) p)))

(define-shader-function boulder-cleave (b p)
  (cleave-by (cleave-by (cleave-by (cleave-by p (boulder-shape-cleave-0 b))
                                   (boulder-shape-cleave-1 b))
                        (boulder-shape-cleave-2 b))
             (boulder-shape-cleave-3 b)))

;;; A corner of the unit lump; below its girth it reaches deeper than it
;;; stands tall, so its underside stays buried on a slope.
(define-shader-function boulder-corner (b unit i)
  (let* ((turned (+ (* (boulder-shape-turn-x b) (swizzle unit :x))
                    (* (boulder-shape-turn-y b) (swizzle unit :y))
                    (* (boulder-shape-turn-z b) (swizzle unit :z))))
         (p (boulder-cleave b (* turned
                                 (+ 0.80 (* 0.36 (tree-hash
                                                  (boulder-shape-seed b)
                                                  (+ (uint 40) i))))))))
    (if (< (swizzle p :y) 0.0)
        (vec3 (swizzle p :x) (* (swizzle p :y) 1.6) (swizzle p :z))
        p)))

(define-shader-function boulder-world (b p)
  (let* ((local (* p (boulder-shape-stretch b) (boulder-shape-radius b))))
    (+ (boulder-shape-centre b) (* (boulder-shape-right b) (swizzle local :x))
       (* (boulder-shape-up b) (swizzle local :y))
       (* (boulder-shape-forward b) (swizzle local :z)))))

(define-shader-abstraction rock-corner (corners b i)
  `(boulder-corner ,b (swizzle (buffer-element ,corners ,i) :xyz) ,i))

(define-shader boulders-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world-position :vec3 :location 0)
               (albedo :vec3 :location 1 :interpolation :flat)
               (moisture :float :location 2 :interpolation :flat)
               (rise :float :location 3)
               (here :vec4 :location 4)
               (then :vec4 :location 5))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (forest :uniform-block :binding 1 :members #.*forest*)
                 (draw :uniform-block :binding 2 :members #.*forest-class*)
                 (rocks :storage-buffer :binding 3 :element :vec4)
                 (candidates :storage-buffer :binding 4 :element :uint)
                 (corners :storage-buffer :binding 5 :element :vec4)
                 (faces :storage-buffer :binding 6 :element :uint)))
  (let* ((at (* (+ (uint (swizzle class :x)) instance-index) (uint 2)))
         (row (* (buffer-element candidates at) (uint 3)))
         (rock (buffer-element rocks row))
         (ground (buffer-element rocks (+ row (uint 1))))
         (b (shape-boulder rock ground (buffer-element rocks (+ row (uint 2)))
                           (tree-root (swizzle rock :xyz) (swizzle world :xy)
                                      (swizzle camera-position :xyz))))
         ;; A coarse vertex is a corner; a fine face has its corners in
         ;; slots 0-2 and its edge midpoints, lifted a little and keyed by
         ;; their edge so neighbouring faces meet, in slots 3-5.
         (fine (> (swizzle class :y) 0.5))
         (face (/ vertex-index (uint 6)))
         (slot (mod vertex-index (uint 6)))
         (first (* face (uint 3)))
         (corner-a (buffer-element faces (+ first (mod slot (uint 3)))))
         (corner-b (buffer-element faces (+ first (mod (+ slot (uint 1))
                                                       (uint 3)))))
         (pa (rock-corner corners b corner-a))
         (pb (rock-corner corners b corner-b))
         (low (min corner-a corner-b))
         (high (max corner-a corner-b))
         (midpoint (* (+ pa pb) 0.5))
         (lift (- (* 0.12 (tree-hash (boulder-shape-seed b)
                                     (+ (uint 60) (* low (uint 12)) high)))
                  0.03))
         (fine-point (if (< slot (uint 3)) pa
                         (boulder-cleave b (+ midpoint
                                              (* (normalize midpoint) lift)))))
         (p (if fine fine-point (rock-corner corners b vertex-index)))
         (point (boulder-world b p))
         (seed (boulder-shape-seed b))
         (hue (- (tree-hash seed (uint 30)) 0.5))
         (value (+ 0.86 (* 0.24 (tree-hash seed (uint 31)))))
         (here-clip (* view-proj (vec4 point 1.0))))
    (set-output clip-position (clip here-clip (swizzle temporal :zw)))
    (set-output world-position point)
    (set-output albedo (srgb (* (vec3 (+ 0.56 (* 0.07 hue)) 0.54
                                      (- 0.51 (* 0.07 hue)))
                                value)))
    (set-output moisture (swizzle ground :w))
    (set-output rise (+ (* (swizzle p :y) (swizzle (boulder-shape-stretch b) :y))
                        0.4))
    (set-output here here-clip)
    (set-output then (* previous-view-proj (vec4 point 1.0)))))

(define-shader boulders-fragment
    (:stage :fragment
     :inputs ((world-position :vec3 :location 0)
              (albedo :vec3 :location 1 :interpolation :flat)
              (moisture :float :location 2 :interpolation :flat)
              (rise :float :location 3)
              (here :vec4 :location 4)
              (then :vec4 :location 5))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (shadow-map :depth-texture-2d :binding 8)
                 (shadow-compare :sampler :binding 3)))
  (let* ((eye (swizzle camera-position :xyz))
         (to-eye (- eye world-position))
         (view (/ to-eye (max (sqrt (dot to-eye to-eye)) 0.001)))
         (light (swizzle sun-direction :xyz))
         ;; One normal per facet, from the surface's own derivatives.
         (facet-normal (normalize (cross3 (derivative-x world-position)
                                          (derivative-y world-position))))
         (n (if (< (dot facet-normal view) 0.0) (* -1.0 facet-normal)
                facet-normal))
         ;; Each facet is its own shade of the stone; lichen crusts the dry
         ;; tops and moss the damp ones, in patches on the upward faces.
         (facet (hash12 (+ (floor (* (swizzle n :xz) 7.0))
                           (vec2 (floor (* (swizzle n :y) 5.0))
                                 (floor (* (swizzle n :y) 5.0))))))
         (patches (value-noise (+ (* (swizzle world-position :xz) 1.7)
                                  (vec2 (* (swizzle world-position :y) 0.9)
                                        (* (swizzle world-position :y) 0.9)))))
         (growth (* (smoothstep 0.55 0.90 (swizzle n :y))
                    (smoothstep 0.45 0.75 (- (+ patches (* 0.35 moisture))
                                             0.10))))
         (crust (mix (srgb (vec3 0.64 0.63 0.52)) (srgb (vec3 0.30 0.38 0.16))
                     (smoothstep 0.30 0.65 moisture)))
         ;; The soil darkens and the light fails where stone meets ground.
         (contact (smoothstep -0.05 0.40 rise))
         (surface (* (mix (* albedo (+ 0.88 (* 0.22 facet))) crust
                          (* 0.55 growth))
                     (mix 0.55 1.0 contact)))
         (at (sun-map-coordinate sun-view world-position))
         (texel (swizzle shadow :y))
         (margin (/ 0.5 1240.0))
         (lit (* 0.25 (+ (shadow-tap shadow-map shadow-compare at -0.5 -0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at 0.5 -0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at -0.5 0.5
                                     texel margin)
                         (shadow-tap shadow-map shadow-compare at 0.5 0.5
                                     texel margin))))
         (visibility (mix 1.0 (mix 0.18 1.0 lit)
                          (* (swizzle shadow :x) (inside-sun-map at))))
         (sun-light (swizzle sun-diffuse :xyz))
         ;; Sunlit ground throws warm light back into the shaded lower faces.
         (bounce (* sun-light (vec3 0.30 0.26 0.15)
                    (clamp (- 0.55 (* 0.45 (swizzle n :y))) 0.0 1.0)))
         (shaded (* surface
                    (+ (* sun-light (* (clamp (dot n light) 0.0 1.0)
                                       visibility 0.95))
                       (* (+ (hemisphere-light (swizzle ambient :xyz) n)
                             (* bounce 0.45))
                          (mix 0.55 0.92 contact))))))
    (set-output color (vec4 (hazed shaded world-position eye fog-color light
                                   relief)
                            1.0))
    (set-output motion (- (clip-uv then) (clip-uv here)))))

(define-shader-program boulders
  :vertex boulders-vertex
  :fragment boulders-fragment)

(define-shader boulders-shadow-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position))
     :resources ((caster :uniform-block :binding 0 :members #.*caster*)
                 (forest :uniform-block :binding 1 :members #.*forest*)
                 (draw :uniform-block :binding 2 :members #.*forest-class*)
                 (rocks :storage-buffer :binding 3 :element :vec4)
                 (candidates :storage-buffer :binding 4 :element :uint)
                 (corners :storage-buffer :binding 5 :element :vec4)))
  (let* ((at (* (+ (uint (swizzle class :x)) instance-index) (uint 2)))
         (row (* (buffer-element candidates at) (uint 3)))
         (rock (buffer-element rocks row))
         (b (shape-boulder rock (buffer-element rocks (+ row (uint 1)))
                           (buffer-element rocks (+ row (uint 2)))
                           (tree-root (swizzle rock :xyz) (swizzle world :xy)
                                      (swizzle focus :xyz)))))
    (set-output clip-position
                (clip (* light-view
                         (vec4 (boulder-world b (rock-corner corners b
                                                             vertex-index))
                               1.0))
                      (vec2 0.0 0.0)))))

(define-shader-program boulders-shadow
  :vertex boulders-shadow-vertex)

;;; -- drizzle ----------------------------------------------------------------
;;;
;;; A fine rain: streaks in a 44-metre box around the camera, each anchored
;;; to the world (so walking through the rain does not drag it along) and
;;; falling at seven metres a second with a little wind.  The camera-side
;;; streaks are kept faint and thin; the rain reads as a whole.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *rain*
    '((rain :vec4))))              ; amount, drops, 0, 0

(define-shader-function drop-position (seed eye time)
  (let* ((reach 22.0)
         (span 18.0)
         (anchor (vec2 (* (tree-hash seed (uint 1)) 2.0 reach)
                       (* (tree-hash seed (uint 2)) 2.0 reach)))
         (wrapped (- (fract (/ (- anchor (swizzle eye :xz)) (* 2.0 reach)))
                     (vec2 0.5 0.5)))
         (fall (fract (+ (/ (* time 7.0) span) (tree-hash seed (uint 3)))))
         (drift (* fall span 0.17)))
    (vec3 (+ (swizzle eye :x) (* (swizzle wrapped :x) 2.0 reach) drift)
          (- (+ (swizzle eye :y) (* 0.45 span)) (* fall span))
          (+ (swizzle eye :z) (* (swizzle wrapped :y) 2.0 reach)
             (* drift 0.4)))))

(define-shader rain-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (shade :vec4 :location 0)
               (here :vec4 :location 1)
               (then :vec4 :location 2))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (weather :uniform-block :binding 1 :members #.*rain*)))
  (let* ((seed (+ (* instance-index (uint 2654435761)) (uint 977)))
         (eye (swizzle camera-position :xyz))
         (time (swizzle camera-position :w))
         (head (drop-position seed eye time))
         (velocity (vec3 1.19 -7.0 0.48))
         (tail (- head (* velocity 0.055)))
         (corner (mod vertex-index (uint 6)))
         (at-tail (or (= corner (uint 1)) (= corner (uint 2))
                      (= corner (uint 4))))
         (side (if (or (= corner (uint 2)) (= corner (uint 3))
                       (= corner (uint 4)))
                   1.0 -1.0))
         (to-eye (- eye head))
         (distance (sqrt (dot to-eye to-eye)))
         (across (normalize (cross3 velocity to-eye)))
         (width (max 0.004 (* distance 0.0011)))
         (point (+ (if at-tail tail head) (* across (* width side))))
         (previous (- point (* velocity (- time (swizzle relief :z)))))
         (here-clip (* view-proj (vec4 point 1.0)))
         (near-fade (smoothstep 0.6 3.0 distance))
         (far-fade (- 1.0 (smoothstep 14.0 22.0 distance)))
         (light (+ (* (swizzle ambient :xyz) 1.4)
                   (* (swizzle sun-diffuse :xyz) 0.25))))
    (set-output clip-position (clip here-clip (swizzle temporal :zw)))
    (set-output shade (vec4 light (* (swizzle rain :x) 0.22 near-fade far-fade
                                     (if at-tail 0.15 1.0))))
    (set-output here here-clip)
    (set-output then (* previous-view-proj (vec4 previous 1.0)))))

(define-shader rain-fragment
    (:stage :fragment
     :inputs ((shade :vec4 :location 0)
              (here :vec4 :location 1)
              (then :vec4 :location 2))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1)))
  (let* ((motion-uv (- (clip-uv then) (clip-uv here))))
    (set-output color shade)
    (set-output motion motion-uv)))

(define-shader-program rain
  :vertex rain-vertex
  :fragment rain-fragment)

;;; -- HUD text -------------------------------------------------------------
;;;
;;; Glyphs and vector shapes by Slug, from moppe's glyph quads
;;; (render::GlyphQuad, six vec4 rows) and its curve and band buffers
;;; (render/slug.hh).  The per-pixel band walk is Luv's: LUV.SLUG's band
;;; steps fold each sorted band's curves into a coverage, a weight, and
;;; whether the walk may stop.

(define-shader-function quad-corner (vertex-index)
  (let* ((i (float vertex-index)))
    (vec2 (if (= i 1.0) 1.0 (if (= i 4.0) 1.0 (if (= i 5.0) 1.0 0.0)))
          (if (= i 2.0) 1.0 (if (= i 3.0) 1.0 (if (= i 5.0) 1.0 0.0))))))

(define-shader slug-text-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (em :vec2 :location 0)
               (bounds :vec4 :location 1 :interpolation :flat)
               (ink :vec4 :location 2 :interpolation :flat)
               (glyph :vec4 :location 3 :interpolation :flat))
     :resources ((hud :uniform-block :binding 0 :members #.*hud*)
                 (quads :storage-buffer :binding 1 :element :vec4)))
  (let* ((row (* instance-index (uint 6.0)))
         (origin (buffer-element quads row))
         (axis-x (swizzle (buffer-element quads (+ row (uint 1.0))) :xy))
         (axis-y (swizzle (buffer-element quads (+ row (uint 2.0))) :xy))
         (glyph-bounds (buffer-element quads (+ row (uint 3.0))))
         (color (buffer-element quads (+ row (uint 4.0))))
         (words (buffer-element quads (+ row (uint 5.0))))
         (corner (quad-corner vertex-index))
         (filter-pixels (max (swizzle origin :w) 1.0))
         ;; Grow the quad past the outline by half the filter and a little
         ;; more, in em along each axis, so every pixel the filter can
         ;; touch is drawn.
         (pixels-per-point (max (swizzle output :y) 0.001))
         (pixels-per-em (max (* (vec2 (sqrt (dot axis-x axis-x))
                                      (sqrt (dot axis-y axis-y)))
                                pixels-per-point)
                             (vec2 0.001 0.001)))
         (dilation (/ (vec2 (+ (* 0.5 filter-pixels) 0.75)
                            (+ (* 0.5 filter-pixels) 0.75))
                      pixels-per-em))
         (low (- (swizzle glyph-bounds :xy) dilation))
         (at (+ low (* (- (+ (swizzle glyph-bounds :zw) dilation) low)
                       corner)))
         (point (+ (swizzle origin :xy) (* axis-x (swizzle at :x))
                   (* axis-y (swizzle at :y)))))
    (set-output clip-position
                (clip (* projection (vec4 point 0.0 1.0)) (vec2 0.0 0.0)))
    (set-output em at)
    (set-output bounds glyph-bounds)
    (set-output ink color)
    (set-output glyph (vec4 (float (bit-cast :uint (swizzle words :x)))
                            (float (bit-cast :uint (swizzle words :y)))
                            (float (bit-cast :uint (swizzle words :z)))
                            filter-pixels))))

(define-shader-function slug-band-of (value low high count)
  (let* ((span (- high low))
         (band (if (> span 0.0) (floor (* (/ (- value low) span) count)) 0.0)))
    (uint (clamp band 0.0 (- count 1.0)))))

(define-shader slug-text-fragment
    (:stage :fragment
     :inputs ((em :vec2 :location 0)
              (bounds :vec4 :location 1 :interpolation :flat)
              (ink :vec4 :location 2 :interpolation :flat)
              (glyph :vec4 :location 3 :interpolation :flat))
     :outputs ((color :vec4 :location 0))
     :resources ((hud :uniform-block :binding 0 :members #.*hud*)
                 (curves :storage-buffer :binding 2 :element :vec4)
                 (bands :storage-buffer :binding 3 :element :uint)))
  (let* ((one (uint 1.0))
         (two (uint 2.0))
         (base (uint (swizzle glyph :x)))
         ;; An empty glyph (a space) has no bands to walk.
         (drawn (and (>= (swizzle glyph :y) 1.0) (>= (swizzle glyph :z) 1.0)))
         (rows (max (swizzle glyph :y) 1.0))
         (columns (max (swizzle glyph :z) 1.0))
         (pixels-per-em (/ (luv.slug::slug-pixels-per-em em)
                           (swizzle glyph :w)))
         (row (slug-band-of (swizzle em :y) (swizzle bounds :y)
                            (swizzle bounds :w) rows))
         (column (+ (uint rows)
                    (slug-band-of (swizzle em :x) (swizzle bounds :x)
                                  (swizzle bounds :z) columns)))
         (row-count (if drawn (min (buffer-element bands (+ base (* two row)))
                                   (uint 1024))
                        (uint 0)))
         (row-list (buffer-element bands (+ base (* two row) one)))
         (column-count (if drawn
                           (min (buffer-element bands (+ base (* two column)))
                                (uint 1024))
                           (uint 0)))
         (column-list (buffer-element bands (+ base (* two column) one)))
         (horizontal
           (counted-fold
               (index row-count state (vec3 0.0 0.0 0.0)
                :until (luv.slug::slug-band-done-p state))
             (let* ((texel (buffer-element bands (+ row-list index))))
               (luv.slug::slug-horizontal-band-step
                state (buffer-element curves texel)
                (buffer-element curves (+ texel one)) em pixels-per-em))))
         (vertical
           (counted-fold
               (index column-count state (vec3 0.0 0.0 0.0)
                :until (luv.slug::slug-band-done-p state))
             (let* ((texel (buffer-element bands (+ column-list index))))
               (luv.slug::slug-vertical-band-step
                state (buffer-element curves texel)
                (buffer-element curves (+ texel one)) em pixels-per-em))))
         (coverage (luv.slug::slug-combine-band-coverage
                    (swizzle horizontal :x) (swizzle horizontal :y)
                    (swizzle vertical :x) (swizzle vertical :y)))
         (rgb (swizzle ink :xyz)))
    (set-output color (vec4 (mix rgb (srgb rgb) (swizzle output :x))
                            (* (swizzle ink :w) coverage)))))

(define-shader-program slug-text
  :vertex slug-text-vertex
  :fragment slug-text-fragment)

;;; -- temporal resolve and presentation ------------------------------------

;;; As in the NHAL demo: follow the nearest surface's motion back into the
;;; history, fetch it through a Catmull-Rom filter, clamp it to this frame's
;;; neighbourhood in YCoCg, and blend.

(define-shader-function scene-texel (centre dx dy size)
  (uvec2 (clamp (+ centre (vec2 dx dy)) (vec2 0.0 0.0)
                (- size (vec2 1.0 1.0)))))

(define-shader-function rgb-to-ycocg (rgb)
  (vec3 (+ (* (swizzle rgb :x) 0.25) (* (swizzle rgb :y) 0.5)
           (* (swizzle rgb :z) 0.25))
        (* 0.5 (- (swizzle rgb :x) (swizzle rgb :z)))
        (+ (* (swizzle rgb :x) -0.25) (* (swizzle rgb :y) 0.5)
           (* (swizzle rgb :z) -0.25))))

(define-shader-function ycocg-to-rgb (value)
  (vec3 (- (+ (swizzle value :x) (swizzle value :y)) (swizzle value :z))
        (+ (swizzle value :x) (swizzle value :z))
        (- (- (swizzle value :x) (swizzle value :y)) (swizzle value :z))))

(define-shader-abstraction neighbourhood (extreme scene centre size)
  (let ((taps (loop for dy in '(-1.0 0.0 1.0)
                    append (loop for dx in '(-1.0 0.0 1.0)
                                 collect `(rgb-to-ycocg
                                           (swizzle
                                            (texel-load ,scene
                                                        (scene-texel ,centre
                                                                     ,dx ,dy
                                                                     ,size))
                                            :rgb))))))
    (reduce (lambda (a b) `(,extreme ,a ,b)) taps)))

(define-shader-function motion-tap (motion depth)
  (vec3 (swizzle motion :x) (swizzle motion :y) (swizzle depth :x)))

(define-shader-function nearer (a b)
  (mix a b (step (swizzle a :z) (swizzle b :z))))

(define-shader-abstraction nearest-motion (motion depth centre size)
  (let ((taps (loop for dy in '(-1.0 0.0 1.0)
                    append (loop for dx in '(-1.0 0.0 1.0)
                                 collect `(motion-tap
                                           (texel-load ,motion
                                                       (scene-texel ,centre
                                                                    ,dx ,dy
                                                                    ,size))
                                           (texel-load ,depth
                                                       (scene-texel ,centre
                                                                    ,dx ,dy
                                                                    ,size)))))))
    (reduce (lambda (a b) `(nearer ,a ,b)) taps)))

(define-shader-abstraction catmull-rom-taps (texture sampler w0 w12 w3 at0 at12 at3)
  (flet ((tap (x y wx wy)
           `(* (swizzle (sample ,texture ,sampler
                                (vec2 (swizzle ,x :x) (swizzle ,y :y)))
                        :rgb)
               (* (swizzle ,wx :x) (swizzle ,wy :y)))))
    `(/ (+ ,(tap at12 at0 w12 w0) ,(tap at0 at12 w0 w12)
           ,(tap at12 at12 w12 w12) ,(tap at3 at12 w3 w12)
           ,(tap at12 at3 w12 w3))
        (+ (* (swizzle ,w12 :x) (swizzle ,w0 :y))
           (* (swizzle ,w0 :x) (swizzle ,w12 :y))
           (* (swizzle ,w12 :x) (swizzle ,w12 :y))
           (* (swizzle ,w3 :x) (swizzle ,w12 :y))
           (* (swizzle ,w12 :x) (swizzle ,w3 :y))))))

(define-shader resolve-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (ndc :vec2 :location 0)))
  (let* ((corner (fullscreen-corner (float vertex-index))))
    (set-output clip-position
                (vec4 (swizzle corner :x) (* -1.0 (swizzle corner :y))
                      0.0 1.0))
    (set-output ndc corner)))

(define-shader resolve-fragment
    (:stage :fragment
     :inputs ((ndc :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (scene :texture-2d :binding 0)
                 (scene-motion :texture-2d :binding 1)
                 (scene-depth :depth-texture-2d :binding 2)
                 (history :texture-2d :binding 3)
                 (linear-clamp :sampler :binding 0)))
  (let* ((size (swizzle temporal :xy))
         (here (ndc-uv (+ ndc (swizzle temporal :zw))))
         (centre (floor (* here size)))
         (fresh (swizzle (sample scene linear-clamp here) :rgb))
         (low (neighbourhood min scene centre size))
         (high (neighbourhood max scene centre size))
         (motion (swizzle (nearest-motion scene-motion scene-depth centre size)
                          :xy))
         (then-uv (+ (ndc-uv ndc) motion))
         (inside (* (step 0.0 (swizzle then-uv :x))
                    (step (swizzle then-uv :x) 1.0)
                    (step 0.0 (swizzle then-uv :y))
                    (step (swizzle then-uv :y) 1.0)))
         (output-size (swizzle temporal-blend :zw))
         (position (* then-uv output-size))
         (middle (+ (floor (- position (vec2 0.5 0.5))) (vec2 0.5 0.5)))
         (f (- position middle))
         (w0 (* f (+ (vec2 -0.5 -0.5) (* f (- (vec2 1.0 1.0) (* f 0.5))))))
         (w1 (+ (vec2 1.0 1.0) (* f (* f (+ (vec2 -2.5 -2.5) (* f 1.5))))))
         (w2 (* f (+ (vec2 0.5 0.5) (* f (- (vec2 2.0 2.0) (* f 1.5))))))
         (w3 (* f (* f (- (* f 0.5) (vec2 0.5 0.5)))))
         (w12 (+ w1 w2))
         (at0 (/ (- middle (vec2 1.0 1.0)) output-size))
         (at3 (/ (+ middle (vec2 2.0 2.0)) output-size))
         (at12 (/ (+ middle (/ w2 w12)) output-size))
         (remembered (ycocg-to-rgb
                      (clamp (rgb-to-ycocg
                              (catmull-rom-taps history linear-clamp
                                                w0 w12 w3 at0 at12 at3))
                             low high)))
         (speed (clamp (* (sqrt (dot motion motion)) 48.0) 0.0 1.0))
         (weight (max (- 1.0 (* (- 1.0 (swizzle temporal-blend :x))
                                (- 1.0 (* speed 0.35))))
                      (- 1.0 inside))))
    (set-output color (vec4 (mix remembered fresh weight) 1.0))))

(define-shader-program resolve
  :vertex resolve-vertex
  :fragment resolve-fragment)

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *present*
    '((grade :vec4)                ; exposure bias, 1 when the drawable is
                                   ; 8-bit, seconds, bloom strength
      (effects :vec4)              ; occlusion on, shafts on, 0, 0
      (sun-glare :vec4))))         ; the sun's uv, its visibility, aspect

;;; Auto-exposure, on the GPU alone: 256 wide taps of the resolved image,
;;; log-averaged, ease the stored exposure toward mid-grey -- clamped to
;;; about a stop either way so night stays night, and faster down than up,
;;; like eyes.  As MetalRenderer::update_exposure, without the readback.
(define-shader exposure-compute
    (:stage :compute
     :workgroup-size (256 1 1)
     :inputs ((local :uint :built-in :local-invocation-index))
     :shared ((brightness :float 256))
     :resources ((image :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)
                 (exposure :storage-buffer :binding 1 :element :float
                           :access :read-write)))
  (let* ((cell (vec2 (float (mod local (uint 16))) (float (/ local (uint 16)))))
         (uv (/ (+ cell (vec2 0.5 0.5)) 16.0))
         (o (vec2 0.008 0.014))
         (c (* 0.2 (+ (swizzle (sample-level image linear-clamp uv 0.0) :rgb)
                      (swizzle (sample-level image linear-clamp (+ uv o) 0.0)
                               :rgb)
                      (swizzle (sample-level image linear-clamp (- uv o) 0.0)
                               :rgb)
                      (swizzle (sample-level image linear-clamp
                                             (+ uv (vec2 (swizzle o :x)
                                                         (* -1.0 (swizzle o :y))))
                                             0.0)
                               :rgb)
                      (swizzle (sample-level image linear-clamp
                                             (+ uv (vec2 (* -1.0 (swizzle o :x))
                                                         (swizzle o :y)))
                                             0.0)
                               :rgb))))
         (luminance (dot c (vec3 0.2126 0.7152 0.0722))))
    (set-shared-element brightness local (log (max luminance 0.0001)))
    (workgroup-barrier)
    (when (= local (uint 0))
      (let* ((sum (counted-fold (index (uint 256) total 0.0)
                    (+ total (shared-element brightness index))))
             (average (exp (/ sum 256.0)))
             (old (buffer-element exposure (uint 0)))
             (target (clamp (/ 0.16 average) 0.55 1.9))
             (rate (if (< target old) 0.10 0.04)))
        (when (> average 0.00015)
          (set-buffer-element exposure (uint 0)
                              (+ old (* (- target old) rate))))))))

(define-shader-program exposure
  :compute exposure-compute)

(define-shader present-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (uv :vec2 :location 0)))
  (let* ((corner (fullscreen-corner (float vertex-index))))
    (set-output clip-position
                (vec4 (swizzle corner :x) (* -1.0 (swizzle corner :y))
                      0.0 1.0))
    (set-output uv (ndc-uv corner))))

;;; Bloom, as post.metal's: the exposed scene's bright parts at a quarter of
;;; the drawable's size, softened by a separable nine-tap Gaussian taken in
;;; five linear samples, then added before the tonemap.
(define-shader bloom-bright-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((present :uniform-block :binding 0 :members #.*present*)
                 (exposure :storage-buffer :binding 1 :element :float)
                 (image :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)))
  (let* ((c (* (swizzle (sample image linear-clamp uv) :rgb)
               (* (buffer-element exposure (uint 0)) (swizzle grade :x))))
         (luma (dot c (vec3 0.2126 0.7152 0.0722))))
    (set-output color (vec4 (* c (smoothstep 0.85 1.35 luma)) 1.0))))

(define-shader-program bloom-bright
  :vertex present-vertex
  :fragment bloom-bright-fragment)

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *blur*
    '((direction :vec4))))         ; texel step x, y

(define-shader bloom-blur-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((blur :uniform-block :binding 0 :members #.*blur*)
                 (source :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)))
  (let* ((step (swizzle direction :xy))
         (near (* step 1.3846154))
         (far (* step 3.2307692))
         (c (+ (* (swizzle (sample source linear-clamp uv) :rgb) 0.227027)
               (* (+ (swizzle (sample source linear-clamp (+ uv near)) :rgb)
                     (swizzle (sample source linear-clamp (- uv near)) :rgb))
                  0.3162162)
               (* (+ (swizzle (sample source linear-clamp (+ uv far)) :rgb)
                     (swizzle (sample source linear-clamp (- uv far)) :rgb))
                  0.0702700))))
    (set-output color (vec4 c 1.0))))

(define-shader-program bloom-blur
  :vertex present-vertex
  :fragment bloom-blur-fragment)

;;; -- ambient occlusion and sun shafts --------------------------------------
;;;
;;; As post.metal's, but kept out of the temporal history: both render into
;;; their own scene-sized targets after the resolve, and the present pass
;;; multiplies the occlusion in and adds the shafts.  The camera basis has
;;; the frustum's half-extents folded into its right and up spans, so a
;;; pixel's view ray needs no matrix inverse.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *post*
    '((eye :vec4)                  ; camera position
      (ray-forward :vec4)
      (ray-right :vec4)            ; right * tan(fov / 2) * aspect
      (ray-up :vec4)               ; up * tan(fov / 2)
      (occlusion :vec4)            ; radius m, strength, near m, far m
      (shafts :vec4)               ; reach m, extinction / m, steps, 0
      (shaft-color :vec4)          ; linear sun colour times strength
      (blur :vec4))))              ; texel step x, y

(define-shader-function view-ray (uv forward right up)
  (normalize (+ forward (* right (- (* 2.0 (swizzle uv :x)) 1.0))
                (* up (- 1.0 (* 2.0 (swizzle uv :y)))))))

;;; Reversed-Z perspective: near maps to 1, far to 0.
(define-shader-function view-depth (z near far)
  (/ (* near far) (+ (* z (- far near)) near)))

(define-shader-function depth-position (uv z eye forward right up near far)
  (let* ((ray (view-ray uv forward right up)))
    (+ eye (* ray (/ (view-depth z near far) (dot ray forward))))))

(define-shader-function interleaved-noise (pixel)
  (fract (* 52.9829189 (fract (+ (* 0.06711056 (swizzle pixel :x))
                                 (* 0.00583715 (swizzle pixel :y)))))))

;;; Alchemy-style obscurance over a jittered spiral of depth taps: crevices,
;;; trunk bases, and the floor of the sward darken by how much nearby
;;; geometry leans over them.
(define-shader gtao-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0)
              (pixel :vec4 :built-in :frag-coord))
     :outputs ((color :vec4 :location 0))
     :resources ((post :uniform-block :binding 0 :members #.*post*)
                 (scene-depth :depth-texture-2d :binding 0)
                 (nearest-clamp :sampler :binding 2)))
  (let* ((forward (swizzle ray-forward :xyz))
         (right (swizzle ray-right :xyz))
         (up (swizzle ray-up :xyz))
         (camera (swizzle eye :xyz))
         (near (swizzle occlusion :z))
         (far (swizzle occlusion :w))
         (z (swizzle (sample scene-depth nearest-clamp uv) :x))
         (position (depth-position uv z camera forward right up near far))
         (normal (normalize (cross3 (derivative-y position)
                                    (derivative-x position))))
         (depth (view-depth z near far))
         (radius (swizzle occlusion :x))
         (uv-radius (* (/ radius (* 2.0 depth))
                       (vec2 (/ 1.0 (sqrt (dot right right)))
                             (/ 1.0 (sqrt (dot up up))))))
         (jitter (interleaved-noise (swizzle pixel :xy)))
         (bias (+ 0.02 (* 0.002 depth)))
         (obscurance
           (counted-fold (i (uint 10) total 0.0)
             (let* ((angle (* 6.2831853 (+ jitter (* (float i) 0.618034))))
                    (reach (expt (/ (+ (float i) 0.5 jitter) 10.0) 0.7))
                    (tap-uv (+ uv (* (vec2 (cos angle) (sin angle))
                                     (* uv-radius reach))))
                    (tap-z (swizzle (sample scene-depth nearest-clamp tap-uv)
                                    :x))
                    (tap (depth-position tap-uv tap-z camera forward right up
                                         near far))
                    (to-tap (- tap position))
                    (lean (/ (* (max 0.0 (- (dot to-tap normal) bias)) radius)
                             (max (dot to-tap to-tap) 0.01))))
               (+ total (if (> tap-z 0.000001) lean 0.0)))))
         (ao (clamp (- 1.0 (/ (* (swizzle occlusion :y) obscurance) 10.0))
                    0.0 1.0)))
    (set-output color (vec4 (if (> z 0.000001) ao 1.0) 0.0 0.0 1.0))))

(define-shader-program gtao
  :vertex present-vertex
  :fragment gtao-fragment)

;;; A separable depth-aware blur: neighbours vote only when they lie on the
;;; same surface, so occlusion never bleeds across a silhouette.
(define-shader gtao-blur-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((post :uniform-block :binding 0 :members #.*post*)
                 (occluded :texture-2d :binding 0)
                 (scene-depth :depth-texture-2d :binding 1)
                 (linear-clamp :sampler :binding 0)
                 (nearest-clamp :sampler :binding 2)))
  (let* ((near (swizzle occlusion :z))
         (far (swizzle occlusion :w))
         (step (swizzle blur :xy))
         (centre (view-depth (swizzle (sample scene-depth nearest-clamp uv) :x)
                             near far))
         (sum (counted-fold (i (uint 5) acc (vec2 0.0 0.0))
                (let* ((k (- (float i) 2.0))
                       (at (+ uv (* step k)))
                       (d (view-depth (swizzle (sample scene-depth
                                                       nearest-clamp at)
                                               :x)
                                      near far))
                       (same (clamp (- 1.0 (/ (* 8.0 (abs (- d centre)))
                                              centre))
                                    0.0 1.0))
                       (w (* same (if (= (abs k) 2.0) 0.153388
                                      (if (= (abs k) 1.0) 0.221461
                                          0.250301)))))
                  (+ acc (vec2 (* w (swizzle (sample occluded linear-clamp at)
                                             :x))
                               w))))))
    (set-output color
                (vec4 (if (> (swizzle sum :y) 0.0)
                          (/ (swizzle sum :x) (swizzle sum :y)) 1.0)
                      0.0 0.0 1.0))))

(define-shader-program gtao-blur
  :vertex present-vertex
  :fragment gtao-blur-fragment)

;;; Sun shafts: march each view ray through the sun's shadow map and gather
;;; forward-scattered sunlight over its lit spans, stopping at the first
;;; surface; a Henyey-Greenstein lobe keeps the beams about the sun.
(define-shader shafts-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0)
              (pixel :vec4 :built-in :frag-coord))
     :outputs ((color :vec4 :location 0))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)
                 (post :uniform-block :binding 1 :members #.*post*)
                 (scene-depth :depth-texture-2d :binding 0)
                 (shadow-map :depth-texture-2d :binding 8)
                 (nearest-clamp :sampler :binding 2)
                 (shadow-compare :sampler :binding 3)))
  (let* ((ray (view-ray uv (swizzle ray-forward :xyz) (swizzle ray-right :xyz)
                        (swizzle ray-up :xyz)))
         (camera (swizzle eye :xyz))
         (surface (swizzle (sample scene-depth nearest-clamp uv) :x))
         (jitter (interleaved-noise (swizzle pixel :xy)))
         (steps (swizzle shafts :z))
         (dt (/ (swizzle shafts :x) steps))
         (sigma (swizzle shafts :y))
         (march
           (counted-fold (i (uint steps) state (vec2 0.0 0.0)
                          :until (> (swizzle state :y) 0.5))
             (let* ((distance (* (+ (float i) jitter) dt))
                    (world (+ camera (* ray distance)))
                    (c (* view-proj (vec4 world 1.0)))
                    (beyond (or (<= (swizzle c :w) 0.0)
                                (< (/ (swizzle c :z) (swizzle c :w)) surface)))
                    (at (sun-map-coordinate sun-view world))
                    (lit (mix 1.0 (shadow-tap shadow-map shadow-compare at
                                              0.0 0.0 0.0 0.0015)
                              (inside-sun-map at))))
               (if beyond (vec2 (swizzle state :x) 1.0)
                   (vec2 (+ (swizzle state :x)
                            (* lit (exp (* -1.0 sigma distance))))
                         0.0)))))
         (scatter (* (swizzle march :x) sigma dt))
         (mu (dot ray (normalize (swizzle sun-direction :xyz))))
         (g 0.60)
         (phase (/ (- 1.0 (* g g))
                   (* 4.0 3.14159265
                      (expt (- (+ 1.0 (* g g)) (* 2.0 g mu)) 1.5)))))
    (set-output color (vec4 (* (swizzle shaft-color :xyz) (* phase scatter))
                            1.0))))

(define-shader-program shafts
  :vertex present-vertex
  :fragment shafts-fragment)

(define-shader-function aces (x)
  (clamp (/ (* x (+ (* x 2.51) (vec3 0.03 0.03 0.03)))
            (+ (* x (+ (* x 2.43) (vec3 0.59 0.59 0.59))) (vec3 0.14 0.14 0.14)))
         (vec3 0.0 0.0 0.0) (vec3 1.0 1.0 1.0)))

(define-shader-function gamma-encode (c)
  (vec3 (expt (swizzle c :x) 0.4545454) (expt (swizzle c :y) 0.4545454)
        (expt (swizzle c :z) 0.4545454)))

;;; As post.metal's present: adapted exposure, ACES, then -- in display
;;; space -- a warm print-film base with cool shadow density, vibrance,
;;; animated grain heavier in the shadows, and a light vignette.  An RGBA16F
;;; drawable is extended linear sRGB, so the graded frame is decoded again.
(define-shader present-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((present :uniform-block :binding 0 :members #.*present*)
                 (exposure :storage-buffer :binding 1 :element :float)
                 (image :texture-2d :binding 0)
                 (bloom :texture-2d :binding 1)
                 (occluded :texture-2d :binding 2)
                 (shafts-image :texture-2d :binding 3)
                 (linear-clamp :sampler :binding 0)))
  (let* ((adapted (* (buffer-element exposure (uint 0)) (swizzle grade :x)))
         (scene (swizzle (sample image linear-clamp uv) :rgb))
         (shaded (* scene (if (> (swizzle effects :x) 0.5)
                              (swizzle (sample occluded linear-clamp uv) :x)
                              1.0)))
         (lit (if (> (swizzle effects :y) 0.5)
                  (+ shaded (swizzle (sample shafts-image linear-clamp uv)
                                     :rgb))
                  shaded))
         ;; A warm veil around the sun: the glare of looking toward it.
         (from-sun (* (- uv (swizzle sun-glare :xy))
                      (vec2 (swizzle sun-glare :w) 1.0)))
         (veil (* (vec3 1.0 0.86 0.62)
                  (* (exp (* -2.6 (sqrt (dot from-sun from-sun)))) 0.15
                     (swizzle sun-glare :z))))
         (exposed (+ (* lit adapted) (* veil adapted)))
         ;; The bright pass saw the exposed scene, so the glow adds in the
         ;; same units.
         (glowing (if (> (swizzle grade :w) 0.0)
                      (+ exposed (* (swizzle (sample bloom linear-clamp uv)
                                             :rgb)
                                    (* 0.45 (swizzle grade :w))))
                      exposed))
         (display (gamma-encode (aces glowing)))
         (film (+ (* display (vec3 1.045 1.005 0.955))
                  (vec3 -0.004 0.002 0.012)))
         (curved (vec3 (expt (max (swizzle film :x) 0.0) 1.05)
                       (expt (max (swizzle film :y) 0.0) 1.02)
                       (max (swizzle film :z) 0.0)))
         (grade-luma (dot curved (vec3 0.299 0.587 0.114)))
         (toned (+ curved
                   (* (vec3 -0.004 0.004 0.010)
                      (- 1.0 (smoothstep 0.10 0.48 grade-luma)))
                   (* (vec3 0.010 0.006 -0.004)
                      (smoothstep 0.58 0.96 grade-luma))))
         (luma (dot toned (vec3 0.299 0.587 0.114)))
         (high (max (swizzle toned :x) (max (swizzle toned :y)
                                            (swizzle toned :z))))
         (low (min (swizzle toned :x) (min (swizzle toned :y)
                                           (swizzle toned :z))))
         (vibrant (mix (vec3 luma luma luma) toned
                       (+ 1.0 (* 0.10 (- 1.0 (- high low))))))
         (time (swizzle grade :z))
         (pixel (* uv (vec2 1920.0 1080.0)))
         (grain-at (- (+ pixel (vec2 (* time 173.0) (* time 251.0)))
                      (* (floor (/ (+ pixel (vec2 (* time 173.0)
                                                  (* time 251.0)))
                                   1024.0))
                         1024.0)))
         (grain (fract (* (sin (dot grain-at (vec2 12.9898 78.233)))
                          43758.5453)))
         (speck (* (- grain 0.5)
                   (* 0.020 (- 1.0 (* 0.65 (dot vibrant
                                                (vec3 0.299 0.587 0.114)))))))
         (grained (+ vibrant (vec3 speck speck speck)))
         (centred (* (- uv (vec2 0.5 0.5)) (vec2 1.0 0.72)))
         (vignette (- 1.0 (* 0.09 (smoothstep 0.22 0.72
                                              (dot centred centred)))))
         (final (clamp (* grained vignette) (vec3 0.0 0.0 0.0)
                       (vec3 1.0 1.0 1.0))))
    (set-output color (vec4 (mix (srgb final) final (swizzle grade :y)) 1.0))))

(define-shader-program present
  :vertex present-vertex
  :fragment present-fragment)

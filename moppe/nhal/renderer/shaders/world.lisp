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
      (relief :vec4)               ; sea level, land relief, previous time, 0
      (view-right :vec4)           ; right * tan(fov x / 2), for view rays
      (view-up :vec4)              ; up * tan(fov y / 2)
      (view-forward :vec4)
      (temporal :vec4)             ; scene width, height, jitter x, y (NDC)
      (temporal-blend :vec4))))    ; new frame's weight, 0, output size

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
(define-shader-function hazed (color world camera fog-color sun relief)
  (let* ((rel (- world camera))
         (distance (sqrt (dot rel rel)))
         (fog (relief-haze (distance-fog distance (swizzle fog-color :w))
                           (swizzle world :y) (swizzle relief :x)
                           (swizzle relief :y))))
    (mix color
         (warmed-fog (swizzle fog-color :xyz) (/ rel (max distance 0.0001)) sun)
         (smoothstep 0.0 0.9 fog))))

;;; -- terrain --------------------------------------------------------------

;;; Heights (R32F) and normals (RG16 snorm: x and z) follow the terrain grid;
;;; the world is periodic, so lattice reads wrap.  A chunk is a grid of
;;; vertices at a step of source samples; near the camera the step is a
;;; quarter sample.  Past its morph start a vertex slides onto the exact
;;; surface of the next coarser level, so levels meet without cracks.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *terrain*
    '((scale :vec4)                ; grid step x, height scale, step z, tex
      (fields :vec4)               ; have materials, 0, 0, 0
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
(define-shader-function ground-albedo (world up distance land floor rise)
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
                            (band (+ (swizzle land :w) (* 0.3 edge)) 0.68)))
         (shore (* (swizzle floor :x) 8.0))
         (shedding (- 1.0 (smoothstep 0.80 0.92 up)))
         (soil (mix forest-floor
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
                 (linear-repeat :sampler :binding 1)))
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
         (albedo (srgb (ground-albedo world (swizzle normal :y) distance
                                      land floor-fields rise)))
         (sun (swizzle sun-direction :xyz))
         (lambert (clamp (/ (+ (dot normal sun) 0.08) 1.08) 0.0 1.0))
         (lit (* albedo (+ (hemisphere-light (swizzle ambient :xyz) normal)
                           (* (swizzle sun-diffuse :xyz) (* 0.9 lambert))))))
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
(define-shader sky-fragment
    (:stage :fragment
     :inputs ((ndc :vec2 :location 0))
     :outputs ((color :vec4 :location 0)
               (motion :vec2 :location 1))
     :resources ((frame :uniform-block :binding 0 :members #.*frame*)))
  (let* ((ray (normalize (+ (swizzle view-forward :xyz)
                            (* (swizzle view-right :xyz) (swizzle ndc :x))
                            (* (swizzle view-up :xyz) (swizzle ndc :y)))))
         (sun (swizzle sun-direction :xyz))
         (up (clamp (swizzle ray :y) 0.0 1.0))
         (haze (warmed-fog (swizzle fog-color :xyz) ray sun))
         (zenith (* (swizzle fog-color :xyz) (vec3 0.55 0.72 1.05)))
         (toward (max (dot ray sun) 0.0))
         (glow (* (swizzle sun-diffuse :xyz)
                  (+ (* (expt toward 1200.0) 8.0) (* (expt toward 16.0) 0.1))))
         (far (vec4 ray 0.0)))
    (set-output color (vec4 (+ (mix haze zenith (expt up 0.55)) glow) 1.0))
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
      (output :vec4))))            ; 1 when the drawable is linear

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
    '((grade :vec4))))             ; exposure, 1 when the drawable is 8-bit

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

;;; Narkowicz's ACES fit; the display's gamma only for an 8-bit drawable,
;;; since an RGBA16F drawable is extended linear sRGB.
(define-shader present-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((present :uniform-block :binding 0 :members #.*present*)
                 (image :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)))
  (let* ((x (* (swizzle (sample image linear-clamp uv) :rgb)
               (swizzle grade :x)))
         (mapped (clamp (/ (* x (+ (* x 2.51) (vec3 0.03 0.03 0.03)))
                           (+ (* x (+ (* x 2.43) (vec3 0.59 0.59 0.59)))
                              (vec3 0.14 0.14 0.14)))
                        (vec3 0.0 0.0 0.0) (vec3 1.0 1.0 1.0)))
         (encoded (vec3 (expt (swizzle mapped :x) 0.4545454)
                        (expt (swizzle mapped :y) 0.4545454)
                        (expt (swizzle mapped :z) 0.4545454))))
    (set-output color (vec4 (mix mapped encoded (swizzle grade :y)) 1.0))))

(define-shader-program present
  :vertex present-vertex
  :fragment present-fragment)

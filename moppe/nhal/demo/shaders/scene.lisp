;;; The NHAL demo scene's shaders in Luv's mathematical shader language.
;;; luv-shaderc lowers each program to MSL for Metal 4 and HLSL for
;;; Direct3D 12, with the reflection NHAL builds pipelines from
;;; (docs/nhal.md).
;;;
;;; Every program but the tonemap reads the frame block at buffer binding
;;; 0.  The vertex stages pull from a storage buffer at binding 1 by vertex
;;; and instance index; there are no vertex buffers.  Clip positions are
;;; written y-down, the language's convention, and each lowering turns them
;;; y-up.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *frame-state*
    '((camera-position :vec4)   ; w: seconds
      (camera-right :vec4)      ; right / tan(fov x / 2); w: tan
      (camera-up :vec4)         ; up / tan(fov y / 2); w: tan
      (camera-forward :vec4)    ; w: near plane
      (sun-direction :vec4)     ; toward the sun
      (sun-color :vec4)
      (sky-zenith :vec4)
      (sky-horizon :vec4)       ; w: fog density per metre
      (terrain :vec4)           ; cell, samples per side, x0, z0
      (forest :vec4)            ; indices per tree, trees, wind, unused
      ;; The sun's orthographic view: light x and y in -1..1 across the
      ;; map, depth 0..1 away from the sun, each a row dotted with (p, 1).
      (shadow-row-x :vec4)
      (shadow-row-y :vec4)
      (shadow-row-z :vec4)
      (shadow :vec4)            ; texel size, receiver bias, unused, unused
      ;; The view frustum's sides and near plane, each (normal, offset): a
      ;; point is inside when dot(normal, p) + offset >= 0.
      (frustum-left :vec4)
      (frustum-right :vec4)
      (frustum-top :vec4)
      (frustum-bottom :vec4)
      (frustum-near :vec4)
      ;; Temporal upscaling: last frame's camera, in the same scaled forms,
      ;; the scene's size, this frame's sub-pixel jitter in y-up NDC, and
      ;; the history's blend (weight of the new frame, 1 when restarting).
      (previous-position :vec4)
      (previous-right :vec4)
      (previous-up :vec4)
      (previous-forward :vec4)
      (temporal :vec4)          ; scene width, height, jitter x, jitter y
      (temporal-blend :vec4))))       ; new frame's weight, unused, output size

;;; Reversed-Z with an infinite far plane: depth is near / view distance.
;;; The jitter moves the whole image by a fraction of a scene pixel (y-up
;;; NDC; the clip position is written y-down) for temporal upscaling.
(define-shader-function project-relative (rel right up forward near jitter)
  (let* ((depth (dot rel forward)))
    (vec4 (+ (dot rel right) (* (swizzle jitter :x) depth))
          (- (* -1.0 (dot rel up)) (* (swizzle jitter :y) depth))
          near
          depth)))

;;; Where a world point lands in the sun's view: (x, y, depth).
(define-shader-function light-space (position row-x row-y row-z)
  (let* ((point (vec4 position 1.0)))
    (vec3 (dot row-x point) (dot row-y point) (dot row-z point))))

;;; A shadow caster's clip position in the sun's view.
(define-shader-function light-clip (light)
  (vec4 (swizzle light :x) (* -1.0 (swizzle light :y))
        (swizzle light :z) 1.0))

;;; The shadow map coordinate a receiver compares at: (u, v, depth).
(define-shader-function shadow-coordinate (light)
  (vec3 (+ (* (swizzle light :x) 0.5) 0.5)
        (- 0.5 (* (swizzle light :y) 0.5))
        (swizzle light :z)))

;;; Five comparison taps in a cross, one texel apart: a soft shadow edge.
(define-shader-abstraction sunlight (map compare coordinate texel bias)
  (flet ((tap (dx dy)
           `(sample-compare ,map ,compare
                            (+ (swizzle ,coordinate :xy)
                               (* (vec2 ,dx ,dy) ,texel))
                            (- (swizzle ,coordinate :z) ,bias))))
    `(* 0.2 (+ ,(tap 0.0 0.0) ,(tap 1.0 0.0) ,(tap -1.0 0.0)
               ,(tap 0.0 1.0) ,(tap 0.0 -1.0)))))

(define-shader-function sky-color (ray horizon zenith sun-direction sun-color)
  (let* ((up (clamp (swizzle ray :y) 0.0 1.0))
         (base (mix horizon zenith (expt up 0.6)))
         (sun (max (dot ray sun-direction) 0.0))
         (glow (+ (* (expt sun 900.0) 6.0) (* (expt sun 12.0) 0.08))))
    (+ base (* sun-color glow))))

(define-shader-function shade (albedo normal world light camera sun-direction
                              sun-color zenith horizon density)
  (let* ((diffuse (* (max (dot normal sun-direction) 0.0) light))
         (ambient (* (mix (* horizon 0.35) zenith
                          (+ (* (swizzle normal :y) 0.5) 0.5))
                     0.45))
         (lit (* albedo (+ (* sun-color diffuse) ambient)))
         (rel (- world camera))
         (distance (sqrt (dot rel rel)))
         (fog (- 1.0 (exp (* -1.0 (* distance density))))))
    (mix lit
         (sky-color (* rel (/ 1.0 distance))
                    horizon zenith sun-direction sun-color)
         fog)))

(define-shader-function lattice (point)
  (fract (* (sin (dot (floor point) (vec2 127.1 311.7))) 43758.5453)))

;;; -- terrain ------------------------------------------------------------

;;; One sample per vertex: (height, normal).  The grid position comes from
;;; the vertex index and the block's terrain lane.
(define-shader-function terrain-world (index sample terrain)
  (let* ((side (swizzle terrain :y))
         (row (floor (/ index side)))
         (column (- index (* row side))))
    (vec3 (+ (swizzle terrain :z) (* column (swizzle terrain :x)))
          (swizzle sample :x)
          (+ (swizzle terrain :w) (* row (swizzle terrain :x))))))

(define-shader terrain-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (albedo :vec3 :location 2)
               (shadow-at :vec3 :location 3))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (samples :storage-buffer :binding 1 :element :vec4)))
  (let* ((sample (buffer-element samples vertex-index))
         (position (terrain-world (float vertex-index) sample terrain)))
    (set-output clip-position
                (project-relative (- position (swizzle camera-position :xyz))
                                  (swizzle camera-right :xyz)
                                  (swizzle camera-up :xyz)
                                  (swizzle camera-forward :xyz)
                                  (swizzle camera-forward :w)
                                  (swizzle temporal :zw)))
    (set-output world position)
    (set-output surface-normal (swizzle sample :yzw))
    (set-output albedo (vec3 0.0 0.0 0.0))
    (set-output shadow-at
                (shadow-coordinate
                 (light-space position shadow-row-x shadow-row-y
                              shadow-row-z)))))

(define-shader terrain-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (albedo :vec3 :location 2)
              (shadow-at :vec3 :location 3))
     :outputs ((color :vec4 :location 0))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (shadow-map :depth-texture-2d :binding 0)
                 (shadow-compare :sampler :binding 3)))
  (let* ((normal (normalize surface-normal))
         (ground (swizzle world :xz))
         (patch (+ (* (lattice (* ground (/ 1.0 9.0))) 0.5)
                   (* (lattice (* ground (/ 1.0 2.3))) 0.5)))
         (grass (mix (vec3 0.10 0.16 0.05) (vec3 0.20 0.22 0.09) patch))
         (rock (* (vec3 0.27 0.25 0.23) (+ 0.8 (* 0.4 patch))))
         (steep (smoothstep 0.78 0.62 (swizzle normal :y)))
         (light (sunlight shadow-map shadow-compare shadow-at
                          (swizzle shadow :x) (swizzle shadow :y))))
    (set-output color
                (vec4 (shade (mix grass rock steep) normal world light
                             (swizzle camera-position :xyz)
                             (swizzle sun-direction :xyz)
                             (swizzle sun-color :xyz)
                             (swizzle sky-zenith :xyz)
                             (swizzle sky-horizon :xyz)
                             (swizzle sky-horizon :w))
                      1.0))))

(define-shader-program terrain
  :vertex terrain-vertex
  :fragment terrain-fragment)

;;; The ground as the sun sees it: depth only.
(define-shader terrain-shadow-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (samples :storage-buffer :binding 1 :element :vec4)))
  (let* ((sample (buffer-element samples vertex-index))
         (position (terrain-world (float vertex-index) sample terrain)))
    (set-output clip-position
                (light-clip (light-space position shadow-row-x shadow-row-y
                                         shadow-row-z)))))

(define-shader-program terrain-shadow
  :vertex terrain-shadow-vertex)

;;; -- wind ---------------------------------------------------------------

;;; Each frame one invocation per tree copies its two placement lanes and
;;; adds a third, the sway of its crown top in metres, from gusts that
;;; travel across the valley.  It then tests the tree's bounding sphere
;;; against the view frustum and appends the visible ones to a list, whose
;;; length an atomic counts into the camera's indexed indirect draw (index
;;; count, instances, first index, base vertex, first instance); the host
;;; zeroes that count before the dispatch.  The sun's shadow draws every
;;; tree, since trees out of view still shade it, from a second record.
(define-shader-function sphere-inside (plane centre radius)
  (>= (+ (dot (swizzle plane :xyz) centre) (swizzle plane :w))
      (* -1.0 radius)))

(define-shader forest-wind-compute
    (:stage :compute
     :workgroup-size (64 1 1)
     :inputs ((invocation :uvec3 :built-in :global-invocation-id))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (instances :storage-buffer :binding 1 :element :vec4)
                 (animated :storage-buffer :binding 2 :element :vec4
                           :access :read-write)
                 (draw-arguments :storage-buffer :binding 3 :element :uint
                                 :access :read-write)
                 (shadow-arguments :storage-buffer :binding 4 :element :uint
                                   :access :read-write)
                 (visible :storage-buffer :binding 5 :element :uint
                          :access :read-write)))
  (let* ((tree (swizzle invocation :x))
         (count (uint (swizzle forest :y)))
         (root (buffer-element instances (* tree (uint 2.0))))
         (shape (buffer-element instances (+ (* tree (uint 2.0)) (uint 1.0))))
         (time (swizzle camera-position :w))
         (travel (+ (* (swizzle root :x) 0.021) (* (swizzle root :z) 0.013)))
         (gust (+ (sin (- (* time 1.1) travel))
                  (* 0.35 (sin (+ (* time 2.9) (* travel 2.7))))
                  0.6))
         (lean (* gust (* (swizzle forest :z) (* (swizzle root :w) 0.012))))
         (out (* tree (uint 3.0)))
         (height (swizzle root :w))
         (centre (+ (swizzle root :xyz) (vec3 0.0 (* height 0.5) 0.0)))
         (radius (+ (* height 0.55) (abs lean)))
         (seen (and (< tree count)
                    (sphere-inside frustum-left centre radius)
                    (sphere-inside frustum-right centre radius)
                    (sphere-inside frustum-top centre radius)
                    (sphere-inside frustum-bottom centre radius)
                    (sphere-inside frustum-near centre radius))))
    (when (< tree count)
      (set-buffer-element animated out root)
      (set-buffer-element animated (+ out (uint 1.0)) shape)
      (set-buffer-element animated (+ out (uint 2.0))
                          (vec4 (* lean 0.8) 0.0 (* lean 0.6) 0.0)))
    (when seen
      (let* ((slot (atomic-add draw-arguments (uint 1.0) (uint 1.0))))
        (set-buffer-element visible slot tree)))
    (when (= tree (uint 0.0))
      (set-buffer-element draw-arguments (uint 0.0) (uint (swizzle forest :x)))
      (set-buffer-element draw-arguments (uint 2.0) (uint 0.0))
      (set-buffer-element draw-arguments (uint 3.0) (uint 0.0))
      (set-buffer-element draw-arguments (uint 4.0) (uint 0.0))
      (set-buffer-element shadow-arguments (uint 0.0) (uint (swizzle forest :x)))
      (set-buffer-element shadow-arguments (uint 1.0) count)
      (set-buffer-element shadow-arguments (uint 2.0) (uint 0.0))
      (set-buffer-element shadow-arguments (uint 3.0) (uint 0.0))
      (set-buffer-element shadow-arguments (uint 4.0) (uint 0.0)))))

(define-shader-program forest-wind
  :compute forest-wind-compute)

;;; -- trees --------------------------------------------------------------

;;; An instance is three lanes, as the wind writes them: (root x, y, z,
;;; height), (trunk radius, crown radius, crown base as a fraction of
;;; height, tint), and (sway x, 0, sway z, 0) at the top.  Vertices 0-17
;;; are the trunk's two rings of nine; then three crown tiers of a nine-
;;; vertex base ring and an apex.  Each function below reads the vertex's
;;; part of that shape: 1 on the trunk, 0 in the crown.
(define-shader-function tree-trunk-p (index)
  (if (< index 17.5) 1.0 0.0))

(define-shader-function tree-angle (index shape)
  (let* ((ring (floor (/ index 9.0)))
         (k (- index 18.0))
         (tier (floor (/ k 10.0)))
         (corner (- k (* tier 10.0))))
    (mix (+ (* corner 0.7853982) (* tier 0.4) (* (swizzle shape :w) 6.28))
         (* (- index (* ring 9.0)) 0.7853982)
         (tree-trunk-p index))))

(define-shader-function tree-local (index root shape)
  (let* ((height (swizzle root :w))
         (crown-base (* (swizzle shape :z) height))
         (ring (floor (/ index 9.0)))
         (k (- index 18.0))
         (tier (floor (/ k 10.0)))
         (apex (if (> (- k (* tier 10.0)) 8.5) 1.0 0.0))
         (crown (- height crown-base))
         (base-y (+ crown-base (* crown (* tier 0.27))))
         (crown-radius (* (swizzle shape :y) (- 1.0 (* tier 0.27))))
         (trunk-radius (* (swizzle shape :x) (- 1.0 (* ring 0.4))))
         (angle (tree-angle index shape))
         (around (vec2 (cos angle) (sin angle)))
         (crown-local (mix (vec3 (* (swizzle around :x) crown-radius)
                                 base-y
                                 (* (swizzle around :y) crown-radius))
                           (vec3 0.0 (+ base-y (* crown 0.48)) 0.0)
                           apex))
         (trunk-local (vec3 (* (swizzle around :x) trunk-radius)
                            (if (< ring 0.5) -0.5 (+ crown-base 0.5))
                            (* (swizzle around :y) trunk-radius))))
    (mix crown-local trunk-local (tree-trunk-p index))))

;;; The trunk leans with the sway, more toward the top.
(define-shader-function tree-world (index root shape sway)
  (let* ((local (tree-local index root shape))
         (rise (/ (swizzle local :y) (swizzle root :w))))
    (+ (swizzle root :xyz) local
       (* (vec3 (swizzle sway :x) 0.0 (swizzle sway :z)) (* rise rise)))))

(define-shader-function tree-normal (index root shape)
  (let* ((height (swizzle root :w))
         (crown (- height (* (swizzle shape :z) height)))
         (k (- index 18.0))
         (tier (floor (/ k 10.0)))
         (apex (if (> (- k (* tier 10.0)) 8.5) 1.0 0.0))
         (radius (* (swizzle shape :y) (- 1.0 (* tier 0.27))))
         (rise (* crown 0.48))
         (angle (tree-angle index shape))
         (around (vec2 (cos angle) (sin angle)))
         (cone (mix (normalize (vec3 (* (swizzle around :x) rise) radius
                                     (* (swizzle around :y) rise)))
                    (vec3 0.0 1.0 0.0)
                    apex)))
    (mix cone
         (vec3 (swizzle around :x) 0.0 (swizzle around :y))
         (tree-trunk-p index))))

(define-shader trees-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (albedo :vec3 :location 2)
               (shadow-at :vec3 :location 3))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (instances :storage-buffer :binding 1 :element :vec4)
                 (visible :storage-buffer :binding 2 :element :uint)))
  (let* ((first (* (buffer-element visible instance-index) (uint 3.0)))
         (root (buffer-element instances first))
         (shape (buffer-element instances (+ first (uint 1.0))))
         (sway (buffer-element instances (+ first (uint 2.0))))
         (index (float vertex-index))
         (position (tree-world index root shape sway))
         (needles (mix (vec3 0.035 0.07 0.04) (vec3 0.06 0.10 0.045)
                       (swizzle shape :w))))
    (set-output clip-position
                (project-relative (- position (swizzle camera-position :xyz))
                                  (swizzle camera-right :xyz)
                                  (swizzle camera-up :xyz)
                                  (swizzle camera-forward :xyz)
                                  (swizzle camera-forward :w)
                                  (swizzle temporal :zw)))
    (set-output world position)
    (set-output surface-normal (tree-normal index root shape))
    (set-output albedo (mix needles (vec3 0.13 0.09 0.06)
                            (tree-trunk-p index)))
    (set-output shadow-at
                (shadow-coordinate
                 (light-space position shadow-row-x shadow-row-y
                              shadow-row-z)))))

(define-shader trees-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (albedo :vec3 :location 2)
              (shadow-at :vec3 :location 3))
     :outputs ((color :vec4 :location 0))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (shadow-map :depth-texture-2d :binding 0)
                 (shadow-compare :sampler :binding 3)))
  (let* ((light (sunlight shadow-map shadow-compare shadow-at
                          (swizzle shadow :x) (swizzle shadow :y))))
    (set-output color
                (vec4 (shade albedo (normalize surface-normal) world light
                             (swizzle camera-position :xyz)
                             (swizzle sun-direction :xyz)
                             (swizzle sun-color :xyz)
                             (swizzle sky-zenith :xyz)
                             (swizzle sky-horizon :xyz)
                             (swizzle sky-horizon :w))
                      1.0))))

(define-shader-program trees
  :vertex trees-vertex
  :fragment trees-fragment)

;;; The swaying trees as the sun sees them: depth only.
(define-shader trees-shadow-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (instances :storage-buffer :binding 1 :element :vec4)))
  (let* ((first (* instance-index (uint 3.0)))
         (position (tree-world (float vertex-index)
                               (buffer-element instances first)
                               (buffer-element instances
                                               (+ first (uint 1.0)))
                               (buffer-element instances
                                               (+ first (uint 2.0))))))
    (set-output clip-position
                (light-clip (light-space position shadow-row-x shadow-row-y
                                         shadow-row-z)))))

(define-shader-program trees-shadow
  :vertex trees-shadow-vertex)

;;; -- sky and tonemap ----------------------------------------------------

;;; One triangle over the screen at the far plane (reversed-Z zero).  The
;;; ndc output is y-up, as the camera basis is.
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

(define-shader sky-fragment
    (:stage :fragment
     :inputs ((ndc :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)))
  (let* ((tan-x (swizzle camera-right :w))
         (tan-y (swizzle camera-up :w))
         (ray (normalize
               (+ (swizzle camera-forward :xyz)
                  (* (swizzle camera-right :xyz)
                     (* (swizzle ndc :x) (* tan-x tan-x)))
                  (* (swizzle camera-up :xyz)
                     (* (swizzle ndc :y) (* tan-y tan-y)))))))
    (set-output color
                (vec4 (sky-color ray
                                 (swizzle sky-horizon :xyz)
                                 (swizzle sky-zenith :xyz)
                                 (swizzle sun-direction :xyz)
                                 (swizzle sun-color :xyz))
                      1.0))))

(define-shader-program sky
  :vertex sky-vertex
  :fragment sky-fragment)

;;; -- temporal upscaling -------------------------------------------------

;;; A jittered scene smaller than the output accumulates into a history at
;;; the output's size.  Each output pixel finds its point in this frame's
;;; scene, rebuilds the world position from the depth there and the camera
;;; basis, and asks where last frame's camera saw it; the history from there,
;;; clamped to this frame's neighbourhood, blends with the new sample.
;;; Only the camera moves things in this reprojection: the wind's sway is
;;; small enough to leave to the clamp.

(define-shader-function view-ray (ndc right up forward)
  (+ forward
     (* (swizzle right :xyz) (* (swizzle ndc :x) (* (swizzle right :w)
                                                      (swizzle right :w))))
     (* (swizzle up :xyz) (* (swizzle ndc :y) (* (swizzle up :w)
                                                 (swizzle up :w))))))

(define-shader-function ndc-uv (ndc)
  (vec2 (+ (* (swizzle ndc :x) 0.5) 0.5) (- 0.5 (* (swizzle ndc :y) 0.5))))

;;; The scene texel at a whole-pixel offset from a centre texel, clamped to
;;; the image.
(define-shader-function scene-texel (centre dx dy size)
  (uvec2 (clamp (+ centre (vec2 dx dy)) (vec2 0.0 0.0)
                (- size (vec2 1.0 1.0)))))

;;; Luminance and two chroma axes, as Luft's resolve clips its history:
;;; a box in this space hugs a neighbourhood's colours more tightly than one
;;; in RGB, so less stale colour survives the clamp.
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

;;; The smallest and largest YCoCg colour among the 3x3 scene texels around
;;; one.
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

;;; The history through a Catmull-Rom filter, in five bilinear taps
;;; (Jimenez's arrangement): sharper than one bilinear fetch, which would
;;; blur a little more with every frame the history is resampled.  The
;;; resolve computes the filter's weights (w0, w12, w3) and tap positions
;;; (at0, at12, at3); this sums the taps.
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
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (scene :texture-2d :binding 0)
                 (scene-depth :depth-texture-2d :binding 1)
                 (history :texture-2d :binding 2)
                 (linear-clamp :sampler :binding 0)))
  (let* ((size (swizzle temporal :xy))
         (jitter (swizzle temporal :zw))
         ;; Where this output point landed in the jittered scene.
         (here (ndc-uv (+ ndc jitter)))
         (centre (floor (* here size)))
         (fresh (swizzle (sample scene linear-clamp here) :rgb))
         (low (neighbourhood min scene centre size))
         (high (neighbourhood max scene centre size))
         (depth (swizzle (texel-load scene-depth
                                     (scene-texel centre 0.0 0.0 size))
                         :x))
         ;; The world point, relative to this frame's eye; the sky (depth
         ;; zero) is a direction far away.
         (distance (/ (swizzle camera-forward :w) (max depth 0.0000001)))
         (rel (* (view-ray ndc camera-right camera-up
                           (swizzle camera-forward :xyz))
                 distance))
         (before (+ rel (- (swizzle camera-position :xyz)
                           (swizzle previous-position :xyz))))
         (behind (dot before (swizzle previous-forward :xyz)))
         (then (vec2 (/ (dot before (swizzle previous-right :xyz)) behind)
                     (/ (dot before (swizzle previous-up :xyz)) behind)))
         (then-uv (ndc-uv then))
         (inside (* (step 0.0 (swizzle then-uv :x))
                    (step (swizzle then-uv :x) 1.0)
                    (step 0.0 (swizzle then-uv :y))
                    (step (swizzle then-uv :y) 1.0)
                    (step 0.0 behind)))
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
         ;; Fast-moving pixels trust their history less, as in Luft.
         (travel (- then-uv (ndc-uv ndc)))
         (speed (clamp (* (sqrt (dot travel travel)) 48.0) 0.0 1.0))
         (weight (max (- 1.0 (* (- 1.0 (swizzle temporal-blend :x))
                                (- 1.0 (* speed 0.35))))
                      (- 1.0 inside))))
    (set-output color (vec4 (mix remembered fresh weight) 1.0))))

(define-shader-program resolve
  :vertex resolve-vertex
  :fragment resolve-fragment)

(define-shader tonemap-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (uv :vec2 :location 0)))
  (let* ((corner (fullscreen-corner (float vertex-index))))
    (set-output clip-position
                (vec4 (swizzle corner :x) (* -1.0 (swizzle corner :y))
                      0.0 1.0))
    (set-output uv (vec2 (+ (* (swizzle corner :x) 0.5) 0.5)
                         (- 0.5 (* (swizzle corner :y) 0.5))))))

;;; Narkowicz's ACES fit, then the display's gamma.
(define-shader tonemap-fragment
    (:stage :fragment
     :inputs ((uv :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((scene :texture-2d :binding 0)
                 (linear-clamp :sampler :binding 0)))
  (let* ((x (* (swizzle (sample scene linear-clamp uv) :rgb) 0.8))
         (mapped (clamp (/ (* x (+ (* x 2.51) (vec3 0.03 0.03 0.03)))
                           (+ (* x (+ (* x 2.43) (vec3 0.59 0.59 0.59)))
                              (vec3 0.14 0.14 0.14)))
                        (vec3 0.0 0.0 0.0) (vec3 1.0 1.0 1.0))))
    (set-output color
                (vec4 (vec3 (expt (swizzle mapped :x) 0.4545454)
                            (expt (swizzle mapped :y) 0.4545454)
                            (expt (swizzle mapped :z) 0.4545454))
                      1.0))))

(define-shader-program tonemap
  :vertex tonemap-vertex
  :fragment tonemap-fragment)

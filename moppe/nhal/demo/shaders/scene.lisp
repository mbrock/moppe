;;; The NHAL demo scene's shaders in Luv's mathematical shader language.
;;; luv-shaderc lowers each program to MSL for Metal 4 and HLSL for
;;; Direct3D 12, with the reflection NHAL builds pipelines from
;;; (docs/nhal.md).
;;;
;;; Every program reads the frame block at buffer binding 0.  The vertex
;;; stages pull from a storage buffer at binding 1 by vertex and instance
;;; index; there are no vertex buffers.  Clip positions are written y-down,
;;; the language's convention, and each lowering turns them y-up.

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
      (terrain :vec4))))        ; cell, samples per side, x0, z0

;;; Reversed-Z with an infinite far plane: depth is near / view distance.
(define-shader-function project-relative (rel right up forward near)
  (vec4 (dot rel right)
        (* -1.0 (dot rel up))
        near
        (dot rel forward)))

(define-shader-function sky-color (ray horizon zenith sun-direction sun-color)
  (let* ((up (clamp (swizzle ray :y) 0.0 1.0))
         (base (mix horizon zenith (expt up 0.6)))
         (sun (max (dot ray sun-direction) 0.0))
         (glow (+ (* (expt sun 900.0) 6.0) (* (expt sun 12.0) 0.08))))
    (+ base (* sun-color glow))))

(define-shader-function shade (albedo normal world camera sun-direction
                              sun-color zenith horizon density)
  (let* ((diffuse (max (dot normal sun-direction) 0.0))
         (ambient (* (mix (* horizon 0.35) zenith
                          (+ (* (swizzle normal :y) 0.5) 0.5))
                     0.55))
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
(define-shader terrain-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (albedo :vec3 :location 2))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (samples :storage-buffer :binding 1 :element :vec4)))
  (let* ((sample (buffer-element samples vertex-index))
         (side (swizzle terrain :y))
         (index (float vertex-index))
         (row (floor (/ index side)))
         (column (- index (* row side)))
         (position (vec3 (+ (swizzle terrain :z)
                            (* column (swizzle terrain :x)))
                         (swizzle sample :x)
                         (+ (swizzle terrain :w)
                            (* row (swizzle terrain :x))))))
    (set-output clip-position
                (project-relative (- position (swizzle camera-position :xyz))
                                  (swizzle camera-right :xyz)
                                  (swizzle camera-up :xyz)
                                  (swizzle camera-forward :xyz)
                                  (swizzle camera-forward :w)))
    (set-output world position)
    (set-output surface-normal (swizzle sample :yzw))
    (set-output albedo (vec3 0.0 0.0 0.0))))

(define-shader terrain-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (albedo :vec3 :location 2))
     :outputs ((color :vec4 :location 0))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)))
  (let* ((normal (normalize surface-normal))
         (ground (swizzle world :xz))
         (patch (+ (* (lattice (* ground (/ 1.0 9.0))) 0.5)
                   (* (lattice (* ground (/ 1.0 2.3))) 0.5)))
         (grass (mix (vec3 0.10 0.16 0.05) (vec3 0.20 0.22 0.09) patch))
         (rock (* (vec3 0.27 0.25 0.23) (+ 0.8 (* 0.4 patch))))
         (steep (smoothstep 0.78 0.62 (swizzle normal :y))))
    (set-output color
                (vec4 (shade (mix grass rock steep) normal world
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

;;; -- trees --------------------------------------------------------------

;;; An instance is two lanes: (root x, y, z, height) and (trunk radius,
;;; crown radius, crown base as a fraction of height, tint).  Vertices 0-17
;;; are the trunk's two rings of nine; then three crown tiers of a nine-
;;; vertex base ring and an apex.
(define-shader trees-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (albedo :vec3 :location 2))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)
                 (instances :storage-buffer :binding 1 :element :vec4)))
  (let* ((root (buffer-element instances (* instance-index (uint 2.0))))
         (shape (buffer-element instances
                                (+ (* instance-index (uint 2.0)) (uint 1.0))))
         (height (swizzle root :w))
         (crown-base (* (swizzle shape :z) height))
         (index (float vertex-index))
         (trunk (if (< index 17.5) 1.0 0.0))
         ;; The trunk: ring 0 at the root, ring 1 into the crown.
         (ring (floor (/ index 9.0)))
         (trunk-angle (* (- index (* ring 9.0)) 0.7853982))
         (trunk-radius (* (swizzle shape :x) (- 1.0 (* ring 0.4))))
         (trunk-y (if (< ring 0.5) -0.5 (+ crown-base 0.5)))
         ;; The crown: tier k of three, each a ring and an apex.
         (k (- index 18.0))
         (tier (floor (/ k 10.0)))
         (corner (- k (* tier 10.0)))
         (apex (if (> corner 8.5) 1.0 0.0))
         (crown (- height crown-base))
         (base-y (+ crown-base (* crown (* tier 0.27))))
         (apex-y (+ base-y (* crown 0.48)))
         (crown-radius (* (swizzle shape :y) (- 1.0 (* tier 0.27))))
         (crown-angle (+ (* corner 0.7853982) (* tier 0.4)
                         (* (swizzle shape :w) 6.28)))
         (angle (mix crown-angle trunk-angle trunk))
         (around (vec2 (cos angle) (sin angle)))
         (rise (- apex-y base-y))
         (crown-local (mix (vec3 (* (swizzle around :x) crown-radius)
                                 base-y
                                 (* (swizzle around :y) crown-radius))
                           (vec3 0.0 apex-y 0.0)
                           apex))
         (crown-normal (mix (normalize (vec3 (* (swizzle around :x) rise)
                                             crown-radius
                                             (* (swizzle around :y) rise)))
                            (vec3 0.0 1.0 0.0)
                            apex))
         (trunk-local (vec3 (* (swizzle around :x) trunk-radius)
                            trunk-y
                            (* (swizzle around :y) trunk-radius)))
         (local (mix crown-local trunk-local trunk))
         (normal (mix crown-normal
                      (vec3 (swizzle around :x) 0.0 (swizzle around :y))
                      trunk))
         (needles (mix (vec3 0.035 0.07 0.04) (vec3 0.06 0.10 0.045)
                       (swizzle shape :w)))
         (position (+ (swizzle root :xyz) local)))
    (set-output clip-position
                (project-relative (- position (swizzle camera-position :xyz))
                                  (swizzle camera-right :xyz)
                                  (swizzle camera-up :xyz)
                                  (swizzle camera-forward :xyz)
                                  (swizzle camera-forward :w)))
    (set-output world position)
    (set-output surface-normal normal)
    (set-output albedo (mix needles (vec3 0.13 0.09 0.06) trunk))))

(define-shader trees-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (albedo :vec3 :location 2))
     :outputs ((color :vec4 :location 0))
     :resources ((frame-state :uniform-block :binding 0
                  :members #.*frame-state*)))
  (set-output color
              (vec4 (shade albedo (normalize surface-normal) world
                           (swizzle camera-position :xyz)
                           (swizzle sun-direction :xyz)
                           (swizzle sun-color :xyz)
                           (swizzle sky-zenith :xyz)
                           (swizzle sky-horizon :xyz)
                           (swizzle sky-horizon :w))
                    1.0)))

(define-shader-program trees
  :vertex trees-vertex
  :fragment trees-fragment)

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
  (let* ((x (* (swizzle (sample scene linear-clamp uv) :rgb) 0.9))
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

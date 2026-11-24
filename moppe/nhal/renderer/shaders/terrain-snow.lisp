;;; Terrain snow, from its climatic coverage to displaced tracks, drift
;;; normals and lighting. WORLD composes it with ground, roads and rain.
;;; The terrain vertices and road ribbons use the same snow model.

;;; How much snow the broad plane holds; patch noise only breaks its edge.
;;; Prevailing maritime westerlies pack extra drift onto cool NNE-E leeward
;;; slopes and into wind-sheltered concave basins (cirque bowls and glacial
;;; snow valleys).
(define-shader-function snow-reach (climate rise support lee shelter wander)
  (* (smoothstep (swizzle climate :x) 0.79
                 (+ rise (* (swizzle climate :y) lee)
                    (* (swizzle climate :z) shelter)
                    (* (swizzle climate :w) wander)))
     (smoothstep 0.55 0.78 support)))

(define-shader-function settled-powder (climate rise normal sheet)
  (let* ((lee (+ (* -0.80 (swizzle normal :z))
                 (* 0.60 (swizzle normal :x)))))
    (* (snow-reach climate rise (swizzle sheet :y) lee 0.0 0.0)
       (smoothstep 0.48 0.53 (swizzle sheet :x))
       (- 1.0 (smoothstep 0.74 0.87 (swizzle sheet :z)))
       (smoothstep 0.55 0.83 (swizzle normal :y)))))

;;; Low lenticular pillows stretched downwind. Height in metres and its
;;; two slopes: the same surface for the silhouette and the lighting.
(define-shader-function drift-profile (xz)
  (let* ((a (dot xz (vec2 0.86 0.32)))
         (b (dot xz (vec2 -0.14 0.31)))
         (c (dot xz (vec2 0.18 -0.09)))
         (phase (+ a (* 1.65 (sin b))))
         (crest (+ 0.50 (* 0.34 (sin phase)) (* 0.16 (sin c))))
         (slope (+ (* 0.34 (cos phase)
                      (+ (vec2 0.86 0.32) (* 1.65 (cos b) (vec2 -0.14 0.31))))
                   (* 0.16 (cos c) (vec2 0.18 -0.09)))))
    (vec3 (+ 0.035 (* 0.14 crest crest)) (* 0.28 crest slope))))

(define-shader-abstraction powder-height (xz)
  `(let* ((uv (* (- ,xz (swizzle snow-window :xy)) (swizzle snow-window :z)))
          (edge (abs (- uv (vec2 0.5 0.5))))
          (span (max (swizzle edge :x) (swizzle edge :y)))
          ;; Feather smoothly to zero across the outer 8% of the window so
          ;; tracks fade naturally instead of snapping off or spiking normals.
          (fade (- 1.0 (smoothstep 0.415 0.492 span))))
     (only-where (and (> (swizzle snow-window :z) 0.0) (> fade 0.0)) 0.0
       (* (- (* (swizzle (sample-level snow-pack linear-repeat uv 0.0) :x)
                 2.007874) 1.007874) (swizzle snow-window :w) fade))))

;;; Unmasked snow coverage. The draw's upper elevation can exclude it
;;; even with full support, shelter and the most favourable noise.
(define-shader-function snow-coverage (climate normal rise support shelter
                                       drift edge)
  (let* ((lee (+ (* -0.80 (swizzle normal :z)) (* 0.60 (swizzle normal :x))))
         (reach (snow-reach climate rise support lee shelter (- drift 0.5))))
    ;; Detail breaks the edge of an existing snowfield. A draw below
    ;; the climatic reach can omit this entire layer.
    (if (> reach 0.0)
        (smoothstep 0.29 0.68 (+ reach (* 0.52 edge)))
        0.0)))

(define-shader-function snow-tone (world patch fleck)
  (let* ((phase (+ (* 1.35 (swizzle world :x)) (* 0.48 (swizzle world :z))
                   (* 6.0 patch) (* 0.8 fleck)))
         (tone (+ 0.48 (* 0.26 patch) (* 0.14 (sin phase))
                  (* 0.12 (- fleck 0.5)))))
    (vec2 (clamp tone 0.0 1.0) (* 0.045 (cos phase)))))

;;; World-fixed micro-crystals of ice flash like pinpoint diamonds in direct
;;; sun. Filter their radius in cell units and fade cleanly with pixel
;;; footprint so distant snow never blooms into stretched white dots.
(define-shader-function crystal-glint (space pixel normal halfway)
  (let* ((cell (floor space))
         (jitter (vec2 (hash12 cell) (hash12 (+ cell (vec2 23.0 41.0)))))
         (offset (- (fract space) (+ (vec2 0.25 0.25) (* jitter 0.50))))
         (width (min 0.14 (+ 0.045 (* 0.45 pixel))))
         (facet-distance (+ (abs (swizzle offset :x)) (abs (swizzle offset :y))))
         (spot (- 1.0 (smoothstep 0.0 width facet-distance)))
         (tilt (- (* 2.0 jitter) (vec2 1.0 1.0)))
         (facet (normalize (+ (* normal (max 0.08 (- 1.0 (dot tilt tilt))))
                              (vec3 (swizzle tilt :x) 0.0 (swizzle tilt :y))))))
    (* spot (/ 0.0064 (* width width)) (- 1.0 (smoothstep 0.14 0.42 pixel))
       (smoothstep 0.94 0.997 (dot facet halfway)))))

;;; Wind-carved alpine snow height (sastrugi) for directional self-shadowing:
;;; elongated wind-packed snow ridges and medium wind-scoured snow dunes.
(define-shader-function sastrugi-height (xz)
  (let* ((wx (vec2 0.86 0.51))
         (wz (vec2 -0.51 0.86))
         (u (dot xz wx))
         (v (dot xz wz))
         (n-broad (value-noise (vec2 (* u 0.085) (* v 0.27))))
         (ridge-b (- 1.0 (abs (- (* 2.0 n-broad) 1.0))))
         (n-mid (value-noise (vec2 (+ (* u 0.24) (* 1.1 n-broad))
                                   (+ (* v 0.82) 7.3))))
         (ridge-m (- 1.0 (abs (- (* 2.0 n-mid) 1.0)))))
    (+ (* 0.54 (* ridge-b ridge-b))
       (* 0.24 (* ridge-m ridge-m)))))

;;; Wind-carved alpine snow relief (sastrugi): elongated wind-packed snow
;;; ridges, medium snow-dune flutes, frost-crust ripples, and close granular
;;; powder crunch, each faded smoothly with pixel footprint.
(define-shader-function sastrugi-slope (xz footprint)
  (let* ((wx (vec2 0.86 0.51))
         (wz (vec2 -0.51 0.86))
         (u (dot xz wx))
         (v (dot xz wz))
         ;; Broad wind-sculpted sastrugi pillows (~8-14 m long, 3 m across).
         (g-broad (value-noise-gradient (vec2 (* u 0.085) (* v 0.27))))
         (f-broad (- 1.0 (smoothstep 1.6 4.4 footprint)))
         ;; Medium asymmetrical snow-drift flutes (~3 m long, 1 m across).
         (g-mid (value-noise-gradient (vec2 (+ (* u 0.24) (* 1.1 (swizzle g-broad :x)))
                                            (+ (* v 0.82) 7.3))))
         (f-mid (- 1.0 (smoothstep 0.35 1.20 footprint)))
         ;; Close frost-crust ripples (~0.35 m).
         (g-fine (value-noise-gradient (vec2 (+ (* u 0.95) 13.7)
                                             (+ (* v 2.90) 4.1))))
         (f-fine (- 1.0 (smoothstep 0.08 0.28 footprint)))
         ;; Foreground granular powder snow crunch (~6-14 cm).
         (g-crunch (value-noise-gradient (* xz 8.5)))
         (f-crunch (- 1.0 (smoothstep 0.025 0.10 footprint)))
         (du (+ (* (swizzle g-broad :y) 0.085 0.56 f-broad)
                (* (swizzle g-mid :y) 0.24 0.30 f-mid)
                (* (swizzle g-fine :y) 0.95 0.068 f-fine)))
         (dv (+ (* (swizzle g-broad :z) 0.27 0.56 f-broad)
                (* (swizzle g-mid :z) 0.82 0.30 f-mid)
                (* (swizzle g-fine :z) 2.90 0.068 f-fine))))
    (+ (* wx du) (* wz dv)
       (* (swizzle g-crunch :yz) 0.18 f-crunch))))

;;; The full-resolution slopes of wind-carved sastrugi drifts, snow
;;; flutes, and pressed ski/belt trenches (with crisp raised berms).
(define-shader-abstraction snow-detail-slope (xz footprint snow-share powder packed)
  `(if (> ,snow-share 0.0)
     (let* ((sastrugi
              (only-where (> ,snow-share 0.01) (vec2 0.0 0.0)
                (* (sastrugi-slope ,xz ,footprint) ,snow-share
                   (- 1.0 (* 0.72 ,packed))))))
       (+ (* ,snow-share (swizzle (drift-profile ,xz) :yz))
          sastrugi
          (* (vec2 1.0 0.36) (swizzle ,powder :y)
             (- 1.0 (smoothstep 0.15 1.0 ,footprint))
             (- 1.0 ,packed))
          (* ,snow-share 14.5
             (- 1.0 (smoothstep 0.14 0.50 ,footprint))
             (vec2 (- (powder-height (+ ,xz (vec2 0.04 0.0)))
                      (powder-height (- ,xz (vec2 0.04 0.0))))
                   (- (powder-height (+ ,xz (vec2 0.0 0.04)))
                      (powder-height (- ,xz (vec2 0.0 0.04))))))))
     (vec2 0.0 0.0)))

;;; Two-tap directional sastrugi self-shadowing along the sun vector:
;;; upwind snow ridges cast crisp cool-blue mini-shadows across troughs.
(define-shader-function snow-self-shadow (xz footprint snow-share sun packed)
  (only-where (and (> snow-share 0.05) (< footprint 2.0)
                 (> (swizzle sun :y) 0.04)) 1.0
    (let* ((sun-xz (swizzle sun :xz))
           (sun-horiz (max (sqrt (dot sun-xz sun-xz)) 0.001))
           (sun-dir (/ sun-xz sun-horiz))
           (slope-tan (/ (swizzle sun :y) sun-horiz))
           (h0 (sastrugi-height xz))
           (h1 (sastrugi-height (+ xz (* sun-dir 0.55))))
           (h2 (sastrugi-height (+ xz (* sun-dir 1.40))))
           (occ1 (clamp (* (- (- h1 h0) (* 0.55 slope-tan 0.48)) 3.6)
                        0.0 1.0))
           (occ2 (clamp (* (- (- h2 h0) (* 1.40 slope-tan 0.48)) 2.8)
                        0.0 1.0))
           (occ (* (max occ1 occ2) (- 1.0 (* 0.72 packed))
                   (- 1.0 (smoothstep 0.6 2.0 footprint)))))
      (- 1.0 (* 0.72 occ snow-share)))))

;;; The contribution of individual facets vanishes as the footprint grows.
(define-shader-function snow-flash (xz footprint snow-share packed direct
                                    overcast shaded sun to-eye)
  (let* ((crystals (* snow-share (- 1.0 packed) direct
                      (- 1.0 overcast)
                      (smoothstep 0.0 0.3 (dot shaded sun)))))
    (only-where (and (> crystals 0.01) (< footprint 0.07)) 0.0
      (let* ((halfway (normalize (+ sun to-eye))))
        (* crystals (+ 0.85 (* 1.6 (- 1.0 (smoothstep 0.1 0.7
                                             (dot shaded to-eye)))))
           (+ (crystal-glint (* xz 22.0) (* footprint 22.0)
                             shaded halfway)
              (* 0.80 (crystal-glint (* xz 46.0) (* footprint 46.0)
                                     shaded halfway))))))))

;;; Ambient tint and direct diffuse response of sunlit powder and ski berms.
(define-shader-function snow-light (snow-share packed press shaded sun to-eye
                                   lambert-base direct overcast)
  (let* ((snow-wrap (clamp (/ (+ (dot shaded sun) 0.14) 1.14) 0.0 1.0))
         (lambert (mix lambert-base (max (* 0.85 lambert-base) (* 0.95 snow-wrap))
                       (* 0.35 snow-share)))
         (snow-shadow (* snow-share (- 1.0 (* 0.85 lambert direct))
                         (- 1.0 (* 0.75 overcast))))
         (snow-sky-tint (mix (vec3 1.0 1.0 1.0) (vec3 0.74 0.95 1.22)
                             snow-shadow))
         (snow-bounce (+ 1.0 (* 0.22 snow-share)))
         (snow-sun-gain (+ 0.95 (* 0.78 snow-share)))
         (berm-lift (* snow-share (smoothstep 0.005 0.042 press) 0.34 direct))
         (powder-sheen (* snow-share (- 1.0 packed) direct
                          (expt (max (- 1.0 (max (dot shaded to-eye) 0.0)) 0.0)
                                3.0)
                          (+ 0.30 (* 0.70 (max (dot (- (vec3 0.0 0.0 0.0) to-eye)
                                                    sun) 0.0)))
                          0.34)))
    (vec4 (* snow-sky-tint snow-bounce)
          (+ (* snow-sun-gain lambert direct) powder-sheen berm-lift))))

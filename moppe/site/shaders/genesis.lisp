;;; The website's genesis view (docs/site.md): the land as it grows, drawn
;;; in four looks the frame block weighs against each other -- the map
;;; sheet (paper tints, hill shade from the north-west, contours, the sea
;;; by its depth), a clay relief model in the sun, the lattice as a wire
;;; drawing, and the land itself (turf, birch, rock, snow, water under a
;;; sky with clouds whose shadows cross it).  luv-shaderc lowers it to WGSL
;;; for the page and SPIR-V for the native capture tool.
;;;
;;; The terrain pulls its vertices from a storage buffer of the lattice's
;;; samples, each the height before and after the step being shown; it
;;; mixes them, so the land moves between steps rather than jumping.  The
;;; lattice is periodic, so its tiles meet seamlessly and the view can be
;;; surrounded by them.  Colours are the display's own: there is no HDR
;;; target and no tonemap.  Clip positions are written y-down and each
;;; lowering turns them y-up.

(eval-when (:compile-toplevel :load-toplevel :execute)
  (defparameter *genesis-frame*
    '((eye :vec4)               ; camera position; w: seconds
      (right :vec4)             ; right / tan(fov x / 2); w: tan
      (up :vec4)                ; up / tan(fov y / 2); w: tan
      (forward :vec4)           ; w: near plane
      (sun :vec4)               ; toward the sun
      (grid :vec4)              ; cell, samples per side, period, tiles
      (look :vec4)              ; map, relief, wire, land
      (water :vec4)             ; sea level, contour interval, step mix,
                                ; height exaggeration
      (paper :vec4)             ; the page's ground
      (zenith :vec4)
      (horizon :vec4))))        ; w: fog density per metre

;;; Reversed-Z with an infinite far plane: depth is near / view distance.
(define-shader-function project (rel right up forward near)
  (let* ((depth (dot rel forward)))
    (vec4 (dot rel right) (* -1.0 (dot rel up)) near depth)))

(define-shader-function hash (point)
  (fract (* (sin (dot point (vec2 127.1 311.7))) 43758.5453)))

(define-shader-function value-noise (point)
  (let* ((cell (floor point))
         (f (fract point))
         (s (* f (* f (- (vec2 3.0 3.0) (* f 2.0)))))
         (a (hash cell))
         (b (hash (+ cell (vec2 1.0 0.0))))
         (c (hash (+ cell (vec2 0.0 1.0))))
         (d (hash (+ cell (vec2 1.0 1.0)))))
    (mix (mix a b (swizzle s :x)) (mix c d (swizzle s :x)) (swizzle s :y))))

(define-shader-function fbm (point)
  (+ (* 0.5 (value-noise point))
     (* 0.25 (value-noise (+ (* point 2.03) (vec2 17.1 -9.7))))
     (* 0.125 (value-noise (+ (* point 4.01) (vec2 -3.3 41.9))))
     (* 0.0625 (value-noise (+ (* point 8.07) (vec2 23.6 5.2))))))

;;; How much of a line a field's level crosses here: 1 on it, 0 a pixel or
;;; so off, whatever the field's rate of change across the screen.  Where
;;; the lines crowd closer than a few pixels they fade, as a sheet leaves
;;; out contours on a cliff.
(define-shader-function line (value width)
  (let* ((rate (max (+ (abs (derivative-x value)) (abs (derivative-y value)))
                    0.0001))
         (off (/ (abs (- (fract (+ value 0.5)) 0.5)) rate)))
    (* (- 1.0 (smoothstep (* width 0.5) (+ (* width 0.5) 1.0) off))
       (- 1.0 (smoothstep 0.18 0.4 rate)))))

(define-shader-function sky-color (ray zenith horizon sun seconds)
  (let* ((rise (clamp (swizzle ray :y) 0.0 1.0))
         (base (mix horizon zenith (expt rise 0.55)))
         (toward (max (dot ray sun) 0.0))
         (glow (+ (* (expt toward 600.0) 3.0) (* (expt toward 8.0) 0.12)))
         (plane (/ (swizzle ray :xz) (max (swizzle ray :y) 0.04)))
         (cloud (* (smoothstep 0.5 0.78
                               (fbm (+ (* plane 1.4)
                                       (vec2 (* seconds 0.004) 0.0))))
                   (smoothstep 0.0 0.18 (swizzle ray :y)))))
    (mix (+ base (* (vec3 1.0 0.93 0.8) glow))
         (vec3 0.97 0.96 0.94)
         (* cloud 0.85))))

;;; -- terrain ------------------------------------------------------------

;;; A lattice sample by column and row, wrapped onto the periodic lattice.
(define-shader-function wrap (value side)
  (- value (* side (floor (/ value side)))))

(define-shader-abstraction lattice-height (samples column row side mix
                                           exaggeration)
  `(let* ((sample (buffer-element
                   ,samples
                   (uint (+ (* (wrap ,row ,side) ,side) (wrap ,column ,side))))))
     (* (mix (swizzle sample :x) (swizzle sample :y) ,mix) ,exaggeration)))

(define-shader genesis-terrain-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index)
              (instance-index :uint :built-in :instance-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (world :vec3 :location 0)
               (surface-normal :vec3 :location 1)
               (ground :vec2 :location 2))
     :resources ((frame :uniform-block :binding 0
                  :members #.*genesis-frame*)
                 (samples :storage-buffer :binding 1 :element :vec4)))
  (let* ((cell (swizzle grid :x))
         (side (swizzle grid :y))
         (period (swizzle grid :z))
         (tiles (swizzle grid :w))
         (across (+ side 1.0))
         (index (float vertex-index))
         (row (floor (/ index across)))
         (column (- index (* row across)))
         (instance (float instance-index))
         (tile-row (floor (/ instance tiles)))
         (tile-column (- instance (* tile-row tiles)))
         (half (floor (/ tiles 2.0)))
         (mixing (swizzle water :z))
         (scale (swizzle water :w))
         (height (lattice-height samples column row side mixing scale))
         (east (lattice-height samples (+ column 1.0) row side mixing scale))
         (west (lattice-height samples (- column 1.0) row side mixing scale))
         (south (lattice-height samples column (+ row 1.0) side mixing scale))
         (north (lattice-height samples column (- row 1.0) side mixing scale))
         (normal (normalize (vec3 (- west east) (* 2.0 cell) (- north south))))
         (sea (* (swizzle water :x) scale))
         (position (vec3 (+ (* column cell) (* (- tile-column half) period))
                         (max height sea)
                         (+ (* row cell) (* (- tile-row half) period)))))
    (set-output clip-position
                (project (- position (swizzle eye :xyz))
                         (swizzle right :xyz)
                         (swizzle up :xyz)
                         (swizzle forward :xyz)
                         (swizzle forward :w)))
    (set-output world position)
    (set-output surface-normal normal)
    ;; The height unexaggerated, and how deep the sea stands over it.
    (set-output ground (vec2 (/ height scale) (/ (- sea height) scale)))))

;;; The map sheet's look: the inks of moppe/game/relief_map.cc.
(define-shader-function map-look (above depth normal contour)
  (let* ((land (mix (mix (mix (vec3 0.955 0.930 0.800)
                              (vec3 0.925 0.870 0.730)
                              (smoothstep 0.0 90.0 above))
                         (vec3 0.900 0.860 0.790)
                         (smoothstep 90.0 200.0 above))
                    (vec3 0.965 0.960 0.945)
                    (smoothstep 200.0 320.0 above)))
         ;; Lit from the north-west and above, as a sheet's hill shade.
         (lit (dot normal (normalize (vec3 -0.55 0.75 -0.55))))
         (shade (- 0.75 lit))
         (shaded (if (> shade 0.0)
                     (mix land (vec3 0.360 0.340 0.430) (* shade 0.7))
                     (mix land (vec3 1.0 0.985 0.94) (* shade -1.2))))
         (minor (line (/ above contour) 1.0))
         (major (line (/ above (* contour 5.0)) 1.6))
         (inked (mix shaded (vec3 0.560 0.360 0.200)
                     (max (* minor 0.45) (* major 0.75))))
         (sea (mix (vec3 0.730 0.865 0.940) (vec3 0.520 0.720 0.880)
                   (smoothstep 0.0 120.0 depth)))
         (shore (line (/ depth 1000.0) 1.4)))
    (if (> depth 0.0)
        (mix sea (vec3 0.180 0.400 0.660) (* shore 0.8))
        inked)))

;;; A clay model in the sun, its sea a pale glaze.
(define-shader-function relief-look (depth normal sun)
  (let* ((lit (max (dot normal sun) 0.0))
         (sky (+ 0.45 (* 0.25 (swizzle normal :y))))
         (clay (* (vec3 0.93 0.91 0.87) (+ (* lit 0.62) sky))))
    (if (> depth 0.0)
        (* (vec3 0.80 0.86 0.90) (+ 0.75 (* 0.25 lit)))
        clay)))

;;; The land: turf and birch on the gentle ground, rock on the steep,
;;; snow on the highest level ground, and water by its depth with the sky
;;; in it; the clouds' shadows drift across.
(define-shader-function land-look (world above depth normal sun zenith horizon
                                   eye seconds)
  (let* ((place (swizzle world :xz))
         (patch (fbm (* place 0.004)))
         (grain (value-noise (* place 0.05)))
         (flat (swizzle normal :y))
         (turf (mix (vec3 0.33 0.42 0.15) (vec3 0.50 0.50 0.22) grain))
         (birch (mix (vec3 0.66 0.50 0.12) (vec3 0.82 0.62 0.20) grain))
         (spruce (vec3 0.12 0.21 0.10))
         (wood (mix spruce birch (smoothstep 0.45 0.7 patch)))
         (cover (mix turf wood (* (smoothstep 0.38 0.55 patch)
                                  (smoothstep 0.80 0.90 flat)
                                  (- 1.0 (smoothstep 150.0 230.0 above)))))
         (rock (* (vec3 0.42 0.40 0.37) (+ 0.8 (* 0.4 grain))))
         (ground (mix cover rock (smoothstep 0.82 0.66 flat)))
         (snowy (* (smoothstep 230.0 290.0 (+ above (* grain 30.0)))
                   (smoothstep 0.7 0.85 flat)))
         (albedo (mix ground (vec3 0.92 0.93 0.95) snowy))
         (shadow (smoothstep 0.45 0.7
                             (fbm (+ (* place 0.0012)
                                     (vec2 (* seconds 0.012)
                                           (* seconds 0.004))))))
         (lit (* (max (dot normal sun) 0.0) (- 1.0 (* shadow 0.55))))
         (ambient (* (mix horizon zenith (+ 0.5 (* 0.5 flat))) 0.42))
         (earth (* albedo (+ (* (vec3 1.45 1.32 1.08) lit) ambient)))
         (view (normalize (- world eye)))
         (fresnel (+ 0.04 (* 0.6 (expt (- 1.0 (abs (swizzle view :y))) 5.0))))
         (deep (mix (vec3 0.16 0.30 0.34) (vec3 0.05 0.12 0.20)
                    (smoothstep 0.0 60.0 depth)))
         (sea (mix (* deep (+ 0.6 (* 0.4 lit)))
                   (mix horizon zenith 0.3) fresnel)))
    (if (> depth 0.0) sea earth)))

(define-shader genesis-terrain-fragment
    (:stage :fragment
     :inputs ((world :vec3 :location 0)
              (surface-normal :vec3 :location 1)
              (ground :vec2 :location 2))
     :outputs ((color :vec4 :location 0))
     :resources ((frame :uniform-block :binding 0
                  :members #.*genesis-frame*)))
  (let* ((normal (normalize surface-normal))
         (height (swizzle ground :x))
         (depth (swizzle ground :y))
         (above (- height (swizzle water :x)))
         (sunward (swizzle sun :xyz))
         (camera (swizzle eye :xyz))
         (seconds (swizzle eye :w))
         (sky-top (swizzle zenith :xyz))
         (sky-low (swizzle horizon :xyz))
         (blend (+ (* (map-look above depth normal (swizzle water :y))
                      (swizzle look :x))
                   (* (relief-look depth normal sunward) (swizzle look :y))
                   (* (land-look world above depth normal sunward sky-top
                                 sky-low camera seconds)
                      (swizzle look :w))))
         ;; The lattice's own lines, every eighth sample.
         (grid-at (/ (swizzle world :xz) (* (swizzle grid :x) 8.0)))
         (wire (max (line (swizzle grid-at :x) 0.7)
                    (line (swizzle grid-at :y) 0.7)))
         (drawn (mix blend (vec3 0.29 0.25 0.20)
                     (* wire (swizzle look :z) 0.55)))
         ;; Distance fades toward the sky; the page's paper where the map is.
         (rel (- world camera))
         (distance (sqrt (dot rel rel)))
         (fog (- 1.0 (exp (* -1.0 (* distance (swizzle horizon :w))))))
         (far (mix (swizzle paper :xyz)
                   (sky-color (/ rel distance) sky-top sky-low sunward seconds)
                   (swizzle look :w))))
    (set-output color (vec4 (mix drawn far fog) 1.0))))

(define-shader-program genesis-terrain
  :vertex genesis-terrain-vertex
  :fragment genesis-terrain-fragment)

;;; -- sky ----------------------------------------------------------------

(define-shader-function fullscreen-corner (index)
  (vec2 (if (= index 1.0) 3.0 -1.0)
        (if (= index 2.0) 3.0 -1.0)))

;;; Behind the land: the page's paper while the land is a map or a model,
;;; the sky with its clouds once it is land.  It sits at the far plane,
;;; which reversed-Z clears to zero, so it fills only what nothing covers.
(define-shader genesis-sky-vertex
    (:stage :vertex
     :inputs ((vertex-index :uint :built-in :vertex-index))
     :outputs ((clip-position :vec4 :built-in :position)
               (ndc :vec2 :location 0)))
  (let* ((corner (fullscreen-corner (float vertex-index))))
    (set-output clip-position
                (vec4 (swizzle corner :x) (* -1.0 (swizzle corner :y))
                      0.0 1.0))
    (set-output ndc corner)))

(define-shader genesis-sky-fragment
    (:stage :fragment
     :inputs ((ndc :vec2 :location 0))
     :outputs ((color :vec4 :location 0))
     :resources ((frame :uniform-block :binding 0
                  :members #.*genesis-frame*)))
  (let* ((tan-x (swizzle right :w))
         (tan-y (swizzle up :w))
         (ray (normalize
               (+ (swizzle forward :xyz)
                  (* (swizzle right :xyz) (* (swizzle ndc :x) (* tan-x tan-x)))
                  (* (swizzle up :xyz) (* (swizzle ndc :y) (* tan-y tan-y))))))
         (sky (sky-color ray (swizzle zenith :xyz) (swizzle horizon :xyz)
                         (swizzle sun :xyz) (swizzle eye :w))))
    (set-output color
                (vec4 (mix (swizzle paper :xyz) sky (swizzle look :w)) 1.0))))

(define-shader-program genesis-sky
  :vertex genesis-sky-vertex
  :fragment genesis-sky-fragment)

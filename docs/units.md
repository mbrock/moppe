# Scientific quantities and units

Moppe's world is not only a picture. Terrain has horizontal extent and
elevation; water occupies area and volume; rivers have gradients and flow;
erosion moves material over time. Giving those quantities explicit units lets
different systems agree, makes parameters portable across resolutions, and
lets us compare a simulation with measurements from geomorphology, hydrology,
soil science, ecology, and atmospheric science.

This document is both a reference for quantities we already use and a unit
policy for systems we may build. It does not claim that every current process
is physically calibrated. In particular, the procedural field graph and the
particle hydraulic erosion model contain normalized and empirical quantities.
Their unit boundaries should be recorded rather than hidden.

## Conventions

Use SI internally unless there is a strong domain reason not to:

| Dimension | Preferred unit | Symbol |
| --- | --- | --- |
| length, elevation, depth | metre | m |
| area | square metre | m² |
| volume | cubic metre | m³ |
| time | second | s |
| geological time | year | yr |
| velocity | metres per second | m/s |
| acceleration | metres per second squared | m/s² |
| mass | kilogram | kg |
| density | kilograms per cubic metre | kg/m³ |
| force | newton | N = kg m/s² |
| pressure and stress | pascal | Pa = N/m² |
| energy | joule | J = N m |
| power | watt | W = J/s |
| temperature | kelvin | K |
| angle | radian | rad |

A year is convenient for landscape evolution, but it is not an SI unit. A
calculation must not silently combine seconds and years. Use a named conversion
constant and state whether `yr` means 365 days, 365.25 days, or a model year.
For geological rates, `mm/yr`, `m/kyr`, and `m/Myr` are often more readable
than very small `m/s` values.

Unit symbols are case-sensitive: `m` is metre, `s` is second, `Pa` is pascal,
and `K` is kelvin. Spell out ambiguous words in prose. In identifiers, encode
units at boundaries and for otherwise ambiguous scalars: `depth_m`,
`area_m2`, `volume_m3`, `speed_m_s`, `uplift_m_per_year`. A dimensionless
quantity may use a descriptive name such as `slope`, `porosity`, or
`saturation`, but its definition and range should still be documented.

Prefer base-unit storage and convert for display. A UI may show kilometres,
hectares, litres per second, millimetres of rain, degrees Celsius, or years,
but those presentation choices should not leak into the model.

## The terrain unit boundary

The materialized terrain carries the physical scale needed to interpret a
heightfield:

- `TerrainGrid::spacing_x` and `spacing_y` are horizontal metres between
  samples;
- `TerrainGrid::height_scale` is metres per stored height unit;
- stored height samples, flood surfaces, and sea level are normalized height
  values unless a field or identifier explicitly says `_m`;
- physical elevation is `height * height_scale` metres;
- one cell represents `spacing_x * spacing_y` square metres.

For the default random world, `WorldParams::map_size` is 5,000 m by 5,000 m
and 650 m high. Its sea level is 50 m. A 2049-sample toroidal heightmap has
2048 unique intervals per side, so its sample spacing is approximately
2.441 m. The duplicated seam is storage for rendering, not extra land area.

This distinction matters. A drop of `0.01` in a normalized heightfield is
6.5 m when the height scale is 650 m. A one-cell move is not one metre unless
the grid spacing says so. Algorithms that use differences between samples
without applying both scales are grid-space algorithms, not physical-space
algorithms.

`ScalarField` coordinates, noise frequencies, amplitudes, masks, and blend
weights currently describe a procedural recipe in a normalized domain. They
are intentionally scale-free until materialization. Frequency there means
cycles across the chosen domain, not automatically cycles per metre. If a
future geological process depends on wavelength, correlation length, or fault
width, express that parameter in metres and convert it using the domain size.

## Geometry and topography

### Position, distance, and elevation

Horizontal position and elevation are lengths in metres. It is useful to keep
the reference surface explicit: elevation may be relative to mean sea level,
a local datum, the geoid, or an arbitrary model zero. Height is a difference
between elevations; depth is normally positive downward from a stated surface.

Common derived quantities include:

- horizontal distance or **run**, `L` in m;
- vertical difference or **rise/drop**, `Delta z` in m;
- path length, m;
- contour length, shoreline length, or channel length, m;
- area, m², often displayed as hectares (`1 ha = 10,000 m²`) or km²;
- volume, m³.

Do not use a three-dimensional point-to-point distance when a law calls for
horizontal run. Waterfall slope, for example, is vertical drop divided by
horizontal distance.

### Slope, aspect, curvature, and relief

Slope is a ratio of vertical to horizontal length and is therefore
dimensionless:

```text
slope = |gradient z| = rise / run
```

It may also be reported as percent grade (`100 * rise/run`) or as an angle
(`atan(rise/run)`) in radians or degrees. These are not interchangeable. A
slope of `1.0` is a 100% grade and a 45 degree angle; it is not one degree.
Moppe's drainage and waterfall slopes are currently ratios in m/m.

Aspect is the direction of steepest descent, preferably radians in a stated
world convention. Curvature is the spatial rate of change of slope, typically
1/m. Profile curvature affects acceleration and erosion along a flow line;
plan curvature affects convergence and divergence across it. Gaussian
curvature has units 1/m².

Relief is an elevation range in metres over a stated window or basin. Surface
roughness needs a definition: it might be RMS elevation residual in metres,
RMS slope (dimensionless), or actual surface area divided by plan area
(dimensionless). The window size is part of the measurement.

Grid resolution is not a physical quantity by itself. Always report sample
count together with extent or spacing. Many derived readings, including local
slope, drainage density, and channel initiation, change with resolution.

## Hydrology

### Water state and storage

Water-surface elevation and water depth are lengths in metres. Flooded area is
m² and stored volume is m³:

```text
depth = max(water_surface_elevation - ground_elevation, 0)
volume = sum(depth * cell_area)
```

Moppe's `FloodField` stores normalized water level and depth because it shares
the terrain raster representation. `LakeCensus` converts its public
measurements to `area_m2`, depth in metres, `volume_m3`, and
`surface_level_m`. `WaterPermanence` thresholds use the same physical units.

Stage is water-surface elevation relative to a datum, in metres. Depth is
stage minus bed elevation. Freeboard is the vertical distance between water
and the top of a bank or structure. These should remain separate even when a
renderer can get by with one value.

Soil-water storage may be expressed as water volume per ground area, which
reduces to an equivalent water depth in metres or millimetres. Volumetric
water content is a dimensionless fraction of bulk soil volume occupied by
water. Saturation is a dimensionless fraction of available pore space filled.
Moppe's current moisture raster is a visual/ecological index in `[0, 1]`, not
either of these measured soil quantities.

### Rain, infiltration, evaporation, and runoff

Precipitation depth is commonly measured in millimetres. A precipitation rate
or intensity is depth per time, such as mm/h or m/s. It becomes a volume only
when multiplied by area:

```text
rainfall_volume = precipitation_depth * catchment_area
```

The same length-per-time dimension is used for infiltration rate,
evaporation, evapotranspiration, snowmelt water equivalent, hydraulic
conductivity, and runoff depth rate. Their meanings differ, so units alone do
not make them interchangeable.

The runoff coefficient is dimensionless: runoff depth divided by precipitation
depth over the same event and area. Antecedent moisture, soil, vegetation,
slope, frozen ground, and rainfall intensity all affect it.

### Drainage area, discharge, and velocity

Contributing or catchment area is m². Moppe initializes each drainage cell
with its physical cell area and accumulates that area downstream. The value is
therefore resolution-aware, unlike a count of upstream cells.

Contributing area is not discharge. Discharge is a volume flux in m³/s:

```text
Q = integral(velocity dot normal dA)
```

For a uniform cross-section, `Q = mean_velocity * cross_section_area`.
Specific discharge may mean discharge per unit channel width (m²/s), while
hydrologists also use specific runoff for discharge per catchment area
(m/s, often displayed as L/s/km²). Name the intended quantity.

Moppe currently uses contributing area as a steady-climate proxy for water
supply and channel size. That is enough to extract river topology, but it does
not model rainfall, losses, travel time, hydrographs, or conservation of water
through time. A future discharge model should introduce those quantities
explicitly rather than relabel area as flow.

Flow velocity is a vector in m/s. The current watercourse sheet stores its
horizontal `(x, z)` velocity in m/s, with zero for standing water. Speed is
the vector magnitude. Travel time is channel distance divided by an
appropriately averaged speed, in seconds.

A hydrograph is discharge as a function of time. Useful event quantities are
peak discharge (m³/s), time to peak (s or h), baseflow (m³/s), and event
volume (the time integral of discharge, m³).

### Hydraulic quantities

Hydraulic head is energy per unit weight and is reported as a length in
metres. It combines elevation head, pressure head, and velocity head:

```text
H = z + p/(rho g) + v^2/(2g)
```

Pressure `p` is Pa, fluid density `rho` is kg/m³, and gravitational
acceleration `g` is m/s². Water near ordinary terrestrial conditions is often
approximated as 1,000 kg/m³, but temperature, salinity, and sediment change it.

Hydraulic gradient is head loss per path length, dimensionless. Hydraulic
conductivity is m/s. Intrinsic permeability is m²; it is a property of the
porous medium, whereas conductivity also depends on fluid density and
viscosity. Dynamic viscosity is Pa s and kinematic viscosity is m²/s.

For open channels, hydraulic radius is cross-sectional area divided by wetted
perimeter, in metres. Froude number `Fr = v/sqrt(gD)` and Reynolds number
`Re = vL/nu` are dimensionless. Froude number distinguishes subcritical and
supercritical free-surface flow; Reynolds number compares inertia with
viscosity. The length scale `D` or `L` must be stated.

## Erosion, sediment, and geomorphology

### Rates and budgets

An erosion, deposition, incision, denudation, or uplift rate is length per
time, commonly mm/yr or m/Myr. A sediment volume rate is m³/s or m³/yr. A
sediment mass rate is kg/s or tonnes/yr. Converting volume to mass requires a
stated density and, for deposited sediment, a porosity or bulk density.

Keep three budgets distinct:

- bed elevation change, m or m/time;
- solid sediment volume, m³ or m³/time;
- sediment mass, kg or kg/time.

Bulk eroded terrain volume is not automatically equal to deposited bulk
volume because fragmentation and pore space can change. Suspended load,
bedload, dissolved load, and material leaving the modeled domain are separate
terms in a complete budget.

Moppe's physical reports use lowered or raised volume in m³ and mean or
maximum elevation change in m. The particle hydraulic report instead records
normalized height amounts. Those terms share a constant cell area and are
useful for an internal balance, but they are not yet a calibrated sediment
volume or mass.

### Stream power

A common detachment-limited landscape-evolution law is:

```text
dz/dt = U - K A^m S^n
```

`z` is elevation, `t` time, `U` uplift rate, `A` drainage area, `S` slope,
and `m` and `n` dimensionless exponents. The dimensions of erodibility `K`
depend on the exponents and on how the law defines area and discharge. With
`A` in m², `S` dimensionless, and time in years, `K` has dimensions
`m^(1 - 2m) / yr`. It is therefore unsafe to copy a numerical `K` from a
paper without copying its complete formulation and unit convention.

Moppe's analytical erosion uses `n = 1`, `time_years`, uplift in m/yr,
physical area in m², and physical path distance in m. Its `erodibility`
parameter therefore follows the model-year dimensional convention above; it
is not dimensionless despite its unsuffixed historical name. Sea level remains
normalized at the terrain API boundary.

Stream power itself can mean total power `rho g Q S` in watts, power per unit
channel length in W/m, or unit stream power per bed area in W/m². Say which
one is meant. A landscape-evolution term called "stream power" may be an
empirical proxy rather than any of these directly measured powers.

### Shear stress and sediment transport

Bed shear stress is force per area in Pa. A common depth-slope estimate is:

```text
tau = rho g R S
```

where `R` is hydraulic radius in m. Critical shear stress for entrainment is
also Pa. The dimensionless Shields parameter compares applied shear stress to
the submerged weight of a grain:

```text
theta = tau / ((rho_s - rho) g d)
```

Here sediment density `rho_s` is kg/m³ and grain diameter `d` is m. Grain
size is often reported in mm or on the dimensionless phi scale; convert it at
the boundary. Settling velocity is m/s.

Sediment concentration must specify its basis: kg/m³ for mass concentration,
m³/m³ for volumetric concentration, or a dimensionless mass fraction.
Sediment transport capacity likewise needs a basis: kg/s for a channel,
kg/(m s) per unit width, or a volumetric equivalent. An arbitrary droplet
"capacity" is not one of these until calibrated.

### Hillslopes, weathering, and mass wasting

A talus or repose threshold may be represented as slope ratio, angle, or
height difference between neighboring samples. Moppe's current thermal
erosion `talus` is a normalized sample-height difference, so its physical
meaning changes with height scale and grid spacing. A future physical form
should use a critical slope or angle and a transport coefficient with stated
dimensions.

Diffusive hillslope evolution is often written:

```text
dz/dt = D div(gradient z)
```

The diffusivity `D` is m²/time. Nonlinear models introduce a critical slope,
dimensionless, and may diverge as that slope is approached. Weathering or soil
production can be a depth rate in m/yr, sometimes dependent on soil thickness
in m.

Landslides require more than a steepness test if modeled mechanically. Cohesion
is Pa, normal and shear stress are Pa, friction angle is radians or degrees,
material density is kg/m³, pore pressure is Pa, and factor of safety is
dimensionless.

## Geology and geophysics

Rock and sediment layers have thickness in m and boundaries at elevations in
m. Bedding strike and dip, fault orientation, and joint orientation are
angles. Fault slip is m and slip rate is m/yr. Uplift and subsidence are
signed rates in m/yr; define which direction is positive.

Geological age is time before present, commonly ka, Ma, or Ga. Duration is an
elapsed time. An age of 10 Ma and a duration of 10 Myr have the same magnitude
but different meanings. In code, retain an epoch or reference time instead of
using an unlabeled float.

Useful material properties include:

| Quantity | Unit |
| --- | --- |
| grain or rock density | kg/m³ |
| porosity | dimensionless fraction |
| permeability | m² |
| Young's modulus | Pa |
| bulk and shear modulus | Pa |
| cohesion and yield strength | Pa |
| fracture toughness | Pa sqrt(m) |
| thermal conductivity | W/(m K) |
| specific heat capacity | J/(kg K) |
| thermal diffusivity | m²/s |
| heat flux | W/m² |

Gravity is an acceleration vector in m/s². Near Earth's surface its magnitude
is approximately 9.81 m/s², but a game may deliberately use another value.
Calling a tuned constant `gravity` does not make a grid-space update SI; the
position, velocity, timestep, and force law must all be dimensionally
consistent.

Isostasy, flexure, and tectonic stress introduce mass per area (kg/m²),
pressure/stress (Pa), elastic thickness (m), and flexural rigidity (N m).
Seismic velocity is m/s; acceleration recorded by a seismometer is m/s²; an
earthquake magnitude is dimensionless and logarithmic. Magnitude is not
energy, intensity, or ground acceleration.

## Soil, ecology, climate, and atmosphere

Soil depth and rooting depth are m. Bulk density is kg/m³. Organic carbon or
nutrient stores may be kg/m², while concentrations may be kg/m³, kg/kg, or
mol/m³. Always retain the basis. pH is dimensionless and logarithmic.

Vegetation models may use leaf area index (one-sided leaf area per ground
area, dimensionless), biomass in kg/m², canopy height in m, growth rate in
kg/(m² s), and fractional cover in `[0, 1]`. Habitat suitability and Moppe's
current moisture value are indices, not measurements, and should not be given
physical unit suffixes.

Air temperature is K internally; degrees Celsius are suitable for display and
temperature differences because a change of 1 C equals a change of 1 K.
Pressure is Pa, air density kg/m³, wind velocity m/s, humidity as either a
dimensionless relative humidity or a specifically defined mass ratio, and
radiative or turbulent flux W/m².

Climate normals are statistics over a stated interval. Mean annual
precipitation in mm/yr is not a rainfall event intensity in mm/h. A geomorphic
model driven by climate should distinguish averages, event distributions,
seasonality, and extremes.

## Scaling and similarity

A visually small world cannot preserve every real-world scale at once. When
compressing space or geological time, decide what the model is meant to
preserve: drainage topology, channel width relative to vehicle size, sediment
budget, characteristic slope, flood travel time, or visual legibility.

Geometric scaling alone is insufficient for dynamics. If lengths are scaled
by a factor, gravity-driven velocity and time do not generally scale by the
same factor. Dimensionless groups such as Froude, Reynolds, Shields, Peclet,
and Courant numbers describe which relationships remain similar. We need not
simulate every group faithfully, but we should know which ones a simplified
model discards.

For numerical models, record at least:

- domain extent and cell spacing in m;
- vertical scale and datum in m;
- timestep and total simulated duration;
- boundary conditions and topology;
- conservation residuals for water and sediment;
- stability constraints or dimensionless timestep criteria;
- the scale at which readings and thresholds were calibrated.

The Courant number `C = v dt / dx` is dimensionless and is central to many
advection schemes. A stable limit depends on the method. Diffusion has a
related constraint involving `D dt / dx^2`. An iteration count is not a time
unless the update defines how much physical time one iteration advances.

## Current Moppe quantity map

| Quantity | Current representation |
| --- | --- |
| world position and map extent | m |
| stored terrain height | normalized; multiply by `height_scale` for m |
| terrain sample spacing | m |
| sea level at world/game boundary | m |
| sea level in terrain analyses | normalized height |
| drainage slope | dimensionless m/m |
| contributing area | m² |
| lake area, depth, volume, surface | m², m, m³, m |
| waterfall drop and run | m |
| watercourse velocity | m/s |
| channel width, depth, bank blend | m |
| carved/lowered terrain report | m and m³ |
| analytical erosion time | yr |
| analytical uplift | m/yr |
| analytical erodibility | dimensional, dependent on area exponent |
| procedural field coordinates and noise | normalized recipe space |
| hydraulic droplet water/sediment ledger | empirical normalized units |
| thermal erosion talus | normalized neighboring-height difference |
| moisture raster | dimensionless visual/ecological index `[0, 1]` |
| frame/game animation time | s |

The table is a snapshot, not permission to perpetuate ambiguous boundaries.
When an existing normalized quantity becomes an input to another physical
system, convert it once at a named boundary and keep the downstream system in
physical units.

## Rules for new systems

1. Start each equation on paper with named quantities, units, and sign
   conventions. Check that both sides have the same dimensions.
2. Store SI values internally. Convert to normalized terrain coordinates,
   texture encodings, or display units only at explicit boundaries.
3. Put units in public identifiers where the type system does not carry them.
   Never rely on a nearby comment for two same-typed lengths in different
   units.
4. Give dimensionless values a definition, expected range, and normalization
   basis. "Strength", "amount", and "scale" are not definitions.
5. Separate state from flux and rate: water volume (m³), discharge (m³/s),
   and rainfall intensity (m/s) are different quantities.
6. Separate counts from measures: cells are not m², iterations are not s,
   and droplets are not m³.
7. Include unit conversions and resolution changes in tests. The same physical
   plane sampled at two resolutions should have the same slope and area;
   volume integrations should converge rather than scale with cell count.
8. Reports and serialized artifacts should include units in field names,
   column headers, or schema metadata. A bare CSV column named `water` will
   eventually be misread.
9. Record empirical calibration alongside the world scale, resolution,
   timestep, and climate assumptions for which it was made.
10. If a model is deliberately artistic, say so. A clearly named empirical
    control is safer and more useful than a physical-sounding parameter with
    inconsistent dimensions.

Explicit units do not force Moppe to become an Earth simulator. They let us
choose where to be physical, where to be scale-model-like, and where to be
poetic without those choices accidentally contaminating one another.

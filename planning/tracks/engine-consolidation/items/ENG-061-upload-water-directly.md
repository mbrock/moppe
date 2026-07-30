+++
id = "ENG-061"
title = "Upload typed water sheets without retained packing vectors"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
depends_on = ["ENG-060"]
order = 20
areas = ["render", "water", "bundles"]
+++
# Upload typed water sheets without retained packing vectors

## Outcome

Water elevation, wave amplitude, and horizontal velocity describe their
renderer textures directly from `terrain::WaterSheets`, matching the existing
ground-reading boundary.

## Scope

Remove `WaterPresentation`'s retained interleaved float vectors and the
backend's second flow conversion. Do not introduce Lavoir storage ownership
or change the water simulation.

## Acceptance

- The Metal backend receives one `TexturePixels` description per water
  texture and writes it once into staging memory.
- WebGPU retains its supported lower-cost ocean path.
- Water-presentation and renderer tests pass.

## Evidence

`upload_water` borrows `WaterSheets` and describes elevation/amplitude as
`RG32F` and planar velocity as `RG16F`. `MetalRenderer::upload_pixels` is the
single checked staging path for ground and water textures; the old retained
float vectors and second flow-halving vector are gone. The recording renderer
decodes the descriptions for
`water_presentation_writes_typed_sections_directly`, and the complete test
binary passes.

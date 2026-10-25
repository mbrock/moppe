// The genesis view in the website (docs/site.md): a module on the page's
// own thread that draws into its canvas through WebGPU. The page acquires
// the GPUDevice and passes it in as preinitializedWebGPUDevice; the
// geology worker's heights come in through genesis_heights, and the page's
// animation frames call genesis_frame (moppe/site/index.html).
#include <moppe/nhal/webgpu/webgpu_device.hh>
#include <moppe/site/genesis.hh>

#include <emscripten/emscripten.h>

#include <exception>
#include <iostream>
#include <memory>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace {
  // #embed cannot sit in a macro; each stage is spelled out.
  const char terrain_vertex[] = {
#embed "genesis_terrain.vertex.wgsl"
    , 0
  };
  const char terrain_fragment[] = {
#embed "genesis_terrain.fragment.wgsl"
    , 0
  };
  const char sky_vertex[] = {
#embed "genesis_sky.vertex.wgsl"
    , 0
  };
  const char sky_fragment[] = {
#embed "genesis_sky.fragment.wgsl"
    , 0
  };

  moppe::nhal::StageCode wgsl (const char* source) {
    return { .wgsl = source };
  }

  std::unique_ptr<moppe::nhal::Device> device;
  std::unique_ptr<moppe::site::Genesis> genesis;
}

#pragma clang diagnostic pop

extern "C" {
// Starts drawing into the canvas `#land`, WIDTH by HEIGHT pixels, a
// lattice LATTICE samples a side. False when WebGPU will not have it.
EMSCRIPTEN_KEEPALIVE int genesis_start (
  int width, int height, int lattice, float period_m, float sea_level_m) {
  try {
    device = moppe::nhal::create_webgpu_device (
      "#land", std::uint32_t (width), std::uint32_t (height));
    genesis = std::make_unique<moppe::site::Genesis> (
      *device,
      moppe::site::GenesisShaders { wgsl (terrain_vertex),
                                    wgsl (terrain_fragment),
                                    wgsl (sky_vertex),
                                    wgsl (sky_fragment) },
      lattice,
      period_m,
      sea_level_m);
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "genesis: " << error.what () << std::endl;
    genesis.reset ();
    device.reset ();
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE void genesis_resize (int width, int height) {
  if (device)
    device->resize_surface (std::uint32_t (width), std::uint32_t (height));
}

// The heights before and after the step shown, COUNT of each.
EMSCRIPTEN_KEEPALIVE void
genesis_heights (const float* from, const float* to, int count) {
  if (genesis)
    genesis->set_heights ({ from, std::size_t (count) },
                          { to, std::size_t (count) });
}

// Draws the view the scroll's PROGRESS puts it at, MIX of the way from
// one step's heights to the next, SECONDS into the page's life.
EMSCRIPTEN_KEEPALIVE void
genesis_frame (float progress, float mix, double seconds) {
  if (!genesis)
    return;
  const float aspect =
    float (device->surface_width ()) / float (device->surface_height ());
  if (genesis->render (
        moppe::site::genesis_choreography (progress, seconds, aspect), mix))
    device->end_frame ();
}
}

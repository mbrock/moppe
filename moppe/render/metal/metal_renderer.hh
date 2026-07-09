#ifndef MOPPE_METAL_RENDERER_HH
#define MOPPE_METAL_RENDERER_HH

#include <moppe/render/renderer.hh>

#include <string>

namespace moppe {
namespace render {
  // Creates the Metal backend attached to an MTKView (passed as
  // void* so this header stays Objective-C-free).  The view's pixel
  // format and colorspace are configured here.  metallib_path names
  // the compiled shader library inside the bundle/build tree.
  Renderer* create_metal_renderer (void* mtk_view,
				   const std::string& metallib_path);
}
}

#endif

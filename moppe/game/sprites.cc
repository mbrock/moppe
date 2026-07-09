#include <moppe/game/sprites.hh>

#include <algorithm>
#include <cmath>
#include <vector>

namespace moppe {
namespace game {
  render::TexturePtr
  make_soft_disc_texture (render::Renderer& r)
  {
    const int N = 64;
    std::vector<unsigned char> img (N * N * 4);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
	const float dx = (x + 0.5f) / N * 2 - 1;
	const float dy = (y + 0.5f) / N * 2 - 1;
	float a = std::max (0.0f,
			    1.0f - std::sqrt (dx * dx + dy * dy));
	a = a * a * (3.0f - 2.0f * a); // smooth shoulder

	unsigned char* px = &img[(y * N + x) * 4];
	px[0] = px[1] = px[2] = 255;
	px[3] = (unsigned char) (255.0f * a);
      }

    render::TextureDesc desc;
    desc.width = N;
    desc.height = N;
    desc.format = render::TextureFormat::RGBA8;
    desc.filter = render::TextureFilter::Linear;
    desc.wrap = render::TextureWrap::Clamp;
    return r.create_texture (desc, &img[0]);
  }
}
}

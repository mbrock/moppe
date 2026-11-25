"""Build the moving M5 experiment's producers and consumers from Moppe's MSL."""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--live-channel', choices=('colour', 'normals', 'wetness'),
                    help='restore a group of live calculations for image diagnosis')
args = parser.parse_args()
out = args.output
out.mkdir(parents=True, exist_ok=True)
stages = Path('build/nhal-world')

def assign(src, name, expr):
    pat = rf'^(  (?:float[234]?|GroundSurface) {name} = )[^;]*;'
    src, n = re.subn(pat, lambda m: m[1] + expr + ';', src, count=1, flags=re.M)
    assert n == 1, name
    return src

fields = 'struct Level { float4 window; };\n'
sampling = '''
float4 material_sample(array<texture2d<float>, 5> layers, sampler sam,
                       int level, float blend, float2 xz, float2 period,
                       constant Level* levels) {
  float2 uv = level == 4 ? xz / period : xz / levels[level].window.z;
  float4 fine = layers[level].sample(sam, uv, metal::level(0));
  if (blend > 0 && level < 4) {
    float2 coarse = level == 3 ? xz / period : xz / levels[level+1].window.z;
    return mix(fine, layers[level+1].sample(sam, coarse, metal::level(0)), blend);
  }
  return fine;
}
'''
lookup = '''
  float2 place = stage_in.world.xz;
  float2 period = terrain.size.xy * terrain.scale.xz;
  int level = 0;
  float blend = 0;
  for (; level < 4; ++level) {
    float2 off = abs((place - levels[level].window.xy) / levels[level].window.z - 0.5f);
    float radius = max(off.x, off.y);
    if (radius < 0.495f) {
      blend = smoothstep(0.40f, 0.495f, radius);
      break;
    }
  }
'''
for group in ('ground', 'roads'):
    road = group == 'roads'
    for program in ('terrain', 'road_ribbon'):
        src = (stages / (program + '.fragment.metal')).read_text()
        marker = re.search(r'^  float2 extent =', src, re.M)[0]
        src = src.replace('fragment ', fields + sampling + '\nfragment ', 1)
        params = ('constant Level* levels [[buffer(5)]],\n' +
                  ',\n'.join(f'    array<texture2d<float>, 5> t{i} [[texture({16+i*5})]]'
                              for i in range(3 if road else 2)))
        src = src.replace('    constant Frame& frame', params + ',\n    constant Frame& frame')
        samples = ''.join(f'  float4 m{i} = material_sample(t{i}, linear_repeat, level, blend, place, period, levels);\n'
                          for i in range(3 if road else 2))
        src = src.replace(marker, lookup + samples + marker, 1)
        if road:
            for name, expr in {
                'normal':'normalize(float3(m2.x, sqrt(max(0.0f, 1.0f-dot(m2.xy,m2.xy))), m2.y))',
                'path_ground':'m0.rgb', 'puddle':'wet * m2.w', 'powder':'m1.zw',
                'slope':'m1.xy * (1.0f-puddle)',
                'dry_rough':'mix(m0.w, 0.28f, packed)',
                'albedo':'pow(max(path_ground,float3(0)),float3(2.2f)) * mix(float3(1),float3(0.55f,0.74f,0.94f),packed) * (1-wet_2*m2.z) * (1-0.45f*puddle)',
            }.items():
                restored = {'colour': ('path_ground',), 'normals': ('normal', 'slope'),
                            'wetness': ('puddle', 'dry_rough', 'albedo')}
                if name not in restored.get(args.live_channel, ()):
                    src = assign(src, name, expr)
        else:
            src = assign(src,'surface','GroundSurface{m0.rgb, m1.xy, m1.z, m0.w}')
        (out / f'{program}-{group}.metal').write_text(src)

    src = (stages / 'terrain.fragment.metal').read_text()
    end = 'dry_rough' if road else 'surface'
    endline = re.search(rf'^  (float|GroundSurface) {end} = .*?;$', src, re.M)
    src = src[:endline.end()]
    outputs = 'struct Bake { float4 a [[color(0)]]; float4 b [[color(1)]];'
    if road: outputs += ' float4 c [[color(2)]];'
    src = src.replace('fragment TerrainFragmentOutput',outputs + ' };\nfragment Bake')
    src = src.replace('TerrainFragmentInput stage_in [[stage_in]],', '''
        float4 pixel [[position]], constant float4& win [[buffer(5)]],
        texture2d<float> heights [[texture(0)]],''')
    src = src.replace('TerrainFragmentOutput result = {};', '''
        TerrainFragmentInput stage_in = {};
        float2 period = terrain.size.xy * terrain.scale.xz;
        float2 uv = pixel.xy / win.w;
        float2 bakedXZ = win.z == 0 ? uv*period :
          win.xy + fract(uv - win.xy / win.z) * win.z;
        stage_in.field_uv = bakedXZ / period;
        float h = heights.sample(linear_repeat, stage_in.field_uv
                     + 0.5f / terrain.size.xy, metal::level(0)).x * terrain.scale.y;
        stage_in.world = float3(bakedXZ.x,h,bakedXZ.y);''')
    src = assign(src,'road_reach','1.0f')
    src = src.replace('  float3 normal =', '''  stage_in.world.y = mix(stage_in.world.y,
      road.centre - terrain.road_shape.z * min(abs(road_across), terrain.road_shape.x), road_flat);
  float3 normal =''',1)
    if road:
        src = assign(src,'wet','0.0f')
        src = assign(src,'packed','0.0f')
        src += '''
  return Bake{float4(path_ground,dry_rough),float4(slope,powder),
    float4(normal.xz,mix(mix(0.25f,0.55f,smoothstep(0.92f,0.6f,surface.rough)),
    0.4f,tread_share),paved_tread*smoothstep(0.3f,0.7f,look.w))};
}'''
    else:
        src += '\n  return Bake{float4(surface.albedo,surface.rough),float4(surface.powder,surface.relief,0)};\n}'
    (out / f'bake-{group}.metal').write_text(src)
(out / 'triangle.metal').write_text('''#include <metal_stdlib>
using namespace metal;
vertex float4 terrain_vertex(uint i [[vertex_id]]) {
  return float4(i == 1 ? 3 : -1, i == 2 ? 3 : -1, 0, 1);
}
''')

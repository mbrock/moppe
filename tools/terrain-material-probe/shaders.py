"""Derive diagnostic Metal programs from the built terrain program.

Run from the checkout root after building moppe. The producer evaluates
the same generated code as the live material up to its surface return.
"""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--nested', action='store_true',
                    help='pack four local scales into the same atlas area')
parser.add_argument('--shader', type=Path,
                    default=Path('build/nhal-world/terrain.fragment.metal'))
args = parser.parse_args()
out = args.output
out.mkdir(parents=True, exist_ok=True)
source = args.shader.read_text()
signature = 'TerrainFragmentInput stage_in [[stage_in]],'
surface = re.search(r'^  GroundSurface surface = .*?;$', source, re.M)
trodden = re.search(r'^  float3 (\w+_trodden) = .*?;$', source, re.M)
assert surface and trodden and signature in source
probe = 'constant float4& patch [[buffer(5)]],'
textures = '\n'.join(
    f'texture2d<float> tile{i} [[texture({16+i})]],' for i in range(3))
consumer = source.replace(signature, f'{signature}\n{probe}\n{textures}')
lookup = '''
  float2 nearUV = (stage_in.world.xz - patch.xy) / patch.z + 0.5f;
  float2 farUV = fract(stage_in.field_uv);
  // The half-texel inset prevents linear filtering across the two tiles.
  float2 atlasUV = all(abs(nearUV - 0.5f) < 0.499f)
    ? nearUV * float2(1, 0.5f)
    : float2(farUV.x, 0.5f + 0.5f * clamp(farUV.y, 0.00049f, 0.99951f));
'''
if args.nested:
    lookup = '''
  float2 d = stage_in.world.xz - patch.xy;
  float radius = max(abs(d.x), abs(d.y));
  float2 global = fract(stage_in.field_uv);
  float2 atlasUV = float2(global.x, 0.5f + 0.5f * clamp(global.y, 0.00049f, 0.99951f));
  if (radius < 998) {
    int ring = radius < 3.99f ? 0 : radius < 31.9f ? 1 : radius < 255.4f ? 2 : 3;
    float span = ring == 0 ? 8 : ring == 1 ? 64 : ring == 2 ? 512 : 2000;
    float2 tile = float2(ring % 2, ring / 2);
    atlasUV = (tile + 0.5f + d / span) * float2(0.5f, 0.25f);
  }
'''
(out / 'colour.metal').write_text(consumer.replace(
    trodden[0], lookup + f'  float3 {trodden[1]} = '
    'tile0.sample(linear_repeat, atlasUV, level(0)).xyz;'))
(out / 'surface.metal').write_text(consumer.replace(surface[0], lookup + '''
  float4 ar = tile1.sample(linear_repeat, atlasUV, level(0));
  float4 pr = tile2.sample(linear_repeat, atlasUV, level(0));
  GroundSurface surface = GroundSurface{ar.xyz, pr.xy, pr.z, ar.w};'''))
road = re.search(r'float4 road_found = \w+;', source)
assert road
(out / 'roadless.metal').write_text(source.replace(
    road[0], 'float4 road_found = float4(1e6f, 0, 0, -1);'))
cheap = source[:source.index('  float2 extent =')] + '''
  result.color = float4(0.25f, 0.3f, 0.2f, 1);
  result.motion = float2(0);
  return result;
}
'''
(out / 'flat.metal').write_text(cheap)

producer = source[:surface.end()]
producer = producer.replace('fragment TerrainFragmentOutput terrain_fragment',
    '''struct BakeOutput {
  float4 colour [[color(0)]];
  float4 albedo_rough [[color(1)]];
  float4 powder_relief [[color(2)]];
};
fragment BakeOutput terrain_fragment''')
producer = producer.replace(signature, f'''float4 pixel [[position]],
    {probe}
    texture2d<float> heights [[texture(0)]],''')
producer = producer.replace('TerrainFragmentOutput result = {};', '''
  TerrainFragmentInput stage_in = {};
  float2 period = terrain.size.xy * terrain.scale.xz;
  float2 uv = pixel.xy / patch.w;
  float2 xz = uv.y < 1 ? patch.xy + (uv - 0.5f) * patch.z
                          : (uv - float2(0, 1)) * period;
  stage_in.field_uv = xz / period;
  float h = heights.sample(linear_repeat, stage_in.field_uv
      + 0.5f / terrain.size.xy, level(0)).x * terrain.scale.y;
  stage_in.world = float3(xz.x, h, xz.y);''')
if args.nested:
    producer = producer.replace('  stage_in.field_uv = xz / period;', '''
  if (uv.y < 1) {
    int2 tile = int2(uv * 2);
    int ring = tile.x + 2 * tile.y;
    float span = ring == 0 ? 8 : ring == 1 ? 64 : ring == 2 ? 512 : 2000;
    xz = patch.xy + (fract(uv * 2) - 0.5f) * span;
  }
  stage_in.field_uv = xz / period;''')
producer += f'''
  return BakeOutput{{float4({trodden[1]}, 1),
                    float4(surface.albedo, surface.rough),
                    float4(surface.powder, surface.relief, 0)}};
}}
'''
(out / 'bake.metal').write_text(producer)
(out / 'triangle.metal').write_text('''#include <metal_stdlib>
using namespace metal;
vertex float4 terrain_vertex(uint id [[vertex_id]]) {
  return float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
}
''')

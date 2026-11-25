"""Build the moving M5 experiment's producers and consumers from Moppe's MSL."""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--live-channel', choices=('colour', 'normals', 'macro', 'detail', 'wetness'),
                    help='restore a group of live calculations for image diagnosis')
parser.add_argument('--filter', choices=('crown', 'axis4', 'both'),
                    help='box-filter the baked crown or integrate slopes along the pixel')
parser.add_argument('--grain', action='store_true',
                    help='cache the fine grain strength and evaluate its slope at the screen footprint')
parser.add_argument('--packed', choices=('24', '32'), help='MiB for the three main outputs')
parser.add_argument('--footprint', action='store_true',
                    help='encode level, camera range and footprint aspect in the motion target')
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
    grain = road and args.grain
    packed = road and args.packed
    channels = 4 if grain else 3 if road else 2
    for program in ('terrain', 'road_ribbon'):
        src = (stages / (program + '.fragment.metal')).read_text()
        marker = re.search(r'^  float2 extent =', src, re.M)[0]
        src = src.replace('fragment ', fields + sampling + '\nfragment ', 1)
        params = ('constant Level* levels [[buffer(5)]],\n' +
                  ',\n'.join(f'    array<texture2d<float>, 5> t{i} [[texture({16+i*5})]]'
                              for i in range(channels)))
        src = src.replace('    constant Frame& frame', params + ',\n    constant Frame& frame')
        samples = ''.join(f'  float4 m{i} = material_sample(t{i}, linear_repeat, level, blend, place, period, levels);\n'
                          for i in range(channels))
        if road and args.filter in ('axis4', 'both'):
            samples += '''
  float2 dx = dfdx(place), dy = dfdy(place);
  float2 axis = dot(dx,dx) > dot(dy,dy) ? dx : dy;
  float spacing = level == 4 ? period.x / 1024 : levels[level].window.z / 512;
  float lengthAxis = length(axis);
  if (lengthAxis > spacing) {
    axis *= min(1.f, 8 * spacing / lengthAxis);
    if (level < 4) {
      float2 local = place - levels[level].window.xy;
      float2 room = max(min(local, levels[level].window.z-local)-spacing, float2(0));
      float2 limit = room / max(abs(axis)*0.5f,float2(1e-5f));
      axis *= min(1.f, min(limit.x,limit.y));
    }
    float4 both = float4(0);
    for (int i=0; i<4; ++i) {
      float2 tap = place + axis*((i+0.5f)*0.25f-0.5f);
      both += float4(material_sample(t1,linear_repeat,level,blend,tap,period,levels).xy,
                     material_sample(t2,linear_repeat,level,blend,tap,period,levels).xy);
    }
    m1.xy = both.xy * 0.25f;
    m2.xy = both.zw * 0.25f;
  }
'''
        if packed:
            samples += '''
  float2 encodedSlope = clamp((255*m1.xy-128)/127, -0.9999f, 0.9999f);
  m1.xy = 0.25f * encodedSlope / (1-abs(encodedSlope));
  m1.w = (255*m1.w-128)/127 * 0.045f;
'''
            if packed == '24':
                samples += '  m2.xy = (255*m2.xy-128)/127;\n'
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
                            'macro': ('normal',), 'detail': ('slope',),
                            'wetness': ('puddle', 'dry_rough', 'albedo')}
                if grain and name == 'slope':
                    fine = re.search(r'float2 grain_slope = .*?0\.35f \* (float2\(.*\))\)\);',src)[1]
                    expr = f'(m1.xy + m3.x * {fine}) * (1.0f-puddle)'
                if name not in restored.get(args.live_channel, ()):
                    src = assign(src, name, expr)
        else:
            src = assign(src,'surface','GroundSurface{m0.rgb, m1.xy, m1.z, m0.w}')
        if args.footprint and road:
            src = src.replace('  return result;', '''
  float2 dxz = dfdx(place), dyz = dfdy(place);
  float trace = dot(dxz,dxz)+dot(dyz,dyz);
  float area = abs(dxz.x*dyz.y - dxz.y*dyz.x);
  float major2 = 0.5f*(trace + sqrt(max(0.f,trace*trace-4*area*area)));
  float rangeXZ = length(place-frame.camera_position.xz);
  result.motion = float2(256*(level+1)+min(255.f,round(rangeXZ)),
                         major2/max(1e-8f,area));
  return result;''')
        (out / f'{program}-{group}.metal').write_text(src)

    src = (stages / 'terrain.fragment.metal').read_text()
    end = 'dry_rough' if road else 'surface'
    endline = re.search(rf'^  (float|GroundSurface) {end} = .*?;$', src, re.M)
    src = src[:endline.end()]
    outputs = 'struct Bake { float4 a [[color(0)]]; float4 b [[color(1)]];'
    if road: outputs += ' float4 c [[color(2)]];'
    if grain: outputs += ' float strength [[color(3)]];'
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
    if road and args.filter in ('crown', 'both'):
        camber = re.search(r'float (\w+_rp_camber) = ', src)[1]
        across = re.search(r'float (\w+_rp_across) = ', src)[1]
        left = re.search(r'float2 (\w+_rp_left) = ', src)[1]
        width = f'(dot(abs({left}), win.z == 0 ? period / win.w : float2(win.z / win.w)))'
        src = src.replace(f'  float {camber} =', f'  float crownWidth = max({width}, 1e-5f);\n  float {camber} =', 1)
        def step_at(offset):
            return f'saturate(0.5f + ({across} {offset}) / crownWidth)'
        src = assign(src, camber, f"terrain.road_shape.z * ({step_at('+ terrain.road_shape.x')} - 2*{step_at('')} + {step_at('- terrain.road_shape.x')})")
    src = assign(src,'road_reach','1.0f')
    src = src.replace('  float3 normal =', '''  stage_in.world.y = mix(stage_in.world.y,
      road.centre - terrain.road_shape.z * min(abs(road_across), terrain.road_shape.x), road_flat);
  float3 normal =''',1)
    if road:
        src = assign(src,'wet','0.0f')
        src = assign(src,'packed','0.0f')
        slope = 'slope'
        extra = ''
        if grain:
            fine = re.search(r'float2 grain_slope = .*?0\.35f \* (float2\(.*\))\)\);',src)[1]
            src += '''
  float strength = mix(0.35f*surface.relief*(1-0.6f*road_flat),
                       0.07f*asphalt_here.grit,paved_tread);
'''
            slope = f'(slope - strength*{fine})'
            extra = ',strength'
        pigment = 'path_ground'
        macro = 'normal.xz'
        snow = 'powder'
        if packed:
            slope = f'((128+127*({slope})/(0.25f+abs({slope})))/255)'
            snow = 'float2(powder.x, (128+127*powder.y/0.045f)/255)'
            if packed == '24': macro = '((128+127*normal.xz)/255)'
        src += f'''
  return Bake{{float4({pigment},dry_rough),float4({slope},{snow}),
    float4({macro},mix(mix(0.25f,0.55f,smoothstep(0.92f,0.6f,surface.rough)),
    0.4f,tread_share),paved_tread*smoothstep(0.3f,0.7f,look.w)){extra}}};
}}'''
    else:
        src += '\n  return Bake{float4(surface.albedo,surface.rough),float4(surface.powder,surface.relief,0)};\n}'
    (out / f'bake-{group}.metal').write_text(src)
(out / 'triangle.metal').write_text('''#include <metal_stdlib>
using namespace metal;
vertex float4 terrain_vertex(uint i [[vertex_id]]) {
  return float4(i == 1 ? 3 : -1, i == 2 ? 3 : -1, 0, 1);
}
''')

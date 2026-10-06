# NHAL's Direct3D 12 backend and demo for Xbox, cross-built by nixbox (see
# xbox.nix and the repository's flake.nix). The Metal side builds with the
# repository's CMake (target nhal-demo).
project('nhal-demo', 'cpp', default_options: ['cpp_std=c++20'])

# luv-shaderc lowers the Lisp shaders to HLSL plus reflection headers, and
# DXC turns each stage into shader_<program>_<stage>.h, a byte array of
# signed DXIL.
shaderc = find_program('luv-shaderc', native: true)
dxc = find_program('dxc', native: true)
profiles = {'vertex': 'vs_6_0', 'fragment': 'ps_6_0', 'compute': 'cs_6_0'}
programs = [
  ['terrain', ['vertex', 'fragment']],
  ['terrain_shadow', ['vertex']],
  ['trees', ['vertex', 'fragment']],
  ['trees_shadow', ['vertex']],
  ['sky', ['vertex', 'fragment']],
  ['tonemap', ['vertex', 'fragment']],
  ['resolve', ['vertex', 'fragment']],
  ['forest_wind', ['compute']],
]
lowered_outputs = []
foreach program : programs
  lowered_outputs += [program[0] + '.hh']
  foreach stage : program[1]
    lowered_outputs += [program[0] + '.' + stage + '.hlsl']
  endforeach
endforeach
lowered = custom_target('luv-shaders',
  input: 'demo/shaders/scene.lisp',
  output: lowered_outputs,
  command: [shaderc, '--out', '@OUTDIR@', '--target', 'hlsl', '@INPUT@'])

shaders = []
index = 0
foreach program : programs
  index += 1 # the program's header
  foreach stage : program[1]
    entry = program[0] + '_' + stage
    shaders += custom_target('shader_' + entry,
      input: lowered[index],
      output: 'shader_' + entry + '.h',
      command: [dxc, '-O3', '-T', profiles[stage], '-E', entry,
                '-Fh', '@OUTPUT@', '-Vn', 'shader_' + entry, '@INPUT@'])
    index += 1
  endforeach
endforeach

# Sources include <moppe/nhal/...> from the repository root.
root = '-I' + (meson.project_source_root() / '..' / '..')

executable('nhal-demo',
  'd3d12/d3d12_device.cc', 'demo/scene.cc', 'demo/main_uwp.cc',
  lowered, shaders,
  cpp_args: [root, '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN'],
  link_args: ['-ld3d12', '-ldxgi'],
  win_subsystem: 'windows',
  install: true)

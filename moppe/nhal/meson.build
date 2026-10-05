# NHAL's Direct3D 12 backend and demo for Xbox, cross-built by nixbox (see
# xbox.nix and the repository's flake.nix). The Metal side builds with the
# repository's CMake (target nhal-demo).
project('nhal-demo', 'cpp', default_options: ['cpp_std=c++20'])

# Each entry point becomes shader_<entry>.h, a byte array of signed DXIL.
dxc = find_program('dxc', native: true)
shaders = []
foreach entry : [
  ['terrain_vertex', 'vs_6_0'], ['terrain_fragment', 'ps_6_0'],
  ['trees_vertex', 'vs_6_0'], ['trees_fragment', 'ps_6_0'],
  ['sky_vertex', 'vs_6_0'], ['sky_fragment', 'ps_6_0'],
  ['tonemap_vertex', 'vs_6_0'], ['tonemap_fragment', 'ps_6_0'],
]
  shaders += custom_target('shader_' + entry[0],
    input: 'demo/shaders/scene.hlsl',
    output: 'shader_' + entry[0] + '.h',
    command: [dxc, '-O3', '-T', entry[1], '-E', entry[0],
              '-Fh', '@OUTPUT@', '-Vn', 'shader_' + entry[0], '@INPUT@'])
endforeach

# Sources include <moppe/nhal/...> from the repository root.
root = '-I' + (meson.project_source_root() / '..' / '..')

executable('nhal-demo',
  'd3d12/d3d12_device.cc', 'demo/scene.cc', 'demo/main_uwp.cc', shaders,
  cpp_args: [root, '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN'],
  link_args: ['-ld3d12', '-ldxgi'],
  win_subsystem: 'windows',
  install: true)

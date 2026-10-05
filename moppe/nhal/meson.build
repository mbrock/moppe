# NHAL's Direct3D 12 backend and demo for Xbox, cross-built by nixbox (see
# xbox.nix and the repository's flake.nix). The Metal side builds with the
# repository's CMake (target nhal-demo).
project('nhal-demo', 'cpp', default_options: ['cpp_std=c++20'])

# luv-shaderc lowers the Lisp shaders to HLSL plus reflection headers, and
# DXC turns each stage into shader_<program>_<stage>.h, a byte array of
# signed DXIL.
shaderc = find_program('luv-shaderc', native: true)
dxc = find_program('dxc', native: true)
programs = ['terrain', 'trees', 'sky', 'tonemap']
stages = [['vertex', 'vs_6_0'], ['fragment', 'ps_6_0']]
lowered_outputs = []
foreach program : programs
  lowered_outputs += [program + '.hh']
  foreach stage : stages
    lowered_outputs += [program + '.' + stage[0] + '.hlsl']
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
  foreach stage : stages
    entry = program + '_' + stage[0]
    shaders += custom_target('shader_' + entry,
      input: lowered[index],
      output: 'shader_' + entry + '.h',
      command: [dxc, '-O3', '-T', stage[1], '-E', entry,
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
